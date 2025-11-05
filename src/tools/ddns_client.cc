/**
 * Dynamic DNS Client for IPv6
 *
 * This tool:
 * 1. Obtains the client's public IPv6 address using folly
 * 2. Checks if home.xiedeacc.com resolves to the current IPv6
 * 3. Updates Route53 if needed (handling DNS cache and multiple addresses)
 */

#include <arpa/inet.h>
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/route53/Route53Client.h>
#include <aws/route53/model/Change.h>
#include <aws/route53/model/ChangeBatch.h>
#include <aws/route53/model/ChangeResourceRecordSetsRequest.h>
#include <aws/route53/model/ListHostedZonesRequest.h>
#include <aws/route53/model/ListResourceRecordSetsRequest.h>
#include <aws/route53/model/ResourceRecord.h>
#include <aws/route53/model/ResourceRecordSet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

constexpr const char* DOMAIN_NAME = "home.xiedeacc.com";
constexpr int CHECK_INTERVAL_SECONDS = 60;
constexpr int DNS_TTL = 60;

// Get public IPv6 address by connecting to a known IPv6 service
std::vector<std::string> getPublicIPv6Addresses() {
  std::vector<std::string> addresses;

  try {
    // Get all IPv6 addresses from all interfaces
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
      std::cerr << "Failed to get interface addresses" << std::endl;
      return addresses;
    }

    std::set<std::string> uniqueAddresses;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) {
        continue;
      }

      // Only process IPv6 addresses
      if (ifa->ifa_addr->sa_family == AF_INET6) {
        struct sockaddr_in6* addr6 =
            reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipstr, sizeof(ipstr));

        std::string ipv6Addr(ipstr);

        // Filter out link-local (fe80::), loopback (::1), and ULA (fc00::/7)
        if (ipv6Addr.find("fe80:") == 0 || ipv6Addr == "::1" ||
            ipv6Addr.find("fc") == 0 || ipv6Addr.find("fd") == 0) {
          continue;
        }

        // This is likely a global unicast address
        uniqueAddresses.insert(ipv6Addr);
      }
    }

    freeifaddrs(ifaddr);

    addresses.assign(uniqueAddresses.begin(), uniqueAddresses.end());

  } catch (const std::exception& e) {
    std::cerr << "Error getting public IPv6: " << e.what() << std::endl;
  }

  return addresses;
}

// Resolve domain to IPv6 addresses (with proper handling for DNS cache)
std::vector<std::string> resolveDomainToIPv6(const std::string& domain) {
  std::vector<std::string> addresses;

  struct addrinfo hints {
  }, *result = nullptr;
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  // Use AI_NUMERICSERV to avoid service name lookup
  hints.ai_flags = 0;

  int ret = getaddrinfo(domain.c_str(), nullptr, &hints, &result);
  if (ret != 0) {
    std::cerr << "DNS resolution failed for " << domain << ": "
              << gai_strerror(ret) << std::endl;
    return addresses;
  }

  for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET6) {
      struct sockaddr_in6* addr6 =
          reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
      char ipstr[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &addr6->sin6_addr, ipstr, sizeof(ipstr));
      addresses.push_back(ipstr);
    }
  }

  freeaddrinfo(result);
  return addresses;
}

// Check if a specific IPv6 is in the list
bool isIPv6InList(const std::string& ipv6,
                  const std::vector<std::string>& list) {
  return std::find(list.begin(), list.end(), ipv6) != list.end();
}

// Get Route53 Hosted Zone ID for the domain
std::string getHostedZoneId(Aws::Route53::Route53Client& client,
                            const std::string& domain) {
  Aws::Route53::Model::ListHostedZonesRequest request;
  auto outcome = client.ListHostedZones(request);

  if (!outcome.IsSuccess()) {
    std::cerr << "Failed to list hosted zones: "
              << outcome.GetError().GetMessage() << std::endl;
    return "";
  }

  std::string searchDomain = domain;
  if (searchDomain.back() != '.') {
    searchDomain += '.';
  }

  for (const auto& zone : outcome.GetResult().GetHostedZones()) {
    if (zone.GetName() == searchDomain) {
      // Extract zone ID (format is "/hostedzone/XXXXX")
      std::string zoneId = zone.GetId();
      size_t pos = zoneId.find_last_of('/');
      if (pos != std::string::npos) {
        return zoneId.substr(pos + 1);
      }
      return zoneId;
    }
  }

  std::cerr << "Hosted zone not found for domain: " << domain << std::endl;
  return "";
}

// Get current AAAA records for the domain from Route53
std::vector<std::string> getCurrentRoute53Records(
    Aws::Route53::Route53Client& client, const std::string& hostedZoneId,
    const std::string& domain) {
  std::vector<std::string> records;

  Aws::Route53::Model::ListResourceRecordSetsRequest request;
  request.SetHostedZoneId(hostedZoneId);

  auto outcome = client.ListResourceRecordSets(request);
  if (!outcome.IsSuccess()) {
    std::cerr << "Failed to list resource record sets: "
              << outcome.GetError().GetMessage() << std::endl;
    return records;
  }

  std::string searchName = domain;
  if (searchName.back() != '.') {
    searchName += '.';
  }

  for (const auto& recordSet : outcome.GetResult().GetResourceRecordSets()) {
    if (recordSet.GetName() == searchName &&
        recordSet.GetType() == Aws::Route53::Model::RRType::AAAA) {
      for (const auto& record : recordSet.GetResourceRecords()) {
        records.push_back(record.GetValue());
      }
    }
  }

  return records;
}

// Update Route53 record with new IPv6 address
bool updateRoute53Record(Aws::Route53::Route53Client& client,
                         const std::string& hostedZoneId,
                         const std::string& domain, const std::string& ipv6) {
  Aws::Route53::Model::ChangeResourceRecordSetsRequest request;
  request.SetHostedZoneId(hostedZoneId);

  // Create resource record
  Aws::Route53::Model::ResourceRecord record;
  record.SetValue(ipv6);

  // Create resource record set
  Aws::Route53::Model::ResourceRecordSet recordSet;
  std::string recordName = domain;
  if (recordName.back() != '.') {
    recordName += '.';
  }
  recordSet.SetName(recordName);
  recordSet.SetType(Aws::Route53::Model::RRType::AAAA);
  recordSet.SetTTL(DNS_TTL);
  recordSet.AddResourceRecords(record);

  // Create change
  Aws::Route53::Model::Change change;
  change.SetAction(Aws::Route53::Model::ChangeAction::UPSERT);
  change.SetResourceRecordSet(recordSet);

  // Create change batch
  Aws::Route53::Model::ChangeBatch changeBatch;
  changeBatch.AddChanges(change);
  changeBatch.SetComment("Updated by DDNS client");

  request.SetChangeBatch(changeBatch);

  auto outcome = client.ChangeResourceRecordSets(request);
  if (!outcome.IsSuccess()) {
    std::cerr << "Failed to update Route53 record: "
              << outcome.GetError().GetMessage() << std::endl;
    return false;
  }

  std::cout << "Successfully updated Route53 record: " << domain << " -> "
            << ipv6 << std::endl;
  return true;
}

int main(int argc, char* argv[]) {
  // Initialize AWS SDK
  Aws::SDKOptions options;
  Aws::InitAPI(options);

  {
    // Create Route53 client
    Aws::Client::ClientConfiguration clientConfig;
    clientConfig.region = "us-east-1";  // Route53 is a global service

    Aws::Route53::Route53Client route53Client(clientConfig);

    // Get hosted zone ID once
    std::string hostedZoneId = getHostedZoneId(route53Client, DOMAIN_NAME);
    if (hostedZoneId.empty()) {
      std::cerr << "Failed to get hosted zone ID. Exiting." << std::endl;
      Aws::ShutdownAPI(options);
      return 1;
    }

    std::cout << "Found hosted zone ID: " << hostedZoneId << std::endl;

    // Main loop
    while (true) {
      std::cout << "\n=== Checking IPv6 and DNS ===" << std::endl;

      // Get current public IPv6 addresses
      auto publicIPv6s = getPublicIPv6Addresses();

      if (publicIPv6s.empty()) {
        std::cerr << "No public IPv6 addresses found. Waiting..." << std::endl;
        std::this_thread::sleep_for(
            std::chrono::seconds(CHECK_INTERVAL_SECONDS));
        continue;
      }

      std::cout << "Current public IPv6 addresses:" << std::endl;
      for (const auto& ipv6 : publicIPv6s) {
        std::cout << "  - " << ipv6 << std::endl;
      }

      // Use the first address as the primary one
      std::string primaryIPv6 = publicIPv6s[0];

      // Check current DNS resolution (may be cached)
      auto dnsIPv6s = resolveDomainToIPv6(DOMAIN_NAME);
      std::cout << "DNS resolved IPv6 addresses for " << DOMAIN_NAME << ":"
                << std::endl;
      for (const auto& ipv6 : dnsIPv6s) {
        std::cout << "  - " << ipv6 << std::endl;
      }

      // Get actual Route53 records (source of truth)
      auto route53IPv6s =
          getCurrentRoute53Records(route53Client, hostedZoneId, DOMAIN_NAME);
      std::cout << "Route53 AAAA records:" << std::endl;
      for (const auto& ipv6 : route53IPv6s) {
        std::cout << "  - " << ipv6 << std::endl;
      }

      // Check if our primary IPv6 is in Route53
      // We check against Route53 directly to avoid DNS caching issues
      bool needsUpdate = false;

      if (route53IPv6s.empty()) {
        std::cout << "No Route53 AAAA records found. Need to create."
                  << std::endl;
        needsUpdate = true;
      } else if (route53IPv6s.size() > 1) {
        std::cout << "Multiple Route53 records found. Using primary IPv6."
                  << std::endl;
        needsUpdate = true;
      } else if (route53IPv6s[0] != primaryIPv6) {
        std::cout << "Route53 record (" << route53IPv6s[0]
                  << ") differs from primary IPv6 (" << primaryIPv6 << ")"
                  << std::endl;
        needsUpdate = true;
      } else {
        std::cout << "Route53 record is up to date." << std::endl;
      }

      if (needsUpdate) {
        std::cout << "Updating Route53 with primary IPv6: " << primaryIPv6
                  << std::endl;
        updateRoute53Record(route53Client, hostedZoneId, DOMAIN_NAME,
                            primaryIPv6);
      }

      std::cout << "Waiting " << CHECK_INTERVAL_SECONDS
                << " seconds before next check..." << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(CHECK_INTERVAL_SECONDS));
    }
  }

  Aws::ShutdownAPI(options);
  return 0;
}
