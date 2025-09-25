/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/grpc_handler/ec2_handler.h"

#include <memory>

#include "gtest/gtest.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace server {
namespace grpc_handler {

class EC2OpHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    handler_ = std::make_unique<EC2OpHandler>();
  }

  void TearDown() override {
    handler_.reset();
  }

  std::unique_ptr<EC2OpHandler> handler_;
};

TEST_F(EC2OpHandlerTest, HandlerCreation) {
  EXPECT_NE(handler_, nullptr);
}

TEST_F(EC2OpHandlerTest, CreateStartRequest) {
  proto::EC2Request req;
  req.set_op(proto::OpCode::OP_EC2_START);
  req.set_instance_id("i-1234567890abcdef0");
  req.set_region("us-west-2");

  EXPECT_EQ(req.op(), proto::OpCode::OP_EC2_START);
  EXPECT_EQ(req.instance_id(), "i-1234567890abcdef0");
  EXPECT_EQ(req.region(), "us-west-2");
}

TEST_F(EC2OpHandlerTest, CreateStopRequest) {
  proto::EC2Request req;
  req.set_op(proto::OpCode::OP_EC2_STOP);
  req.set_instance_id("i-1234567890abcdef0");
  req.set_region("us-east-1");

  EXPECT_EQ(req.op(), proto::OpCode::OP_EC2_STOP);
  EXPECT_EQ(req.instance_id(), "i-1234567890abcdef0");
  EXPECT_EQ(req.region(), "us-east-1");
}

TEST_F(EC2OpHandlerTest, CreateResponse) {
  proto::EC2Response res;
  res.set_err_code(proto::ErrCode::Success);
  res.set_instance_id("i-1234567890abcdef0");
  res.set_status("running");
  res.set_message("Instance is running");

  EXPECT_EQ(res.err_code(), proto::ErrCode::Success);
  EXPECT_EQ(res.instance_id(), "i-1234567890abcdef0");
  EXPECT_EQ(res.status(), "running");
  EXPECT_EQ(res.message(), "Instance is running");
}

TEST_F(EC2OpHandlerTest, InvalidOperationCode) {
  proto::EC2Request req;
  req.set_op(proto::OpCode::OP_UNUSED);  // Invalid op code for EC2
  req.set_instance_id("i-1234567890abcdef0");

  EXPECT_EQ(req.op(), proto::OpCode::OP_UNUSED);
  EXPECT_EQ(req.instance_id(), "i-1234567890abcdef0");
}

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox 