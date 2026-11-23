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

#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/if/gen-cpp2/TestCtrl.h"

namespace facebook::fboss {

class SwSwitch;

class TestThriftHandler : public ThriftHandler,
                          public apache::thrift::ServiceHandler<TestCtrl> {
 public:
  explicit TestThriftHandler(SwSwitch* sw);
  ~TestThriftHandler() override = default;

  void gracefullyRestartService(
      std::unique_ptr<std::string> serviceName,
      int32_t delayInSeconds) override;
  void ungracefullyRestartService(
      std::unique_ptr<std::string> serviceName) override;
  void gracefullyRestartServiceWithDelay(
      std::unique_ptr<std::string> serviceName,
      int32_t delayInSeconds) override;

  void addNeighbor(
      int32_t interfaceID,
      std::unique_ptr<BinaryAddress> ip,
      std::unique_ptr<std::string> mac,
      int32_t portID) override;

  void setSwitchDrainState(cfg::SwitchDrainState switchDrainState) override;

  void setSelfHealingLagState(int32_t portId, bool enable) override;

  void setConditionalEntropyRehash(int32_t portId, bool enable) override;

 private:
  // Forbidden copy constructor and assignment operator
  TestThriftHandler(TestThriftHandler const&) = delete;
  TestThriftHandler& operator=(TestThriftHandler const&) = delete;
};

} // namespace facebook::fboss
