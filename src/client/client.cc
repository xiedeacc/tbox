/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include <condition_variable>
#include <mutex>

#include "folly/init/Init.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "src/client/grpc_client/grpc_client.h"

#if !defined(_WIN32)
#include <signal.h>
#endif

#include "src/util/config_manager.h"
#include "src/util/thread_pool.h"

#if !defined(_WIN32)
// https://github.com/grpc/grpc/issues/24884
tbox::client::GrpcClient *grpc_client_ptr = nullptr;

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
  grpc_client_ptr->Shutdown();
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
  LOG(INFO) << "Client initializing ...";
  folly::Init init(&argc, &argv, false);
  // google::InitGoogleLogging(argv[0]); // already called in folly::Init
  google::SetStderrLogging(google::GLOG_INFO);
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  LOG(INFO) << "CommandLine: " << google::GetArgv();

  tbox::util::ConfigManager::Instance()->Init("./conf/client_base_config.json");
  tbox::util::ThreadPool::Instance()->Init();

#if !defined(_WIN32)
  RegisterSignalHandler();
  std::thread shutdown_thread(ShutdownCheckingThread);
#endif

  tbox::client::GrpcClient grpc_client(
      tbox::util::ConfigManager::Instance()->ServerAddr(),
      std::to_string(tbox::util::ConfigManager::Instance()->GrpcServerPort()));
  ::grpc_client_ptr = &grpc_client;
  if (!grpc_client.Init()) {
    goto EXIT;
  }

  grpc_client.Start();
  grpc_client.Await();
  LOG(INFO) << "Now stopped grpc client";

EXIT:
#if !defined(_WIN32)
  if (shutdown_thread.joinable()) {
    shutdown_thread.join();
  }
#endif
  tbox::util::ThreadPool::Instance()->Stop();
  return 0;
}
