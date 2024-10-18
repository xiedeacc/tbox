/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_TEST_TEST_UTIL_H
#define TBOX_TEST_TEST_UTIL_H

#include "src/util/util.h"

namespace tbox {
namespace test {

class Util {
 private:
  Util() = delete;
  ~Util() = delete;

 public:
  static bool IsBazelRunUnitTest() {
    auto runfile_dir = util::Util::GetEnv("TEST_SRCDIR");
    if (runfile_dir.has_value()) {
      return true;
    }
    return false;
  }
};

}  // namespace test
}  // namespace tbox

#endif  // TBOX_TEST_TEST_UTIL_H
