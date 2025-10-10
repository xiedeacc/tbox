/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/ssl_config_manager.h"

#include <fstream>

#include "gtest/gtest.h"
#include "src/util/util.h"

namespace tbox {
namespace client {
namespace {

class SSLConfigManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Get singleton instance
    manager_ = SSLConfigManager::Instance();
    ASSERT_NE(manager_, nullptr);

    // Create a temporary test certificate file
    test_cert_content_ =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIDXTCCAkWgAwIBAgIJAKL0UG+mRKSzMA0GCSqGSIb3DQEBCwUAMEUxCzAJBgNV\n"
        "-----END CERTIFICATE-----\n";

    home_dir_ = tbox::util::Util::HomeDir();
    test_cert_path_ = "/test_ca.cer";
    full_test_path_ = home_dir_ + test_cert_path_;

    // Write test certificate
    std::ofstream out(full_test_path_);
    if (out.is_open()) {
      out << test_cert_content_;
      out.close();
    }
  }

  void TearDown() override {
    // Clean up test file
    std::remove(full_test_path_.c_str());
  }

  std::shared_ptr<SSLConfigManager> manager_;
  std::string test_cert_content_;
  std::string home_dir_;
  std::string test_cert_path_;
  std::string full_test_path_;
};

/// @brief Test that SSLConfigManager singleton instance can be retrieved.
TEST_F(SSLConfigManagerTest, GetInstance) {
  EXPECT_NE(manager_, nullptr);

  // Verify singleton behavior - same instance
  auto another_manager = SSLConfigManager::Instance();
  EXPECT_EQ(manager_, another_manager);
}

TEST_F(SSLConfigManagerTest, LoadCACert_Success) {
  std::string loaded = manager_->LoadCACert(test_cert_path_);

  EXPECT_FALSE(loaded.empty());
  EXPECT_EQ(loaded, test_cert_content_);
  EXPECT_GT(loaded.size(), 0);
}

TEST_F(SSLConfigManagerTest, LoadCACert_NonExistentFile) {
  std::string loaded = manager_->LoadCACert("/non_existent_file.cer");

  EXPECT_TRUE(loaded.empty());
}

TEST_F(SSLConfigManagerTest, LoadCACert_EmptyFile) {
  // Create an empty file
  std::string empty_path = "/test_empty.cer";
  std::string full_empty_path = home_dir_ + empty_path;

  std::ofstream out(full_empty_path);
  out.close();

  std::string loaded = manager_->LoadCACert(empty_path);

  EXPECT_TRUE(loaded.empty());

  // Clean up
  std::remove(full_empty_path.c_str());
}

TEST_F(SSLConfigManagerTest, LoadCACert_RelativePath) {
  // Test that relative path is properly resolved with HomeDir
  std::string loaded = manager_->LoadCACert(test_cert_path_);

  EXPECT_FALSE(loaded.empty());
  EXPECT_EQ(loaded.size(), test_cert_content_.size());
}

}  // namespace
}  // namespace client
}  // namespace tbox
