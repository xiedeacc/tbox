/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/user_manager.h"

#include <unistd.h>

#include <filesystem>

#include "folly/init/Init.h"
#include "gtest/gtest.h"
#include "src/common/error.h"
#include "src/util/util.h"

namespace tbox {
namespace impl {

TEST(UserManager, UserExists) {
  // Use a writable temp home dir for the test and ensure data dir exists
  const auto tmp_root = std::filesystem::temp_directory_path() / "tbox_um_test";
  std::error_code ec;
  std::filesystem::remove_all(tmp_root, ec);
  ASSERT_TRUE(std::filesystem::create_directories(tmp_root / "data"));
  // Change CWD so Util::HomeDir() points to tmp_root
  ASSERT_EQ(::chdir(tmp_root.string().c_str()), 0);

  EXPECT_EQ(UserManager::Instance()->Init(), true);
  std::string user = "admin";
  std::string plain_passwd = "admin";
  std::string token;
  EXPECT_EQ(UserManager::Instance()->UserExists(user), Err_User_exists);

  EXPECT_EQ(UserManager::Instance()->UserLogin(
                user, util::Util::SHA256(plain_passwd), &token),
            Err_Success);
  EXPECT_EQ(token.empty(), false);

  plain_passwd = "admin1";
  EXPECT_EQ(UserManager::Instance()->ChangePassword(
                "admin", util::Util::SHA256(plain_passwd), &token),
            Err_Success);
  EXPECT_EQ(UserManager::Instance()->UserExists(user), Err_User_exists);

  plain_passwd = "admin";
  EXPECT_EQ(UserManager::Instance()->UserLogin(
                user, util::Util::SHA256(plain_passwd), &token),
            Err_User_invalid_passwd);
  EXPECT_EQ(token.empty(), false);

  plain_passwd = "admin1";
  EXPECT_EQ(UserManager::Instance()->UserLogin(
                user, util::Util::SHA256(plain_passwd), &token),
            Err_Success);
  EXPECT_EQ(token.empty(), false);

  user = "xiedeacc";
  plain_passwd = "admin";
  EXPECT_EQ(UserManager::Instance()->UserRegister(
                user, util::Util::SHA256(plain_passwd), &token),
            Err_Success);
  EXPECT_EQ(UserManager::Instance()->UserExists(user), Err_User_exists);
  EXPECT_EQ(UserManager::Instance()->UserDelete(user, user, token),
            Err_Success);
  EXPECT_EQ(UserManager::Instance()->UserExists(user), Err_User_not_exists);
}

}  // namespace impl
}  // namespace tbox
