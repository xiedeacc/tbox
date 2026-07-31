/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_PLATFORM_CA_BUNDLE_H_
#define TBOX_CLIENT_PLATFORM_CA_BUNDLE_H_

#include <string>

namespace tbox {
namespace client {

/// @brief Synchronizes the configured PEM bundle with platform trust roots.
class PlatformCABundle {
 public:
  enum class UpdateResult {
    kUnchanged,
    kUpdated,
    kError,
  };

  /// @brief Export platform trust roots to a PEM bundle when they changed.
  /// @details Windows exports LocalMachine and CurrentUser ROOT stores. If the
  ///          stores cannot be read during first bootstrap, Git for Windows is
  ///          used as a fallback. Linux and macOS copy their system CA bundle.
  static UpdateResult Refresh(const std::string& destination);
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_PLATFORM_CA_BUNDLE_H_
