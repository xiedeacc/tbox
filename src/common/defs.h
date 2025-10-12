/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_COMMON_DEFS_H
#define TBOX_COMMON_DEFS_H

#include <stdint.h>

namespace tbox {
namespace common {

constexpr int64_t NET_BUFFER_SIZE_BYTES = 4 * 1024 * 1024;  // 8MB
constexpr int64_t CALC_BUFFER_SIZE_BYTES = 64 * 1024;       // 64KB
constexpr int64_t SESSION_INTERVAL = 5 * 60 * 1000;         // 5min

constexpr int kSaltSize = 16;        // 16 bytes (128 bits)
constexpr int kDerivedKeySize = 32;  // 32 bytes (256 bits)
constexpr int kIterations = 100000;  // PBKDF2 iterations

}  // namespace common
}  // namespace tbox

#endif  // TBOX_COMMON_DEFS_H
