/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/util.h"

#include <algorithm>
#include <set>

#include "folly/IPAddress.h"
#include "glog/logging.h"
#include "gtest/gtest.h"

namespace tbox {
namespace util {
namespace {

// Helper to validate IPv4 address format
bool IsValidIPv4(const std::string& ip) {
  if (ip == "unknown")
    return true;  // Special case
  try {
    folly::IPAddress addr(ip);
    return addr.isV4();
  } catch (const std::exception&) {
    return false;
  }
}

// Helper to validate IPv6 address format
bool IsValidIPv6(const std::string& ip) {
  try {
    folly::IPAddress addr(ip);
    return addr.isV6();
  } catch (const std::exception&) {
    return false;
  }
}

// Helper to check if address is loopback
bool IsLoopback(const std::string& ip) {
  try {
    folly::IPAddress addr(ip);
    return addr.isLoopback();
  } catch (const std::exception&) {
    return false;
  }
}

// Helper to check if address is link-local
bool IsLinkLocal(const std::string& ip) {
  try {
    folly::IPAddress addr(ip);
    return addr.isLinkLocal();
  } catch (const std::exception&) {
    return false;
  }
}

// Helper to check if address is private
bool IsPrivate(const std::string& ip) {
  try {
    folly::IPAddress addr(ip);
    return addr.isPrivate();
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

TEST(Util, MemLeak) {
  char* arr = new char[8];
  snprintf(arr, 8, "%s", "test");
  LOG(INFO) << arr;
  LOG(INFO) << "TEST";
}

TEST(Util, GetLocalIPv4Addresses) {
  auto addresses = Util::GetLocalIPv4Addresses();

  // Should return at least one address (even if just "unknown")
  EXPECT_FALSE(addresses.empty());

  // All addresses should be valid IPv4
  for (const auto& addr : addresses) {
    EXPECT_TRUE(IsValidIPv4(addr)) << "Invalid IPv4 address: " << addr;
  }

  // No loopback addresses should be present (except "unknown")
  for (const auto& addr : addresses) {
    if (addr != "unknown") {
      EXPECT_FALSE(IsLoopback(addr)) << "Loopback address found: " << addr;
    }
  }

  // No duplicates
  std::set<std::string> unique_addrs(addresses.begin(), addresses.end());
  EXPECT_EQ(unique_addrs.size(), addresses.size())
      << "Duplicate addresses found";

  LOG(INFO) << "Found " << addresses.size() << " IPv4 address(es):";
  for (const auto& addr : addresses) {
    LOG(INFO) << "  - " << addr;
  }
}

TEST(Util, GetLocalIPv6Addresses) {
  auto addresses = Util::GetLocalIPv6Addresses();

  // May be empty if no IPv6 is configured
  LOG(INFO) << "Found " << addresses.size() << " IPv6 address(es):";

  // All addresses should be valid IPv6
  for (const auto& addr : addresses) {
    EXPECT_TRUE(IsValidIPv6(addr)) << "Invalid IPv6 address: " << addr;
  }

  // No loopback addresses should be present
  for (const auto& addr : addresses) {
    EXPECT_FALSE(IsLoopback(addr)) << "Loopback address found: " << addr;
  }

  // No link-local addresses should be present
  for (const auto& addr : addresses) {
    EXPECT_FALSE(IsLinkLocal(addr)) << "Link-local address found: " << addr;
  }

  // No duplicates
  std::set<std::string> unique_addrs(addresses.begin(), addresses.end());
  EXPECT_EQ(unique_addrs.size(), addresses.size())
      << "Duplicate addresses found";

  for (const auto& addr : addresses) {
    LOG(INFO) << "  - " << addr;
  }
}

TEST(Util, GetAllLocalIPAddresses) {
  auto all_addresses = Util::GetAllLocalIPAddresses();
  auto ipv4_addresses = Util::GetLocalIPv4Addresses();
  auto ipv6_addresses = Util::GetLocalIPv6Addresses();

  // Should contain all IPv4 and IPv6 addresses
  EXPECT_EQ(all_addresses.size(), ipv4_addresses.size() + ipv6_addresses.size())
      << "GetAllLocalIPAddresses should return IPv4 + IPv6 addresses";

  // All IPv4 addresses should be in the combined list
  for (const auto& ipv4 : ipv4_addresses) {
    EXPECT_TRUE(std::find(all_addresses.begin(), all_addresses.end(), ipv4) !=
                all_addresses.end())
        << "IPv4 address " << ipv4 << " not found in all addresses";
  }

  // All IPv6 addresses should be in the combined list
  for (const auto& ipv6 : ipv6_addresses) {
    EXPECT_TRUE(std::find(all_addresses.begin(), all_addresses.end(), ipv6) !=
                all_addresses.end())
        << "IPv6 address " << ipv6 << " not found in all addresses";
  }

  LOG(INFO) << "Total addresses: " << all_addresses.size();
  LOG(INFO) << "  IPv4: " << ipv4_addresses.size();
  LOG(INFO) << "  IPv6: " << ipv6_addresses.size();
}

TEST(Util, GetPublicIPv6Addresses) {
  auto addresses = Util::GetPublicIPv6Addresses();

  // May be empty if no public IPv6 is available
  LOG(INFO) << "Found " << addresses.size() << " public IPv6 address(es):";

  // All addresses should be valid IPv6
  for (const auto& addr : addresses) {
    EXPECT_TRUE(IsValidIPv6(addr)) << "Invalid IPv6 address: " << addr;
  }

  // No loopback addresses should be present
  for (const auto& addr : addresses) {
    EXPECT_FALSE(IsLoopback(addr)) << "Loopback address found: " << addr;
  }

  // No link-local addresses should be present
  for (const auto& addr : addresses) {
    EXPECT_FALSE(IsLinkLocal(addr)) << "Link-local address found: " << addr;
  }

  // No private addresses should be present
  for (const auto& addr : addresses) {
    EXPECT_FALSE(IsPrivate(addr)) << "Private address found: " << addr;
  }

  // No duplicates
  std::set<std::string> unique_addrs(addresses.begin(), addresses.end());
  EXPECT_EQ(unique_addrs.size(), addresses.size())
      << "Duplicate addresses found";

  // All should be global unicast (public) - verified by checking they are not:
  // loopback, link-local, private, or multicast
  for (const auto& addr : addresses) {
    folly::IPAddressV6 ipv6_addr(addr);
    // Already verified by GetPublicIPv6Addresses filtering
    EXPECT_FALSE(ipv6_addr.isMulticast())
        << "Address " << addr << " is multicast";
    LOG(INFO) << "  - " << addr;
  }
}

TEST(Util, ResolveDomainToIPv6_Google) {
  // Test with a well-known domain that has IPv6
  auto addresses = Util::ResolveDomainToIPv6("google.com");

  // Google should have IPv6 addresses (unless network doesn't support IPv6)
  if (!addresses.empty()) {
    LOG(INFO) << "Resolved " << addresses.size()
              << " IPv6 address(es) for google.com:";

    // All addresses should be valid IPv6
    for (const auto& addr : addresses) {
      EXPECT_TRUE(IsValidIPv6(addr)) << "Invalid IPv6 address: " << addr;
      LOG(INFO) << "  - " << addr;
    }
  } else {
    LOG(WARNING) << "No IPv6 addresses resolved for google.com "
                 << "(IPv6 might not be available)";
  }
}

TEST(Util, ResolveDomainToIPv6_Localhost) {
  // Test with localhost - should resolve to ::1
  auto addresses = Util::ResolveDomainToIPv6("localhost");

  if (!addresses.empty()) {
    LOG(INFO) << "Resolved " << addresses.size()
              << " IPv6 address(es) for localhost:";
    for (const auto& addr : addresses) {
      LOG(INFO) << "  - " << addr;
      // Localhost should resolve to ::1
      EXPECT_TRUE(IsLoopback(addr))
          << "localhost did not resolve to loopback address";
    }
  } else {
    LOG(INFO) << "localhost did not resolve to any IPv6 address "
              << "(expected behavior for c-ares)";
  }
}

TEST(Util, ResolveDomainToIPv6_InvalidDomain) {
  // Test with an invalid domain
  auto addresses = Util::ResolveDomainToIPv6(
      "this-domain-definitely-does-not-exist-12345.com");

  // Should fail gracefully and return empty vector
  EXPECT_TRUE(addresses.empty())
      << "Invalid domain should not resolve to any addresses";
  LOG(INFO) << "Invalid domain correctly returned 0 addresses";
}

TEST(Util, ResolveDomainToIPv6_EmptyDomain) {
  // Test with empty domain
  auto addresses = Util::ResolveDomainToIPv6("");

  // Should handle empty domain gracefully
  EXPECT_TRUE(addresses.empty())
      << "Empty domain should not resolve to any addresses";
  LOG(INFO) << "Empty domain correctly returned 0 addresses";
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

TEST(Util, HashPassword) {
  std::string salt = "452c0306730b0f3ac3086d4f62effc20";
  std::string standard_hashed_password =
      "e64de2fcaef0b98d035c3c241e4f8fda32f3b09067ef0f1b1706869a54f9d3b7";

  std::string hashed_password;
  if (!Util::HashPassword(Util::SHA256("admin"), salt, &hashed_password)) {
    LOG(INFO) << "error";
  }
  LOG(INFO) << hashed_password;

  EXPECT_EQ(standard_hashed_password, hashed_password);
}

}  // namespace util
}  // namespace tbox
