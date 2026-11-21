// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <folly/io/async/AsyncSignalHandler.h>
#include "fboss/fsdb/common/Types.h"
#include "fboss/fsdb/server/FsdbConfig.h"
#include "fboss/fsdb/server/ServiceHandler.h"
#include "fboss/lib/restart_tracker/RestartTimeTracker.h"

#include <signal.h>

namespace facebook::fboss::fsdb {

class SignalHandler : public folly::AsyncSignalHandler {
 public:
  SignalHandler(folly::EventBase* evb, Callback&& cleanup)
      : AsyncSignalHandler(evb), cleanup_(cleanup) {
    registerSignalHandler(SIGINT);
    registerSignalHandler(SIGTERM);
  }

  void signalReceived(int signum) noexcept override {
    XLOG(INFO) << "Got signal to stop " << signum;
    cleanup_();
  }

 private:
  Callback cleanup_;
};

std::shared_ptr<ServiceHandler> createThriftHandler(
    std::shared_ptr<FsdbConfig> fsdbConfig);

std::shared_ptr<apache::thrift::ThriftServer> createThriftServer(
    std::shared_ptr<FsdbConfig> fsdbConfig,
    std::shared_ptr<ServiceHandler> handler);

void startThriftServer(
    std::shared_ptr<apache::thrift::ThriftServer> server,
    std::shared_ptr<ServiceHandler> handler);

std::shared_ptr<FsdbConfig> parseConfig(int argc, char** argv);

void initFlagDefaults(
    const std::unordered_map<std::string, std::string>& defaults);

void setVersionString();

} // namespace facebook::fboss::fsdb
