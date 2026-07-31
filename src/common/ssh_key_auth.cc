/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/common/ssh_key_auth.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

#include "src/util/util.h"

namespace tbox {
namespace common {
namespace {

void AppendString(std::string_view value, std::string* output) {
  const uint32_t length = static_cast<uint32_t>(value.size());
  output->push_back(static_cast<char>((length >> 24) & 0xff));
  output->push_back(static_cast<char>((length >> 16) & 0xff));
  output->push_back(static_cast<char>((length >> 8) & 0xff));
  output->push_back(static_cast<char>(length & 0xff));
  output->append(value);
}

bool ReadFile(const std::string& path, std::string* content) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  *content = stream.str();
  return !content->empty();
}

constexpr size_t kSha256Bytes = 32;
constexpr size_t kSha256BlockBytes = 64;

void SecureClear(std::string* value) {
  volatile unsigned char* bytes =
      reinterpret_cast<volatile unsigned char*>(value->data());
  for (size_t i = 0; i < value->size(); ++i) {
    bytes[i] = 0;
  }
  value->clear();
}

bool ConstantTimeEqual(const void* left, const void* right, size_t length) {
  const auto* left_bytes = static_cast<const unsigned char*>(left);
  const auto* right_bytes = static_cast<const unsigned char*>(right);
  unsigned char difference = 0;
  for (size_t i = 0; i < length; ++i) {
    difference |= left_bytes[i] ^ right_bytes[i];
  }
  return difference == 0;
}

int HexValue(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool Sha256Raw(const std::string& input, std::string* output) {
  const std::string hex = util::Util::SHA256(input);
  if (hex.size() != kSha256Bytes * 2) {
    return false;
  }
  output->resize(kSha256Bytes);
  for (size_t i = 0; i < kSha256Bytes; ++i) {
    const int high = HexValue(hex[i * 2]);
    const int low = HexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) {
      output->clear();
      return false;
    }
    (*output)[i] = static_cast<char>((high << 4) | low);
  }
  return true;
}

bool CreateProof(const std::string& private_key_path,
                 const std::string& public_key_path,
                 std::string_view purpose, const std::string& message,
                 std::string* proof) {
  if (!proof) {
    return false;
  }

  const std::string expanded_private_path =
      SshKeyAuth::ExpandUserPath(private_key_path);
  std::string private_file;
  std::string public_file;
  const std::string expanded_public_path =
      SshKeyAuth::ExpandUserPath(public_key_path);
  if (!ReadFile(expanded_private_path, &private_file)) {
    return false;
  }
  if (!ReadFile(expanded_public_path, &public_file)) {
    if (!private_file.empty()) {
      SecureClear(&private_file);
    }
    return false;
  }

  std::istringstream public_stream(public_file);
  std::string key_type;
  std::string encoded_public_key;
  public_stream >> key_type >> encoded_public_key;
  if (key_type != "ssh-ed25519" || encoded_public_key.empty()) {
    SecureClear(&private_file);
    return false;
  }
  std::string key_material = private_file;
  AppendString(key_type, &key_material);
  AppendString(encoded_public_key, &key_material);
  std::string hmac_key;
  if (!Sha256Raw(key_material, &hmac_key)) {
    SecureClear(&private_file);
    SecureClear(&key_material);
    return false;
  }
  hmac_key.resize(kSha256BlockBytes, '\0');
  std::string inner_pad(kSha256BlockBytes, '\x36');
  std::string outer_pad(kSha256BlockBytes, '\x5c');
  for (size_t i = 0; i < kSha256BlockBytes; ++i) {
    inner_pad[i] = static_cast<char>(inner_pad[i] ^ hmac_key[i]);
    outer_pad[i] = static_cast<char>(outer_pad[i] ^ hmac_key[i]);
  }

  std::string proof_input;
  AppendString(purpose, &proof_input);
  AppendString(message, &proof_input);
  std::string inner_hash;
  const bool result =
      Sha256Raw(inner_pad + proof_input, &inner_hash) &&
      Sha256Raw(outer_pad + inner_hash, proof);

  SecureClear(&private_file);
  SecureClear(&key_material);
  SecureClear(&hmac_key);
  SecureClear(&inner_pad);
  SecureClear(&outer_pad);
  if (!inner_hash.empty()) {
    SecureClear(&inner_hash);
  }
  return result;
}

bool VerifyProof(const std::string& private_key_path,
                 const std::string& public_key_path,
                 std::string_view purpose, const std::string& message,
                 const std::string& proof) {
  std::string expected;
  const bool created =
      CreateProof(private_key_path, public_key_path, purpose, message,
                  &expected);
  const bool verified = created && proof.size() == expected.size() &&
                        ConstantTimeEqual(proof.data(), expected.data(),
                                          expected.size());
  if (!expected.empty()) {
    SecureClear(&expected);
  }
  return verified;
}

}  // namespace

std::string SshKeyAuth::ExpandUserPath(const std::string& path) {
  const char* configured_home = nullptr;
#if defined(_WIN32)
  configured_home = std::getenv("USERPROFILE");
#else
  configured_home = std::getenv("HOME");
#endif
  const std::string home =
      configured_home && *configured_home ? configured_home
                                          : util::Util::HomeDir();
  if (path == "~") {
    return home;
  }
  if (path.rfind("~/", 0) == 0 || path.rfind("~\\", 0) == 0) {
    return home + "/" + path.substr(2);
  }
  return path;
}

bool SshKeyAuth::CreateServerProof(const std::string& private_key_path,
                                   const std::string& public_key_path,
                                   const std::string& message,
                                   std::string* proof) {
  return CreateProof(private_key_path, public_key_path,
                     "tbox-login-server-proof-v1", message, proof);
}

bool SshKeyAuth::VerifyServerProof(const std::string& private_key_path,
                                   const std::string& public_key_path,
                                   const std::string& message,
                                   const std::string& proof) {
  return VerifyProof(private_key_path, public_key_path,
                     "tbox-login-server-proof-v1", message, proof);
}

bool SshKeyAuth::CreateClientProof(const std::string& private_key_path,
                                   const std::string& public_key_path,
                                   const std::string& message,
                                   std::string* proof) {
  return CreateProof(private_key_path, public_key_path,
                     "tbox-login-client-proof-v1", message, proof);
}

bool SshKeyAuth::VerifyClientProof(const std::string& private_key_path,
                                   const std::string& public_key_path,
                                   const std::string& message,
                                   const std::string& proof) {
  return VerifyProof(private_key_path, public_key_path,
                     "tbox-login-client-proof-v1", message, proof);
}

std::string SshKeyAuth::BuildLoginMessage(const std::string& challenge_id,
                                          const std::string& client_nonce,
                                          const std::string& server_nonce,
                                          const std::string& user,
                                          const std::string& client_id) {
  std::string message = "tbox-login-shared-ssh-key-v1";
  AppendString(challenge_id, &message);
  AppendString(client_nonce, &message);
  AppendString(server_nonce, &message);
  AppendString(user, &message);
  AppendString(client_id, &message);
  return message;
}

}  // namespace common
}  // namespace tbox
