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

#include "fboss/agent/hw/sai/diag/Repl.h"

#include <folly/File.h>
#include <folly/logging/xlog.h>

#include <thread>
#include <vector>

namespace facebook::fboss {

class PythonRepl : public Repl {
 public:
  explicit PythonRepl(int fd);
  ~PythonRepl() noexcept override;
  std::string getPrompt() const override;
  // Get the list of streams the repl expects the shell to dup2 with
  // the pty slave fd passed into the repl constructor
  std::vector<folly::File> getStreams() const override;

 private:
  std::string warnForTestEnvironment() const;
  void doRun() override;
  void runPythonInterpreter();
  std::unique_ptr<std::thread> pyThread_;
  int fd_;
};

} // namespace facebook::fboss
