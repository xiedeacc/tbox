/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/report_manager.h"

#include "glog/logging.h"
#include "gtest/gtest.h"

namespace tbox {
namespace client {

/// @brief Test fixture for ReportManager tests.
class ReportManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Get ReportManager instance
    manager_ = ReportManager::Instance();
  }

  void TearDown() override {
    // Stop the manager if it's running
    if (manager_->IsRunning()) {
      manager_->Stop();
    }
  }

  std::shared_ptr<ReportManager> manager_;
};

/// @brief Test that ReportManager singleton instance can be retrieved.
TEST_F(ReportManagerTest, GetInstance) {
  EXPECT_NE(manager_, nullptr);

  // Verify singleton behavior - same instance
  auto another_manager = ReportManager::Instance();
  EXPECT_EQ(manager_, another_manager);
}

/// @brief Test that ReportManager is not running initially.
TEST_F(ReportManagerTest, InitiallyNotRunning) {
  EXPECT_FALSE(manager_->IsRunning());
}

/// @brief Test basic lifecycle without initialization.
TEST_F(ReportManagerTest, LifecycleWithoutInit) {
  // Should not be running initially
  EXPECT_FALSE(manager_->IsRunning());

  // Stop should not crash even if not initialized
  EXPECT_NO_THROW(manager_->Stop());

  // Should still not be running
  EXPECT_FALSE(manager_->IsRunning());
}

}  // namespace client
}  // namespace tbox
