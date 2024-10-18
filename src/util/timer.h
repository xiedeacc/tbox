
/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_TIMER_H
#define TBOX_UTIL_TIMER_H

#include "src/util/util.h"

namespace tbox {
namespace util {

struct Timer {
  Timer() : start_(Util::CurrentTimeMillis()) {}
  ~Timer() { LOG(INFO) << "cost " << Util::CurrentTimeMillis() - start_; }

 private:
  int64_t start_;
};

}  // namespace util
}  // namespace tbox

#endif  // TBOX_UTIL_TIMER_H
