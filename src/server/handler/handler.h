/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HANDLER_PROXY_H
#define TBOX_SERVER_HANDLER_PROXY_H

#include <array>
#include <cstdio>
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
        ReadFileContent(cert_base_path + "/fullchain.cer");
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
   * @brief Handle OP_GET_FULLCHAIN_CERT_HASH - Return SHA256 hash of fullchain certificate (ReportResponse)
   */
  static void HandleGetFullchainCertHash(const proto::ReportRequest& req,
                                         proto::ReportResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
    std::string command = "openssl dgst -sha256 " + cert_path + " 2>/dev/null | awk '{print $2}'";
    std::string hash_result = ExecuteCommand(command);
    
    // Remove any trailing whitespace
    hash_result.erase(hash_result.find_last_not_of(" \t\r\n") + 1);
    
    if (!hash_result.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->add_client_ip(hash_result);  // Return hash in client_ip field
      res->set_message("Fullchain certificate hash: " + hash_result.substr(0, 16) + "...");
      LOG(INFO) << "Sent fullchain certificate hash: " << hash_result.substr(0, 16) << "...";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to calculate fullchain certificate hash");
      LOG(ERROR) << "Failed to calculate hash for fullchain certificate: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_FULLCHAIN_CERT_HASH - Return SHA256 hash of fullchain certificate (CertResponse)
   */
  static void HandleGetFullchainCertHash(const proto::CertRequest& req,
                                         proto::CertResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
    std::string command = "openssl dgst -sha256 " + cert_path + " 2>/dev/null | awk '{print $2}'";
    std::string hash_result = ExecuteCommand(command);
    
    // Remove any trailing whitespace
    hash_result.erase(hash_result.find_last_not_of(" \t\r\n") + 1);
    
    if (!hash_result.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_message(hash_result);  // Return hash in message field
      LOG(INFO) << "Sent fullchain certificate hash: " << hash_result.substr(0, 16) << "...";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to calculate fullchain certificate hash");
      LOG(ERROR) << "Failed to calculate hash for fullchain certificate: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_CA_CERT_HASH - Return SHA256 hash of CA certificate (ReportResponse)
   */
  static void HandleGetCACertHash(const proto::ReportRequest& req,
                                  proto::ReportResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
    std::string command = "openssl dgst -sha256 " + cert_path + " 2>/dev/null | awk '{print $2}'";
    std::string hash_result = ExecuteCommand(command);
    
    // Remove any trailing whitespace
    hash_result.erase(hash_result.find_last_not_of(" \t\r\n") + 1);
    
    if (!hash_result.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->add_client_ip(hash_result);  // Return hash in client_ip field
      res->set_message("CA certificate hash: " + hash_result.substr(0, 16) + "...");
      LOG(INFO) << "Sent CA certificate hash: " << hash_result.substr(0, 16) << "...";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to calculate CA certificate hash");
      LOG(ERROR) << "Failed to calculate hash for CA certificate: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_CA_CERT_HASH - Return SHA256 hash of CA certificate (CertResponse)
   */
  static void HandleGetCACertHash(const proto::CertRequest& req,
                                  proto::CertResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
    std::string command = "openssl dgst -sha256 " + cert_path + " 2>/dev/null | awk '{print $2}'";
    std::string hash_result = ExecuteCommand(command);
    
    // Remove any trailing whitespace
    hash_result.erase(hash_result.find_last_not_of(" \t\r\n") + 1);
    
    if (!hash_result.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_message(hash_result);  // Return hash in message field
      LOG(INFO) << "Sent CA certificate hash: " << hash_result.substr(0, 16) << "...";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to calculate CA certificate hash");
      LOG(ERROR) << "Failed to calculate hash for CA certificate: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_FULLCHAIN_CERT - Return fullchain certificate content (ReportResponse)
   */
  static void HandleGetFullchainCert(const proto::ReportRequest& req,
                                     proto::ReportResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
    std::string cert_content = ReadFileContent(cert_path);
    
    if (!cert_content.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_message(cert_content);  // Return certificate in message field
      LOG(INFO) << "Sent fullchain certificate content (" << cert_content.length() << " bytes)";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to read fullchain certificate");
      LOG(ERROR) << "Failed to read fullchain certificate from: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_FULLCHAIN_CERT - Return fullchain certificate content (CertResponse)
   */
  static void HandleGetFullchainCert(const proto::CertRequest& req,
                                     proto::CertResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
    std::string cert_content = ReadFileContent(cert_path);
    
    if (!cert_content.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_certificate(cert_content);  // Return certificate in certificate field
      res->set_message("Fullchain certificate retrieved successfully");
      LOG(INFO) << "Sent fullchain certificate content (" << cert_content.length() << " bytes)";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to read fullchain certificate");
      LOG(ERROR) << "Failed to read fullchain certificate from: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_CA_CERT - Return CA certificate content (ReportResponse)
   */
  static void HandleGetCACert(const proto::ReportRequest& req,
                              proto::ReportResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
    std::string cert_content = ReadFileContent(cert_path);
    
    if (!cert_content.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_message(cert_content);  // Return certificate in message field
      LOG(INFO) << "Sent CA certificate content (" << cert_content.length() << " bytes)";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to read CA certificate");
      LOG(ERROR) << "Failed to read CA certificate from: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_CA_CERT - Return CA certificate content (CertResponse)
   */
  static void HandleGetCACert(const proto::CertRequest& req,
                              proto::CertResponse* res) {
    std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
    std::string cert_content = ReadFileContent(cert_path);
    
    if (!cert_content.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_ca_certificate(cert_content);  // Return certificate in ca_certificate field
      res->set_message("CA certificate retrieved successfully");
      LOG(INFO) << "Sent CA certificate content (" << cert_content.length() << " bytes)";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to read CA certificate");
      LOG(ERROR) << "Failed to read CA certificate from: " << cert_path;
    }
  }

  /**
   * @brief Handle OP_GET_PRIVATE_KEY_HASH - Return SHA256 hash of private key (ReportResponse)
   */
  static void HandleGetPrivateKeyHash(const proto::ReportRequest& req,
                                      proto::ReportResponse* res) {
    // Get SHA256 hash of the private key file
    std::string key_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
    std::string command = "openssl dgst -sha256 " + key_path + " 2>/dev/null | awk '{print $2}'";
    std::string hash_result = ExecuteCommand(command);
    
    // Remove any trailing whitespace
    hash_result.erase(hash_result.find_last_not_of(" \t\r\n") + 1);
    
    if (!hash_result.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->add_client_ip(hash_result);  // Return hash in client_ip field
      res->set_message("Private key hash: " + hash_result.substr(0, 16) + "...");
      LOG(INFO) << "Sent private key hash: " << hash_result.substr(0, 16) << "...";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to calculate private key hash");
      LOG(ERROR) << "Failed to calculate hash for private key: " << key_path;
    }
  }

  /**
   * @brief Handle OP_GET_PRIVATE_KEY_HASH - Return SHA256 hash of private key (CertResponse)
   */
  static void HandleGetPrivateKeyHash(const proto::CertRequest& req,
                                      proto::CertResponse* res) {
    // Get SHA256 hash of the private key file
    std::string key_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
    std::string command = "openssl dgst -sha256 " + key_path + " 2>/dev/null | awk '{print $2}'";
    std::string hash_result = ExecuteCommand(command);
    
    // Remove any trailing whitespace
    hash_result.erase(hash_result.find_last_not_of(" \t\r\n") + 1);
    
    if (!hash_result.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_message(hash_result);  // Return hash in message field
      LOG(INFO) << "Sent private key hash: " << hash_result.substr(0, 16) << "...";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to calculate private key hash");
      LOG(ERROR) << "Failed to calculate hash for private key: " << key_path;
    }
  }

  /**
   * @brief Handle OP_GET_PRIVATE_KEY - Return private key content (ReportResponse)
   */
  static void HandleGetPrivateKey(const proto::ReportRequest& req,
                                  proto::ReportResponse* res) {
    // Read private key file content
    std::string key_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
    std::string private_key_content = ReadFileContent(key_path);
    
    if (!private_key_content.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_message(private_key_content);  // Return key in message field
      LOG(INFO) << "Sent private key content (" << private_key_content.length() << " bytes)";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to read private key");
      LOG(ERROR) << "Failed to read private key from: " << key_path;
    }
  }

  /**
   * @brief Handle OP_GET_PRIVATE_KEY - Return private key content (CertResponse)
   */
  static void HandleGetPrivateKey(const proto::CertRequest& req,
                                  proto::CertResponse* res) {
    // Read private key file content
    std::string key_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
    std::string private_key_content = ReadFileContent(key_path);
    
    if (!private_key_content.empty()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_private_key(private_key_content);  // Return key in private_key field
      res->set_message("Private key retrieved successfully");
      LOG(INFO) << "Sent private key content (" << private_key_content.length() << " bytes)";
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to read private key");
      LOG(ERROR) << "Failed to read private key from: " << key_path;
    }
  }

  /**
   * @brief Execute shell command and return output
   */
  static std::string ExecuteCommand(const std::string& command) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
      LOG(ERROR) << "Failed to execute command: " << command;
      return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      result += buffer.data();
    }
    return result;
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
