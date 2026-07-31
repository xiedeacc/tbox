/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_COMMON_SSH_KEY_AUTH_H_
#define TBOX_COMMON_SSH_KEY_AUTH_H_

#include <string>

namespace tbox {
namespace common {

class SshKeyAuth final {
 public:
  static bool CreateServerProof(const std::string& private_key_path,
                                const std::string& public_key_path,
                                const std::string& message,
                                std::string* proof);

  static bool VerifyServerProof(const std::string& private_key_path,
                                const std::string& public_key_path,
                                const std::string& message,
                                const std::string& proof);

  static bool CreateClientProof(const std::string& private_key_path,
                                const std::string& public_key_path,
                                const std::string& message,
                                std::string* proof);

  static bool VerifyClientProof(const std::string& private_key_path,
                                const std::string& public_key_path,
                                const std::string& message,
                                const std::string& proof);

  static std::string BuildLoginMessage(const std::string& challenge_id,
                                       const std::string& client_nonce,
                                       const std::string& server_nonce,
                                       const std::string& user,
                                       const std::string& client_id);

  static std::string ExpandUserPath(const std::string& path);
};

}  // namespace common
}  // namespace tbox

#endif  // TBOX_COMMON_SSH_KEY_AUTH_H_
