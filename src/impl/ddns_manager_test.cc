/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/ddns_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace tbox {
namespace impl {
namespace {

class FakeDnsProvider final : public dns::DnsProvider {
 public:
  bool Init() override { return true; }
  std::string Name() const override { return "fake"; }
  std::string GetZoneId(const std::string&) override {
    ++zone_calls;
    return "test-zone";
  }
  bool ListRecords(const std::string&, const std::string&,
                   dns::RecordType type,
                   std::vector<dns::Record>* records) override {
    ++list_calls;
    records->clear();
    if (!stored_value.empty()) {
      records->push_back({"", "", stored_value, type, 60});
    }
    return true;
  }
  bool UpsertRecord(const std::string&, const std::string&, dns::RecordType,
                    const std::string& value, int) override {
    ++upsert_calls;
    stored_value = value;
    return true;
  }
  bool DeleteRecord(const std::string&, const std::string&, dns::RecordType,
                    const std::string&) override {
    return true;
  }

  int zone_calls = 0;
  int list_calls = 0;
  int upsert_calls = 0;
  std::string stored_value;
};

class DDNSManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto provider = std::make_unique<FakeDnsProvider>();
    provider_ = provider.get();
    DDNSManager::Instance()->SetProviderForTesting(std::move(provider));
  }

  FakeDnsProvider* provider_ = nullptr;
};

TEST_F(DDNSManagerTest, CachesSuccessfulRecordValue) {
  ASSERT_TRUE(DDNSManager::Instance()->UpdateDomains(
      {"Home.Example.com."}, {"198.51.100.10"}, {"A"}));
  EXPECT_EQ(provider_->zone_calls, 1);
  EXPECT_EQ(provider_->list_calls, 1);
  EXPECT_EQ(provider_->upsert_calls, 1);

  ASSERT_TRUE(DDNSManager::Instance()->UpdateDomains(
      {"home.example.com"}, {"198.51.100.10"}, {"A"}));
  EXPECT_EQ(provider_->zone_calls, 1);
  EXPECT_EQ(provider_->list_calls, 1);
  EXPECT_EQ(provider_->upsert_calls, 1);

  ASSERT_TRUE(DDNSManager::Instance()->UpdateDomains(
      {"home.example.com"}, {"198.51.100.11"}, {"A"}));
  EXPECT_EQ(provider_->list_calls, 2);
  EXPECT_EQ(provider_->upsert_calls, 2);
}

TEST_F(DDNSManagerTest, IgnoresPrivateAddresses) {
  EXPECT_TRUE(DDNSManager::Instance()->UpdateDomains(
      {"home.example.com"}, {"192.168.1.10", "fd00::10"}, {}));
  EXPECT_EQ(provider_->list_calls, 0);
  EXPECT_EQ(provider_->upsert_calls, 0);
}

TEST_F(DDNSManagerTest, RejectsInvalidDomains) {
  EXPECT_FALSE(DDNSManager::Instance()->UpdateDomains(
      {"not a domain"}, {"198.51.100.10"}, {"A"}));
  EXPECT_EQ(provider_->zone_calls, 0);
}

TEST(DDNSManagerConfigurationTest, ConfigurationValues) {
  EXPECT_EQ(DDNSManager::kDnsTtl, 60);
  EXPECT_EQ(DDNSManager::kMaxDomainsPerReport, 32);
}

}  // namespace
}  // namespace impl
}  // namespace tbox
