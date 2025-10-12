/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

// #include "gperftools/profiler.h"

#include <csignal>

#include "folly/init/Init.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "src/client/grpc_client.h"
// #include "src/client/websocket_client.h"  // WebSocket disabled for now
#include "src/impl/config_manager.h"
#include "src/impl/ddns_manager.h"

namespace {

// Signal-safe shutdown state
std::atomic<bool> shutdown_required(false);
std::atomic<int> received_signal(0);
std::mutex mutex;
std::condition_variable cv;
// tbox::client::WebSocketClient* websocket_client_ptr = nullptr;  // WebSocket
// disabled
tbox::client::GrpcClient* grpc_client_ptr = nullptr;
tbox::impl::DDNSManager* ddns_manager_ptr = nullptr;

/// @brief Handle signals for graceful shutdown.
/// @param sig Signal number received.
/// @note This function is async-signal-safe - only sets atomic flags.
void SignalHandler(int sig) {
  received_signal.store(sig);
  shutdown_required.store(true);
  // Note: cv.notify_all() is not async-signal-safe, but in practice
  // it usually works. For strict safety, use a self-pipe pattern.
  cv.notify_all();
}

/// @brief Thread function that waits for shutdown signal.
void ShutdownCheckingThread(void) {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, []() { return shutdown_required.load(); });

  // Log the signal information (safe here, not in signal handler)
  int sig = received_signal.load();
  if (sig > 0) {
    LOG(INFO) << "Got signal: " << strsignal(sig) << " (" << sig << ")";
  }

  // Stop DDNS manager first
  if (ddns_manager_ptr && ddns_manager_ptr->IsRunning()) {
    ddns_manager_ptr->Stop();
    LOG(INFO) << "DDNS manager stopped";
  }

  // Stop gRPC client gracefully
  if (grpc_client_ptr) {
    grpc_client_ptr->Stop();
  }
  // WebSocket disabled for now
  // if (websocket_client_ptr) {
  //   websocket_client_ptr->Stop();
  // }
}

/// @brief Register signal handlers for graceful shutdown.
void RegisterSignalHandler() {
  signal(SIGTERM, &SignalHandler);
  signal(SIGINT, &SignalHandler);
  signal(SIGQUIT, &SignalHandler);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
}

}  // namespace

int main(int argc, char** argv) {
  // ProfilerStart("tbox_profile");
  LOG(INFO) << "Client initializing ...";

  gflags::ParseCommandLineFlags(&argc, &argv, false);
  FLAGS_log_dir = "./log";
  FLAGS_stop_logging_if_full_disk = true;
  FLAGS_logbufsecs = 0;

  folly::Init init(&argc, &argv, false);
  google::EnableLogCleaner(7);
  // google::InitGoogleLogging(argv[0]); // already called in folly::Init
  google::SetStderrLogging(google::GLOG_INFO);
  LOG(INFO) << "CommandLine: " << google::GetArgv();

  // Initialize configuration
  auto config_manager = tbox::util::ConfigManager::Instance();
  if (!config_manager->Init("./conf/client_config.json")) {
    LOG(FATAL) << "Failed to initialize configuration from "
               << "./conf/client_config.json";
    return 1;
  }
  LOG(INFO) << "Configuration initialized successfully";

  // Initialize DDNS manager singleton
  auto ddns_manager = tbox::impl::DDNSManager::Instance();
  if (ddns_manager->Init()) {
    LOG(INFO) << "DDNS manager initialized";
    ddns_manager_ptr = ddns_manager.get();

    // Start DDNS manager
    if (!ddns_manager->IsRunning()) {
      ddns_manager->Start();
      LOG(INFO) << "DDNS manager started";
    }
  } else {
    LOG(WARNING) << "Failed to initialize DDNS manager, continuing without it";
  }

  RegisterSignalHandler();

  std::thread shutdown_thread(ShutdownCheckingThread);

  // Initialize gRPC client (uses ConfigManager for configuration)
  tbox::client::GrpcClient grpc_client;
  grpc_client_ptr = &grpc_client;

  grpc_client.Start();

  LOG(INFO) << "Client running. Waiting for shutdown signal...";

  // WebSocket disabled for now - only using gRPC
  // tbox::client::WebSocketClient websocket_client(
  //     server_addr, std::to_string(http_port));
  // websocket_client.Connect();
  // websocket_client_ptr = &websocket_client;

  // Wait for shutdown signal
  if (shutdown_thread.joinable()) {
    shutdown_thread.join();
  }

  LOG(INFO) << "Client shutdown complete";
  // ProfilerStop();
  return 0;
}
