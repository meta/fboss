/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/utils/ConfigUtils.h"

#include <gtest/gtest.h>

namespace facebook::fboss::utility {
namespace {

// NetLake 1.0 (Intel) puts the two Tomahawk6 ASICs here, and these are the
// addresses configerator carries.
constexpr auto kNl1Switch0 = "0000:15:00";
constexpr auto kNl1Switch1 = "0000:18:00";
// NetLake 2.0 (AMD) numbers from bus 1, so the same ASICs land here.
constexpr auto kNl2Switch0 = "0000:03:00";
constexpr auto kNl2Switch1 = "0000:04:00";

std::map<SwitchID, cfg::SwitchInfo> makeSwitchInfo(
    const std::vector<std::string>& connectionHandles) {
  std::map<SwitchID, cfg::SwitchInfo> switchIdToSwitchInfo;
  for (auto i = 0; i < connectionHandles.size(); i++) {
    cfg::SwitchInfo switchInfo;
    switchInfo.switchType() = cfg::SwitchType::NPU;
    switchInfo.connectionHandle() = connectionHandles[i];
    switchIdToSwitchInfo.emplace(SwitchID(i), switchInfo);
  }
  return switchIdToSwitchInfo;
}

std::vector<std::string> connectionHandles(
    const std::map<SwitchID, cfg::SwitchInfo>& switchIdToSwitchInfo) {
  std::vector<std::string> handles;
  for (const auto& [_, switchInfo] : switchIdToSwitchInfo) {
    handles.push_back(switchInfo.connectionHandle().value_or("<unset>"));
  }
  return handles;
}

} // namespace

// The box is the variant the config was written for, so nothing changes.
TEST(ApplyTomahawk6PciAddrs, ConfigMatchesBusIsNoOp) {
  auto switchIdToSwitchInfo = makeSwitchInfo({"0000:15:00=0", "0000:18:00=0"});
  applyTomahawk6PciAddrs({kNl1Switch0, kNl1Switch1}, switchIdToSwitchInfo);

  const std::vector<std::string> expected{"0000:15:00=0", "0000:18:00=0"};
  EXPECT_EQ(connectionHandles(switchIdToSwitchInfo), expected);
}

// The configured addresses are not on this box, so take what is.
TEST(ApplyTomahawk6PciAddrs, ConfigAbsentFromBusIsRewritten) {
  auto switchIdToSwitchInfo = makeSwitchInfo({"0000:15:00=0", "0000:18:00=0"});
  applyTomahawk6PciAddrs({kNl2Switch0, kNl2Switch1}, switchIdToSwitchInfo);

  const std::vector<std::string> expected{"0000:03:00=0", "0000:04:00=0"};
  EXPECT_EQ(connectionHandles(switchIdToSwitchInfo), expected);
}

// A config that pairs switch ids with addresses in an order other than
// ascending BDF is left alone, rather than silently reordered.
TEST(ApplyTomahawk6PciAddrs, DescendingConfigOrderIsPreserved) {
  auto switchIdToSwitchInfo = makeSwitchInfo({"0000:18:00=0", "0000:15:00=0"});
  applyTomahawk6PciAddrs({kNl1Switch0, kNl1Switch1}, switchIdToSwitchInfo);

  const std::vector<std::string> expected{"0000:18:00=0", "0000:15:00=0"};
  EXPECT_EQ(connectionHandles(switchIdToSwitchInfo), expected);
}

// Nothing on the bus means the ASICs are absent, not that the config is fine.
TEST(ApplyTomahawk6PciAddrs, NoDeviceFoundThrows) {
  auto switchIdToSwitchInfo = makeSwitchInfo({"0000:15:00=0", "0000:18:00=0"});
  EXPECT_THROW(applyTomahawk6PciAddrs({}, switchIdToSwitchInfo), FbossError);
}

// A count that disagrees with the config leaves the pairing undefined.
TEST(ApplyTomahawk6PciAddrs, CountMismatchThrows) {
  auto switchIdToSwitchInfo = makeSwitchInfo({"0000:15:00=0", "0000:18:00=0"});
  EXPECT_THROW(
      applyTomahawk6PciAddrs({kNl2Switch0}, switchIdToSwitchInfo), FbossError);
}

} // namespace facebook::fboss::utility
