/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/config_manager.h"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "src/common/logging.h"

namespace tbox {
namespace util {

TEST(ConfigManager, Init) {
  const auto config_path =
      std::filesystem::temp_directory_path() /
      "tbox_config_manager_test.json";
  {
    std::ofstream config(config_path, std::ios::binary);
    config << R"({
      "server_addr": "127.0.0.1",
      "grpc_server_port": 10001,
      "dns_provider": "cloudflare",
      "cloudflare_api_token": "cloudflare-secret",
      "route53_hosted_zone_id": "route53-zone",
      "aws_access_key_id": "aws-access-key",
      "aws_secret_access_key": "aws-secret-key"
    })";
  }

  auto config_manager = ConfigManager::Instance();
  ASSERT_TRUE(config_manager->Init(config_path.string()));
  std::error_code error;
  std::filesystem::remove(config_path, error);

  const std::string redacted_config = config_manager->ToRedactedString();
  tbox::proto::BaseConfig parsed_config;
  ASSERT_TRUE(Util::JsonToMessage(redacted_config, &parsed_config));
  EXPECT_EQ(parsed_config.cloudflare_api_token(), "[REDACTED]");
  EXPECT_EQ(parsed_config.aws_access_key_id(), "[REDACTED]");
  EXPECT_EQ(parsed_config.aws_secret_access_key(), "[REDACTED]");
  EXPECT_EQ(parsed_config.route53_hosted_zone_id(),
            config_manager->Route53HostedZoneId());
}

}  // namespace util
}  // namespace tbox
