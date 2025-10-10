/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/ssl_config_manager.h"

#include "glog/logging.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

static folly::Singleton<SSLConfigManager> ssl_config_manager;

std::shared_ptr<SSLConfigManager> SSLConfigManager::Instance() {
  return ssl_config_manager.try_get();
}

std::string SSLConfigManager::LoadCACert(const std::string& path) {
  std::string home_dir = tbox::util::Util::HomeDir();
  std::string full_path = home_dir + path;

  std::string ca_cert;
  if (!tbox::util::Util::LoadSmallFile(full_path, &ca_cert)) {
    LOG(ERROR) << "Failed to load certificate file: " << full_path;
    return "";
  }

  if (ca_cert.empty()) {
    LOG(ERROR) << "Certificate file is empty: " << full_path;
    return "";
  }

  LOG(INFO) << "Successfully loaded CA certificate from: " << full_path
            << " (" << ca_cert.size() << " bytes)";
  return ca_cert;
}

}  // namespace client
}  // namespace tbox

