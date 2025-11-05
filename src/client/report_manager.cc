/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/report_manager.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <vector>

#include "glog/logging.h"
#include "src/async_grpc/client.h"
#include "src/async_grpc/common/time.h"
#include "src/client/authentication_manager.h"
#include "src/client/ssl_config_manager.h"
#include "src/impl/config_manager.h"
#include "src/server/grpc_handler/meta.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

static folly::Singleton<ReportManager> report_manager;

std::shared_ptr<ReportManager> ReportManager::Instance() {
  return report_manager.try_get();
}

ReportManager::ReportManager()
    : running_(false), should_stop_(false), connection_healthy_(false) {}

bool ReportManager::Init(std::shared_ptr<grpc::Channel> channel,
                         int report_interval_seconds, int login_retry_seconds) {
  std::lock_guard<std::mutex> lock(init_mutex_);

  if (initialized_) {
    LOG(WARNING) << "ReportManager already initialized";
    return true;
  }

  channel_ = channel;
  report_interval_seconds_ = report_interval_seconds;
  login_retry_seconds_ = login_retry_seconds;

  initialized_ = true;
  return true;
}

ReportManager::~ReportManager() {
  Stop();
}

void ReportManager::Start() {
  // Check if thread is already running or if old thread needs cleanup
  if (running_.load() || reporting_thread_.joinable()) {
    if (running_.load()) {
      LOG(WARNING) << "Reporting thread is already running";
    } else {
      LOG(WARNING) << "Previous thread still needs cleanup, joining...";
      reporting_thread_.join();
    }
    return;
  }

  if (!initialized_) {
    LOG(ERROR) << "Cannot start ReportManager without initialization";
    return;
  }

  should_stop_ = false;
  running_ = true;

  reporting_thread_ = std::thread(&ReportManager::ReportingLoop, this);
  LOG(INFO) << "Started IP reporting thread with interval "
            << report_interval_seconds_ << " seconds";
}

void ReportManager::Stop() {
  // Check if thread is already stopped and cleaned up
  if (!running_.load() && !reporting_thread_.joinable()) {
    return;
  }

  LOG(INFO) << "Stopping IP reporting thread...";

  // Signal the thread to stop (set this even if thread already exited)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    should_stop_ = true;
  }
  cv_.notify_all();

  // Wait for the thread to finish (always join if joinable, even if running_ is
  // false)
  if (reporting_thread_.joinable()) {
    reporting_thread_.join();
  }

  // Set running_ to false after thread has finished
  // (Note: ReportingLoop also sets this when it exits naturally)
  running_.store(false);
  LOG(INFO) << "IP reporting thread stopped";
}

bool ReportManager::ShouldReport(const std::vector<std::string>& current_ips) {
  std::lock_guard<std::mutex> lock(ip_tracking_mutex_);

  int64_t now_millis = util::Util::CurrentTimeMillis();
  int64_t time_since_last_report_seconds =
      (now_millis - last_report_time_millis_) / 1000;

  // Check if heartbeat interval has passed (1 hour)
  if (time_since_last_report_seconds >= kHeartbeatIntervalSeconds) {
    LOG(INFO) << "Heartbeat interval reached (" << kHeartbeatIntervalSeconds
              << " seconds). Reporting even if IP unchanged.";
    return true;
  }

  // Check if IP addresses have changed
  if (current_ips.size() != last_reported_ips_.size()) {
    LOG(INFO) << "IP address count changed: " << last_reported_ips_.size()
              << " -> " << current_ips.size();
    return true;
  }

  // Sort both vectors for comparison (IPs may be in different order)
  std::vector<std::string> sorted_current = current_ips;
  std::vector<std::string> sorted_last = last_reported_ips_;
  std::sort(sorted_current.begin(), sorted_current.end());
  std::sort(sorted_last.begin(), sorted_last.end());

  if (sorted_current != sorted_last) {
    LOG(INFO) << "IP addresses changed. Need to report.";
    return true;
  }

  // No change detected and heartbeat not due
  return false;
}

bool ReportManager::ReportClientIP() {
  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot report client IP";
    return false;
  }

  // Buffer all log messages for atomic output
  std::vector<std::string> log_buffer;

  // Get public IP addresses from server
  std::string public_ipv4 = GetPublicIPv4();
  std::string public_ipv6 = GetPublicIPv6();

  if (!public_ipv4.empty()) {
    log_buffer.push_back("Got public IPv4 from server: " + public_ipv4);
  }
  if (!public_ipv6.empty()) {
    log_buffer.push_back("Got public IPv6 from server: " + public_ipv6);
  }

  // Get all local IP addresses (already filtered - no loopback)
  std::vector<std::string> all_local_ips = util::Util::GetAllLocalIPAddresses();

  // Smart IP filtering logic for different network scenarios
  std::vector<std::string> client_ips;

  // First, try to find local IPs that match public IPs (direct connection case)
  for (const auto& local_ip : all_local_ips) {
    if ((!public_ipv4.empty() && local_ip == public_ipv4) ||
        (!public_ipv6.empty() && local_ip == public_ipv6)) {
      client_ips.push_back(local_ip);
      log_buffer.push_back("Local IP " + local_ip + " matches public IP");
    }
  }

  // If no matches found (NAT/router scenario), use actual public IPs from
  // interfaces
  if (client_ips.empty()) {
    log_buffer.push_back(
        "No local IPs match server-detected public IPs (NAT scenario)");

    // Get actual public IPv6 addresses from local interfaces
    std::vector<std::string> public_ipv6_addrs =
        util::Util::GetPublicIPv6Addresses();
    for (const auto& addr : public_ipv6_addrs) {
      client_ips.push_back(addr);
      log_buffer.push_back("Using actual public IPv6: " + addr);
    }

    // If still no public IPs found, fall back to best available local IPs
    if (client_ips.empty()) {
      log_buffer.push_back("No public IPs available, using best local IPs");

      // Prefer non-private IPv4 addresses first, then any non-loopback
      for (const auto& local_ip : all_local_ips) {
        try {
          folly::IPAddress ip_addr(local_ip);

          // For IPv4: skip private ranges but allow other addresses
          if (ip_addr.isV4()) {
            folly::IPAddressV4 ipv4 = ip_addr.asV4();
            // Skip private ranges: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
            if (!ipv4.inSubnet(folly::IPAddressV4("10.0.0.0"), 8) &&
                !ipv4.inSubnet(folly::IPAddressV4("172.16.0.0"), 12) &&
                !ipv4.inSubnet(folly::IPAddressV4("192.168.0.0"), 16) &&
                !ipv4.inSubnet(folly::IPAddressV4("169.254.0.0"),
                               16)) {  // Skip link-local
              client_ips.push_back(local_ip);
              log_buffer.push_back("Using non-private IPv4: " + local_ip);
            }
          }
          // For IPv6: already filtered by GetAllLocalIPAddresses (no
          // loopback/link-local)
          else if (ip_addr.isV6()) {
            folly::IPAddressV6 ipv6 = ip_addr.asV6();
            if (!ipv6.isPrivate()) {  // Skip ULA addresses
              client_ips.push_back(local_ip);
              log_buffer.push_back("Using non-private IPv6: " + local_ip);
            }
          }
        } catch (const std::exception& e) {
          // Skip invalid IP addresses
          continue;
        }
      }

      // Last resort: use private IPs if no public/non-private ones available
      if (client_ips.empty()) {
        log_buffer.push_back(
            "No public IPs available, falling back to private IPs");

        // Just use the first few local IPs (they're already filtered to exclude
        // loopback)
        size_t max_private_ips = 3;  // Limit to avoid too many private IPs
        for (size_t i = 0; i < std::min(all_local_ips.size(), max_private_ips);
             ++i) {
          client_ips.push_back(all_local_ips[i]);
          log_buffer.push_back("Using private IP as fallback: " +
                               all_local_ips[i]);
        }
      }
    }
  }

  if (client_ips.empty()) {
    log_buffer.push_back("No public IPs to report after filtering");
    // Output buffered logs
    for (const auto& msg : log_buffer) {
      LOG(INFO) << msg;
    }
    return false;
  }

  try {
    // Set a timeout to make the call more responsive to stop signals
    async_grpc::Client<tbox::server::grpc_handler::ReportOpMethod> client(
        channel_, async_grpc::common::FromSeconds(10));

    tbox::proto::ReportRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_REPORT);
    request.set_token(auth_manager->GetToken());
    request.set_client_id(util::ConfigManager::Instance()->ClientId());

    // Add filtered client IP addresses
    for (const auto& ip : client_ips) {
      request.add_client_ip(ip);
    }

    request.set_timestamp(util::Util::CurrentTimeSeconds());
    request.set_client_info("TBox C++ Client");

    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      log_buffer.push_back("Successfully reported client IP");
      // Output all buffered logs atomically
      for (const auto& msg : log_buffer) {
        LOG(INFO) << msg;
      }
      // Update last reported IPs and timestamp on success
      {
        std::lock_guard<std::mutex> lock(ip_tracking_mutex_);
        last_reported_ips_ = client_ips;
        last_report_time_millis_ = util::Util::CurrentTimeMillis();
      }
      return true;
    } else {
      // Check if failure is due to authentication (server restart scenario)
      if (status.error_code() == grpc::StatusCode::UNAUTHENTICATED ||
          status.error_message().find("authentication") != std::string::npos ||
          status.error_message().find("token") != std::string::npos) {
        log_buffer.push_back(
            "Authentication failed - server may have restarted. Will re-login "
            "on next cycle.");
        // Clear token to force re-login
        auth_manager->ClearToken();
      }

      log_buffer.push_back(
          "Report failed - status: " + std::to_string(status.error_code()) +
          ", message: " + status.error_message());
      // Output buffered logs
      for (const auto& msg : log_buffer) {
        LOG(INFO) << msg;
      }
    }
  } catch (const std::exception& e) {
    log_buffer.push_back("Exception while reporting client IP: " +
                         std::string(e.what()));
    // Output buffered logs
    for (const auto& msg : log_buffer) {
      LOG(INFO) << msg;
    }
  }

  return false;
}

void ReportManager::ReportingLoop() {
  LOG(INFO) << "IP reporting loop started";

  auto auth_manager = client::AuthenticationManager::Instance();

  // Attempt login with retry logic
  while (!should_stop_.load()) {
    if (auth_manager->Login()) {
      LOG(INFO) << "Successfully logged in";
      // Mark connection as healthy after successful login
      {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        last_successful_op_millis_ = util::Util::CurrentTimeMillis();
        connection_healthy_.store(true);
      }
      break;
    }

    LOG(WARNING) << "Failed to login, will retry in " << login_retry_seconds_
                 << " seconds";

    // Wait before retry, but check for stop signal
    std::unique_lock<std::mutex> lock(mutex_);
    if (cv_.wait_for(lock, std::chrono::seconds(login_retry_seconds_),
                     [this] { return should_stop_.load(); })) {
      // should_stop_ became true, exit loop
      LOG(INFO) << "Login retry interrupted by stop signal";
      running_.store(false);
      return;
    }
  }

  // Main reporting loop
  while (true) {
    // Wait for the configured interval or until stop signal
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (cv_.wait_for(lock, std::chrono::seconds(report_interval_seconds_),
                       [this] { return should_stop_.load(); })) {
        // should_stop_ became true, exit loop
        break;
      }
    }

    // Check stop signal again before doing any work
    if (should_stop_.load()) {
      break;
    }

    // Output separator for this check cycle
    LOG(INFO) << "=== Checking Local IP Addresses ===";

    // Get public IP addresses from server to determine reportable IPs
    std::string public_ipv4 = GetPublicIPv4();

    // Check stop signal after each potentially blocking network call
    if (should_stop_.load()) {
      break;
    }

    std::string public_ipv6 = GetPublicIPv6();

    // Check stop signal after potentially blocking network calls
    if (should_stop_.load()) {
      break;
    }

    // Get all local IP addresses
    std::vector<std::string> all_local_ips =
        util::Util::GetAllLocalIPAddresses();

    // Filter to reportable IPs (same smart logic as ReportClientIP)
    std::vector<std::string> reportable_ips;

    // First, try to find local IPs that match public IPs (direct connection
    // case)
    for (const auto& local_ip : all_local_ips) {
      if ((!public_ipv4.empty() && local_ip == public_ipv4) ||
          (!public_ipv6.empty() && local_ip == public_ipv6)) {
        reportable_ips.push_back(local_ip);
      }
    }

    // If no matches found (NAT/router scenario), use actual public IPs from
    // interfaces
    if (reportable_ips.empty()) {
      // Get actual public IPv6 addresses from local interfaces
      std::vector<std::string> public_ipv6_addrs =
          util::Util::GetPublicIPv6Addresses();
      reportable_ips.insert(reportable_ips.end(), public_ipv6_addrs.begin(),
                            public_ipv6_addrs.end());

      // If still no public IPs found, fall back to best available local IPs
      if (reportable_ips.empty()) {
        // Prefer non-private IPv4 addresses first, then any non-loopback
        for (const auto& local_ip : all_local_ips) {
          try {
            folly::IPAddress ip_addr(local_ip);

            // For IPv4: skip private ranges but allow other addresses
            if (ip_addr.isV4()) {
              folly::IPAddressV4 ipv4 = ip_addr.asV4();
              if (!ipv4.inSubnet(folly::IPAddressV4("10.0.0.0"), 8) &&
                  !ipv4.inSubnet(folly::IPAddressV4("172.16.0.0"), 12) &&
                  !ipv4.inSubnet(folly::IPAddressV4("192.168.0.0"), 16) &&
                  !ipv4.inSubnet(folly::IPAddressV4("169.254.0.0"), 16)) {
                reportable_ips.push_back(local_ip);
              }
            }
            // For IPv6: already filtered by GetAllLocalIPAddresses (no
            // loopback/link-local)
            else if (ip_addr.isV6()) {
              folly::IPAddressV6 ipv6 = ip_addr.asV6();
              if (!ipv6.isPrivate()) {  // Skip ULA addresses
                reportable_ips.push_back(local_ip);
              }
            }
          } catch (const std::exception& e) {
            // Skip invalid IP addresses
            continue;
          }
        }

        // Last resort: use private IPs if no public/non-private ones available
        if (reportable_ips.empty()) {
          size_t max_private_ips = 3;  // Limit to avoid too many private IPs
          for (size_t i = 0;
               i < std::min(all_local_ips.size(), max_private_ips); ++i) {
            reportable_ips.push_back(all_local_ips[i]);
          }
        }
      }
    }

    // Check stop signal before continuing
    if (should_stop_.load()) {
      break;
    }

    // Check if we should report (reportable IPs changed or heartbeat due)
    if (!ShouldReport(reportable_ips)) {
      LOG(INFO) << "IP addresses unchanged and heartbeat not due. Skipping "
                   "report.";
      continue;
    }

    // Check stop signal before authentication check
    if (should_stop_.load()) {
      break;
    }

    // Check if we need to re-authenticate (e.g., after server restart)
    if (!auth_manager->IsAuthenticated()) {
      LOG(INFO) << "Authentication lost, attempting to re-login...";
      if (auth_manager->Login()) {
        LOG(INFO) << "Re-login successful after authentication loss";
        std::lock_guard<std::mutex> lock(connection_mutex_);
        last_successful_op_millis_ = util::Util::CurrentTimeMillis();
        connection_healthy_.store(true);
      } else {
        LOG(WARNING) << "Re-login failed, will retry on next cycle";
        connection_healthy_.store(false);
        continue;
      }
    }

    // Check stop signal before reporting
    if (should_stop_.load()) {
      break;
    }

    // Report client IP addresses
    try {
      bool success = ReportClientIP();
      if (success) {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        last_successful_op_millis_ = util::Util::CurrentTimeMillis();
        connection_healthy_.store(true);
      } else {
        connection_healthy_.store(false);
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Exception in reporting loop: " << e.what();
      connection_healthy_.store(false);
    }

    // Check stop signal after reporting
    if (should_stop_.load()) {
      break;
    }
  }

  // Set running_ to false when loop exits naturally
  running_.store(false);
  LOG(INFO) << "IP reporting loop ended";
}

bool ReportManager::IsConnectionHealthy() {
  std::lock_guard<std::mutex> lock(connection_mutex_);
  int64_t now_millis = util::Util::CurrentTimeMillis();
  int64_t time_since_last_success_seconds =
      (now_millis - last_successful_op_millis_) / 1000;

  return connection_healthy_.load() &&
         time_since_last_success_seconds < kHealthCheckTimeoutSeconds;
}

bool ReportManager::Reconnect() {
  // For now, just return true as channel reconnection is handled by gRPC
  // We'll mark as healthy and let the next report attempt determine success
  connection_healthy_.store(true);
  return true;
}

std::shared_ptr<tbox::proto::TBOXService::Stub>
ReportManager::CreateIPv4Stub() {
  auto config_manager = util::ConfigManager::Instance();
  std::string server_addr = config_manager->ServerAddr();
  uint32_t grpc_port = config_manager->GrpcServerPort();

  // Parse hostname
  std::string hostname = server_addr;
  bool use_http = false;
  if (hostname.find("http://") == 0) {
    hostname = hostname.substr(7);
    use_http = true;
  } else if (hostname.find("https://") == 0) {
    hostname = hostname.substr(8);
    use_http = false;
  }

  // Resolve hostname to IPv4 address
  struct addrinfo hints, *res = nullptr;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;  // Force IPv4
  hints.ai_socktype = SOCK_STREAM;

  int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
  if (status != 0 || res == nullptr) {
    LOG(WARNING) << "Failed to resolve " << hostname
                 << " to IPv4: " << gai_strerror(status);
    return nullptr;
  }

  // Get IPv4 address
  char ipv4_str[INET_ADDRSTRLEN];
  struct sockaddr_in* ipv4 =
      reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
  inet_ntop(AF_INET, &(ipv4->sin_addr), ipv4_str, INET_ADDRSTRLEN);
  std::string ipv4_address = std::string(ipv4_str);
  freeaddrinfo(res);

  // Create channel to IPv4 address
  std::string target = ipv4_address + ":" + std::to_string(grpc_port);
  std::shared_ptr<grpc::ChannelCredentials> creds;
  grpc::ChannelArguments args;

  // Set authority to the original hostname for nginx routing
  args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, hostname);

  if (use_http) {
    creds = grpc::InsecureChannelCredentials();
  } else {
    auto ssl_config_manager = SSLConfigManager::Instance();
    auto config_manager = util::ConfigManager::Instance();
    std::string ca_cert =
        ssl_config_manager->LoadCACert(config_manager->LocalCertPath());
    if (ca_cert.empty()) {
      LOG(WARNING) << "Failed to load CA cert for IPv4 stub";
      return nullptr;
    }
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = ca_cert;
    creds = grpc::SslCredentials(ssl_opts);
    args.SetSslTargetNameOverride(hostname);
  }

  auto channel = grpc::CreateCustomChannel(target, creds, args);
  if (!channel) {
    LOG(WARNING) << "Failed to create IPv4 channel to " << target;
    return nullptr;
  }

  return tbox::proto::TBOXService::NewStub(channel);
}

std::shared_ptr<tbox::proto::TBOXService::Stub>
ReportManager::CreateIPv6Stub() {
  auto config_manager = util::ConfigManager::Instance();
  std::string server_addr = config_manager->ServerAddr();
  uint32_t grpc_port = config_manager->GrpcServerPort();

  // Parse hostname
  std::string hostname = server_addr;
  bool use_http = false;
  if (hostname.find("http://") == 0) {
    hostname = hostname.substr(7);
    use_http = true;
  } else if (hostname.find("https://") == 0) {
    hostname = hostname.substr(8);
    use_http = false;
  }

  // Resolve hostname to IPv6 address
  struct addrinfo hints, *res = nullptr;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;  // Force IPv6
  hints.ai_socktype = SOCK_STREAM;

  int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
  if (status != 0 || res == nullptr) {
    LOG(INFO) << "No IPv6 address available for " << hostname << ": "
              << gai_strerror(status);
    return nullptr;
  }

  // Get IPv6 address
  char ipv6_str[INET6_ADDRSTRLEN];
  struct sockaddr_in6* ipv6 =
      reinterpret_cast<struct sockaddr_in6*>(res->ai_addr);
  inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipv6_str, INET6_ADDRSTRLEN);
  std::string ipv6_address = std::string(ipv6_str);
  freeaddrinfo(res);

  // Create channel to IPv6 address (wrap in brackets for URL)
  std::string target = "[" + ipv6_address + "]:" + std::to_string(grpc_port);
  std::shared_ptr<grpc::ChannelCredentials> creds;
  grpc::ChannelArguments args;

  // Set authority to the original hostname for nginx routing
  args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, hostname);

  if (use_http) {
    creds = grpc::InsecureChannelCredentials();
  } else {
    auto ssl_config_manager = SSLConfigManager::Instance();
    auto config_manager = util::ConfigManager::Instance();
    std::string ca_cert =
        ssl_config_manager->LoadCACert(config_manager->LocalCertPath());
    if (ca_cert.empty()) {
      LOG(WARNING) << "Failed to load CA cert for IPv6 stub";
      return nullptr;
    }
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = ca_cert;
    creds = grpc::SslCredentials(ssl_opts);
    args.SetSslTargetNameOverride(hostname);
  }

  auto channel = grpc::CreateCustomChannel(target, creds, args);
  if (!channel) {
    LOG(WARNING) << "Failed to create IPv6 channel to " << target;
    return nullptr;
  }

  return tbox::proto::TBOXService::NewStub(channel);
}

std::string ReportManager::GetPublicIPv4() {
  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot get public IPv4";
    return "";
  }

  // Create IPv4-specific stub if not already created
  if (!ipv4_stub_) {
    ipv4_stub_ = CreateIPv4Stub();
    if (!ipv4_stub_) {
      LOG(WARNING) << "Failed to create IPv4 stub";
      return "";
    }
  }

  try {
    grpc::ClientContext context;
    // Set a short timeout to make the call more responsive to stop signals
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    context.set_deadline(deadline);

    tbox::proto::ReportRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_PUBLIC_IPV4);
    request.set_token(auth_manager->GetToken());
    request.set_client_id(util::ConfigManager::Instance()->ClientId());

    tbox::proto::ReportResponse response;
    grpc::Status status = ipv4_stub_->ReportOp(&context, request, &response);

    if (status.ok()) {
      if (response.err_code() == tbox::proto::ErrCode::Success &&
          response.client_ip_size() > 0) {
        return response.client_ip(0);
      }
    } else {
      // Check for authentication failure
      if (status.error_code() == grpc::StatusCode::UNAUTHENTICATED ||
          status.error_message().find("authentication") != std::string::npos ||
          status.error_message().find("token") != std::string::npos) {
        LOG(WARNING) << "Authentication failed while getting public IPv4";
        auth_manager->ClearToken();
      }
    }
  } catch (const std::exception& e) {
    // Silent failure, will be logged by caller
  }

  return "";
}

std::string ReportManager::GetPublicIPv6() {
  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot get public IPv6";
    return "";
  }

  // Create IPv6-specific stub if not already created
  if (!ipv6_stub_) {
    ipv6_stub_ = CreateIPv6Stub();
    if (!ipv6_stub_) {
      LOG(INFO) << "IPv6 not available, skipping IPv6 detection";
      return "";
    }
  }

  try {
    grpc::ClientContext context;
    // Set a short timeout to make the call more responsive to stop signals
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    context.set_deadline(deadline);

    tbox::proto::ReportRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_PUBLIC_IPV6);
    request.set_token(auth_manager->GetToken());
    request.set_client_id(util::ConfigManager::Instance()->ClientId());

    tbox::proto::ReportResponse response;
    grpc::Status status = ipv6_stub_->ReportOp(&context, request, &response);

    if (status.ok()) {
      if (response.err_code() == tbox::proto::ErrCode::Success &&
          response.client_ip_size() > 0) {
        return response.client_ip(0);
      }
    } else {
      // Check for authentication failure
      if (status.error_code() == grpc::StatusCode::UNAUTHENTICATED ||
          status.error_message().find("authentication") != std::string::npos ||
          status.error_message().find("token") != std::string::npos) {
        LOG(WARNING) << "Authentication failed while getting public IPv6";
        auth_manager->ClearToken();
      }
    }
  } catch (const std::exception& e) {
    // Silent failure, will be logged by caller
  }

  return "";
}

}  // namespace client
}  // namespace tbox
