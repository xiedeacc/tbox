/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_SSL_CONFIG_MANAGER_H_
#define TBOX_CLIENT_SSL_CONFIG_MANAGER_H_

#include <memory>
#include <string>

#include "folly/Singleton.h"
#include "glog/logging.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

/// @brief Manages SSL configuration for gRPC connections.
/// @details Thread-safe singleton that handles loading SSL certificates
///          and other SSL-related configurations.
class SSLConfigManager final {
 public:
  /// @brief Get singleton instance.
  /// @return Shared pointer to SSLConfigManager instance.
  static std::shared_ptr<SSLConfigManager> Instance();

  ~SSLConfigManager() = default;

  /// @brief Load CA certificate from file.
  /// @param path Relative path from home directory.
  /// @return Certificate content as string, or empty on failure.
  std::string LoadCACert(const std::string& path);

 private:
  friend class folly::Singleton<SSLConfigManager>;
  SSLConfigManager() = default;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_SSL_CONFIG_MANAGER_H_

