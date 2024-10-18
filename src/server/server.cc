/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "folly/init/Init.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "src/server/http_server_impl.h"

#if !defined(_WIN32)
#include <signal.h>
#endif

#include "src/server/grpc_server_impl.h"
#include "src/server/http_server_impl.h"
#include "src/server/server_context.h"
#include "src/util/config_manager.h"
#include "src/util/thread_pool.h"

#if !defined(_WIN32)
// https://github.com/grpc/grpc/issues/24884
tbox::server::GrpcServer *grpc_server_ptr = nullptr;
tbox::server::HttpServer *http_server_ptr = nullptr;
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
  grpc_server_ptr->Shutdown();
  http_server_ptr->Shutdown();
}

void RegisterSignalHandler() {
  signal(SIGTERM, &SignalHandler);
  signal(SIGINT, &SignalHandler);
  signal(SIGQUIT, &SignalHandler);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
}
#endif

int main(int argc, char **argv) {
  // ProfilerStart("tbox_profile");
  LOG(INFO) << "Grpc server initializing ...";

  folly::Init init(&argc, &argv, false);
  // google::InitGoogleLogging(argv[0]); // already called in folly::Init
  google::SetStderrLogging(google::GLOG_INFO);
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  LOG(INFO) << "CommandLine: " << google::GetArgv();

  tbox::util::ConfigManager::Instance()->Init("./conf/base_config.json");
  tbox::util::ThreadPool::Instance()->Init();

#if !defined(_WIN32)
  RegisterSignalHandler();
  std::thread shutdown_thread(ShutdownCheckingThread);
#endif

  std::shared_ptr<tbox::server::ServerContext> server_context =
      std::make_shared<tbox::server::ServerContext>();

  tbox::server::GrpcServer grpc_server(server_context);
  ::grpc_server_ptr = &grpc_server;

  tbox::server::HttpServer http_server(server_context);
  ::http_server_ptr = &http_server;

  grpc_server.Start();
  http_server.Start();

  grpc_server.WaitForShutdown();

  LOG(INFO) << "Now stopped grpc server";
  LOG(INFO) << "Now stopped http server";

#if !defined(_WIN32)
  if (shutdown_thread.joinable()) {
    shutdown_thread.join();
  }
#endif
  tbox::util::ThreadPool::Instance()->Stop();

  // ProfilerStop();
  return 0;
}
