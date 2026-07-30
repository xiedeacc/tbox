/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include <memory>
#include <string>

#include "src/common/logging.h"
#include "src/impl/config_manager.h"
#include "src/impl/dns/cloudflare_provider.h"
#include "src/impl/dns/dns_provider.h"
#include "src/impl/dns/route53_provider.h"

namespace tbox {
namespace impl {
namespace dns {

const char* RecordTypeToString(RecordType type) {
  return type == RecordType::kAAAA ? "AAAA" : "A";
}

std::unique_ptr<DnsProvider> CreateDnsProvider(const std::string& name) {
  if (name == "route53") {
    return std::make_unique<Route53Provider>();
  }
  if (name == "cloudflare") {
    return std::make_unique<CloudflareProvider>();
  }
  LOG(ERROR) << "Unknown DNS provider: " << name
             << ", supported values are route53 and cloudflare";
  return nullptr;
}

std::unique_ptr<DnsProvider> CreateDnsProvider() {
  return CreateDnsProvider(util::ConfigManager::Instance()->DnsProvider());
}

}  // namespace dns
}  // namespace impl
}  // namespace tbox
