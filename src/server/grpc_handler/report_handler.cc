/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/grpc_handler/report_handler.h"

namespace tbox {
namespace server {
namespace grpc_handler {

// Define static members
std::unordered_map<std::string, ClientIPInfo> ReportOpHandler::clients_map_;
std::mutex ReportOpHandler::clients_mutex_;

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox
