/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/handler/handler.h"

#include <filesystem>

#include "gtest/gtest.h"
#include "src/impl/user_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace handler {
namespace {

TEST(HandlerTest, WebLoginDoesNotDisableGrpcChallenge) {
  const auto root =
      std::filesystem::temp_directory_path() / "tbox_handler_test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ASSERT_TRUE(std::filesystem::create_directories(root / "data"));
  std::filesystem::current_path(root);
  ASSERT_TRUE(impl::UserManager::Instance()->Init());

  proto::UserRequest request;
  request.set_op(proto::OpCode::OP_USER_LOGIN);
  request.set_request_id("admin-web-test");
  request.set_user("admin");
  request.set_password(util::Util::SHA256("qh6288QHW"));

  proto::UserResponse web_response;
  Handler::WebUserOpHandle(request, &web_response);
  EXPECT_EQ(web_response.err_code(), proto::ErrCode::Success);
  EXPECT_FALSE(web_response.token().empty());

  proto::UserResponse grpc_response;
  Handler::UserOpHandle(request, &grpc_response);
  EXPECT_NE(grpc_response.err_code(), proto::ErrCode::Success);
  EXPECT_TRUE(grpc_response.token().empty());
}

}  // namespace
}  // namespace handler
}  // namespace server
}  // namespace tbox
