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

#include <folly/File.h>

#include <string>

namespace facebook::fboss {
class Repl {
 public:
  Repl() = default;
  virtual ~Repl() noexcept = default;
  void run() {
    doRun();
    running_ = true;
  }
  bool running() const {
    return running_;
  }
  // True once the underlying shell loop has exited.
  virtual bool hasExited() const {
    return false;
  }
  virtual std::string getPrompt() const = 0;
  virtual std::vector<folly::File> getStreams() const = 0;

 private:
  virtual void doRun() = 0;
  bool running_{false};
};
} // namespace facebook::fboss
