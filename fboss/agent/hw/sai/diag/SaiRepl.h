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

#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/diag/Repl.h"

#include <folly/File.h>
#include <folly/logging/xlog.h>

#include <atomic>
#include <thread>
#include <vector>

namespace facebook::fboss {
class SaiSwitch;

class SaiRepl : public Repl {
 public:
  explicit SaiRepl(SwitchSaiId switchId)
      : saiSwitchId_(switchId), exited_(std::atomic<bool>(false)) {}
  ~SaiRepl() noexcept override;
  bool hasExited() const override {
    return exited_.load();
  }
  std::string getPrompt() const override;
  // Get the list of streams the repl expects the shell to dup2 with
  // the pty slave fd passed into the repl constructor
  std::vector<folly::File> getStreams() const override;

 private:
  void doRun() override;
  std::unique_ptr<std::thread> shellThread_;
  const SwitchSaiId saiSwitchId_;
  std::atomic<bool> exited_;
};

} // namespace facebook::fboss
