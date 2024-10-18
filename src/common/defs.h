/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_COMMON_DEFS_H
#define TBOX_COMMON_DEFS_H

namespace tbox {
namespace common {

constexpr int64_t NET_BUFFER_SIZE_BYTES = 4 * 1024 * 1024;       // 8MB
constexpr int64_t CALC_BUFFER_SIZE_BYTES = 64 * 1024;            // 64KB
constexpr int64_t MAX_GRPC_MSG_SIZE = 2 * 64 * 1024 * 1024 * 8;  // 128MB
constexpr double TRACING_SAMPLER_PROBALITITY = 0.01;             // 1 Percent
constexpr int64_t SESSION_INTERVAL = 5 * 60 * 1000;              // 5min

enum HashMethod {
  Hash_NONE = 0,
  Hash_CRC32,
  Hash_MD5,
  Hash_SHA256,
  Hash_BLAKE3,
};

}  // namespace common
}  // namespace tbox

#endif  // TBOX_COMMON_DEFS_H
