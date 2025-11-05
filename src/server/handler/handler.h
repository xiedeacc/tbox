/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HANDLER_PROXY_H
#define TBOX_SERVER_HANDLER_PROXY_H

#include <fstream>
#include <utility>

#include "aws/core/Aws.h"
#include "aws/route53/Route53Client.h"
#include "aws/route53/model/Change.h"
#include "aws/route53/model/ChangeBatch.h"
#include "aws/route53/model/ChangeResourceRecordSetsRequest.h"
#include "aws/route53/model/ResourceRecord.h"
#include "aws/route53/model/ResourceRecordSet.h"
#include "glog/logging.h"
#include "src/impl/session_manager.h"
#include "src/impl/user_manager.h"
#include "src/proto/service.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace handler {

class Handler {
 public:
  static absl::base_internal::SpinLock kLock;


  static void UserOpHandle(const proto::UserRequest& req,
                           proto::UserResponse* res) {
    // Log request_id (client_id) for login operations
    if (req.op() == proto::OpCode::OP_USER_LOGIN ||
        req.op() == proto::OpCode::OP_USER_CREATE) {
      LOG(INFO) << "User operation: " << req.op()
                << ", Client ID: " << req.request_id()
                << ", User: " << req.user();
    }

    std::string session_user;
    if (req.token().empty() && req.op() != proto::OpCode::OP_USER_CREATE &&
        req.op() != proto::OpCode::OP_USER_LOGIN) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
    }
    if (!req.token().empty() &&
        !impl::SessionManager::Instance()->ValidateSession(req.token(),
                                                           &session_user)) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
      return;
    }
    if (!session_user.empty() && session_user != req.user()) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
      return;
    }

    int32_t ret = Err_Success;
    switch (req.op()) {
      case proto::OpCode::OP_USER_CREATE:
        ret = impl::UserManager::Instance()->UserRegister(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::OpCode::OP_USER_DELETE:
        ret = impl::UserManager::Instance()->UserDelete(
            session_user, req.to_delete_user(), req.token());
        break;
      case proto::OpCode::OP_USER_LOGIN:
        ret = impl::UserManager::Instance()->UserLogin(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::OpCode::OP_USER_CHANGE_PASSWORD:
        ret = impl::UserManager::Instance()->ChangePassword(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::OpCode::OP_USER_LOGOUT:
        ret = impl::UserManager::Instance()->UserLogout(req.token());
        break;
      default:
        ret = Err_Unsupported_op;
        LOG(ERROR) << "Unsupported operation";
    }

    if (ret) {
      res->set_err_code(proto::ErrCode(ret));
    } else {
      res->set_err_code(proto::ErrCode::Success);
    }
  }

  /**
   * @brief Handle certificate operations.
   *
   * @param req Certificate request containing operation details
   * @param res Certificate response to populate
   */
  static void CertOpHandle(const proto::CertRequest& req,
                           proto::CertResponse* res) {
    LOG(INFO) << "Certificate request for domain: " << req.domain();

    try {
      if (req.op() == proto::OpCode::OP_CERT_GET) {
        HandleGetCertificate(req, res);
      } else {
        res->set_err_code(proto::ErrCode::Fail);
        res->set_message("Invalid operation code for certificate management");
        LOG(ERROR) << "Invalid operation code: " << req.op();
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Certificate operation failed: " << e.what();
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message(std::string("Operation failed: ") + e.what());
    }
  }

 private:
  /**
   * @brief Handle certificate retrieval operation.
   *
   * @param req Certificate request
   * @param res Certificate response to populate
   */
  static void HandleGetCertificate(const proto::CertRequest& req,
                                   proto::CertResponse* res) {
    const std::string cert_base_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc";

    // Read certificate files
    std::string cert_content =
        ReadFileContent(cert_base_path + "/xiedeacc.com.cer");
    std::string key_content =
        ReadFileContent(cert_base_path + "/xiedeacc.com.key");
    std::string ca_content = ReadFileContent(cert_base_path + "/ca.cer");

    if (cert_content.empty() || key_content.empty() || ca_content.empty()) {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to read certificate files");
      LOG(ERROR) << "Failed to read certificate files from: " << cert_base_path;
      return;
    }

    res->set_err_code(proto::ErrCode::Success);
    res->set_certificate(cert_content);
    res->set_private_key(key_content);
    res->set_ca_certificate(ca_content);
    res->set_message("Certificate retrieved successfully");

    LOG(INFO) << "Successfully retrieved certificate for domain: "
              << req.domain();
  }

  /**
   * @brief Read content from a file.
   *
   * @param file_path Path to the file to read
   * @return File content as string, empty if file cannot be read
   */
  static std::string ReadFileContent(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      LOG(ERROR) << "Failed to open file: " << file_path;
      return "";
    }

    std::string content;
    std::string line;
    while (std::getline(file, line)) {
      content += line + "\n";
    }
    file.close();

    return content;
  }
};

}  // namespace handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HANDLER_PROXY_H
