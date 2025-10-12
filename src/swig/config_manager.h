/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SWIG_CONFIG_MANAGER_H_
#define TBOX_SWIG_CONFIG_MANAGER_H_

#include <map>
#include <string>

#include "src/impl/config_manager.h"

namespace tbox {
namespace swig {

using std::map;
using std::string;

class ConfigManager {
 public:
  bool Init(const string& base_config_path);
  string ToString() { return util::ConfigManager::Instance()->ToString(); }
};

}  // namespace swig
}  // namespace tbox

#endif  // TBOX_SWIG_CONFIG_MANAGER_H_
