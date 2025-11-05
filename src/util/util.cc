/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/util.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <random>
#include <set>
#include <string>
#include <system_error>
#include <thread>

#include "absl/time/time.h"
#include "ares.h"
#include "boost/algorithm/string/predicate.hpp"
#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string/trim_all.hpp"
#include "boost/beast/core/detail/base64.hpp"
#include "boost/uuid/random_generator.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "crc32c/crc32c.h"
#include "external/blake3/c/blake3.h"
#include "external/xz/src/liblzma/api/lzma.h"
#include "fmt/core.h"
#include "folly/Range.h"
#include "folly/String.h"
#include "glog/logging.h"
#include "google/protobuf/json/json.h"
#include "openssl/evp.h"
#include "openssl/rand.h"
#include "sodium/crypto_hash_sha256.h"
#include "src/MurmurHash2.h"
#include "src/common/defs.h"
#include "src/common/error.h"
#include "src/proto/service.pb.h"

#if defined(_WIN32)
#elif defined(__linux__)
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

using google::protobuf::json::ParseOptions;
using google::protobuf::json::PrintOptions;
using std::string;

namespace tbox {
namespace util {

int64_t Util::CurrentTimeMillis() {
  return absl::GetCurrentTimeNanos() / 1000000;
}

int64_t Util::CurrentTimeSeconds() {
  return absl::GetCurrentTimeNanos() / 1000000000;
}

int64_t Util::CurrentTimeNanos() {
  return absl::GetCurrentTimeNanos();
}

int64_t Util::StrToTimeStampUTC(const string& time) {
  return Util::StrToTimeStamp(time, "%Y-%m-%d%ET%H:%M:%E3S%Ez");
}

int64_t Util::StrToTimeStampUTC(const string& time, const string& format) {
  absl::TimeZone tz = absl::UTCTimeZone();
  absl::Time t;
  string err;
  if (!absl::ParseTime(format, time, tz, &t, &err)) {
    LOG(ERROR) << err << " " << time << ", format: " << format;
    return -1;
  }
  return absl::ToUnixMillis(t);
}

int64_t Util::StrToTimeStamp(const string& time) {
  return Util::StrToTimeStamp(time, "%Y-%m-%d%ET%H:%M:%E3S%Ez");
}

int64_t Util::StrToTimeStamp(const string& time, const string& format) {
  absl::TimeZone tz = absl::LocalTimeZone();
  absl::Time t;
  string err;
  if (!absl::ParseTime(format, time, tz, &t, &err)) {
    LOG(ERROR) << err << " " << time << ", format: " << format;
    return -1;
  }
  return absl::ToUnixMillis(t);
}

int64_t Util::StrToTimeStamp(const string& time, const string& format,
                             const string& tz_str) {
  absl::TimeZone tz;
  if (!absl::LoadTimeZone(tz_str, &tz)) {
    LOG(ERROR) << "Load time zone error: " << tz_str;
  }
  absl::Time t;
  string err;
  if (!absl::ParseTime(format, time, tz, &t, &err)) {
    LOG(ERROR) << err << " " << time << ", format: " << format;
    return -1;
  }

  return absl::ToUnixMillis(t);
}

string Util::ToTimeStrUTC() {
  return Util::ToTimeStrUTC(Util::CurrentTimeMillis(),
                            "%Y-%m-%d%ET%H:%M:%E3S%Ez");
}

string Util::ToTimeStrUTC(const int64_t ts, const string& format) {
  absl::TimeZone tz = absl::UTCTimeZone();
  return absl::FormatTime(format, absl::FromUnixMillis(ts), tz);
}

string Util::ToTimeStr() {
  return Util::ToTimeStr(Util::CurrentTimeMillis(), "%Y-%m-%d%ET%H:%M:%E3S%Ez",
                         "localtime");
}

string Util::ToTimeStr(const int64_t ts) {
  return Util::ToTimeStr(ts, "%Y-%m-%d%ET%H:%M:%E3S%Ez", "localtime");
}

string Util::ToTimeStr(const int64_t ts, const string& format) {
  absl::TimeZone tz = absl::LocalTimeZone();
  return absl::FormatTime(format, absl::FromUnixMillis(ts), tz);
}

string Util::ToTimeStr(const int64_t ts, const string& format,
                       const string& tz_str) {
  absl::TimeZone tz;
  if (!absl::LoadTimeZone(tz_str, &tz)) {
    LOG(ERROR) << "Load time zone error: " << tz_str;
  }
  return absl::FormatTime(format, absl::FromUnixMillis(ts), tz);
}

struct timespec Util::ToTimeSpec(const int64_t ts) {
  struct timespec time;
  time.tv_sec = ts / 1000;
  time.tv_nsec = (ts % 1000) * 1000000;
  return time;
}

int64_t Util::Random(int64_t start, int64_t end) {
  static thread_local std::mt19937 generator(CurrentTimeNanos());
  std::uniform_int_distribution<int64_t> distribution(start, end - 1);
  return distribution(generator);
}

void Util::Sleep(int64_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int32_t Util::WriteToFile(const string& path, const string& content,
                          const bool append) {
  try {
    if (!std::filesystem::exists(path)) {
      std::filesystem::path s_path(path);
      if (s_path.has_parent_path()) {
        std::filesystem::create_directories(s_path.parent_path());
      }
    }

    std::ofstream ofs(path, (append ? std::ios::app : std::ios::trunc) |
                                std::ios::out | std::ios::binary);
    if (ofs && ofs.is_open()) {
      ofs << content;
      ofs.close();
      return Err_Success;
    } else {
      if (errno == EACCES) {
        return Err_File_permission;
      } else if (errno == ENOSPC) {
        return Err_File_disk_full;
      }
      LOG(INFO) << std::strerror(errno);
      return Err_Fail;
    }
  } catch (const std::filesystem::filesystem_error& e) {
    LOG(ERROR) << (!append ? "Write to " : "Append to ") << path
               << ", error: " << e.what();
  }
  return Err_Fail;
}

bool Util::LoadSmallFile(const string& path, string* content) {
  std::ifstream in(path, std::ios::binary);
  if (!in || !in.is_open()) {
    LOG(ERROR) << "Fail to open " << path
               << ", please check file exists and file permission";
    return false;
  }

  in.seekg(0, std::ios::end);
  content->reserve(in.tellg());
  in.seekg(0, std::ios::beg);

  std::copy((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>(), std::back_inserter(*content));
  in.close();
  return true;
}

string Util::ToUpper(const string& str) {
  string ret = str;
  transform(ret.begin(), ret.end(), ret.begin(),
            [](unsigned char c) { return toupper(c); });
  // transform(ret.begin(), ret.end(), ret.begin(), ::toupper);
  return ret;
}

string Util::ToLower(const string& str) {
  string ret = str;
  transform(ret.begin(), ret.end(), ret.begin(), ::tolower);
  return ret;
}

void Util::ToLower(string* str) {
  transform(str->begin(), str->end(), str->begin(), ::tolower);
}

void Util::Trim(string* str) {
  boost::algorithm::trim_right(*str);
  boost::algorithm::trim_left(*str);
}

string Util::Trim(const string& str) {
  string trimmed_str = str;
  Trim(&trimmed_str);
  return trimmed_str;
}

bool Util::StartWith(const string& str, const string& prefix) {
  return boost::starts_with(str, prefix);
}

bool Util::EndWith(const string& str, const string& postfix) {
  return boost::ends_with(str, postfix);
}

void Util::Split(const string& str, const string& delim,
                 std::vector<string>* result, bool trim_empty) {
  result->clear();
  if (str.empty()) {
    return;
  }
  if (trim_empty) {
    string trimed_str = boost::algorithm::trim_all_copy(str);
    boost::split(*result, trimed_str, boost::is_any_of(delim));
    return;
  }
  boost::split(*result, str, boost::is_any_of(delim));
}

string Util::UUID() {
  boost::uuids::random_generator generator;
  return boost::uuids::to_string(generator());
}

string Util::ToHexStr(const uint64_t in, const bool use_upper_case) {
  if (use_upper_case) {
    return fmt::format("{:016X}", in);
  } else {
    return fmt::format("{:016x}", in);
  }
}

void Util::ToHexStr(const string& in, string* out, const bool use_upper_case) {
  out->clear();
  out->reserve(in.size() * 2);
  for (std::size_t i = 0; i < in.size(); ++i) {
    if (use_upper_case) {
      out->append(fmt::format("{:02X}", (unsigned char)in[i]));
    } else {
      out->append(fmt::format("{:02x}", (unsigned char)in[i]));
    }
  }
}

string Util::ToHexStr(const string& in, const bool use_upper_case) {
  string out;
  ToHexStr(in, &out, use_upper_case);
  return out;
}

bool Util::HexToStr(const string& in, string* out) {
  return folly::unhexlify(in, *out);
}

string Util::HexToStr(const string& in) {
  string out;
  auto ret = HexToStr(in, &out);
  if (ret) {
    return out;
  }
  out.clear();
  return out;
}

bool Util::HexStrToInt64(const string& in, int64_t* out) {
  *out = 0;
  auto result = std::from_chars(in.data(), in.data() + in.size(), *out, 16);
  if (result.ec == std::errc()) {
    return false;
  }
  return true;
}

uint32_t Util::CRC32(const string& content) {
  return crc32c::Crc32c(content);
}

bool Util::Blake3(const string& content, string* out,
                  const bool use_upper_case) {
  uint8_t hash[BLAKE3_OUT_LEN];

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, content.data(), content.size());
  blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);

  string s(reinterpret_cast<const char*>(hash), BLAKE3_OUT_LEN);
  Util::ToHexStr(s, out, use_upper_case);
  return true;
}

bool Util::FileBlake3(const string& path, string* out,
                      const bool use_upper_case) {
  std::ifstream file(path, std::ios::binary);
  if (!file || !file.is_open()) {
    return false;
  }

  uint8_t hash[BLAKE3_OUT_LEN];

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);

  std::vector<char> buffer(common::CALC_BUFFER_SIZE_BYTES);  // 16KB buffer
  while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
    blake3_hasher_update(&hasher, buffer.data(), file.gcount());
  }

  blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);

  if (file.bad()) {
    return false;
  }

  string s(reinterpret_cast<const char*>(hash), BLAKE3_OUT_LEN);
  Util::ToHexStr(s, out, use_upper_case);
  return true;
}

void Util::Base64Encode(const string& input, string* out) {
  out->resize(boost::beast::detail::base64::encoded_size(input.size()));
  auto const ret = boost::beast::detail::base64::encode(
      out->data(), input.data(), input.size());
  out->resize(ret);
}

string Util::Base64Encode(const string& input) {
  string out;
  Base64Encode(input, &out);
  return out;
}

void Util::Base64Decode(const string& input, string* out) {
  out->resize(boost::beast::detail::base64::decoded_size(input.size()));
  auto const ret = boost::beast::detail::base64::decode(
      out->data(), input.data(), input.size());
  out->resize(ret.first);
  return;
}

string Util::Base64Decode(const string& input) {
  string out;
  Base64Decode(input, &out);
  return out;
}

int64_t Util::MurmurHash64A(const string& str) {
  return ::MurmurHash64A(str.data(), str.size(), 42L);
}

EVP_MD_CTX* Util::HashInit(const EVP_MD* type) {
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (!context) {
    return nullptr;
  }
  if (EVP_DigestInit_ex(context, type, nullptr) != 1) {
    EVP_MD_CTX_free(context);
    return nullptr;
  }
  return context;
}

bool Util::HashUpdate(EVP_MD_CTX* context, const string& str) {
  if (EVP_DigestUpdate(context, str.data(), str.size()) != 1) {
    EVP_MD_CTX_free(context);
    return false;
  }
  return true;
}

bool Util::HashFinal(EVP_MD_CTX* context, string* out,
                     const bool use_upper_case) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int length;
  if (EVP_DigestFinal_ex(context, hash, &length) != 1) {
    EVP_MD_CTX_free(context);
    return false;
  }

  EVP_MD_CTX_free(context);

  string s(reinterpret_cast<const char*>(hash), length);
  Util::ToHexStr(s, out, use_upper_case);
  return true;
}

EVP_MD_CTX* Util::SHA256Init() {
  return HashInit(EVP_sha256());
}

bool Util::SHA256Update(EVP_MD_CTX* context, const string& str) {
  return HashUpdate(context, str);
}

bool Util::SHA256Final(EVP_MD_CTX* context, string* out,
                       const bool use_upper_case) {
  return HashFinal(context, out, use_upper_case);
}

bool Util::Hash(const string& str, const EVP_MD* type, string* out,
                const bool use_upper_case) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int length;

  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (!context) {
    return false;
  }
  if (EVP_DigestInit_ex(context, type, nullptr) != 1 ||
      EVP_DigestUpdate(context, str.data(), str.size()) != 1 ||
      EVP_DigestFinal_ex(context, hash, &length) != 1) {
    EVP_MD_CTX_free(context);
    return false;
  }

  EVP_MD_CTX_free(context);

  string s(reinterpret_cast<const char*>(hash), length);
  Util::ToHexStr(s, out, use_upper_case);
  return true;
}

bool Util::FileHash(const string& path, const EVP_MD* type, string* out,
                    const bool use_upper_case) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int length;

  std::ifstream file(path);
  if (!file || !file.is_open()) {
    LOG(ERROR) << "Check file exists or file permissions";
    return false;
  }

  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) {
    return false;
  }

  if (EVP_DigestInit_ex(context, type, nullptr) != 1) {
    EVP_MD_CTX_free(context);
    return false;
  }

  std::vector<char> buffer(common::CALC_BUFFER_SIZE_BYTES);
  while (file.read(buffer.data(), common::CALC_BUFFER_SIZE_BYTES) ||
         file.gcount() > 0) {
    if (EVP_DigestUpdate(context, buffer.data(), file.gcount()) != 1) {
      EVP_MD_CTX_free(context);
      return false;
    }
  }

  if (EVP_DigestFinal_ex(context, hash, &length) != 1) {
    EVP_MD_CTX_free(context);
    return false;
  }

  EVP_MD_CTX_free(context);

  string s(reinterpret_cast<const char*>(hash), length);
  Util::ToHexStr(s, out, use_upper_case);
  return true;
}

bool Util::SmallFileHash(const string& path, const EVP_MD* type, string* out,
                         const bool use_upper_case) {
  string str;
  if (!Util::LoadSmallFile(path, &str)) {
    return false;
  }
  return Hash(str, type, out, use_upper_case);
}

bool Util::MD5(const string& str, string* out, const bool use_upper_case) {
  return Hash(str, EVP_md5(), out, use_upper_case);
}

bool Util::SmallFileMD5(const string& path, string* out,
                        const bool use_upper_case) {
  return SmallFileHash(path, EVP_md5(), out, use_upper_case);
}

bool Util::FileMD5(const string& path, string* out, const bool use_upper_case) {
  return Util::FileHash(path, EVP_md5(), out, use_upper_case);
}

bool Util::SHA256(const string& str, string* out, const bool use_upper_case) {
  return Hash(str, EVP_sha256(), out, use_upper_case);
}

string Util::SHA256(const string& str, const bool use_upper_case) {
  string out;
  Hash(str, EVP_sha256(), &out, use_upper_case);
  return out;
}

bool Util::SHA256_libsodium(const string& str, string* out,
                            const bool use_upper_case) {
  unsigned char hash[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(hash, reinterpret_cast<const unsigned char*>(str.data()),
                     str.size());

  string s(reinterpret_cast<const char*>(hash), crypto_hash_sha256_BYTES);
  Util::ToHexStr(s, out, use_upper_case);
  return true;
}

bool Util::SmallFileSHA256(const string& path, string* out,
                           const bool use_upper_case) {
  return SmallFileHash(path, EVP_sha256(), out, use_upper_case);
}

bool Util::FileSHA256(const string& path, string* out,
                      const bool use_upper_case) {
  return FileHash(path, EVP_sha256(), out, use_upper_case);
}

string Util::GenerateSalt() {
  std::string salt;
  salt.resize(kSaltSize);
  if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()), kSaltSize) !=
      1) {
    throw std::runtime_error("Failed to generate random salt.");
  }
  // Store salt as hex string to align with oceanfile implementation
  return Util::ToHexStr(salt);
}

bool Util::HashPassword(const string& password, const string& salt,
                        string* hash) {
  hash->resize(kDerivedKeySize);
  if (PKCS5_PBKDF2_HMAC(password.c_str(), password.size(),
                        reinterpret_cast<const unsigned char*>(salt.c_str()),
                        salt.size(), kIterations, EVP_sha256(), kDerivedKeySize,
                        reinterpret_cast<unsigned char*>(hash->data())) != 1) {
    return false;
  }
  // Store derived key as hex string to align with oceanfile implementation
  *hash = Util::ToHexStr(*hash);
  return true;
}

bool Util::VerifyPassword(const string& password, const string& salt,
                          const string& stored_hash) {
  string computed_hash;
  if (!HashPassword(password, salt, &computed_hash)) {
    return false;
  }
  return computed_hash == stored_hash;
}

bool Util::LZMACompress(const string& data, string* out) {
  lzma_stream strm = LZMA_STREAM_INIT;
  lzma_ret ret =
      lzma_easy_encoder(&strm, LZMA_PRESET_DEFAULT, LZMA_CHECK_CRC64);

  if (ret != LZMA_OK) {
    return false;
  }

  out->clear();
  out->resize(data.size() + data.size() / 3 + 128);

  strm.next_in = reinterpret_cast<const uint8_t*>(data.data());
  strm.avail_in = data.size();
  strm.next_out = reinterpret_cast<uint8_t*>(out->data());
  strm.avail_out = out->size();

  ret = lzma_code(&strm, LZMA_FINISH);

  if (ret != LZMA_STREAM_END) {
    lzma_end(&strm);
    return false;
  }

  out->resize(out->size() - strm.avail_out);
  lzma_end(&strm);
  return true;
}

bool Util::LZMADecompress(const string& data, string* out) {
  lzma_stream strm = LZMA_STREAM_INIT;
  lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);

  if (ret != LZMA_OK) {
    return false;
  }

  std::vector<uint8_t> decompressed_data(common::CALC_BUFFER_SIZE_BYTES);

  strm.next_in = reinterpret_cast<const uint8_t*>(data.data());
  strm.avail_in = data.size();
  strm.next_out = decompressed_data.data();
  strm.avail_out = decompressed_data.size();

  do {
    ret = lzma_code(&strm, LZMA_FINISH);
    if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
      out->append(reinterpret_cast<const char*>(decompressed_data.data()),
                  common::CALC_BUFFER_SIZE_BYTES - strm.avail_out);
      strm.next_out = decompressed_data.data();
      strm.avail_out = common::CALC_BUFFER_SIZE_BYTES;
    }

    if (ret != LZMA_OK) {
      break;
    }
  } while (true);

  if (ret != LZMA_STREAM_END) {
    lzma_end(&strm);
    return false;
  }

  lzma_end(&strm);
  return true;
}

void Util::PrintProtoMessage(const google::protobuf::Message& msg) {
  static PrintOptions option = {false, true, true, true, true};
  string json_value;
  if (!MessageToJsonString(msg, &json_value, option).ok()) {
    LOG(ERROR) << "to json string failed";
  }
  LOG(INFO) << "json_value: " << json_value;
}

bool Util::MessageToJson(const google::protobuf::Message& msg, string* json,
                         const bool format) {
  PrintOptions option = {false, true, true, true, true};
  if (format) {
    option.add_whitespace = true;
  }
  if (!MessageToJsonString(msg, json, option).ok()) {
    return false;
  }
  return true;
}

string Util::MessageToJson(const google::protobuf::Message& msg,
                           const bool format) {
  PrintOptions option = {false, true, true, true, true};
  if (format) {
    option.add_whitespace = true;
  }
  std::string json;
  if (!MessageToJsonString(msg, &json, option).ok()) {
    return "";
  }
  return json;
}

bool Util::MessageToPrettyJson(const google::protobuf::Message& msg,
                               string* json) {
  static PrintOptions option = {true, true, false, true, true};
  if (!MessageToJsonString(msg, json, option).ok()) {
    return false;
  }
  return true;
}

bool Util::JsonToMessage(const string& json, google::protobuf::Message* msg) {
  static ParseOptions option = {true, false};
  if (!JsonStringToMessage(json, msg, option).ok()) {
    return false;
  }
  return true;
}

int64_t Util::FDCount() {
  int fd_count = 0;
#if defined(__linux__)

  struct rlimit limit;
  getrlimit(RLIMIT_NOFILE, &limit);

  const char* fd_dir = "/proc/self/fd";
  DIR* dir = opendir(fd_dir);
  if (dir == nullptr) {
    limit.rlim_cur += 10000;
    setrlimit(RLIMIT_NOFILE, &limit);

    dir = opendir(fd_dir);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_name[0] == '.')
        continue;
      fd_count++;
      string fd_path = string(fd_dir) + "/" + entry->d_name;
      char link_target[256];
      ssize_t len =
          readlink(fd_path.c_str(), link_target, sizeof(link_target) - 1);
      if (len != -1) {
        link_target[len] = '\0';
        LOG(INFO) << link_target;
      } else {
        perror("Failed to read link");
      }
    }
    LOG(INFO) << fd_count << ", soft limit: " << limit.rlim_cur << ", "
              << limit.rlim_max;

    perror("Could not open /proc/self/fd");
    return -1;
  }

  closedir(dir);
#endif
  return fd_count;
}

int64_t Util::MemUsage() {
#if defined(__linux__)
  std::ifstream statm("/proc/self/statm");
  if (!statm) {
    return -1;
  }

  long size, resident, share, text, lib, data, dt;
  statm >> size >> resident >> share >> text >> lib >> data >> dt;

  long pageSize = sysconf(_SC_PAGESIZE);  // in bytes
  return resident * pageSize / 1024 / 1024;
#endif
}

// TODO(xieyz) detect mount point
bool IsMountPoint(const string& path) {
  if (path == "/data") {
    return true;
  }
  return false;
}

void Util::ListAllIPAddresses(std::vector<folly::IPAddress>* ip_addrs) {
#if defined(__linux__) || defined(__APPLE__)
  struct ifaddrs* ifAddrStruct = nullptr;
  struct ifaddrs* ifa = nullptr;
  void* addr_ptr = nullptr;
  getifaddrs(&ifAddrStruct);

  for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr)
      continue;
    if (ifa->ifa_addr->sa_family == AF_INET) {  // IPv4
      addr_ptr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
      char addr_buf[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, addr_ptr, addr_buf, INET_ADDRSTRLEN);
      ip_addrs->emplace_back(folly::IPAddress(addr_buf));
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {  // IPv6
      addr_ptr = &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
      char addr_buf[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, addr_ptr, addr_buf, INET6_ADDRSTRLEN);
      ip_addrs->emplace_back(folly::IPAddress(addr_buf));
    }
  }

  if (ifAddrStruct != nullptr) {
    freeifaddrs(ifAddrStruct);
  }
#elif defined(_WIN32)

#endif
}

// RAII wrapper for ifaddrs to ensure proper cleanup
class IfAddrsGuard {
 public:
  explicit IfAddrsGuard(struct ifaddrs* ptr) : ptr_(ptr) {}
  ~IfAddrsGuard() {
    if (ptr_) {
      freeifaddrs(ptr_);
    }
  }

  // Delete copy operations
  IfAddrsGuard(const IfAddrsGuard&) = delete;
  IfAddrsGuard& operator=(const IfAddrsGuard&) = delete;

  struct ifaddrs* get() const { return ptr_; }

 private:
  struct ifaddrs* ptr_;
};

std::vector<std::string> Util::GetLocalIPv4Addresses() {
  struct ifaddrs* ifaddrs_ptr = nullptr;
  std::vector<std::string> ip_addresses;

  if (getifaddrs(&ifaddrs_ptr) == -1) {
    LOG(ERROR) << "Failed to get network interfaces: " << std::strerror(errno);
    return ip_addresses;
  }

  IfAddrsGuard guard(ifaddrs_ptr);
  std::set<std::string> unique_addresses;

  for (struct ifaddrs* ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }

    // Skip loopback interface by name
    if (ifa->ifa_name && std::string(ifa->ifa_name) == "lo") {
      continue;
    }

    try {
      auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
      folly::IPAddress ip_addr(
          folly::IPAddressV4::fromLongHBO(ntohl(sa->sin_addr.s_addr)));

      // Skip loopback addresses using folly's built-in check
      if (ip_addr.isLoopback()) {
        continue;
      }

      std::string ip_str = ip_addr.str();
      unique_addresses.insert(ip_str);
    } catch (const std::exception& e) {
      LOG(WARNING) << "Failed to parse IPv4 address for interface: "
                   << (ifa->ifa_name ? ifa->ifa_name : "unknown") << " - "
                   << e.what();
    }
  }

  ip_addresses.assign(unique_addresses.begin(), unique_addresses.end());

  if (ip_addresses.empty()) {
    LOG(WARNING) << "No non-loopback IPv4 addresses found";
    ip_addresses.push_back("unknown");
  }

  return ip_addresses;
}

std::vector<std::string> Util::GetLocalIPv6Addresses() {
  struct ifaddrs* ifaddrs_ptr = nullptr;
  std::vector<std::string> ip_addresses;

  if (getifaddrs(&ifaddrs_ptr) == -1) {
    LOG(ERROR) << "Failed to get network interfaces: " << std::strerror(errno);
    return ip_addresses;
  }

  IfAddrsGuard guard(ifaddrs_ptr);
  std::set<std::string> unique_addresses;

  for (struct ifaddrs* ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    // Skip loopback interface by name
    if (ifa->ifa_name && std::string(ifa->ifa_name) == "lo") {
      continue;
    }

    try {
      auto* sa = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
      folly::IPAddressV6 ipv6_addr = folly::IPAddressV6::fromBinary(
          folly::ByteRange(sa->sin6_addr.s6_addr, 16));

      // Skip loopback and link-local addresses using folly's built-in checks
      if (ipv6_addr.isLoopback() || ipv6_addr.isLinkLocal()) {
        continue;
      }

      std::string ip_str = ipv6_addr.str();
      unique_addresses.insert(ip_str);
    } catch (const std::exception& e) {
      LOG(WARNING) << "Failed to parse IPv6 address for interface: "
                   << (ifa->ifa_name ? ifa->ifa_name : "unknown") << " - "
                   << e.what();
    }
  }

  ip_addresses.assign(unique_addresses.begin(), unique_addresses.end());

  if (ip_addresses.empty()) {
    LOG(WARNING) << "No non-loopback/non-link-local IPv6 addresses found";
  }

  return ip_addresses;
}

std::vector<std::string> Util::GetAllLocalIPAddresses() {
  std::vector<std::string> all_ips;

  // Get IPv4 addresses
  auto ipv4_addrs = GetLocalIPv4Addresses();
  all_ips.insert(all_ips.end(), ipv4_addrs.begin(), ipv4_addrs.end());

  // Get IPv6 addresses
  auto ipv6_addrs = GetLocalIPv6Addresses();
  all_ips.insert(all_ips.end(), ipv6_addrs.begin(), ipv6_addrs.end());

  return all_ips;
}

std::vector<std::string> Util::GetPublicIPv6Addresses() {
  struct ifaddrs* ifaddrs_ptr = nullptr;
  std::vector<std::string> addresses;

  if (getifaddrs(&ifaddrs_ptr) == -1) {
    LOG(ERROR) << "Failed to get network interfaces: " << std::strerror(errno);
    return addresses;
  }

  IfAddrsGuard guard(ifaddrs_ptr);

  // Store addresses with their prefix lengths for sorting
  // Use map to deduplicate: IP -> prefix length
  std::map<std::string, int> address_prefix_map;

  for (struct ifaddrs* ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    try {
      auto* sa = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
      folly::IPAddressV6 ipv6_addr = folly::IPAddressV6::fromBinary(
          folly::ByteRange(sa->sin6_addr.s6_addr, 16));

      // Filter out non-public addresses using folly's comprehensive checks
      // - isLoopback(): ::1
      // - isLinkLocal(): fe80::/10
      // - isPrivate(): fc00::/7 (ULA - Unique Local Address)
      // - isMulticast(): ff00::/8
      if (ipv6_addr.isLoopback() || ipv6_addr.isLinkLocal() ||
          ipv6_addr.isPrivate() || ipv6_addr.isMulticast()) {
        continue;
      }

      // Calculate prefix length from netmask
      int prefix_len = 0;
      if (ifa->ifa_netmask != nullptr &&
          ifa->ifa_netmask->sa_family == AF_INET6) {
        auto* netmask =
            reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_netmask);
        // Count the number of 1 bits in the netmask
        for (int i = 0; i < 16; ++i) {
          uint8_t byte = netmask->sin6_addr.s6_addr[i];
          for (int bit = 7; bit >= 0; --bit) {
            if (byte & (1 << bit)) {
              prefix_len++;
            } else {
              // Once we hit a 0 bit, rest should be 0
              goto done_counting;
            }
          }
        }
      done_counting:;
      }

      // Store IP with its prefix length (keep longest if duplicate)
      std::string ip_str = ipv6_addr.str();
      auto it = address_prefix_map.find(ip_str);
      if (it == address_prefix_map.end() || prefix_len > it->second) {
        address_prefix_map[ip_str] = prefix_len;
      }
    } catch (const std::exception& e) {
      LOG(WARNING) << "Failed to parse IPv6 address for interface: "
                   << (ifa->ifa_name ? ifa->ifa_name : "unknown") << " - "
                   << e.what();
    }
  }

  // Convert to vector of pairs for sorting
  std::vector<std::pair<std::string, int>> addr_prefix_vec(
      address_prefix_map.begin(), address_prefix_map.end());

  // Sort by prefix length (longest first), then alphabetically
  std::sort(addr_prefix_vec.begin(), addr_prefix_vec.end(),
            [](const std::pair<std::string, int>& a,
               const std::pair<std::string, int>& b) {
              if (a.second != b.second) {
                return a.second > b.second;  // Longer prefix first
              }
              return a.first < b.first;  // Alphabetical for same prefix
            });

  // Extract just the addresses
  for (const auto& pair : addr_prefix_vec) {
    addresses.push_back(pair.first);
  }

  if (addresses.empty()) {
    LOG(WARNING) << "No public IPv6 addresses found";
  }

  return addresses;
}

std::vector<std::string> Util::ResolveDomainToIPv6(const std::string& domain) {
  std::vector<std::string> addresses;

  // Initialize c-ares with options (modern API)
  ares_channel channel;
  struct ares_options options = {};
  int optmask = 0;

  int status = ares_init_options(&channel, &options, optmask);
  if (status != ARES_SUCCESS) {
    LOG(ERROR) << "Failed to initialize c-ares: " << ares_strerror(status);
    return addresses;
  }

  // Ensure cleanup on exit
  struct ChannelGuard {
    ares_channel ch;
    explicit ChannelGuard(ares_channel c) : ch(c) {}
    ~ChannelGuard() { ares_destroy(ch); }
  } guard(channel);

  // Set up hints for IPv6 resolution
  struct ares_addrinfo_hints hints = {};
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;

  // Structure to hold results
  struct QueryResult {
    std::vector<std::string> addresses;
    bool done = false;
    int status = ARES_SUCCESS;
  } result;

  // Callback for ares_getaddrinfo
  auto callback = [](void* arg, int status, int /*timeouts*/,
                     struct ares_addrinfo* res) {
    auto* query_result = static_cast<QueryResult*>(arg);
    query_result->status = status;

    if (status == ARES_SUCCESS && res != nullptr) {
      for (struct ares_addrinfo_node* node = res->nodes; node != nullptr;
           node = node->ai_next) {
        if (node->ai_family == AF_INET6) {
          auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(node->ai_addr);
          char ipstr[INET6_ADDRSTRLEN];
          if (inet_ntop(AF_INET6, &addr6->sin6_addr, ipstr, INET6_ADDRSTRLEN) !=
              nullptr) {
            query_result->addresses.push_back(ipstr);
          }
        }
      }
      ares_freeaddrinfo(res);
    }

    query_result->done = true;
  };

  // Start the query
  ares_getaddrinfo(channel, domain.c_str(), nullptr, &hints, callback, &result);

  // Process responses using poll() (no FD_SETSIZE limitation)
  while (!result.done) {
    // Use ares_timeout to get the timeout for poll
    struct timeval tv_timeout;
    struct timeval* tv = ares_timeout(channel, nullptr, &tv_timeout);

    int timeout_ms = -1;
    if (tv != nullptr) {
      timeout_ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
    }

    // Get sockets to wait on
    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int bitmask = ares_getsock(channel, socks, ARES_GETSOCK_MAXNUM);

    if (bitmask == 0) {
      break;  // No active queries
    }

    // Setup pollfd array
    struct pollfd pfds[ARES_GETSOCK_MAXNUM];
    int nfds = 0;

    for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      pfds[i].fd = socks[i];
      pfds[i].events = 0;
      pfds[i].revents = 0;

      if (ARES_GETSOCK_READABLE(bitmask, i)) {
        pfds[i].events |= POLLIN;
        nfds = i + 1;
      }
      if (ARES_GETSOCK_WRITABLE(bitmask, i)) {
        pfds[i].events |= POLLOUT;
        nfds = i + 1;
      }
    }

    if (nfds == 0) {
      break;
    }

    int count = poll(pfds, nfds, timeout_ms);
    if (count < 0) {
      LOG(ERROR) << "poll() failed during DNS resolution: "
                 << std::strerror(errno);
      break;
    }

    // Process the sockets based on poll results
    for (int i = 0; i < nfds; i++) {
      ares_process_fd(
          channel, (pfds[i].revents & POLLIN) ? pfds[i].fd : ARES_SOCKET_BAD,
          (pfds[i].revents & POLLOUT) ? pfds[i].fd : ARES_SOCKET_BAD);
    }
  }

  if (result.status != ARES_SUCCESS) {
    LOG(ERROR) << "DNS resolution failed for " << domain << ": "
               << ares_strerror(result.status);
    return addresses;
  }

  addresses = std::move(result.addresses);

  if (addresses.empty()) {
    LOG(WARNING) << "No IPv6 addresses found for domain: " << domain;
  } else {
    LOG(INFO) << "Resolved " << addresses.size() << " IPv6 address(es) for "
              << domain;
  }

  return addresses;
}

string Util::ExecutablePath() {
#if defined(__linux__)
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  return std::string(result, (count > 0) ? count : 0);
#elif defined(_WIN32)
#elif defined(__APPLE__)

#endif
}

string Util::HomeDir() {
  auto current_path = std::filesystem::current_path().string();
  if (current_path == "/") {
    std::filesystem::path workspace(tbox::util::Util::ExecutablePath());
    return workspace.parent_path().parent_path().string();
  }
  return current_path;
}

}  // namespace util
}  // namespace tbox
