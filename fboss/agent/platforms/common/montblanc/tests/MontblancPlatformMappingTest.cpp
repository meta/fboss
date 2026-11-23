/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/common/montblanc/MontblancPlatformMapping.h"

#include <set>

#include <folly/String.h>
#include <gtest/gtest.h>

namespace facebook::fboss {
namespace {

// Downlink (host/NIC-facing) transceivers for a San Miguel/VR200 montblanc RSW,
// per the cabling plan in P2466723647
std::set<int> downlinkTransceivers() {
  return {2, 3, 10, 11, 18, 19, 26, 27, 34, 35, 38, 39, 46, 47, 54, 55, 62, 63};
}

int transceiverOf(const std::string& portName) {
  // Port names look like "eth1/<tcvr>/<subport>"; the transceiver is the
  // second '/'-separated field.
  std::vector<std::string_view> parts;
  folly::split('/', portName, parts);
  return folly::to<int>(parts.at(1));
}

// Transceivers that have TX or RX precoding set on any supported profile pin.
std::set<int> precodedTransceivers(const PlatformMapping& mapping) {
  std::set<int> tcvrs;
  for (const auto& [portId, portEntry] : mapping.getPlatformPorts()) {
    for (const auto& [profile, portConfig] : *portEntry.supportedProfiles()) {
      for (const auto& pin : *portConfig.pins()->iphy()) {
        bool precoded =
            (pin.tx().has_value() && pin.tx()->precoding().has_value()) ||
            (pin.rx().has_value() && pin.rx()->precoding().has_value());
        if (precoded) {
          tcvrs.insert(transceiverOf(*portEntry.mapping()->name()));
        }
      }
    }
  }
  return tcvrs;
}

// Transceivers that have RX_REACH (rxReach) set on any supported profile pin.
std::set<int> extendedReachTransceivers(const PlatformMapping& mapping) {
  std::set<int> tcvrs;
  for (const auto& [portId, portEntry] : mapping.getPlatformPorts()) {
    for (const auto& [profile, portConfig] : *portEntry.supportedProfiles()) {
      for (const auto& pin : *portConfig.pins()->iphy()) {
        if (pin.rx().has_value() && pin.rx()->rxReach().has_value()) {
          tcvrs.insert(transceiverOf(*portEntry.mapping()->name()));
        }
      }
    }
  }
  return tcvrs;
}

} // namespace

TEST(MontblancPlatformMappingTest, defaultPrecodesDownlinksOnly) {
  MontblancPlatformMapping mapping;
  EXPECT_FALSE(mapping.getPlatformPorts().empty());
  EXPECT_EQ(precodedTransceivers(mapping), downlinkTransceivers());
  EXPECT_EQ(extendedReachTransceivers(mapping), downlinkTransceivers());
}

TEST(MontblancPlatformMappingTest, mgmtPortBreaksOutInto4x25G) {
  MontblancPlatformMapping mapping;
  const std::vector<PortID> expectedMgmtPorts{
      PortID(351), PortID(352), PortID(353), PortID(354)};
  EXPECT_EQ(
      mapping.getPlatformPorts(cfg::PortType::MANAGEMENT_PORT),
      expectedMgmtPorts);

  const auto& ports = mapping.getPlatformPorts();
  for (const auto& portId : expectedMgmtPorts) {
    const auto& entry = ports.at(portId);
    EXPECT_EQ(*entry.mapping()->controllingPort(), 351);
    for (auto profile :
         {cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_COPPER,
          cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_OPTICAL}) {
      EXPECT_TRUE(entry.supportedProfiles()->contains(profile))
          << "port " << portId << " missing 25G profile";
    }
  }

  // 100G on eth1/65/1 uses all four lanes, so it subsumes the 25G subports
  const std::vector<int32_t> expectedSubsumedPorts{352, 353, 354};
  for (auto profile :
       {cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER,
        cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL}) {
    EXPECT_EQ(
        ports.at(351).supportedProfiles()->at(profile).subsumedPorts().value(),
        expectedSubsumedPorts);
  }
}

} // namespace facebook::fboss
