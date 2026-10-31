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

#include <folly/String.h>

#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

class CmdArgsLists {
 public:
  static std::shared_ptr<CmdArgsLists> getInstance();

  const std::vector<std::string>& at(int i) const {
    return data_[i];
  }

  std::vector<std::string>& refAt(int i) {
    if (i >= MAX_DEPTH) {
      throw std::runtime_error(
          "There is a nested command deeper than max depth, need to increase the depth");
    }
    return data_[i];
  }

  // Reset all depth levels to empty. Needed when the CLI is invoked multiple
  // times in the same process (e.g. from the integration test harness), since
  // CLI11's vector-backed add_option() appends to the vector rather than
  // replacing it, and this singleton would otherwise accumulate stale args
  // from previous invocations into subsequent parses.
  void clear() {
    for (auto& level : data_) {
      level.clear();
    }
  }

  template <typename UnfilteredTypes, typename Types>
  Types getTypedArgs() {
    const auto& unfiltered =
        utils::arrayToTuple<std::tuple_size_v<UnfilteredTypes>>(data_);
    const auto& untyped =
        utils::filterTupleMonostates<UnfilteredTypes>(unfiltered);
    // We're getting implicit conversion from std::vector<std::string> to the
    // corresponding type eg: std::tuple<std::vector<std::string>,
    // std::vector<std::string>> -> std::tuple<utils::PortList,
    // utils::PortState>
    return untyped;
  }

  std::string getArgStr() {
    std::vector<std::string> args;
    for (const auto& level : data_) {
      if (!level.empty()) {
        auto arg =
            folly::join<std::string, std::vector<std::string>>(",", level);
        args.emplace_back(arg);
      }
    }
    return folly::join<std::string, std::vector<std::string>>("|", args);
  }

 private:
  // since we do a lot of work of figuring out argument types at compile time,
  // for now we need to know the the theoretical max depth of nested subcommands
  static constexpr auto MAX_DEPTH = 6;

  std::array<std::vector<std::string>, MAX_DEPTH> data_;
};

} // namespace facebook::fboss
