/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include <signal.h>

// #include "gperftools/profiler.h"

#include "folly/init/Init.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "src/server/grpc_server_impl.h"
#include "src/server/http_server_impl.h"
#include "src/server/server_context.h"
#include "src/util/config_manager.h"

// https://github.com/grpc/grpc/issues/24884
tbox::server::HttpServer *http_server_ptr = nullptr;
tbox::server::GrpcServer *grpc_server_ptr = nullptr;
bool shutdown_required = false;
std::mutex mutex;
std::condition_variable cv;

void SignalHandler(int sig) {
  LOG(INFO) << "Got signal: " << strsignal(sig) << std::endl;
  shutdown_required = true;
  cv.notify_all();
}

void ShutdownCheckingThread(void) {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, []() { return shutdown_required; });
  if (http_server_ptr) {
    http_server_ptr->Shutdown();
  }
  if (grpc_server_ptr) {
    grpc_server_ptr->Shutdown();
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
  LOG(INFO) << "Server initializing ...";

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
      "./conf/server_config.json");

  RegisterSignalHandler();

  std::thread shutdown_thread(ShutdownCheckingThread);

  std::shared_ptr<tbox::server::ServerContext> server_context =
      std::make_shared<tbox::server::ServerContext>();

  // Initialize gRPC server
  tbox::server::GrpcServer grpc_server(server_context);
  ::grpc_server_ptr = &grpc_server;
  
  LOG(INFO) << "Starting gRPC server on " 
            << tbox::util::ConfigManager::Instance()->ServerAddr() << ":"
            << tbox::util::ConfigManager::Instance()->GrpcServerPort();
  grpc_server.Start();
  LOG(INFO) << "gRPC server started successfully";

  // Initialize HTTP server
  tbox::server::HttpServer http_server(server_context);
  ::http_server_ptr = &http_server;

  LOG(INFO) << "Starting HTTP server on " 
            << tbox::util::ConfigManager::Instance()->ServerAddr() << ":"
            << tbox::util::ConfigManager::Instance()->HttpServerPort();
  http_server.Start();
  LOG(INFO) << "HTTP server started successfully";

  LOG(INFO) << "All servers running. Waiting for shutdown signal...";

  if (shutdown_thread.joinable()) {
    shutdown_thread.join();
  }

  LOG(INFO) << "Server shutdown complete";

  // ProfilerStop();
  return 0;
}
