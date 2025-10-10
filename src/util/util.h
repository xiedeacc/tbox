/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_UTIL_H
#define TBOX_UTIL_UTIL_H

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

#include "boost/algorithm/string/replace.hpp"
#include "folly/IPAddress.h"
#include "google/protobuf/message.h"
#include "openssl/types.h"

namespace tbox {
namespace util {

class Util final {
 private:
  Util() {}
  ~Util() {}

 public:
  static constexpr int kSaltSize = 16;        // 16 bytes (128 bits)
  static constexpr int kDerivedKeySize = 32;  // 32 bytes (256 bits)
  static constexpr int kIterations = 100000;  // PBKDF2 iterations

  static int64_t CurrentTimeMillis();
  static int64_t CurrentTimeSeconds();
  static int64_t CurrentTimeNanos();
  static int64_t StrToTimeStampUTC(const std::string& time);
  static int64_t StrToTimeStampUTC(const std::string& time,
                                   const std::string& format);
  static int64_t StrToTimeStamp(const std::string& time);
  static int64_t StrToTimeStamp(const std::string& time,
                                const std::string& format);
  static int64_t StrToTimeStamp(const std::string& time,
                                const std::string& format,
                                const std::string& tz_str);

  static std::string ToTimeStrUTC();
  static std::string ToTimeStrUTC(const int64_t ts, const std::string& format);
  // CST chinese standard time
  static std::string ToTimeStr();
  static std::string ToTimeStr(const int64_t ts);
  static std::string ToTimeStr(const int64_t ts, const std::string& format);
  static std::string ToTimeStr(const int64_t ts, const std::string& format,
                               const std::string& tz_str);
  static struct timespec ToTimeSpec(const int64_t ts);

  static int64_t Random(int64_t start, int64_t end);

  static void Sleep(int64_t ms);

  int32_t WriteToFile(const std::string& path, const std::string& content,
                      const bool append);
  static bool LoadSmallFile(const std::string& path, std::string* content);

  static std::string UUID();
  static std::string ToUpper(const std::string& str);

  static std::string ToLower(const std::string& str);

  static void ToLower(std::string* str);

  static void Trim(std::string* s);

  static std::string Trim(const std::string& s);

  template <class TypeName>
  static bool ToInt(std::string_view str, TypeName* value) {
    auto result = std::from_chars(str.data(), str.data() + str.size(), *value);
    if (result.ec != std::errc{}) {
      return false;
    }
    return true;
  }

  template <class TypeName>
  static TypeName ToInt(std::string_view str) {
    TypeName num = 0;
    auto result = std::from_chars(str.data(), str.data() + str.size(), num);
    if (result.ec != std::errc{}) {
      return 0;
    }
    return num;
  }

  static bool StartWith(const std::string& str, const std::string& prefix);

  static bool EndWith(const std::string& str, const std::string& postfix);

  static void ReplaceAll(std::string* s, const std::string& from,
                         const std::string& to) {
    boost::algorithm::replace_all(*s, from, to);
  }

  template <class TypeName>
  static void ReplaceAll(std::string* s, const std::string& from,
                         const TypeName to) {
    boost::algorithm::replace_all(*s, from, std::to_string(to));
  }

  static void Split(const std::string& str, const std::string& delim,
                    std::vector<std::string>* result, bool trim_empty = true);

  static std::string Base64Encode(const std::string& input);

  static std::string Base64Decode(const std::string& input);

  static void Base64Encode(const std::string& input, std::string* out);

  static void Base64Decode(const std::string& input, std::string* out);

  static uint32_t CRC32(const std::string& content);

  static bool Blake3(const std::string& content, std::string* out,
                     const bool use_upper_case = false);
  static bool FileBlake3(const std::string& path, std::string* out,
                         const bool use_upper_case = false);

  static EVP_MD_CTX* HashInit(const EVP_MD* type);
  static bool HashUpdate(EVP_MD_CTX* context, const std::string& str);
  static bool HashFinal(EVP_MD_CTX* context, std::string* out,
                        const bool use_upper_case = false);

  static EVP_MD_CTX* SHA256Init();
  static bool SHA256Update(EVP_MD_CTX* context, const std::string& str);
  static bool SHA256Final(EVP_MD_CTX* context, std::string* out,
                          const bool use_upper_case = false);

  static bool Hash(const std::string& str, const EVP_MD* type, std::string* out,
                   const bool use_upper_case = false);

  static bool FileHash(const std::string& path, const EVP_MD* type,
                       std::string* out, const bool use_upper_case = false);

  static bool SmallFileHash(const std::string& path, const EVP_MD* type,
                            std::string* out,
                            const bool use_upper_case = false);

  static bool SHA256(const std::string& str, std::string* out,
                     const bool use_upper_case = false);

  static std::string SHA256(const std::string& str,
                            const bool use_upper_case = false);

  static bool SHA256_libsodium(const std::string& str, std::string* out,
                               const bool use_upper_case = false);

  static bool SmallFileSHA256(const std::string& path, std::string* out,
                              const bool use_upper_case = false);

  static bool FileSHA256(const std::string& path, std::string* out,
                         const bool use_upper_case = false);

  static bool MD5(const std::string& str, std::string* out,
                  const bool use_upper_case = false);

  static bool SmallFileMD5(const std::string& path, std::string* out,
                           const bool use_upper_case = false);

  static bool FileMD5(const std::string& path, std::string* out,
                      const bool use_upper_case = false);

  static std::string GenerateSalt();
  static bool HashPassword(const std::string& password, const std::string& salt,
                           std::string* hash);
  static bool VerifyPassword(const std::string& password,
                             const std::string& salt,
                             const std::string& stored_hash);

  static bool HexStrToInt64(const std::string& in, int64_t* out);

  static std::string ToHexStr(const uint64_t in,
                              const bool use_upper_case = false);

  static void ToHexStr(const std::string& in, std::string* out,
                       const bool use_upper_case = false);

  static std::string ToHexStr(const std::string& in,
                              const bool use_upper_case = false);
  static std::string HexToStr(const std::string& in);
  static bool HexToStr(const std::string& in, std::string* out);

  static int64_t MurmurHash64A(const std::string& str);

  static bool LZMACompress(const std::string& data, std::string* out);
  static bool LZMADecompress(const std::string& data, std::string* out);

  static void PrintProtoMessage(const google::protobuf::Message& msg);

  static bool MessageToJson(const google::protobuf::Message& msg,
                            std::string* json, const bool format = false);
  static std::string MessageToJson(const google::protobuf::Message& msg,
                                   const bool format = false);
  static bool MessageToPrettyJson(const google::protobuf::Message& msg,
                                  std::string* json);
  static bool JsonToMessage(const std::string& json,
                            google::protobuf::Message* msg);

  static std::optional<std::string> GetEnv(const char* var_name) {
    const char* value = std::getenv(var_name);
    if (value) {
      return std::string(value);
    } else {
      return std::nullopt;
    }
  }

  static int64_t FDCount();
  static int64_t MemUsage();
  static bool IsMountPoint(const std::string& path);

  static void ListAllIPAddresses(std::vector<folly::IPAddress>* ip_addrs);

  // Network information utilities
  // Get all local IPv4 addresses (excluding loopback)
  static std::vector<std::string> GetLocalIPv4Addresses();

  // Get all local IPv6 addresses (excluding loopback and link-local)
  static std::vector<std::string> GetLocalIPv6Addresses();

  // Get all local IP addresses (both IPv4 and IPv6)
  static std::vector<std::string> GetAllLocalIPAddresses();

  // Get public IPv6 addresses (excludes link-local, loopback, private, and ULA)
  static std::vector<std::string> GetPublicIPv6Addresses();

  // Resolve domain to IPv6 addresses using c-ares
  // c-ares provides better control over DNS resolution, caching, and TTL
  static std::vector<std::string> ResolveDomainToIPv6(
      const std::string& domain);

  static std::string ExecutablePath();
  static std::string HomeDir();
};

}  // namespace util
}  // namespace tbox

#endif /* TBOX_UTIL_UTIL_H */
