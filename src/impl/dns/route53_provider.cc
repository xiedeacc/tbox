/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/dns/route53_provider.h"

#include <memory>
#include <string>
#include <vector>

#include "aws/core/Aws.h"
#include "aws/core/auth/AWSCredentials.h"
#include "aws/core/client/ClientConfiguration.h"
#include "aws/route53/Route53Client.h"
#include "aws/route53/model/Change.h"
#include "aws/route53/model/ChangeBatch.h"
#include "aws/route53/model/ChangeResourceRecordSetsRequest.h"
#include "aws/route53/model/ListHostedZonesRequest.h"
#include "aws/route53/model/ListResourceRecordSetsRequest.h"
#include "aws/route53/model/RRType.h"
#include "aws/route53/model/ResourceRecord.h"
#include "aws/route53/model/ResourceRecordSet.h"
#include "src/common/logging.h"
#include "src/impl/config_manager.h"

namespace tbox {
namespace impl {
namespace dns {
namespace {

/// @brief Append a trailing dot as required by the Route53 API.
/// @param domain Domain name to normalise.
/// @return Domain name ending with a dot.
std::string ToAbsoluteName(const std::string& domain) {
  if (!domain.empty() && domain.back() == '.') {
    return domain;
  }
  return domain + ".";
}

/// @brief Map a record type to the Route53 SDK enumeration.
/// @param type Record type to map.
/// @return Corresponding RRType value.
Aws::Route53::Model::RRType ToRRType(RecordType type) {
  return type == RecordType::kAAAA ? Aws::Route53::Model::RRType::AAAA
                                   : Aws::Route53::Model::RRType::A;
}

}  // namespace

Route53Provider::Route53Provider() = default;

Route53Provider::~Route53Provider() {
  client_.reset();
  if (sdk_initialized_) {
    Aws::SDKOptions aws_options;
    Aws::ShutdownAPI(aws_options);
    LOG(INFO) << "AWS SDK shutdown for Route53Provider";
  }
}

bool Route53Provider::Init() {
  Aws::SDKOptions aws_options;
  Aws::InitAPI(aws_options);
  sdk_initialized_ = true;
  LOG(INFO) << "AWS SDK initialized for Route53Provider";

  auto config_manager = util::ConfigManager::Instance();

  Aws::Client::ClientConfiguration client_config;
  std::string region = config_manager->AwsRegion();
  if (region.empty()) {
    region = kDefaultRegion;
  }
  client_config.region = region;
  LOG(INFO) << "Route53 backend using AWS region: " << region;

  const std::string access_key_id = config_manager->AwsAccessKeyId();
  const std::string secret_access_key = config_manager->AwsSecretAccessKey();
  if (!access_key_id.empty() && !secret_access_key.empty()) {
    Aws::Auth::AWSCredentials credentials(access_key_id.c_str(),
                                          secret_access_key.c_str());
    client_ = std::make_unique<Aws::Route53::Route53Client>(credentials,
                                                            client_config);
    LOG(INFO) << "Route53 backend using credentials from config file";
  } else {
    client_ = std::make_unique<Aws::Route53::Route53Client>(client_config);
    LOG(INFO) << "Route53 backend using AWS default credential chain";
  }
  return true;
}

std::string Route53Provider::GetZoneId(const std::string& domain) {
  const std::string configured =
      util::ConfigManager::Instance()->Route53HostedZoneId();
  if (!configured.empty()) {
    return configured;
  }

  if (!client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return "";
  }

  Aws::Route53::Model::ListHostedZonesRequest request;
  auto outcome = client_->ListHostedZones(request);
  if (!outcome.IsSuccess()) {
    LOG(ERROR) << "Failed to list hosted zones: "
               << outcome.GetError().GetMessage();
    return "";
  }

  const std::string search_name = ToAbsoluteName(domain);
  for (const auto& zone : outcome.GetResult().GetHostedZones()) {
    if (zone.GetName() != search_name) {
      continue;
    }
    // Zone identifiers are returned in "/hostedzone/XXXX" form.
    const std::string zone_id = zone.GetId();
    const size_t pos = zone_id.find_last_of('/');
    return pos == std::string::npos ? zone_id : zone_id.substr(pos + 1);
  }

  LOG(ERROR) << "Hosted zone not found for domain: " << domain;
  return "";
}

bool Route53Provider::ListRecords(const std::string& zone_id,
                                  const std::string& domain, RecordType type,
                                  std::vector<Record>* records) {
  records->clear();
  if (!client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return false;
  }

  Aws::Route53::Model::ListResourceRecordSetsRequest request;
  request.SetHostedZoneId(zone_id);

  auto outcome = client_->ListResourceRecordSets(request);
  if (!outcome.IsSuccess()) {
    LOG(ERROR) << "Failed to list resource record sets: "
               << outcome.GetError().GetMessage();
    return false;
  }

  const std::string search_name = ToAbsoluteName(domain);
  const Aws::Route53::Model::RRType wanted = ToRRType(type);
  for (const auto& record_set : outcome.GetResult().GetResourceRecordSets()) {
    if (record_set.GetName() != search_name ||
        record_set.GetType() != wanted) {
      continue;
    }
    for (const auto& record : record_set.GetResourceRecords()) {
      Record item;
      item.name = domain;
      item.type = type;
      item.value = record.GetValue();
      item.ttl = static_cast<int>(record_set.GetTTL());
      records->push_back(item);
    }
  }
  return true;
}

bool Route53Provider::ApplyChange(const std::string& zone_id,
                                  const std::string& domain, RecordType type,
                                  const std::string& value, int ttl,
                                  bool upsert) {
  if (!client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return false;
  }

  Aws::Route53::Model::ResourceRecord record;
  record.SetValue(value);

  Aws::Route53::Model::ResourceRecordSet record_set;
  record_set.SetName(ToAbsoluteName(domain));
  record_set.SetType(ToRRType(type));
  record_set.SetTTL(ttl);
  record_set.AddResourceRecords(record);

  Aws::Route53::Model::Change change;
  change.SetAction(upsert ? Aws::Route53::Model::ChangeAction::UPSERT
                          : Aws::Route53::Model::ChangeAction::DELETE_);
  change.SetResourceRecordSet(record_set);

  Aws::Route53::Model::ChangeBatch change_batch;
  change_batch.AddChanges(change);
  change_batch.SetComment("Updated by DDNS manager");

  Aws::Route53::Model::ChangeResourceRecordSetsRequest request;
  request.SetHostedZoneId(zone_id);
  request.SetChangeBatch(change_batch);

  auto outcome = client_->ChangeResourceRecordSets(request);
  if (!outcome.IsSuccess()) {
    LOG(ERROR) << "Failed to " << (upsert ? "upsert" : "delete")
               << " Route53 " << RecordTypeToString(type)
               << " record: " << outcome.GetError().GetMessage();
    return false;
  }
  return true;
}

bool Route53Provider::UpsertRecord(const std::string& zone_id,
                                   const std::string& domain, RecordType type,
                                   const std::string& value, int ttl) {
  if (!ApplyChange(zone_id, domain, type, value, ttl, true)) {
    return false;
  }
  LOG(INFO) << "Successfully updated Route53 " << RecordTypeToString(type)
            << " record: " << domain << " -> " << value;
  return true;
}

bool Route53Provider::DeleteRecord(const std::string& zone_id,
                                   const std::string& domain, RecordType type,
                                   const std::string& value) {
  // Route53 deletions must match the stored TTL, so read it back first.
  std::vector<Record> existing;
  int ttl = 60;
  if (ListRecords(zone_id, domain, type, &existing)) {
    for (const auto& record : existing) {
      if (record.value == value) {
        ttl = record.ttl;
        break;
      }
    }
  }

  if (!ApplyChange(zone_id, domain, type, value, ttl, false)) {
    return false;
  }
  LOG(WARNING) << "Deleted Route53 " << RecordTypeToString(type)
               << " record: " << domain << " -> " << value;
  return true;
}

}  // namespace dns
}  // namespace impl
}  // namespace tbox
