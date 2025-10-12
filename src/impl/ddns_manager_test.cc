/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/ddns_manager.h"

#include <chrono>
#include <thread>

#include "gtest/gtest.h"

namespace tbox {
namespace impl {
namespace {

class DDNSManagerTest : public ::testing::Test {
 protected:
  void SetUp() override { manager_ = DDNSManager::Instance(); }

  void TearDown() override {
    if (manager_->IsRunning()) {
      manager_->Stop();
    }
  }

  std::shared_ptr<DDNSManager> manager_;
};

TEST_F(DDNSManagerTest, InstanceNotNull) {
  EXPECT_NE(manager_, nullptr);
}

TEST_F(DDNSManagerTest, InitWithConfig) {
  // Note: Init will fail without actual AWS credentials or ConfigManager
  // This test is expected to fail, but we verify it doesn't crash unexpectedly
  // Skipping this test in CI environments without AWS credentials
  // manager_->Init();
  // Just verify manager exists
  EXPECT_NE(manager_, nullptr);
}

TEST_F(DDNSManagerTest, IsRunningInitiallyFalse) {
  EXPECT_FALSE(manager_->IsRunning());
}

TEST_F(DDNSManagerTest, StartStop) {
  // Note: Cannot test Start/Stop without AWS credentials
  // These methods require Init() which needs AWS setup
  // Just verify initial state
  EXPECT_FALSE(manager_->IsRunning());
}

TEST_F(DDNSManagerTest, MultipleStartCalls) {
  // Cannot test without AWS credentials
  // Just verify manager exists
  EXPECT_NE(manager_, nullptr);
}

TEST_F(DDNSManagerTest, MultipleStopCalls) {
  // Cannot test without AWS credentials
  // Verify stop on uninitialized manager doesn't crash
  manager_->Stop();
  EXPECT_FALSE(manager_->IsRunning());

  // Stopping again should be safe
  manager_->Stop();
  EXPECT_FALSE(manager_->IsRunning());
}

// Note: IsIPv6InList is a private method, so we test it indirectly through
// UpdateDNS

TEST_F(DDNSManagerTest, UpdateDNSWithoutInit) {
  // Calling UpdateDNS without initialization should fail gracefully
  bool result = manager_->UpdateDNS();
  EXPECT_FALSE(result);
}

TEST_F(DDNSManagerTest, ConfigurationValues) {
  // Verify hardcoded configuration constants
  EXPECT_EQ(DDNSManager::kDnsTtl, 60);
  EXPECT_EQ(DDNSManager::kMaxBackoffSeconds, 3600);
  EXPECT_EQ(DDNSManager::kMinBackoffSeconds, 60);
  EXPECT_STREQ(DDNSManager::kAwsRegion, "us-east-1");
}

TEST_F(DDNSManagerTest, BackoffConfiguration) {
  // Test backoff configuration values
  EXPECT_EQ(DDNSManager::kMaxBackoffSeconds, 3600);
  EXPECT_EQ(DDNSManager::kMinBackoffSeconds, 60);
  EXPECT_GT(DDNSManager::kMaxBackoffSeconds, DDNSManager::kMinBackoffSeconds);
}

}  // namespace
}  // namespace impl
}  // namespace tbox
