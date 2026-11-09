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

#include <folly/io/async/EventBase.h>
#include "fboss/agent/types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook {
namespace fboss {

class PhyManager;
class PortManager;
class TransceiverManager;

class StatsPublisher {
 public:
  StatsPublisher(
      TransceiverManager* transceiverManager,
      PhyManager* phyManager,
      PortManager* portManager)
      : transceiverManager_(transceiverManager),
        phyManager_(phyManager),
        portManager_(portManager) {}
  void init();
  void publishStats(folly::EventBase* evb, int32_t stats_publish_interval);
  void publishFbagentCounters(
      const std::map<int32_t, TransceiverInfo>& infoMap,
      const std::map<int32_t, SignalFlags>& signalFlagsMap,
      const std::map<int32_t, std::map<int, MediaLaneSignals>>&
          mediaSignalsMap);
  static void bumpPciLockHeld();
  static void bumpReadFailure();
  static void bumpWriteFailure();
  static void bumpModuleErrors();
  static void bumpFwStorageHandleMissingFromConfig();
  static void missingPorts(TransceiverID module);
  static void bumpAOIOverride();
  static void bumpHighTemp();
  static void bumpHighVcc();
  static void bumpHighTempPort(std::string& portName);
  static void bumpHighVccPort(std::string& portName);
  static void initPerPortFb303Stats(std::set<std::string>& portNames);

 private:
  void publishBootType();

  TransceiverManager* transceiverManager_{nullptr};
  // Owned by either TransceiverManager or, in Port Manager mode, PortManager.
  // Created once during initialization and never replaced, so caching the raw
  // pointer for the lifetime of the process is safe.
  PhyManager* phyManager_{nullptr};
  // Null outside Port Manager mode, where there are no port state machines
  PortManager* portManager_{nullptr};
};
} // namespace fboss
} // namespace facebook
