/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include <signal.h>

#include <cstdlib>

#include "folly/init/Init.h"
#include "gflags/gflags.h"
#include "src/common/logging.h"
#include "src/impl/cert_manager.h"
#include "src/impl/config_manager.h"
#include "src/impl/ddns_manager.h"
#include "src/impl/user_manager.h"
#include "src/server/grpc_server_impl.h"
#include "src/server/http_server_impl.h"
#include "src/server/server_context.h"
#include "src/server/tcp_handler/vlmcsd_handler.h"
#include "src/server/version_info.h"

// https://github.com/grpc/grpc/issues/24884
tbox::server::HttpServer* http_server_ptr = nullptr;
tbox::server::GrpcServer* grpc_server_ptr = nullptr;
tbox::server::tcp_handler::VlmcsdHandler* vlmcsd_handler_ptr = nullptr;
tbox::impl::DDNSManager* ddns_manager_ptr = nullptr;
tbox::impl::CertManager* cert_manager_ptr = nullptr;
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

  // Stop DDNS manager first
  if (ddns_manager_ptr && ddns_manager_ptr->IsRunning()) {
    ddns_manager_ptr->Stop();
    LOG(INFO) << "DDNS manager stopped";
  }

  // Stop certificate manager
  if (cert_manager_ptr && cert_manager_ptr->IsRunning()) {
    cert_manager_ptr->Stop();
    LOG(INFO) << "Certificate manager stopped";
  }

  if (http_server_ptr) {
    http_server_ptr->Shutdown();
  }
  if (grpc_server_ptr) {
    grpc_server_ptr->Shutdown();
  }
  if (vlmcsd_handler_ptr) {
    vlmcsd_handler_ptr->Shutdown();
  }
}

void RegisterSignalHandler() {
  signal(SIGTERM, &SignalHandler);
  signal(SIGINT, &SignalHandler);
  signal(SIGQUIT, &SignalHandler);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  folly::Init init(&argc, &argv, false);
  const char* log_dir = std::getenv("TBOX_LOG_DIR");
  if (!log_dir) {
    log_dir = std::getenv("GLOG_log_dir");
  }
  tbox::logging::Initialize(argv[0], log_dir ? log_dir : "./logs");

  LOG(INFO) << "Server initializing ...";
  LOG(INFO) << "Git commit: " << GIT_VERSION;
  LOG(INFO) << "CommandLine: " << tbox::logging::CommandLine(argc, argv);

  if (!tbox::util::ConfigManager::Instance()->Init(
          "./conf/server_config.json")) {
    LOG(FATAL) << "Failed to initialize configuration from "
               << "./conf/server_config.json";
    return 1;
  }
  LOG(INFO) << "Configuration initialized successfully";

  // Initialize UserManager to create preset users
  if (!tbox::impl::UserManager::Instance()->Init()) {
    LOG(ERROR) << "Failed to initialize UserManager";
    return 1;
  }
  LOG(INFO) << "UserManager initialized successfully";

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

  // Initialize certificate manager singleton
  auto cert_manager = tbox::impl::CertManager::Instance();
  if (cert_manager->Init()) {
    LOG(INFO) << "Certificate manager initialized";
    cert_manager_ptr = cert_manager.get();

    // Start certificate manager
    if (!cert_manager->IsRunning()) {
      cert_manager->Start();
      LOG(INFO) << "Certificate manager started";
    }
  } else {
    LOG(WARNING)
        << "Failed to initialize certificate manager, continuing without it";
  }

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

  tbox::server::tcp_handler::VlmcsdHandler vlmcsd_handler(1688);
  ::vlmcsd_handler_ptr = &vlmcsd_handler;
  if (vlmcsd_handler.Start()) {
    LOG(INFO) << "vlmcsd TCP handler started successfully";
  } else {
    LOG(ERROR) << "Failed to start vlmcsd TCP handler";
  }

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
  tbox::logging::Shutdown();

  return 0;
}
