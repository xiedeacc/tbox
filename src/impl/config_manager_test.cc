/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/config_manager.h"

#include "src/common/logging.h"
#include "gtest/gtest.h"

namespace tbox {
namespace util {

TEST(ConfigManager, Init) {
  auto config_manager = ConfigManager::Instance();
  EXPECT_TRUE(config_manager->Init("./conf/server_example_config.json"));

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
