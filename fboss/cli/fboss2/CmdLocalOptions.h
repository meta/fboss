/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

class CmdLocalOptions {
 public:
  static std::shared_ptr<CmdLocalOptions> getInstance();

  // Exchanges the process-wide embedding override; nullptr selects the
  // singleton. Returns the previous override. Serialize invocations and keep
  // the override installed until their workers finish.
  static std::shared_ptr<CmdLocalOptions> exchangeInvocationOverride(
      std::shared_ptr<CmdLocalOptions> instance);

  std::map<std::string, std::string>& getLocalOptionMap(
      const std::string& fullCmd) {
    if (auto cmdEntry = data_.find(fullCmd); cmdEntry != data_.end()) {
      return cmdEntry->second;
    }
    data_[fullCmd] = std::map<std::string, std::string>();
    return data_[fullCmd];
  }

  std::string getLocalOption(
      const std::string& fullCmd,
      const std::string& localOption) {
    auto& localOptions = getLocalOptionMap(fullCmd);
    if (auto localOptionEntry = localOptions.find(localOption);
        localOptionEntry != localOptions.end()) {
      return localOptionEntry->second;
    }
    return "";
  }

  void setLocalOption(
      const std::string& fullCmd,
      const std::string& localOption,
      const std::string& value) {
    auto& localOptions = getLocalOptionMap(fullCmd);
    localOptions[localOption] = value;
  }

  // Drop every parsed local option. Only meaningful for in-process harnesses
  // that parse more than one command line per process: CLI11 leaves bound
  // map entries untouched when an option isn't passed, so a value from a
  // previous parse would otherwise apply to later commands.
  void clear() {
    data_.clear();
  }

 private:
  std::map<std::string, std::map<std::string, std::string>> data_;
};

} // namespace facebook::fboss
