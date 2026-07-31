/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/swig/config_manager.h"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace tbox {
namespace swig {

TEST(ConfigManager, Init) {
  const auto path =
      std::filesystem::temp_directory_path() / "tbox_swig_config_test.json";
  {
    std::ofstream config(path);
    config << R"({"server_addr":"127.0.0.1","grpc_server_port":10001})";
  }
  ConfigManager config_manager;
  EXPECT_TRUE(config_manager.Init(path.string()));
  std::error_code error;
  std::filesystem::remove(path, error);
  LOG(INFO) << config_manager.ToString();
}

}  // namespace swig
}  // namespace tbox
