/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_SSL_CONFIG_LOADER_H_
#define TBOX_CLIENT_SSL_CONFIG_LOADER_H_

#include <fstream>
#include <string>

#include "glog/logging.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

class SSLConfigLoader {
 public:
  // Load CA certificate from file
  static std::string LoadCACert(const std::string& path) {
    std::string home_dir = tbox::util::Util::HomeDir();
    std::string full_path = home_dir + path;
    
    std::ifstream file(full_path);
    if (!file.is_open()) {
      LOG(ERROR) << "Failed to open certificate file: " << full_path;
      return "";
    }

    std::string ca_cert((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    if (ca_cert.empty()) {
      LOG(ERROR) << "Certificate file is empty: " << full_path;
      return "";
    }
    
    LOG(INFO) << "Successfully loaded CA certificate from: " << full_path
              << " (" << ca_cert.size() << " bytes)";
    return ca_cert;
  }
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_SSL_CONFIG_LOADER_H_

