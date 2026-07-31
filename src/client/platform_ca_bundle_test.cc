/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/platform_ca_bundle.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "gtest/gtest.h"

namespace tbox {
namespace client {
namespace {

TEST(PlatformCABundleTest, RefreshesAndKeepsPlatformRoots) {
  const std::filesystem::path bundle_path =
      std::filesystem::current_path() / "platform_ca_bundle_test.pem";
  std::remove(bundle_path.string().c_str());

  EXPECT_EQ(PlatformCABundle::Refresh(bundle_path.string()),
            PlatformCABundle::UpdateResult::kUpdated);

  std::ifstream input(bundle_path, std::ios::binary);
  ASSERT_TRUE(input.is_open());
  const std::string content{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
  EXPECT_NE(content.find("-----BEGIN CERTIFICATE-----"), std::string::npos);

  EXPECT_EQ(PlatformCABundle::Refresh(bundle_path.string()),
            PlatformCABundle::UpdateResult::kUnchanged);
  std::remove(bundle_path.string().c_str());
}

}  // namespace
}  // namespace client
}  // namespace tbox
