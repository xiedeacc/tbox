/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/common/logging.h"

#include <chrono>
#include <filesystem>
#include <string>

#include "gtest/gtest.h"

namespace tbox {
namespace logging {
namespace {

std::filesystem::path UniqueLogDirectory(const char* suffix) {
  const auto timestamp = std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count();
  return std::filesystem::temp_directory_path() /
         (std::string("tbox_logging_test_") + suffix + "_" +
          std::to_string(timestamp));
}

TEST(LoggingTest, DisabledLoggingDoesNotCreateDirectory) {
  const auto log_directory = UniqueLogDirectory("disabled");
  Initialize("logging_test", log_directory.string(), false);
  LOG(INFO) << "This message must be discarded";
  Shutdown();

  EXPECT_FALSE(std::filesystem::exists(log_directory));
}

TEST(LoggingTest, EnabledLoggingCreatesRotatingFiles) {
  const auto log_directory = UniqueLogDirectory("enabled");
  Initialize("logging_test", log_directory.string(), true);
  LOG(INFO) << "This message must be written";
  Shutdown();

  EXPECT_TRUE(std::filesystem::is_regular_file(log_directory /
                                                "logging_test.INFO.log"));
  std::error_code error;
  std::filesystem::remove_all(log_directory, error);
}

}  // namespace
}  // namespace logging
}  // namespace tbox
