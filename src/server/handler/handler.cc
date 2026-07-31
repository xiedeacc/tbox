/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/handler/handler.h"

#include <fstream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>

#include "src/common/logging.h"
#include "src/impl/config_manager.h"
#include "src/impl/session_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace handler {
namespace {

bool IsConfiguredCertificateRequest(const proto::CertRequest& req) {
  std::string session_user;
  if (req.token().empty() ||
      !impl::SessionManager::Instance()->ValidateSession(req.token(),
                                                         &session_user)) {
    return false;
  }

  auto config = util::ConfigManager::Instance();
  const auto clients = config->CertificateSyncClientIds();
  const auto files = config->CertificateFiles();
  return std::find(clients.begin(), clients.end(), req.client_id()) !=
             clients.end() &&
         std::find(files.begin(), files.end(), req.filename()) != files.end() &&
         std::filesystem::path(req.filename()).filename().string() ==
             req.filename();
}

std::string ConfiguredCertificatePath(const std::string& filename) {
  return (std::filesystem::path(
              util::ConfigManager::Instance()->CertificatePath()) /
          filename)
      .string();
}

}  // namespace

void Handler::HandleGetCertificate(const proto::CertRequest& req,
                                   proto::CertResponse* res) {
  const std::string cert_base_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc";

  // Read certificate files
  std::string cert_content = ReadFileContent(cert_base_path + "/fullchain.cer");
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

void Handler::HandleGetFullchainCertHash(const proto::ReportRequest&,
                                         proto::ReportResponse* res) {
  std::string cert_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
  std::string hash_result;
  bool success = util::Util::FileSHA256(cert_path, &hash_result);

  if (success && !hash_result.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->add_client_ip(hash_result);  // Return hash in client_ip field
    res->set_message(
        "Fullchain certificate hash: " + hash_result.substr(0, 16) + "...");
    LOG(INFO) << "Sent fullchain certificate hash: "
              << hash_result.substr(0, 16) << "...";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to calculate fullchain certificate hash");
    LOG(ERROR) << "Failed to calculate hash for fullchain certificate: "
               << cert_path;
  }
}

void Handler::HandleGetFullchainCertHash(const proto::CertRequest&,
                                         proto::CertResponse* res) {
  std::string cert_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
  std::string hash_result;
  bool success = util::Util::FileSHA256(cert_path, &hash_result);

  if (success && !hash_result.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_message(hash_result);  // Return hash in message field
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to calculate fullchain certificate hash");
    LOG(ERROR) << "Failed to calculate hash for fullchain certificate: "
               << cert_path;
  }
}

void Handler::HandleGetCACertHash(const proto::ReportRequest&,
                                  proto::ReportResponse* res) {
  std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
  std::string hash_result;
  bool success = util::Util::FileSHA256(cert_path, &hash_result);

  if (success && !hash_result.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->add_client_ip(hash_result);  // Return hash in client_ip field
    res->set_message("CA certificate hash: " + hash_result.substr(0, 16) +
                     "...");
    LOG(INFO) << "Sent CA certificate hash: " << hash_result.substr(0, 16)
              << "...";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to calculate CA certificate hash");
    LOG(ERROR) << "Failed to calculate hash for CA certificate: " << cert_path;
  }
}

void Handler::HandleGetCACertHash(const proto::CertRequest&,
                                  proto::CertResponse* res) {
  std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
  std::string hash_result;
  bool success = util::Util::FileSHA256(cert_path, &hash_result);

  if (success && !hash_result.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_message(hash_result);  // Return hash in message field
    LOG(INFO) << "Sent CA certificate hash: " << hash_result.substr(0, 16)
              << "...";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to calculate CA certificate hash");
    LOG(ERROR) << "Failed to calculate hash for CA certificate: " << cert_path;
  }
}

void Handler::HandleGetFullchainCert(const proto::ReportRequest&,
                                     proto::ReportResponse* res) {
  std::string cert_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
  std::string cert_content = ReadFileContent(cert_path);

  if (!cert_content.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_message(cert_content);  // Return certificate in message field
    LOG(INFO) << "Sent fullchain certificate content (" << cert_content.length()
              << " bytes)";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to read fullchain certificate");
    LOG(ERROR) << "Failed to read fullchain certificate from: " << cert_path;
  }
}

void Handler::HandleGetFullchainCert(const proto::CertRequest&,
                                     proto::CertResponse* res) {
  std::string cert_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/fullchain.cer";
  std::string cert_content = ReadFileContent(cert_path);

  if (!cert_content.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_certificate(
        cert_content);  // Return certificate in certificate field
    res->set_message("Fullchain certificate retrieved successfully");
    LOG(INFO) << "Sent fullchain certificate content (" << cert_content.length()
              << " bytes)";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to read fullchain certificate");
    LOG(ERROR) << "Failed to read fullchain certificate from: " << cert_path;
  }
}

void Handler::HandleGetCACert(const proto::ReportRequest&,
                              proto::ReportResponse* res) {
  std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
  std::string cert_content = ReadFileContent(cert_path);

  if (!cert_content.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_message(cert_content);  // Return certificate in message field
    LOG(INFO) << "Sent CA certificate content (" << cert_content.length()
              << " bytes)";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to read CA certificate");
    LOG(ERROR) << "Failed to read CA certificate from: " << cert_path;
  }
}

void Handler::HandleGetCACert(const proto::CertRequest&,
                              proto::CertResponse* res) {
  std::string cert_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/ca.cer";
  std::string cert_content = ReadFileContent(cert_path);

  if (!cert_content.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_ca_certificate(
        cert_content);  // Return certificate in ca_certificate field
    res->set_message("CA certificate retrieved successfully");
    LOG(INFO) << "Sent CA certificate content (" << cert_content.length()
              << " bytes)";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to read CA certificate");
    LOG(ERROR) << "Failed to read CA certificate from: " << cert_path;
  }
}

void Handler::HandleGetPrivateKeyHash(const proto::ReportRequest&,
                                      proto::ReportResponse* res) {
  // Get SHA256 hash of the private key file
  std::string key_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
  std::string hash_result;
  bool success = util::Util::FileSHA256(key_path, &hash_result);

  if (success && !hash_result.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->add_client_ip(hash_result);  // Return hash in client_ip field
    res->set_message("Private key hash: " + hash_result.substr(0, 16) + "...");
    LOG(INFO) << "Sent private key hash: " << hash_result.substr(0, 16)
              << "...";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to calculate private key hash");
    LOG(ERROR) << "Failed to calculate hash for private key: " << key_path;
  }
}

void Handler::HandleGetPrivateKeyHash(const proto::CertRequest&,
                                      proto::CertResponse* res) {
  // Get SHA256 hash of the private key file
  std::string key_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
  std::string hash_result;
  bool success = util::Util::FileSHA256(key_path, &hash_result);

  if (success && !hash_result.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_message(hash_result);  // Return hash in message field
    LOG(INFO) << "Sent private key hash: " << hash_result.substr(0, 16)
              << "...";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to calculate private key hash");
    LOG(ERROR) << "Failed to calculate hash for private key: " << key_path;
  }
}

void Handler::HandleGetPrivateKey(const proto::ReportRequest&,
                                  proto::ReportResponse* res) {
  // Read private key file content
  std::string key_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
  std::string private_key_content = ReadFileContent(key_path);

  if (!private_key_content.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_message(private_key_content);  // Return key in message field
    LOG(INFO) << "Sent private key content (" << private_key_content.length()
              << " bytes)";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to read private key");
    LOG(ERROR) << "Failed to read private key from: " << key_path;
  }
}

void Handler::HandleGetPrivateKey(const proto::CertRequest&,
                                  proto::CertResponse* res) {
  // Read private key file content
  std::string key_path =
      "/home/ubuntu/.acme.sh/xiedeacc.com_ecc/xiedeacc.com.key";
  std::string private_key_content = ReadFileContent(key_path);

  if (!private_key_content.empty()) {
    res->set_err_code(proto::ErrCode::Success);
    res->set_private_key(
        private_key_content);  // Return key in private_key field
    res->set_message("Private key retrieved successfully");
    LOG(INFO) << "Sent private key content (" << private_key_content.length()
              << " bytes)";
  } else {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Failed to read private key");
    LOG(ERROR) << "Failed to read private key from: " << key_path;
  }
}

void Handler::HandleGetCertFileHash(const proto::CertRequest& req,
                                    proto::CertResponse* res) {
  if (!IsConfiguredCertificateRequest(req)) {
    res->set_err_code(proto::ErrCode::User_session_error);
    res->set_message("Certificate synchronization is not authorized");
    return;
  }

  std::string hash;
  const std::string path = ConfiguredCertificatePath(req.filename());
  if (!util::Util::FileSHA256(path, &hash) || hash.empty()) {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Certificate file is unavailable");
    return;
  }
  res->set_err_code(proto::ErrCode::Success);
  res->set_message(hash);
}

void Handler::HandleGetCertFile(const proto::CertRequest& req,
                                proto::CertResponse* res) {
  if (!IsConfiguredCertificateRequest(req)) {
    res->set_err_code(proto::ErrCode::User_session_error);
    res->set_message("Certificate synchronization is not authorized");
    return;
  }

  const std::string content =
      ReadFileContent(ConfiguredCertificatePath(req.filename()));
  if (content.empty()) {
    res->set_err_code(proto::ErrCode::Fail);
    res->set_message("Certificate file is unavailable");
    return;
  }
  res->set_err_code(proto::ErrCode::Success);
  res->set_file_content(content);
}

std::string Handler::ReadFileContent(const std::string& file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    LOG(ERROR) << "Failed to open file: " << file_path;
    return "";
  }

  std::ostringstream content;
  content << file.rdbuf();
  return content.str();
}

}  // namespace handler
}  // namespace server
}  // namespace tbox
