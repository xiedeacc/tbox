/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/platform_ca_bundle.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "src/common/logging.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace tbox {
namespace client {
namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool WriteFileAtomically(const std::filesystem::path& destination,
                         const std::string& content) {
  std::error_code error;
  if (!destination.parent_path().empty()) {
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
      LOG(ERROR) << "Failed to create CA bundle directory: "
                 << error.message();
      return false;
    }
  }

  std::filesystem::path temporary = destination;
  temporary += ".new";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      LOG(ERROR) << "Failed to open temporary CA bundle: "
                 << temporary.string();
      return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (output.fail()) {
      LOG(ERROR) << "Failed to write temporary CA bundle: "
                 << temporary.string();
      std::filesystem::remove(temporary, error);
      return false;
    }
  }

#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    LOG(ERROR) << "Failed to replace CA bundle, Windows error: "
               << GetLastError();
    std::filesystem::remove(temporary, error);
    return false;
  }
#else
  if (std::rename(temporary.c_str(), destination.c_str()) != 0) {
    LOG(ERROR) << "Failed to replace CA bundle";
    std::filesystem::remove(temporary, error);
    return false;
  }
#endif
  return true;
}

#if defined(_WIN32)

bool AddStoreCertificates(
    DWORD store_location,
    std::set<std::vector<unsigned char>>* certificates) {
  HCERTSTORE store = CertOpenStore(
      CERT_STORE_PROV_SYSTEM_W, 0, 0,
      store_location | CERT_STORE_OPEN_EXISTING_FLAG |
          CERT_STORE_READONLY_FLAG,
      L"ROOT");
  if (store == nullptr) {
    LOG(WARNING) << "Failed to open a Windows ROOT certificate store, error: "
                 << GetLastError();
    return false;
  }

  PCCERT_CONTEXT context = nullptr;
  while ((context = CertEnumCertificatesInStore(store, context)) != nullptr) {
    certificates->emplace(context->pbCertEncoded,
                          context->pbCertEncoded + context->cbCertEncoded);
  }
  CertCloseStore(store, 0);
  return true;
}

std::string CertificateToPEM(
    const std::vector<unsigned char>& certificate) {
  DWORD output_size = 0;
  if (!CryptBinaryToStringA(certificate.data(),
                            static_cast<DWORD>(certificate.size()),
                            CRYPT_STRING_BASE64HEADER, nullptr,
                            &output_size)) {
    return "";
  }

  std::string pem(output_size, '\0');
  if (!CryptBinaryToStringA(certificate.data(),
                            static_cast<DWORD>(certificate.size()),
                            CRYPT_STRING_BASE64HEADER, pem.data(),
                            &output_size)) {
    return "";
  }
  if (!pem.empty() && pem.back() == '\0') {
    pem.pop_back();
  }
  return pem;
}

std::string ExportWindowsRootStores() {
  std::set<std::vector<unsigned char>> certificates;
  const bool machine_store_opened =
      AddStoreCertificates(CERT_SYSTEM_STORE_LOCAL_MACHINE, &certificates);
  const bool user_store_opened =
      AddStoreCertificates(CERT_SYSTEM_STORE_CURRENT_USER, &certificates);
  if ((!machine_store_opened && !user_store_opened) || certificates.empty()) {
    return "";
  }

  std::string bundle;
  for (const auto& certificate : certificates) {
    std::string pem = CertificateToPEM(certificate);
    if (pem.empty()) {
      LOG(ERROR) << "Failed to encode a Windows root certificate as PEM";
      return "";
    }
    bundle += pem;
    if (bundle.back() != '\n') {
      bundle += "\r\n";
    }
  }
  return bundle;
}

std::string GitForWindowsFallback() {
  std::vector<std::filesystem::path> candidates;
  for (const char* variable : {"ProgramW6432", "ProgramFiles"}) {
    DWORD required = GetEnvironmentVariableA(variable, nullptr, 0);
    if (required <= 1) {
      continue;
    }
    std::string value(required, '\0');
    GetEnvironmentVariableA(variable, value.data(), required);
    if (!value.empty() && value.back() == '\0') {
      value.pop_back();
    }
    const std::filesystem::path git_root =
        std::filesystem::path(value) / "Git";
    candidates.push_back(git_root / "mingw64/etc/ssl/certs/ca-bundle.crt");
    candidates.push_back(git_root / "usr/ssl/certs/ca-bundle.crt");
  }

  for (const auto& candidate : candidates) {
    std::string content = ReadFile(candidate);
    if (content.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
      LOG(WARNING) << "Using Git for Windows CA bundle fallback: "
                   << candidate.string();
      return content;
    }
  }
  return "";
}

#else

std::string ReadSystemCABundle() {
#if defined(__APPLE__)
  const std::vector<std::filesystem::path> candidates = {
      "/etc/ssl/cert.pem",
  };
#else
  const std::vector<std::filesystem::path> candidates = {
      "/etc/ssl/certs/ca-certificates.crt",
      "/etc/ssl/cert.pem",
  };
#endif

  for (const auto& candidate : candidates) {
    std::string content = ReadFile(candidate);
    if (content.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
      return content;
    }
  }
  return "";
}

#endif

}  // namespace

PlatformCABundle::UpdateResult PlatformCABundle::Refresh(
    const std::string& destination) {
#if defined(_WIN32)
  if (destination.empty()) {
    LOG(ERROR) << "CA bundle destination is empty";
    return UpdateResult::kError;
  }

  const std::filesystem::path destination_path(destination);
  std::string bundle = ExportWindowsRootStores();
  if (bundle.empty()) {
    const std::string existing = ReadFile(destination_path);
    if (existing.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
      LOG(WARNING) << "Keeping the existing CA bundle because Windows ROOT "
                      "stores could not be exported";
      return UpdateResult::kUnchanged;
    }
    bundle = GitForWindowsFallback();
  }
  if (bundle.empty()) {
    LOG(ERROR) << "No Windows trusted roots or Git CA bundle are available";
    return UpdateResult::kError;
  }

  if (ReadFile(destination_path) == bundle) {
    return UpdateResult::kUnchanged;
  }
  if (!WriteFileAtomically(destination_path, bundle)) {
    return UpdateResult::kError;
  }

  LOG(INFO) << "Updated platform CA bundle: " << destination;
  return UpdateResult::kUpdated;
#else
  if (destination.empty()) {
    LOG(ERROR) << "CA bundle destination is empty";
    return UpdateResult::kError;
  }
  const std::string bundle = ReadSystemCABundle();
  if (bundle.empty()) {
    LOG(ERROR) << "No operating system CA bundle is available";
    return UpdateResult::kError;
  }

  const std::filesystem::path destination_path(destination);
  if (ReadFile(destination_path) == bundle) {
    return UpdateResult::kUnchanged;
  }
  if (!WriteFileAtomically(destination_path, bundle)) {
    return UpdateResult::kError;
  }
  LOG(INFO) << "Updated platform CA bundle: " << destination;
  return UpdateResult::kUpdated;
#endif
}

}  // namespace client
}  // namespace tbox
