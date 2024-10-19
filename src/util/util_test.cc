/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/util.h"

#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "gtest/gtest.h"

namespace tbox {
namespace util {

TEST(Util, CurrentTimeMillis) {
  LOG(INFO) << Util::CurrentTimeMillis();
  EXPECT_GT(Util::CurrentTimeMillis(), 1704038400000);
  EXPECT_LT(Util::CurrentTimeMillis(), 1904038400000);
}

TEST(Util, CurrentTimeNanos) {
  LOG(INFO) << Util::CurrentTimeNanos();
  EXPECT_GT(Util::CurrentTimeNanos(), 1727101022292387983);
  EXPECT_LT(Util::CurrentTimeNanos(), 1927101022292387983);
}

TEST(Util, StrToTimeStamp) {
  std::string time = "2024-09-24 13:36:44";
  std::string format = "%Y-%m-%d HH:MM:SS";
  int64_t ts = 1727185004000;
  EXPECT_EQ(Util::StrToTimeStamp(time, format), -1);

  time = "2024-09-24 13:36:44";
  format = "%Y-%m-%d %H:%M:%S";
  ts = 1727185004000;
  EXPECT_EQ(Util::StrToTimeStampUTC(time, format), ts);

  time = "2024-09-24 13:36:44";
  format = "%Y-%m-%d %H:%M:%S";
  ts = 1727156204000;
  EXPECT_EQ(Util::StrToTimeStamp(time, format, "Asia/Shanghai"), ts);

  time = "2024-09-24 21:36:44";
  format = "%Y-%m-%d %H:%M:%S";
  ts = 1727185004000;
  // CST time
  EXPECT_EQ(Util::StrToTimeStamp(time, format, "Asia/Shanghai"), ts);

  // UTC time
  time = "2024-09-24 13:36:44";
  format = "%Y-%m-%d %H:%M:%S";
  ts = 1727185004000;
  EXPECT_EQ(Util::StrToTimeStamp(time, format, "UTC"), ts);

  // local time, bazel sandbox use UTC time, but this project used
  // --test_env=TZ=Asia/Shanghai
  time = "2024-09-24 13:36:44";
  format = "%Y-%m-%d %H:%M:%S";
  ts = 1727156204000;
  EXPECT_EQ(Util::StrToTimeStamp(time, format, "localtime"), ts);

  time = "2024-09-24 21:36:44.123";
  format = "%Y-%m-%d %H:%M:%E3S";
  ts = 1727185004123;
  EXPECT_EQ(Util::StrToTimeStamp(time, format, "Asia/Shanghai"), ts);

  time = "2024-09-24 13:36:44.123";
  format = "%Y-%m-%d %H:%M:%E2S%E3f";
  ts = 1727156204123;
  EXPECT_EQ(Util::StrToTimeStamp(time, format), ts);

  time = "2024-09-24 13:36:44.123";
  format = "%Y-%m-%d %H:%M:%E3S";
  ts = 1727156204123;
  EXPECT_EQ(Util::StrToTimeStamp(time, format), ts);

  time = "2024-09-24T13:36:44.000+0000";
  format = "%Y-%m-%d %H:%M:%E2S%E3f%Ez";
  ts = 1727185004000;
  EXPECT_EQ(Util::StrToTimeStamp(time), ts);

  time = "2024-09-24 13:36:44.123+08:30:24";
  format = "%Y-%m-%d %H:%M:%E3S%E*z";
  ts = 1727154380123;
  EXPECT_EQ(Util::StrToTimeStamp(time, format), ts);
}

TEST(Util, ToTimeStr) {
  int64_t ts = 1727185004000;
  std::string time = "2024-09-24T21:36:44.000+08:00 CST";
  std::string format = "%Y-%m-%d%ET%H:%M:%E3S%Ez %Z";
  EXPECT_EQ(Util::ToTimeStr(ts, format, "Asia/Shanghai"), time);

  // local time, bazel sandbox use UTC time, but this project used
  // --test_env=TZ=Asia/Shanghai
  time = "2024-09-24T21:36:44.000+08:00";
  format = "%Y-%m-%d %H:%M:%S";
  EXPECT_EQ(Util::ToTimeStr(ts), time);

  time = "2024-09-24 13:36:44";
  EXPECT_EQ(Util::ToTimeStrUTC(ts, format), time);

  time = "2024-09-24 21:36:44";
  EXPECT_EQ(Util::ToTimeStr(ts, format, "Asia/Shanghai"), time);
}

TEST(Util, ToTimeSpec) {
  auto time = Util::ToTimeSpec(2727650275042);
  EXPECT_EQ(time.tv_sec, 2727650275);
  EXPECT_EQ(time.tv_nsec, 42000000);
}

TEST(Util, Random) {
  auto generator = [](int thread_num) {
    for (int i = 0; i < 10000; ++i) {
      auto ret = Util::Random(0, 100);
      EXPECT_GT(ret, -1);
      EXPECT_LT(ret, 100);
    }
  };

  std::thread threads[12];
  for (int i = 0; i < 12; ++i) {
    threads[i] = std::thread(std::bind(generator, i));
  }

  for (int i = 0; i < 12; ++i) {
    if (threads[i].joinable()) {
      threads[i].join();
    }
  }
}

TEST(Util, LoadSmallFile) {
  std::string content;
  std::string path = "test_data/util_test/never_modify";
  EXPECT_EQ(Util::LoadSmallFile(path, &content), true);
  EXPECT_EQ(content, "abcd\n");
}

TEST(Util, UUID) {
  // eg: 2b68792c-0580-41a2-a7b1-ab3a96a1a58e
  LOG(INFO) << Util::UUID();
}

TEST(Util, CRC32) {
  // https://crccalc.com/?crc=123456789&method=&datatype=0&outtype=0
  // CRC-32/ISCSI
  auto start = Util::CurrentTimeMillis();
  std::string content =
      "A cyclic redundancy check (CRC) is an error-detecting code used to "
      "detect data corruption. When sending data, short checksum is generated "
      "based on data content and sent along with data. When receiving data, "
      "checksum is generated again and compared with sent checksum. If the two "
      "are equal, then there is no data corruption. The CRC-32 algorithm "
      "itself converts a variable-length string into an 8-character string.";
  LOG(INFO) << "file size: " << content.size() / 1024 / 1024
            << "M, crc32:" << Util::CRC32(content)
            << ", cost: " << Util::CurrentTimeMillis() - start;
  EXPECT_EQ(Util::CRC32(content), 2331864810);
}

TEST(Util, SHA256) {
  std::string content;
  std::string out;
  content =
      R"(A cyclic redundancy check (CRC) is an error-detecting code used to detect data corruption. When sending data, short checksum is generated based on data content and sent along with data. When receiving data, checksum is generated again and compared with sent checksum. If the two are equal, then there is no data corruption. The CRC-32 algorithm itself converts a variable-length string into an 8-character string.)";
  Util::SHA256(content, &out);
  EXPECT_EQ(out,
            "3f5d419c0386a26df1c75d0d1c488506fb641b33cebaa2a4917127ae33030b31");
}

TEST(Util, Blake3) {
  std::string content;
  std::string out;
  content =
      R"(A cyclic redundancy check (CRC) is an error-detecting code used to detect data corruption. When sending data, short checksum is generated based on data content and sent along with data. When receiving data, checksum is generated again and compared with sent checksum. If the two are equal, then there is no data corruption. The CRC-32 algorithm itself converts a variable-length string into an 8-character string.)";
  Util::Blake3(content, &out);
  EXPECT_EQ(out,
            "9b12d05351596e6851917bc73dcaf39eb12a27a196e56d38492ed730a60edf8e");
}

TEST(Util, FileHashCompare) {
  const auto& path =
      "/usr/local/gcc/14.1.0/libexec/gcc/x86_64-pc-linux-gnu/14.1.0/cc1plus";
  std::string out;
  auto start = Util::CurrentTimeMillis();
  Util::FileBlake3(path, &out);
  auto now = Util::CurrentTimeMillis();
  LOG(INFO) << "Cost: " << now - start;
  EXPECT_EQ(out,
            "4195ca072574b8da14152cbd76b3de16dd5e290d5dbb3582a047ee17bb1b6fd4");
  start = now;

  Util::FileSHA256(path, &out);
  now = Util::CurrentTimeMillis();
  LOG(INFO) << "Cost: " << now - start;
  EXPECT_EQ(out,
            "4084ec48f2affbd501c02a942b674abcfdbbc6475070049de2c89fb6aa25a3f0");
  start = now;

  Util::FileBlake3(path, &out);
  now = Util::CurrentTimeMillis();
  LOG(INFO) << "Cost: " << now - start;
  EXPECT_EQ(out,
            "4195ca072574b8da14152cbd76b3de16dd5e290d5dbb3582a047ee17bb1b6fd4");
  start = now;

  Util::FileSHA256(path, &out);
  now = Util::CurrentTimeMillis();
  LOG(INFO) << "Cost: " << now - start;
  EXPECT_EQ(out,
            "4084ec48f2affbd501c02a942b674abcfdbbc6475070049de2c89fb6aa25a3f0");
}

TEST(Util, LZMA) {
  std::string data = "/usr/local/llvm";
  std::string compressed_data;
  std::string decompressed_data;

  std::string compressed_data_hex;
  std::string decompressed_data_hex;

  if (!Util::LZMACompress(data, &compressed_data)) {
    LOG(ERROR) << "compress error";
  }
  Util::ToHexStr(compressed_data, &compressed_data_hex);

  EXPECT_EQ(compressed_data_hex,
            "fd377a585a000004e6d6b4460200210116000000742fe5a301000e2f7573722f6c"
            "6f63616c2f6c6c766d0000efb8e6d242765b590001270fdf1afc6a1fb6f37d0100"
            "00000004595a");

  if (!Util::LZMADecompress(compressed_data, &decompressed_data)) {
    LOG(ERROR) << "decompress error";
  }

  EXPECT_EQ(decompressed_data, "/usr/local/llvm");
}

TEST(Util, HashPassword) {
  std::vector<uint8_t> salt_arr{0x45, 0x2c, 0x03, 0x06, 0x73, 0x0b, 0x0f, 0x3a,
                                0xc3, 0x08, 0x6d, 0x4f, 0x62, 0xef, 0xfc, 0x20};
  std::vector<uint8_t> hashed_password_arr{
      0x29, 0x9a, 0xe5, 0x3a, 0xb2, 0x2c, 0x08, 0x5a, 0x47, 0x96, 0xb5,
      0x91, 0x87, 0xd2, 0xb5, 0x4c, 0x21, 0x7e, 0x48, 0x30, 0xb4, 0xab,
      0xe4, 0xad, 0xe7, 0x9d, 0x7d, 0x8e, 0x6d, 0x90, 0xf5, 0x1a};
  std::string salt(salt_arr.begin(), salt_arr.end());
  std::string standard_hashed_password(hashed_password_arr.begin(),
                                       hashed_password_arr.end());

  std::string hashed_password;
  if (!Util::HashPassword(Util::SHA256("admin"), salt, &hashed_password)) {
    LOG(INFO) << "error";
  }

  // std::string format_hex;
  // for (auto i : hashed_password) {
  // format_hex.append(fmt::format("0x{:02x}", static_cast<uint8_t>(i)));
  // format_hex.append(", ");
  // }
  // LOG(INFO) << "hashed_password: " << format_hex;
  EXPECT_EQ(standard_hashed_password, hashed_password);
}

TEST(Util, ListAllIPAddresses) {
  std::vector<std::string> public_ipv4_addrs;
  std::vector<std::string> private_ipv4_addrs;
  std::vector<std::string> public_ipv6_addrs;
  std::vector<std::string> private_ipv6_addrs;
  std::vector<folly::IPAddress> ip_addrs;
  Util::ListAllIPAddresses(&ip_addrs);

  for (const auto& addr : ip_addrs) {
    if (addr.isV4()) {
      if (!addr.isPrivate()) {
        public_ipv4_addrs.emplace_back(addr.str());
      } else {
        private_ipv4_addrs.emplace_back(addr.str());
      }
    } else if (addr.isV6()) {
      if (!addr.isPrivate()) {
        public_ipv6_addrs.emplace_back(addr.str());
      } else {
        private_ipv6_addrs.emplace_back(addr.str());
      }
    }
  }

  for (const auto& addr : public_ipv4_addrs) {
    LOG(INFO) << "public ipv4: " << addr;
  }

  for (const auto& addr : private_ipv4_addrs) {
    LOG(INFO) << "private ipv4: " << addr;
  }

  for (const auto& addr : public_ipv6_addrs) {
    LOG(INFO) << "public ipv6: " << addr;
  }

  for (const auto& addr : private_ipv6_addrs) {
    LOG(INFO) << "private ipv6: " << addr;
  }
}

TEST(Util, HexToStr) {
  std::string passwd = "YZLoveLi@1314";
  std::string hash = Util::SHA256(passwd);
  EXPECT_EQ(hash,
            "e882beb29c57a710b60f15726e45aabaca8db51f752a4391239c8557b373f3d3");
}

}  // namespace util
}  // namespace tbox
