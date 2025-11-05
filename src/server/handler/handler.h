/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HANDLER_PROXY_H
#define TBOX_SERVER_HANDLER_PROXY_H

#include <fstream>
#include <memory>
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

  /**
   * @brief Handle certificate retrieval operation.
   *
   * @param req Certificate request
   * @param res Certificate response to populate
   */
  static void HandleGetCertificate(const proto::CertRequest& req,
                                   proto::CertResponse* res);

  /**
   * @brief Handle OP_GET_FULLCHAIN_CERT_HASH - Return SHA256 hash of fullchain
   * certificate (ReportResponse)
   */
  static void HandleGetFullchainCertHash(const proto::ReportRequest& req,
                                         proto::ReportResponse* res);

  /**
   * @brief Handle OP_GET_FULLCHAIN_CERT_HASH - Return SHA256 hash of fullchain
   * certificate (CertResponse)
   */
  static void HandleGetFullchainCertHash(const proto::CertRequest& req,
                                         proto::CertResponse* res);

  /**
   * @brief Handle OP_GET_CA_CERT_HASH - Return SHA256 hash of CA certificate
   * (ReportResponse)
   */
  static void HandleGetCACertHash(const proto::ReportRequest& req,
                                  proto::ReportResponse* res);

  /**
   * @brief Handle OP_GET_CA_CERT_HASH - Return SHA256 hash of CA certificate
   * (CertResponse)
   */
  static void HandleGetCACertHash(const proto::CertRequest& req,
                                  proto::CertResponse* res);

  /**
   * @brief Handle OP_GET_FULLCHAIN_CERT - Return fullchain certificate content
   * (ReportResponse)
   */
  static void HandleGetFullchainCert(const proto::ReportRequest& req,
                                     proto::ReportResponse* res);

  /**
   * @brief Handle OP_GET_FULLCHAIN_CERT - Return fullchain certificate content
   * (CertResponse)
   */
  static void HandleGetFullchainCert(const proto::CertRequest& req,
                                     proto::CertResponse* res);

  /**
   * @brief Handle OP_GET_CA_CERT - Return CA certificate content
   * (ReportResponse)
   */
  static void HandleGetCACert(const proto::ReportRequest& req,
                              proto::ReportResponse* res);

  /**
   * @brief Handle OP_GET_CA_CERT - Return CA certificate content (CertResponse)
   */
  static void HandleGetCACert(const proto::CertRequest& req,
                              proto::CertResponse* res);

  /**
   * @brief Handle OP_GET_PRIVATE_KEY_HASH - Return SHA256 hash of private key
   * (ReportResponse)
   */
  static void HandleGetPrivateKeyHash(const proto::ReportRequest& req,
                                      proto::ReportResponse* res);

  /**
   * @brief Handle OP_GET_PRIVATE_KEY_HASH - Return SHA256 hash of private key
   * (CertResponse)
   */
  static void HandleGetPrivateKeyHash(const proto::CertRequest& req,
                                      proto::CertResponse* res);

  /**
   * @brief Handle OP_GET_PRIVATE_KEY - Return private key content
   * (ReportResponse)
   */
  static void HandleGetPrivateKey(const proto::ReportRequest& req,
                                  proto::ReportResponse* res);

  /**
   * @brief Handle OP_GET_PRIVATE_KEY - Return private key content
   * (CertResponse)
   */
  static void HandleGetPrivateKey(const proto::CertRequest& req,
                                  proto::CertResponse* res);

  /**
   * @brief Read content from a file.
   *
   * @param file_path Path to the file to read
   * @return File content as string, empty if file cannot be read
   */
  static std::string ReadFileContent(const std::string& file_path);

 private:
};

}  // namespace handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HANDLER_PROXY_H
