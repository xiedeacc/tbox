/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

// #include "gperftools/profiler.h"

#include "folly/init/Init.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "src/client/grpc_client.h"
#include "src/client/websocket_client.h"
#include "src/util/config_manager.h"

bool shutdown_required = false;
std::mutex mutex;
std::condition_variable cv;
tbox::client::WebSocketClient *websocket_client_ptr = nullptr;
tbox::client::GrpcClient *grpc_client_ptr = nullptr;


void SignalHandler(int sig) {
  LOG(INFO) << "Got signal: " << strsignal(sig) << std::endl;
  shutdown_required = true;
  cv.notify_all();
}

void ShutdownCheckingThread(void) {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, []() { return shutdown_required; });
  
  // Stop both clients gracefully
  if (grpc_client_ptr) {
    grpc_client_ptr->Stop();
  }
  if (websocket_client_ptr) {
    websocket_client_ptr->Stop();
  }
}

void RegisterSignalHandler() {
  signal(SIGTERM, &SignalHandler);
  signal(SIGINT, &SignalHandler);
  signal(SIGQUIT, &SignalHandler);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char **argv) {
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

  tbox::util::ConfigManager::Instance()->Init(
      "./conf/client_config.json");

  RegisterSignalHandler();

  std::thread shutdown_thread(ShutdownCheckingThread);

  auto server_addr =
      tbox::util::ConfigManager::Instance()->ServerAddr();
  auto http_port =
      tbox::util::ConfigManager::Instance()->HttpServerPort();
  auto grpc_port =
      tbox::util::ConfigManager::Instance()->GrpcServerPort();
  auto report_interval =
      tbox::util::ConfigManager::Instance()->ReportIntervalSeconds();
  
  // Initialize gRPC client with configured reporting interval
  tbox::client::GrpcClientConfig grpc_config;
  grpc_config.host = server_addr;
  grpc_config.port = grpc_port;
  grpc_config.report_interval_seconds = report_interval;
  
  tbox::client::GrpcClient grpc_client(grpc_config);
  grpc_client_ptr = &grpc_client;
  
  // Start continuous IP reporting in background thread
  LOG(INFO) << "Starting background IP reporting thread with interval " 
            << report_interval << " seconds...";
  grpc_client.Start();
  
  // Connect WebSocket client
  tbox::client::WebSocketClient websocket_client(
      server_addr, std::to_string(http_port));
  websocket_client.Connect();

  websocket_client_ptr = &websocket_client;

  LOG(INFO) << "Now stopped websocket client";

  if (shutdown_thread.joinable()) {
    shutdown_thread.join();
  }
  // ProfilerStop();
  return 0;
}
