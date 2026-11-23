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

#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/api/VirtualChannelApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiObjectWithCounters.h"
#include "fboss/agent/types.h"

#include <folly/container/F14Map.h>

#include <memory>
#include <vector>

namespace facebook::fboss {

class Port;
class SaiPlatform;
class SaiStore;

#if defined(SAI_CBFC_SUPPORTED)

// SaiObjectHasStats is true for the traits, so SaiStore hands back the
// counters flavour; naming the base here would drop updateStats/getStats.
using SaiVirtualChannel = SaiObjectWithCounters<SaiVirtualChannelTraits>;
using SaiCbfcCreditProfile = SaiObject<SaiCbfcCreditProfileTraits>;

// Holds a port's CBFC objects alive. Dropping a handle, or overwriting it with
// a smaller set, is what removes virtual channels from hardware.
//
// Member order is load-bearing and must not be tidied: members are destroyed in
// reverse declaration order, so virtualChannels is released before the credit
// profiles it references. brcm-sai refcounts profile references and returns
// OBJECT_IN_USE while any virtual channel still points at one;
// SaiObject::remove rethrows anything but ITEM_NOT_FOUND, and that throw would
// escape ~SaiObject and terminate the process.
struct SaiVirtualChannelHandle {
  std::vector<std::shared_ptr<SaiCbfcCreditProfile>> creditProfiles;
  std::vector<std::shared_ptr<SaiVirtualChannel>> virtualChannels;
};

#endif

class SaiVirtualChannelManager {
 public:
  SaiVirtualChannelManager(SaiStore* saiStore, const SaiPlatform* platform);

  // Brings the port's virtual channels in line with swPort. Handles creation,
  // update and removal, so callers do not need to distinguish them.
  void programVirtualChannels(
      const std::shared_ptr<Port>& swPort,
      PortSaiId portSaiId);

  void removeVirtualChannels(PortID portId);

#if defined(SAI_CBFC_SUPPORTED)
  // Null when the port has no virtual channels. The handle holds the objects
  // the manager created, so this is a create-time view rather than a port
  // getAttribute, which is what hardware tests need to read back against.
  const SaiVirtualChannelHandle* getVirtualChannelHandle(PortID portId) const;
#endif

  // No-op for a port with no virtual channels.
  void updateStats(PortID portId, HwPortStats& hwPortStats);

 private:
#if defined(SAI_CBFC_SUPPORTED)
  std::shared_ptr<SaiCbfcCreditProfile> getOrCreateCreditProfile(
      int64_t reservedCreditSize);

  folly::F14FastMap<PortID, SaiVirtualChannelHandle> handles_;
#endif

  // Only read from the gated implementation above.
  [[maybe_unused]] SaiStore* saiStore_;
  [[maybe_unused]] const SaiPlatform* platform_;
};

} // namespace facebook::fboss
