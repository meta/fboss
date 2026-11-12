/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/wedge/tests/PlatformMappingTest.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/common/elbert/Elbert16QPimPlatformMapping.h"
#include "fboss/agent/platforms/common/fake_test/FakeTestPlatformMapping.h"
#include "fboss/agent/platforms/common/fuji/Fuji16QPimPlatformMapping.h"
#include "fboss/agent/platforms/common/galaxy/GalaxyFCPlatformMapping.h"
#include "fboss/agent/platforms/common/galaxy/GalaxyLCPlatformMapping.h"
#include "fboss/agent/platforms/common/meru800bfa/Meru800bfaPlatformMapping.h"
#include "fboss/agent/platforms/common/minipack/Minipack16QPimPlatformMapping.h"
#include "fboss/agent/platforms/common/wedge100/Wedge100PlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400/Wedge400PlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400c/Wedge400CPlatformMapping.h"
#include "fboss/agent/platforms/common/yamp/Yamp16QPimPlatformMapping.h"
#include "fboss/agent/platforms/common/yamp/YampPlatformMapping.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"
#include "fboss/lib/platforms/PlatformMode.h"

#include <gtest/gtest.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <optional>

namespace facebook::fboss::test {

cfg::PlatformPortProfileConfigEntry createPlatformPortProfileConfigEntry(
    cfg::PortProfileID profileID,
    std::set<int> pimIDs,
    cfg::PortSpeed speed,
    phy::FecMode iphyFec) {
  cfg::PlatformPortConfigFactor factor;
  factor.profileID() = profileID;
  factor.pimIDs() = pimIDs;

  phy::PortProfileConfig config;
  phy::ProfileSideConfig iphy;
  iphy.fec() = iphyFec;
  config.iphy() = iphy;
  config.speed() = speed;

  cfg::PlatformPortProfileConfigEntry configEntry;
  configEntry.factor() = factor;
  configEntry.profile() = config;
  return configEntry;
}

TEST_F(PlatformMappingTest, VerifyWedge400PlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER,
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_COPPER};

  // Wedge400 has 16 uplinks + 4 * 32 downlink ports = 144
  // 32 TH3 Blackhawk cores + 48 transceivers
  setExpectation(144, 32, 0, 48, expectedProfiles);

  auto mapping = std::make_unique<Wedge400PlatformMapping>();
  verify(mapping.get());
}

TEST_F(PlatformMappingTest, VerifyWedge400CPlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_COPPER,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER,
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL};

  // Wedge400 has 16 uplinks + 4 * 32 downlink ports = 144
  // 12 IFG + 48 transceivers
  setExpectation(144, 12, 0, 48, expectedProfiles);

  auto mapping = std::make_unique<Wedge400CPlatformMapping>();
  verify(mapping.get());
}

TEST(PlatformDescriptorMappingTest, DefaultRegistryHasNoWedge800BACTMapping) {
  auto externalMappingStr =
      PlatformDescriptorRegistry::get().loadPlatformMapping(
          PlatformType::PLATFORM_WEDGE800BACT);
  EXPECT_FALSE(externalMappingStr.has_value());
}

TEST_F(PlatformMappingTest, VerifyWedge400PortIphyPinConfigs) {
  // All the downlink ports should be using the same tx_settings: nrz:-8:89:0
  auto mapping = std::make_unique<Wedge400PlatformMapping>();
  for (auto port : mapping->getPlatformPorts()) {
    const auto& profiles = *port.second.supportedProfiles();

    // Only downlink ports has 10G profile
    if (profiles.find(cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER) ==
        profiles.end()) {
      continue;
    }
    for (const auto& profile : profiles) {
      auto pinCfgs = mapping->getPortIphyPinConfigs(
          PlatformPortProfileConfigMatcher(profile.first, PortID(port.first)));
      EXPECT_TRUE(!pinCfgs.empty());

      auto itProfileCfg = mapping->getPortProfileConfig(
          PlatformPortProfileConfigMatcher(PlatformPortProfileConfigMatcher(
              profile.first, PortID(port.first))));
      EXPECT_TRUE(itProfileCfg.has_value());

      for (auto pinCfg : pinCfgs) {
        auto tx = pinCfg.tx();
        if (*itProfileCfg->iphy()->modulation() == phy::IpModulation::NRZ) {
          EXPECT_TRUE(tx.has_value());
          if (profile.first ==
              cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL) {
            verifyTxSettings(*tx, {0, -6, 69, 0, 0, 0});
          } else if (
              profile.first !=
              cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL) {
            verifyTxSettings(*tx, {0, -8, 89, 0, 0, 0});
          }
        } else if (
            *itProfileCfg->iphy()->modulation() == phy::IpModulation::PAM4) {
          if (profile.first ==
                  cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER ||
              profile.first ==
                  cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER ||
              profile.first ==
                  cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_COPPER) {
            EXPECT_TRUE(tx.has_value());
            verifyTxSettings(*tx, {4, -20, 140, 0, 0, 0});
          } else {
            EXPECT_FALSE(tx.has_value());
          }
        } else {
          EXPECT_FALSE(tx.has_value());
        }
      }
    }
  }

  // Not override case will read directly from PlatformPortConfig
  std::unordered_map<PortID, std::array<int, 6>> uplinkTxMapForProfile23 = {
      {PortID(36), {0, -6, 92, -24, 0, 0}},
      {PortID(37), {0, -2, 90, -18, 0, 0}},
      {PortID(56), {0, -2, 90, -18, 0, 0}},
      {PortID(57), {0, -2, 90, -18, 0, 0}},
      {PortID(17), {0, -6, 92, -24, 0, 0}},
      {PortID(18), {0, -2, 90, -22, 0, 0}},
      {PortID(76), {0, -2, 78, -10, 0, 0}},
      {PortID(77), {0, -2, 66, -8, 0, 0}},
      {PortID(96), {0, -2, 66, -8, 0, 0}},
      {PortID(97), {0, -2, 78, -10, 0, 0}},
      {PortID(156), {0, -2, 88, -20, 0, 0}},
      {PortID(157), {0, -6, 92, -24, 0, 0}},
      {PortID(116), {0, -2, 90, -18, 0, 0}},
      {PortID(117), {0, -2, 90, -18, 0, 0}},
      {PortID(136), {0, -2, 88, -20, 0, 0}},
      {PortID(137), {0, -6, 92, -24, 0, 0}},
  };

  for (const auto& uplinkTx : uplinkTxMapForProfile23) {
    // this is profile 23
    const auto& pinCfgs =
        mapping->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
            uplinkTx.first));
    EXPECT_EQ(pinCfgs.size(), 4);
    for (auto pinCfg : pinCfgs) {
      auto tx = pinCfg.tx();
      EXPECT_TRUE(tx.has_value());
      verifyTxSettings(*tx, uplinkTx.second);
    }
  }

  std::unordered_map<PortID, std::array<int, 6>> downlinkTxMapForProfile23 = {
      {PortID(20), {0, -2, 94, -24, 0, 0}},
      {PortID(24), {0, -4, 89, -24, 0, 0}},

      {PortID(28), {0, -2, 94, -22, 0, 0}},
      {PortID(32), {0, -2, 94, -22, 0, 0}},
      {PortID(40), {0, -2, 94, -22, 0, 0}},

      {PortID(44), {0, -2, 88, -20, 0, 0}},
      {PortID(48), {0, -2, 88, -20, 0, 0}},
      {PortID(52), {0, -2, 88, -18, 0, 0}},
      {PortID(1), {0, -2, 94, -22, 0, 0}},
      {PortID(5), {0, -4, 89, -24, 0, 0}},
      {PortID(9), {0, -2, 92, -20, 0, 0}},
      {PortID(13), {0, -2, 92, -22, 0, 0}},
      {PortID(60), {0, -2, 66, -8, 0, 0}},
      {PortID(64), {0, 0, 88, -16, 0, 0}},

      {PortID(68), {0, -2, 66, -8, 0, 0}},
      {PortID(72), {0, -2, 66, -8, 0, 0}},
      {PortID(80), {0, -2, 66, -8, 0, 0}},
      {PortID(84), {0, -2, 66, -8, 0, 0}},
      {PortID(88), {0, -2, 66, -10, 0, 0}},

      {PortID(92), {0, 0, 88, -16, 0, 0}},
      {PortID(140), {0, -2, 92, -20, 0, 0}},
      {PortID(144), {0, -2, 90, -20, 0, 0}},
      {PortID(148), {0, -2, 94, -22, 0, 0}},
      {PortID(152), {0, -6, 94, -24, 0, 0}},
      {PortID(100), {0, -2, 92, -22, 0, 0}},
      {PortID(104), {0, -2, 92, -22, 0, 0}},
      {PortID(108), {0, -2, 90, -20, 0, 0}},
      {PortID(112), {0, -2, 90, -20, 0, 0}},

      {PortID(120), {0, -6, 94, -24, 0, 0}},
      {PortID(124), {0, -6, 94, -24, 0, 0}},

      {PortID(128), {0, -6, 94, -22, 0, 0}},
      {PortID(132), {0, -6, 94, -22, 0, 0}},
  };

  for (const auto& uplinkTx : downlinkTxMapForProfile23) {
    // this is profile 23
    const auto& pinCfgs =
        mapping->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
            uplinkTx.first));

    EXPECT_EQ(pinCfgs.size(), 4);
    for (auto pinCfg : pinCfgs) {
      auto tx = pinCfg.tx();
      EXPECT_TRUE(tx.has_value());
      verifyTxSettings(*tx, uplinkTx.second);
    }
  }

  // profile25 has different tx settings per lane
  //  {portID: { laneId1: {txSettings1}, laneId2: {txSettings2} .... }}
  std::unordered_map<PortID, std::unordered_map<int, std::array<int, 6>>>
      uplinkTxMapForProfile25 = {
          {
              PortID(17),
              {
                  {0, {0, -12, 140, -16, 0, 0}},
                  {1, {0, -12, 140, -16, 0, 0}},
                  {2, {0, -12, 140, -16, 0, 0}},
                  {3, {0, -12, 140, -16, 0, 0}},
              },
          },
          {
              PortID(18),
              {
                  {0, {0, -12, 140, -12, 0, 0}},
                  {1, {0, -12, 140, -16, 0, 0}},
                  {2, {0, -12, 140, -16, 0, 0}},
                  {3, {0, -12, 140, -12, 0, 0}},
              },
          },
          {
              PortID(36),
              {
                  {0, {0, -12, 136, -16, 0, 0}},
                  {1, {0, -12, 136, -16, 0, 0}},
                  {2, {0, -12, 136, -16, 0, 0}},
                  {3, {0, -12, 132, -16, 0, 0}},
              },
          },
          {
              PortID(37),
              {
                  {0, {0, -16, 140, -12, 0, 0}},
                  {1, {0, -16, 140, -12, 0, 0}},
                  {2, {0, -16, 140, -12, 0, 0}},
                  {3, {0, -16, 136, -12, 0, 0}},
              },
          },
          {
              PortID(56),
              {
                  {0, {0, -8, 136, -16, 0, 0}},
                  {1, {0, -12, 140, -16, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -16, 140, -12, 0, 0}},
              },
          },
          {
              PortID(57),
              {
                  {0, {0, -12, 140, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
              },
          },
          {
              PortID(76),
              {
                  {0, {0, -12, 136, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
              },
          },
          {
              PortID(77),
              {
                  {0, {0, -8, 132, -8, 0, 0}},
                  {1, {0, -8, 136, -8, 0, 0}},
                  {2, {0, -8, 128, -4, 0, 0}},
                  {3, {0, -8, 136, -8, 0, 0}},
              },
          },
          {
              PortID(96),
              {
                  {0, {0, -8, 136, -8, 0, 0}},
                  {1, {0, -8, 136, -8, 0, 0}},
                  {2, {0, -8, 136, -8, 0, 0}},
                  {3, {0, -8, 136, -8, 0, 0}},
              },
          },
          {
              PortID(97),
              {
                  {0, {0, -8, 136, -8, 0, 0}},
                  {1, {0, -8, 136, -8, 0, 0}},
                  {2, {0, -8, 136, -8, 0, 0}},
                  {3, {0, -8, 136, -8, 0, 0}},
              },
          },
          {
              PortID(116),
              {
                  {0, {0, -12, 140, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 140, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
              },
          },
          {
              PortID(117),
              {
                  {0, {0, -12, 132, -12, 0, 0}},
                  {1, {0, -12, 132, -12, 0, 0}},
                  {2, {0, -12, 132, -12, 0, 0}},
                  {3, {0, -12, 132, -12, 0, 0}},
              },
          },
          {
              PortID(136),
              {
                  {0, {0, -12, 132, -12, 0, 0}},
                  {1, {0, -12, 132, -16, 0, 0}},
                  {2, {0, -12, 132, -12, 0, 0}},
                  {3, {0, -12, 132, -12, 0, 0}},
              },
          },
          {
              PortID(137),
              {
                  {0, {0, -16, 136, -12, 0, 0}},
                  {1, {0, -8, 128, -16, 0, 0}},
                  {2, {0, -12, 132, -16, 0, 0}},
                  {3, {0, -12, 140, -16, 0, 0}},
              },
          },
          {
              PortID(156),
              {
                  {0, {0, -12, 136, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
              },
          },
          {
              PortID(157),
              {
                  {0, {0, -16, 136, -12, 0, 0}},
                  {1, {0, -16, 136, -12, 0, 0}},
                  {2, {0, -16, 136, -12, 0, 0}},
                  {3, {0, -16, 136, -12, 0, 0}},
              },
          },
      };

  for (const auto& laneToUplinkTxSettingsToPortMap : uplinkTxMapForProfile25) {
    // this is profile 25
    const auto& pinCfgs =
        mapping->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
            laneToUplinkTxSettingsToPortMap.first));
    EXPECT_EQ(pinCfgs.size(), 4);
    for (auto pinCfg : pinCfgs) {
      auto pinId = *pinCfg.id();
      auto laneId = *pinId.lane();
      auto tx = pinCfg.tx();
      EXPECT_TRUE(tx.has_value());
      auto laneToUplinkTxSettinsMap = laneToUplinkTxSettingsToPortMap.second;
      auto uplinkTxSettings = laneToUplinkTxSettinsMap[laneId];
      verifyTxSettings(*tx, uplinkTxSettings);
    }
  }

  // profile26 has different tx settings per lane
  //  {portID: { laneId1: {txSettings1}, laneId2: {txSettings2} .... }}
  std::unordered_map<PortID, std::unordered_map<int, std::array<int, 6>>>
      uplinkTxMapForProfile26 = {
          {
              // cd4
              PortID(17),
              {
                  {0, {0, -12, 140, -16, 0, 0}},
                  {1, {0, -12, 140, -16, 0, 0}},
                  {2, {0, -12, 140, -16, 0, 0}},
                  {3, {0, -12, 140, -16, 0, 0}},
                  {4, {0, -12, 140, -16, 0, 0}},
                  {5, {0, -12, 140, -16, 0, 0}},
                  {6, {0, -12, 140, -16, 0, 0}},
                  {7, {0, -12, 140, -16, 0, 0}},
              },
          },
          {
              // cd5
              PortID(18),
              {
                  {0, {0, -12, 140, -12, 0, 0}},
                  {1, {0, -12, 140, -16, 0, 0}},
                  {2, {0, -12, 140, -16, 0, 0}},
                  {3, {0, -12, 140, -12, 0, 0}},
                  {4, {0, -12, 140, -12, 0, 0}},
                  {5, {0, -12, 140, -12, 0, 0}},
                  {6, {0, -12, 136, -16, 0, 0}},
                  {7, {0, -12, 140, -16, 0, 0}},
              },
          },
          {
              // cd0
              PortID(36),
              {
                  {0, {0, -12, 136, -16, 0, 0}},
                  {1, {0, -12, 136, -16, 0, 0}},
                  {2, {0, -12, 136, -16, 0, 0}},
                  {3, {0, -12, 132, -16, 0, 0}},
                  {4, {0, -12, 132, -16, 0, 0}},
                  {5, {0, -12, 140, -16, 0, 0}},
                  {6, {0, -12, 140, -16, 0, 0}},
                  {7, {0, -12, 140, -16, 0, 0}},
              },
          },
          {
              // cd1
              PortID(37),
              {
                  {0, {0, -16, 140, -12, 0, 0}},
                  {1, {0, -16, 140, -12, 0, 0}},
                  {2, {0, -16, 140, -12, 0, 0}},
                  {3, {0, -16, 136, -12, 0, 0}},
                  {4, {0, -12, 136, -16, 0, 0}},
                  {5, {0, -12, 136, -16, 0, 0}},
                  {6, {0, -12, 136, -16, 0, 0}},
                  {7, {0, -12, 136, -16, 0, 0}},
              },
          },
          {
              // cd2
              PortID(56),
              {
                  {0, {0, -8, 136, -16, 0, 0}},
                  {1, {0, -12, 140, -16, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -16, 140, -12, 0, 0}},
                  {4, {0, -12, 140, -12, 0, 0}},
                  {5, {0, -12, 140, -12, 0, 0}},
                  {6, {0, -12, 140, -12, 0, 0}},
                  {7, {0, -12, 140, -12, 0, 0}},
              },
          },
          {
              // cd3
              PortID(57),
              {
                  {0, {0, -12, 140, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
                  {4, {0, -12, 136, -12, 0, 0}},
                  {5, {0, -12, 136, -16, 0, 0}},
                  {6, {0, -12, 136, -16, 0, 0}},
                  {7, {0, -12, 136, -16, 0, 0}},
              },
          },
          {
              // cd6
              PortID(76),
              {
                  {0, {0, -12, 136, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
                  {4, {0, -12, 136, -12, 0, 0}},
                  {5, {0, -12, 136, -12, 0, 0}},
                  {6, {0, -12, 136, -12, 0, 0}},
                  {7, {0, -12, 136, -12, 0, 0}},
              },
          },
          {
              // cd7
              PortID(77),
              {
                  {0, {0, -8, 132, -8, 0, 0}},
                  {1, {0, -8, 136, -8, 0, 0}},
                  {2, {0, -8, 128, -4, 0, 0}},
                  {3, {0, -8, 136, -8, 0, 0}},
                  {4, {0, -8, 136, -8, 0, 0}},
                  {5, {0, -8, 136, -8, 0, 0}},
                  {6, {0, -8, 136, -8, 0, 0}},
                  {7, {0, -8, 136, -8, 0, 0}},
              },
          },
          {
              // cd8
              PortID(96),
              {
                  {0, {0, -8, 136, -8, 0, 0}},
                  {1, {0, -8, 136, -8, 0, 0}},
                  {2, {0, -8, 136, -8, 0, 0}},
                  {3, {0, -8, 136, -8, 0, 0}},
                  {4, {0, -12, 132, -8, 0, 0}},
                  {5, {0, -12, 132, -8, 0, 0}},
                  {6, {0, -12, 132, -8, 0, 0}},
                  {7, {0, -12, 132, -8, 0, 0}},
              },
          },
          {
              // cd9
              PortID(97),
              {
                  {0, {0, -8, 136, -8, 0, 0}},
                  {1, {0, -8, 136, -8, 0, 0}},
                  {2, {0, -8, 136, -8, 0, 0}},
                  {3, {0, -8, 136, -8, 0, 0}},
                  {4, {0, -8, 136, -8, 0, 0}},
                  {5, {0, -8, 136, -8, 0, 0}},
                  {6, {0, -8, 136, -8, 0, 0}},
                  {7, {0, -8, 136, -8, 0, 0}},
              },
          },
          {
              // cd12
              PortID(116),
              {
                  {0, {0, -12, 140, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 140, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
                  {4, {0, -8, 140, -16, 0, 0}},
                  {5, {0, -8, 140, -16, 0, 0}},
                  {6, {0, -8, 140, -12, 0, 0}},
                  {7, {0, -8, 140, -12, 0, 0}},
              },
          },
          {
              // cd 13
              PortID(117),
              {
                  {0, {0, -12, 132, -12, 0, 0}},
                  {1, {0, -12, 132, -12, 0, 0}},
                  {2, {0, -12, 132, -12, 0, 0}},
                  {3, {0, -12, 132, -12, 0, 0}},
                  {4, {0, -12, 132, -12, 0, 0}},
                  {5, {0, -12, 132, -12, 0, 0}},
                  {6, {0, -12, 132, -12, 0, 0}},
                  {7, {0, -12, 132, -12, 0, 0}},
              },
          },
          {
              // cd14
              PortID(136),
              {
                  {0, {0, -12, 132, -12, 0, 0}},
                  {1, {0, -12, 132, -16, 0, 0}},
                  {2, {0, -12, 132, -12, 0, 0}},
                  {3, {0, -12, 132, -16, 0, 0}},
                  {4, {0, -12, 132, -12, 0, 0}},
                  {5, {0, -12, 132, -12, 0, 0}},
                  {6, {0, -12, 132, -12, 0, 0}},
                  {7, {0, -12, 140, -12, 0, 0}},
              },
          },
          {
              // cd15
              PortID(137),
              {
                  {0, {0, -16, 136, -12, 0, 0}},
                  {1, {0, -8, 128, -16, 0, 0}},
                  {2, {0, -12, 132, -16, 0, 0}},
                  {3, {0, -12, 140, -16, 0, 0}},
                  {4, {0, -12, 136, -16, 0, 0}},
                  {5, {0, -12, 132, -16, 0, 0}},
                  {6, {0, -12, 140, -16, 0, 0}},
                  {7, {0, -16, 132, -16, 0, 0}},
              },
          },
          {
              // cd10
              PortID(156),
              {
                  {0, {0, -12, 136, -12, 0, 0}},
                  {1, {0, -12, 136, -12, 0, 0}},
                  {2, {0, -12, 136, -12, 0, 0}},
                  {3, {0, -12, 136, -12, 0, 0}},
                  {4, {0, -8, 136, -16, 0, 0}},
                  {5, {0, -8, 136, -16, 0, 0}},
                  {6, {0, -12, 140, -16, 0, 0}},
                  {7, {0, -8, 140, -16, 0, 0}},
              },
          },
          {
              // cd11
              PortID(157),
              {
                  {0, {0, -16, 136, -12, 0, 0}},
                  {1, {0, -16, 136, -12, 0, 0}},
                  {2, {0, -16, 136, -12, 0, 0}},
                  {3, {0, -16, 136, -12, 0, 0}},
                  {4, {0, -12, 140, -16, 0, 0}},
                  {5, {0, -12, 140, -16, 0, 0}},
                  {6, {0, -12, 140, -16, 0, 0}},
                  {7, {0, -12, 140, -16, 0, 0}},
              },
          },
      };

  for (const auto& laneToUplinkTxSettingsToPortMap : uplinkTxMapForProfile26) {
    const auto& pinCfgs =
        mapping->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL,
            laneToUplinkTxSettingsToPortMap.first));
    EXPECT_EQ(pinCfgs.size(), 8);
    for (auto pinCfg : pinCfgs) {
      auto pinId = *pinCfg.id();
      auto laneId = *pinId.lane();
      auto tx = pinCfg.tx();
      EXPECT_TRUE(tx.has_value());
      auto laneToUplinkTxSettinsMap = laneToUplinkTxSettingsToPortMap.second;
      auto uplinkTxSettings = laneToUplinkTxSettinsMap[laneId];
      verifyTxSettings(*tx, uplinkTxSettings);
    }
  }
}

TEST_F(PlatformMappingTest, VerifyMinipack2PortPhyPinConfigs) {
  auto mapping = std::make_unique<Fuji16QPimPlatformMapping>();
  // 400G optics profile has different tx settings per lane
  //  {portID: { laneId1: {txSettings1}, laneId2: {txSettings2} .... }}
  std::array<std::array<int, 6>, 8> txSettingsFor400gOpticsProfile = {{
      {0, -8, 112, -4, 0, 0},
      {0, -8, 112, -4, 0, 0},
      {0, -8, 112, -4, 0, 0},
      {0, -8, 112, -4, 0, 0},
      {0, -12, 132, -12, 0, 0},
      {0, -12, 132, -12, 0, 0},
      {0, -16, 136, -8, 0, 0},
      {0, -16, 136, -8, 0, 0},
  }};

  // all controlling ports
  std::set<int32_t> portsSupporting400g = {
      3,   17,  34,  51,  68,  85,  102, 119, 87,  70,  121, 104, 19,
      1,   53,  36,  21,  5,   55,  38,  89,  72,  125, 106, 74,  91,
      108, 123, 7,   23,  40,  57,  187, 170, 153, 138, 255, 238, 221,
      204, 240, 257, 206, 223, 172, 189, 136, 155, 174, 191, 140, 157,
      242, 261, 208, 225, 259, 244, 227, 210, 193, 176, 159, 142,
  };
  EXPECT_EQ(portsSupporting400g.size(), 64);

  for (const auto portID : portsSupporting400g) {
    // sanity check that we are checking the controlling ports
    auto port = mapping->getPlatformPorts().find(portID);
    CHECK(port != mapping->getPlatformPorts().end());
    EXPECT_EQ(*port->second.mapping()->controllingPort(), portID);
    const auto& pinCfgs = mapping->getPortXphySidePinConfigs(
        PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL,
            PortID(portID)),
        phy::Side::LINE);

    EXPECT_EQ(pinCfgs.size(), 8);
    for (auto pinCfg : pinCfgs) {
      auto pinId = *pinCfg.id();
      auto laneId = *pinId.lane();
      auto tx = pinCfg.tx();

      EXPECT_TRUE(tx.has_value());
      verifyTxSettings(*tx, txSettingsFor400gOpticsProfile[laneId % 8]);
    }

    const auto& xphySysPinCfgs = mapping->getPortXphySidePinConfigs(
        PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL,
            PortID(portID)),
        phy::Side::SYSTEM);

    EXPECT_EQ(xphySysPinCfgs.size(), 8);
    for (auto pinCfg : xphySysPinCfgs) {
      auto tx = pinCfg.tx();
      EXPECT_TRUE(tx.has_value());
      verifyTxSettings(*tx, {0, -16, 152, 0, 0, 0});
    }

    auto iphyPinCfgs =
        mapping->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL,
            PortID(portID)));
    EXPECT_EQ(iphyPinCfgs.size(), 8);
    for (auto pinCfg : iphyPinCfgs) {
      auto tx = pinCfg.tx();
      EXPECT_TRUE(tx.has_value());
      verifyTxSettings(*tx, {0, -16, 132, 0, 0, 0});
    }
  }
}

TEST_F(PlatformMappingTest, VerifyYampPortProfileConfigOverride) {
  auto mapping = std::make_unique<YampPlatformMapping>("");
  cfg::PlatformPortConfigOverrideFactor factor;
  for (const auto& port : mapping->getPlatformPorts()) {
    factor.mediaInterfaceCode() = MediaInterfaceCode::CWDM4_100G;
    auto portProfileConfig =
        mapping->getPortProfileConfig(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC,
            PortID(port.first),
            factor));
    EXPECT_TRUE(*portProfileConfig->xphyLine()->fec() == phy::FecMode::NONE);

    factor.mediaInterfaceCode() = MediaInterfaceCode::CWDM4_100G;
    portProfileConfig =
        mapping->getPortProfileConfig(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528,
            PortID(port.first),
            factor));
    EXPECT_TRUE(*portProfileConfig->xphyLine()->fec() == phy::FecMode::RS528);

    factor.mediaInterfaceCode() = MediaInterfaceCode::FR1_100G;
    portProfileConfig =
        mapping->getPortProfileConfig(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528,
            PortID(port.first),
            factor));
    EXPECT_TRUE(*portProfileConfig->xphyLine()->fec() == phy::FecMode::NONE);

    factor.mediaInterfaceCode() = MediaInterfaceCode::FR4_200G;
    portProfileConfig =
        mapping->getPortProfileConfig(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N,
            PortID(port.first),
            factor));
    EXPECT_TRUE(*portProfileConfig->xphyLine()->fec() == phy::FecMode::NONE);
  }
}

TEST_F(PlatformMappingTest, VerifyYampPortXphyLinePinConfigOverride) {
  auto mapping = std::make_unique<YampPlatformMapping>("");
  cfg::PlatformPortConfigOverrideFactor factor;

  std::array<int, 6> YampPort100GSffXphyLinePinConfig = {0, -4, 23, -12, 0, 0};
  std::array<int, 6> YampPort100GCmisXphyLinePinConfig = {0, -2, 15, -7, 0, 0};

  for (const auto& port : mapping->getPlatformPorts()) {
    factor.transceiverManagementInterface() =
        TransceiverManagementInterface::SFF;
    auto portXphyLinePinConfigs = mapping->getPortXphySidePinConfigs(
        PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528,
            PortID(port.first),
            factor),
        phy::Side::LINE);

    EXPECT_EQ(portXphyLinePinConfigs.size(), 4);
    for (auto portXphyLinePinConfig : portXphyLinePinConfigs) {
      auto pinId = *portXphyLinePinConfig.id();
      if (auto tx = portXphyLinePinConfig.tx()) {
        verifyTxSettings(*tx, YampPort100GSffXphyLinePinConfig);
      }
    }

    factor.transceiverManagementInterface() =
        TransceiverManagementInterface::CMIS;
    portXphyLinePinConfigs = mapping->getPortXphySidePinConfigs(
        PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528,
            PortID(port.first),
            factor),
        phy::Side::LINE);

    EXPECT_EQ(portXphyLinePinConfigs.size(), 4);
    for (auto portXphyLinePinConfig : portXphyLinePinConfigs) {
      auto pinId = *portXphyLinePinConfig.id();
      if (auto tx = portXphyLinePinConfig.tx()) {
        verifyTxSettings(*tx, YampPort100GCmisXphyLinePinConfig);
      }
    }
  }
}

TEST_F(PlatformMappingTest, VerifyYampPlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528,
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N};

  // Yamp has 128 ports
  // 32 TH3 Blackhawk cores + 128 transceivers + 64 xphy
  setExpectation(128, 32, 64, 128, expectedProfiles);

  auto mapping = std::make_unique<YampPlatformMapping>("");
  verify(mapping.get());
}

TEST_F(PlatformMappingTest, VerifyMinipackMiln4_2_16QPlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528};

  // Minipack16Q has 128 ports
  // 32 TH3 Blackhawk cores + 128 transceivers + 32 xphy
  setExpectation(128, 32, 32, 128, expectedProfiles);

  auto mapping = std::make_unique<Minipack16QPimPlatformMapping>(
      ExternalPhyVersion::MILN4_2);
  verify(mapping.get());
}

TEST_F(PlatformMappingTest, VerifyMinipackMiln5_2_16QPlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528,
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N};

  // Minipack16Q has 128 ports
  // 32 TH3 Blackhawk cores + 128 transceivers + 32 xphy
  setExpectation(128, 32, 32, 128, expectedProfiles);

  auto mapping = std::make_unique<Minipack16QPimPlatformMapping>(
      ExternalPhyVersion::MILN5_2);
  verify(mapping.get());
}

TEST_F(PlatformMappingTest, VerifyOverrideMerge) {
  auto miln4_2 = std::make_unique<Minipack16QPimPlatformMapping>(
      ExternalPhyVersion::MILN4_2);
  auto miln5_2 = std::make_unique<Minipack16QPimPlatformMapping>(
      ExternalPhyVersion::MILN5_2);
  for (const auto& port : miln5_2->getPlatformPorts()) {
    miln4_2->mergePortConfigOverrides(
        port.first, miln5_2->getPortConfigOverrides(port.first));
  }

  // Now 4.2 should be exactly the same as 5.2
  EXPECT_EQ(miln4_2->getPortConfigOverrides().size(), 2);
  for (auto miln4_2Override : miln4_2->getPortConfigOverrides()) {
    auto overrideProfileList = miln4_2Override.factor()->profiles();
    EXPECT_TRUE(overrideProfileList);
    EXPECT_TRUE(
        std::find(
            overrideProfileList->begin(),
            overrideProfileList->end(),
            cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528) !=
            overrideProfileList->end() ||
        std::find(
            overrideProfileList->begin(),
            overrideProfileList->end(),
            cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N) !=
            overrideProfileList->end());

    auto overridePortList = miln4_2Override.factor()->ports();
    EXPECT_EQ(overridePortList->size(), miln4_2->getPlatformPorts().size());
    // Now all the port should use this override
    for (const auto& port : miln4_2->getPlatformPorts()) {
      EXPECT_TRUE(
          std::find(
              overridePortList->begin(), overridePortList->end(), port.first) !=
          overridePortList->end());

      auto pinCfgs =
          miln4_2->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
              cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528,
              PortID(port.first)));
      for (auto pinCfg : pinCfgs) {
        auto tx = pinCfg.tx();
        EXPECT_TRUE(tx.has_value());
        verifyTxSettings(*tx, {0, 0, 128, 0, 0, 0});
      }
    }
  }
}

TEST_F(PlatformMappingTest, VerifyWedge100PlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER_RACK_YV3_T1};

  // Wedge100 has 32 * 4 = 128 logical ports
  // 32 TH Falcon cores + 32 transceivers
  setExpectation(128, 32, 0, 32, expectedProfiles);

  auto mapping = std::make_unique<Wedge100PlatformMapping>();
  verify(mapping.get());
}

TEST_F(PlatformMappingTest, VerifyGalaxyFCPlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC_COPPER};

  // Galaxy FC has 32 * 4 = 128 logical ports
  // 32 TH Falcon cores + 0 transceivers
  setExpectation(128, 32, 0, 0, expectedProfiles);

  auto mapping = std::make_unique<GalaxyFCPlatformMapping>("fc003");
  verify(mapping.get());

  // also check all the port name should starts with fab3
  for (const auto& port : mapping->getPlatformPorts()) {
    EXPECT_EQ(port.second.mapping()->name()->rfind("fab3", 0), 0);
  }
}

TEST_F(PlatformMappingTest, VerifyGalaxyLCPlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_OPTICAL,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC_COPPER};

  // Galaxy LC has 32 * 4 = 128 logical ports
  // 32 TH Falcon cores + 16 transceivers
  setExpectation(128, 32, 0, 16, expectedProfiles);

  auto mapping = std::make_unique<GalaxyLCPlatformMapping>("lc301");
  verify(mapping.get());

  // also check all the port name should starts with fab3/eth301
  for (const auto& port : mapping->getPlatformPorts()) {
    EXPECT_TRUE(
        port.second.mapping()->name()->rfind("fab301", 0) == 0 ||
        port.second.mapping()->name()->rfind("eth301", 0) == 0);
  }
}

// Array of tx setting override groups for wedge100
// Each group is a mapping of cableLength => {driveCurrent_ref().value(),
// pre, main, post}
static const std::vector<std::map<double, std::pair<std::array<int, 6>, int>>>
    kWedge100DownlinkTxGroups = {
        {
            {1.0, {{0, 0x4, 0x3c, 0x30, 0, 0}, 0xa}},
            {1.5, {{0, 0x4, 0x3c, 0x30, 0, 0}, 0xa}},
            {2.0, {{0, 0x4, 0x3c, 0x30, 0, 0}, 0xa}},
            {2.5, {{0, 0x6, 0x3e, 0x32, 0, 0}, 0xc}},
            {3.0, {{0, 0x6, 0x3e, 0x32, 0, 0}, 0xc}},
        },
        {
            {1.0, {{0, 0x6, 0x40, 0x2a, 0, 0}, 0xa}},
            {1.5, {{0, 0x7, 0x3e, 0x2b, 0, 0}, 0xa}},
            {2.0, {{0, 0x8, 0x3c, 0x2c, 0, 0}, 0xb}},
            {2.5, {{0, 0x7, 0x3d, 0x2c, 0, 0}, 0xc}},
            {3.0, {{0, 0x6, 0x3c, 0x2e, 0, 0}, 0xc}},
        },
        {
            {1.0, {{0, 0x8, 0x42, 0x26, 0, 0}, 0x9}},
            {1.5, {{0, 0x9, 0x41, 0x26, 0, 0}, 0x9}},
            {2.0, {{0, 0x9, 0x40, 0x27, 0, 0}, 0x9}},
            {2.5, {{0, 0x9, 0x3f, 0x28, 0, 0}, 0x9}},
            {3.0, {{0, 0x8, 0x40, 0x28, 0, 0}, 0xa}},
        },
        {
            {1.0, {{0, 0x6, 0x46, 0x24, 0, 0}, 0x8}},
            {1.5, {{0, 0x6, 0x46, 0x24, 0, 0}, 0x9}},
            {2.0, {{0, 0x7, 0x45, 0x24, 0, 0}, 0x9}},
            {2.5, {{0, 0x8, 0x43, 0x25, 0, 0}, 0x9}},
            {3.0, {{0, 0x8, 0x43, 0x25, 0, 0}, 0xa}},
        },
        {
            {1.0, {{0, 0x6, 0x4c, 0x1e, 0, 0}, 0x8}},
            {1.5, {{0, 0x7, 0x4b, 0x1e, 0, 0}, 0x9}},
            {2.0, {{0, 0x7, 0x4b, 0x1e, 0, 0}, 0x9}},
            {2.5, {{0, 0x8, 0x49, 0x1f, 0, 0}, 0x9}},
            {3.0, {{0, 0x8, 0x48, 0x20, 0, 0}, 0xa}},
        },
        {
            {1.0, {{0, 0x6, 0x4e, 0x1c, 0, 0}, 0x8}},
            {1.5, {{0, 0x6, 0x4d, 0x1d, 0, 0}, 0x9}},
            {2.0, {{0, 0x7, 0x4b, 0x1e, 0, 0}, 0xa}},
            {2.5, {{0, 0x8, 0x49, 0x1f, 0, 0}, 0xa}},
            {3.0, {{0, 0x8, 0x48, 0x20, 0, 0}, 0xa}},
        },
        {
            {1.0, {{0, 0x6, 0x50, 0x1a, 0, 0}, 0x8}},
            {1.5, {{0, 0x6, 0x4e, 0x1c, 0, 0}, 0x9}},
            {2.0, {{0, 0x6, 0x4e, 0x1c, 0, 0}, 0x9}},
            {2.5, {{0, 0x7, 0x4b, 0x1e, 0, 0}, 0x9}},
            {3.0, {{0, 0x8, 0x4a, 0x1e, 0, 0}, 0x9}},
        },
};

// Each front panel port maps to one trace group in the above
// vector. The index is the TransceiverID, the value is the index
// for which set of overrides to use from kWedge100DownlinkTxGroups.
static const std::array<uint8_t, 28> kWedge100DownlinkPortGroupMapping = {{
    1, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5,
    6, 6, 6, 5, 5, 5, 4, 4, 3, 3, 3, 3, 2, 2,
}};

TEST_F(PlatformMappingTest, VerifyWedge100DownlinkPortIphyPinConfigs) {
  std::unordered_set<cfg::PortProfileID> downlinkProfiles({
      cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER,
      cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER,
      cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER,
  });
  auto mapping = std::make_unique<Wedge100PlatformMapping>();
  for (auto& port : mapping->getPlatformPorts()) {
    const auto& chipName =
        apache::thrift::get_pointer(
            port.second.mapping().value().pins().value()[0].z())
            ->end()
            ->chip()
            .value();
    const auto& chip = mapping->getChips().at(chipName);
    auto transID = folly::copy(chip.physicalID().value());
    // Skip uplinks
    if (transID >= kWedge100DownlinkPortGroupMapping.size()) {
      continue;
    }

    const auto& profiles = *port.second.supportedProfiles();
    for (const auto& profile : profiles) {
      // skip uplink profiles
      if (downlinkProfiles.find(profile.first) == downlinkProfiles.end()) {
        continue;
      }
      auto itProfileCfg = mapping->getPortProfileConfig(
          PlatformPortProfileConfigMatcher(profile.first, PortID(port.first)));
      EXPECT_TRUE(itProfileCfg.has_value());

      const auto& txSettingsGroup =
          kWedge100DownlinkTxGroups[kWedge100DownlinkPortGroupMapping[transID]];
      for (auto& setting : txSettingsGroup) {
        cfg::PlatformPortConfigOverrideFactor factor;
        factor.cableLengths() = {setting.first};
        auto expectedTx = setting.second.first;
        auto expectedDriveCurrent = setting.second.second;
        auto pinCfgs =
            mapping->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
                profile.first, PortID(port.first), factor));
        auto pinCfg = pinCfgs.at(0);
        auto tx = pinCfg.tx();
        EXPECT_TRUE(tx.has_value());
        verifyTxSettings(*tx, expectedTx, true);
        EXPECT_EQ(*tx->driveCurrent(), expectedDriveCurrent);
      }
    }
  }
}

TEST_F(PlatformMappingTest, VerifyWedge100YV3T1DownlinkPortIphyPinConfigs) {
  static const std::unordered_set<cfg::PortProfileID> YV3T1DownlinkProfiles({
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1,
      cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER_RACK_YV3_T1,
  });
  static auto constexpr kLastYV3T1DownlinkTransceiverID = 23;
  static std::array<int, 6> YV3T1DownlinkTx = {0, 16, 96, 56, 0, 0};
  static auto constexpr YV3T1DownlinkDriveCurrent = 8;
  cfg::PlatformPortConfigOverrideFactor factor;
  factor.cableLengths() = {1.5};

  auto mapping = std::make_unique<Wedge100PlatformMapping>();
  for (auto& port : mapping->getPlatformPorts()) {
    const auto& chipName =
        apache::thrift::get_pointer(
            port.second.mapping().value().pins().value()[0].z())
            ->end()
            ->chip()
            .value();
    const auto& chip = mapping->getChips().at(chipName);
    auto transID = folly::copy(chip.physicalID().value());
    // Skip uplinks
    if (transID > kLastYV3T1DownlinkTransceiverID) {
      continue;
    }

    // Now check these downlink ports should have the YV3_T1
    const auto& supportedProfiles = *port.second.supportedProfiles();
    for (auto profile : YV3T1DownlinkProfiles) {
      if (const auto& profileConfigIt = supportedProfiles.find(profile);
          profileConfigIt != supportedProfiles.end()) {
        // Check whether we can get the new serdes even with override factor
        const auto& pinCfgs =
            mapping->getPortIphyPinConfigs(PlatformPortProfileConfigMatcher(
                profileConfigIt->first, PortID(port.first), factor));
        const auto& pinCfg = pinCfgs.at(0);
        auto tx = pinCfg.tx();
        EXPECT_TRUE(tx.has_value());
        verifyTxSettings(*tx, YV3T1DownlinkTx, true);
        EXPECT_EQ(*tx->driveCurrent(), YV3T1DownlinkDriveCurrent);
      } else if (
          profile ==
              cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1 &&
          *port.second.mapping()->controllingPort() != port.first) {
        // 100G should only be used by the controlling port
        continue;
      } else {
        throw FbossError(
            "Missiong profile:",
            apache::thrift::util::enumNameSafe(profile),
            " in supported profile list for port:",
            port.first);
      }
    }
  }
}

TEST_F(PlatformMappingTest, VerifyMeru800bfaPlatformPortConfigOverrideFactor) {
  const cfg::PortProfileID meru800bfaProfile =
      cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL;
  Vendor vendor = Vendor();
  *vendor.name() = "Vendor_1";
  *vendor.oui() = "0x1";
  *vendor.partNumber() = "PartNum_1";
  *vendor.rev() = "Rev_1";
  *vendor.serialNumber() = "0x123";
  *vendor.dateCode() = "0x1234";

  cfg::PlatformPortConfigOverrideFactor factor;
  factor.vendor() = vendor;
  // Set product version to 5 to get the kJsonMultiNpuProdPlatformMappingStr
  auto mapping = std::make_unique<Meru800bfaPlatformMapping>(
      /*multiNpuPlatformMapping*/ true, 5);
  auto overrides = mapping->getPortConfigOverrides();

  // Check that there are overrides for this profile.
  CHECK(!overrides.empty());
  for (auto& override : overrides) {
    CHECK(override.factor()->profiles().has_value());
    CHECK_EQ(override.factor()->profiles()->size(), 1);
    CHECK(override.factor()->profiles()->front() == meru800bfaProfile);
    const auto& overrideFactor = override.factor();
    EXPECT_EQ(overrideFactor->vendor()->name(), factor.vendor()->name());
    EXPECT_EQ(
        overrideFactor->vendor()->partNumber(), factor.vendor()->partNumber());
    auto pins = override.pins();
    CHECK(pins.has_value());
    auto iphy = pins->iphy();
    CHECK(!iphy->empty());
    for (auto pin : iphy.value()) {
      auto tx = pin.tx();
      // Check that the tx settings are set for this override.
      CHECK(tx.has_value());
    }
  }
}

namespace {
Vendor makeVendor(const std::string& name, const std::string& partNumber) {
  Vendor vendor;
  vendor.name() = name;
  vendor.partNumber() = partNumber;
  return vendor;
}

TransceiverVendorPartNumber makeVendorPartNumber(
    const std::string& vendor,
    const std::string& partNumber) {
  TransceiverVendorPartNumber vendorPartNumber;
  vendorPartNumber.vendor() = vendor;
  vendorPartNumber.partNumber() = partNumber;
  return vendorPartNumber;
}

std::vector<TransceiverVendorOverride> makeVendorOverrides() {
  TransceiverVendorOverride vendorOverride;
  vendorOverride.placeholder() = makeVendorPartNumber("Vendor_1", "PartNum_1");
  vendorOverride.transceivers() = {
      makeVendorPartNumber("FBOSSONE", "PN_1"),
      makeVendorPartNumber("FBOSSONE", "PN_2")};
  return {vendorOverride};
}

cfg::PlatformPortConfigOverrideFactor makeVendorOverrideFactor() {
  cfg::PlatformPortConfigOverrideFactor overrideFactor;
  overrideFactor.vendor() = makeVendor("Vendor_1", "PartNum_1");
  return overrideFactor;
}

PlatformPortProfileConfigMatcher makeVendorMatcher(
    const Vendor& currentVendor,
    const std::optional<std::vector<TransceiverVendorOverride>>&
        vendorOverrides) {
  cfg::PlatformPortConfigOverrideFactor currentFactor;
  currentFactor.vendor() = currentVendor;
  currentFactor.vendorOverrides().from_optional(vendorOverrides);
  return PlatformPortProfileConfigMatcher(
      cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL,
      PortID(1),
      currentFactor);
}
} // namespace

TEST_F(PlatformMappingTest, VendorOverridesMatchListedTransceiver) {
  auto matcher =
      makeVendorMatcher(makeVendor("FBOSSONE", "PN_2"), makeVendorOverrides());
  EXPECT_TRUE(matcher.matchOverrideWithFactor(makeVendorOverrideFactor()));
}

TEST_F(PlatformMappingTest, VendorOverridesSkipUnlistedTransceiver) {
  auto matcher =
      makeVendorMatcher(makeVendor("FBOSSONE", "PN_3"), makeVendorOverrides());
  EXPECT_FALSE(matcher.matchOverrideWithFactor(makeVendorOverrideFactor()));
}

TEST_F(PlatformMappingTest, VendorOverridesSkipUnknownPlaceholder) {
  auto matcher =
      makeVendorMatcher(makeVendor("FBOSSONE", "PN_1"), makeVendorOverrides());
  cfg::PlatformPortConfigOverrideFactor overrideFactor;
  overrideFactor.vendor() = makeVendor("Vendor_2", "PartNum_2");
  EXPECT_FALSE(matcher.matchOverrideWithFactor(overrideFactor));
}

TEST_F(PlatformMappingTest, VendorOverrideWithoutOverridesUsesBuiltInMatch) {
  // FBOSSONE isn't in the built in map, so without the qsfp_service
  // provided overrides the placeholder doesn't resolve.
  auto matcher =
      makeVendorMatcher(makeVendor("FBOSSONE", "PN_2"), std::nullopt);
  EXPECT_FALSE(matcher.matchOverrideWithFactor(makeVendorOverrideFactor()));
}

// Array of expected tx settings for wedge100 uplinks for 100G Optical
// Index is transciever Id - 24 (i.e first element is tcvr 24, the first
// uplink) value is {driveCurrent, pre, main, post}
const std::vector<std::array<int, 6>> kWedge100UplinkTxSettings = {
    {0, 2, 72, 38, 0, 0},
    {0, 2, 66, 44, 0, 0},
    {0, 4, 68, 40, 0, 0},
    {0, 2, 68, 42, 0, 0},
    {0, 4, 64, 44, 0, 0},
    {0, 2, 64, 46, 0, 0},
    {0, 2, 62, 48, 0, 0},
    {0, 2, 62, 48, 0, 0}};
const auto kWedge100UplinkDriveCurrent = 0x8;

TEST_F(PlatformMappingTest, VerifyWedge100UplinkPortIphyPinConfigs) {
  auto mapping = std::make_unique<Wedge100PlatformMapping>();
  for (auto& port : mapping->getPlatformPorts()) {
    const auto& chipName =
        apache::thrift::get_pointer(
            port.second.mapping().value().pins().value()[0].z())
            ->end()
            ->chip()
            .value();
    const auto& chip = mapping->getChips().at(chipName);
    auto transID = folly::copy(chip.physicalID().value());
    // skip downlinks
    if (transID < 24) {
      continue;
    }
    const auto& profiles = *port.second.supportedProfiles();
    for (const auto& profile : profiles) {
      auto pinCfgs = mapping->getPortIphyPinConfigs(
          PlatformPortProfileConfigMatcher(profile.first, PortID(port.first)));
      for (auto pinCfg : pinCfgs) {
        auto tx = pinCfg.tx();
        // Only the this profile should have tx settings
        if (profile.first ==
            cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL) {
          auto expectedTx = kWedge100UplinkTxSettings[transID - 24];
          EXPECT_TRUE(tx.has_value());
          verifyTxSettings(*tx, expectedTx, true);
          EXPECT_EQ(*tx->driveCurrent(), kWedge100UplinkDriveCurrent);
        } else {
          EXPECT_FALSE(tx.has_value());
        }
      }
    }
  }
}

TEST_F(PlatformMappingTest, VerifyElbert16QPlatformMapping) {
  // supported profiles
  std::vector<cfg::PortProfileID> expectedProfiles = {
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
      cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL};

  // Elbert16Q has 128 ports
  // 64 TH3 Blackhawk cores + 128 transceivers + no xphy
  setExpectation(128, 64, 0, 128, expectedProfiles);

  // both xphy version should pass the same standard
  auto mapping = std::make_unique<Elbert16QPimPlatformMapping>();
  verify(mapping.get());
}

TEST_F(PlatformMappingTest, VerifyElbert16QIphyPinConfigs) {
  auto mapping = std::make_unique<Elbert16QPimPlatformMapping>();
  std::map<cfg::PortProfileID, std::array<int, 6>> expectedTxSettings = {
      {cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
       {0, -10, 101, -16, 0, 0}},
      {cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL,
       {0, -24, 132, -12, 0, 0}},
      {cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL,
       {0, -24, 132, -12, 0, 0}},
  };
  verifyTxSettingsByProfile(
      mapping.get(), mapping->getPlatformPorts(), expectedTxSettings);
}

TEST_F(PlatformMappingTest, VerifyPlatformSupportedProfileMerge) {
  auto platformMapping = PlatformMapping();
  cfg::PlatformPortProfileConfigEntry configEntry1 =
      createPlatformPortProfileConfigEntry(
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL,
          {1, 2},
          cfg::PortSpeed::HUNDREDG,
          phy::FecMode::CL74);
  cfg::PlatformPortProfileConfigEntry configEntry2 =
      createPlatformPortProfileConfigEntry(
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL,
          {1, 3},
          cfg::PortSpeed::HUNDREDG,
          phy::FecMode::CL74);
  cfg::PlatformPortProfileConfigEntry configEntry3 =
      createPlatformPortProfileConfigEntry(
          cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL,
          {1, 2, 3},
          cfg::PortSpeed::HUNDREDG,
          phy::FecMode::CL74);
  cfg::PlatformPortProfileConfigEntry configEntry4 =
      createPlatformPortProfileConfigEntry(
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL,
          {2},
          cfg::PortSpeed::HUNDREDG,
          phy::FecMode::RS528);

  platformMapping.mergePlatformSupportedProfile(configEntry1);
  platformMapping.mergePlatformSupportedProfile(configEntry2);
  platformMapping.mergePlatformSupportedProfile(configEntry3);

  // pims 1-3 with PROFILE_100G_4_NRZ_CL91_OPTICAL
  for (auto pimID : {1, 2, 3}) {
    auto profile =
        platformMapping.getPortProfileConfig(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL, PimID(pimID)));
    EXPECT_TRUE(profile.has_value());
    EXPECT_EQ(profile.value(), configEntry1.profile());
  }

  // pims 1-3 with PROFILE_25G_1_NRZ_NOFEC_OPTICAL
  for (auto pimID : {1, 2, 3}) {
    auto profile =
        platformMapping.getPortProfileConfig(PlatformPortProfileConfigMatcher(
            cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL, PimID(pimID)));
    EXPECT_TRUE(profile.has_value());
    EXPECT_EQ(profile.value(), configEntry3.profile());
  }

  // No match at pim 4
  auto profileDoesNotExists =
      platformMapping.getPortProfileConfig(PlatformPortProfileConfigMatcher(
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL, PimID(4)));
  EXPECT_FALSE(profileDoesNotExists.has_value());

  // config 4 has the same profile id and overlapping pimID as config 1, but
  // different profile config. There's no reasonable merge in this case so
  // expect an error
  EXPECT_THROW(
      platformMapping.mergePlatformSupportedProfile(configEntry4), FbossError);
}

TEST_F(PlatformMappingTest, VerifyYampXphyLinePolaritySwap) {
  auto mapping = std::make_unique<Yamp16QPimPlatformMapping>();

  std::map<cfg::PortProfileID, std::map<LaneID, phy::PolaritySwap>>
      expectedPolaritySwap = {
          {cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC, {}},
          {cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528, {}},
          {cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N, {}},
          {cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL, {}},
          {cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL, {}},
          {cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL, {}},
      };
  verifyXphyLinePolaritySwapByProfile(
      mapping.get(), mapping->getPlatformPorts(), expectedPolaritySwap);
}
TEST_F(PlatformMappingTest, VerifyGetTransceiverBankIdNoCoreId) {
  auto mapping = std::make_unique<FakeTestPlatformMapping>(std::vector<int>{1});
  auto ports = mapping->getPlatformPorts();
  ASSERT_FALSE(ports.empty());

  auto portId = PortID(ports.begin()->first);
  auto profiles = *ports.begin()->second.supportedProfiles();
  ASSERT_FALSE(profiles.empty());
  auto profileId = profiles.begin()->first;

  auto bankId = mapping->getTransceiverBankId(
      PlatformPortProfileConfigMatcher(profileId, portId, std::nullopt));
  EXPECT_FALSE(bankId.has_value());
}

TEST_F(PlatformMappingTest, VerifyGetTransceiverBankIdWithCoreId) {
  auto mapping = std::make_unique<FakeTestPlatformMapping>(std::vector<int>{1});

  std::string tcvrChipName;
  for (const auto& [name, chip] : mapping->getChips()) {
    if (*chip.type() == phy::DataPlanePhyChipType::TRANSCEIVER) {
      tcvrChipName = name;
      break;
    }
  }
  ASSERT_FALSE(tcvrChipName.empty());

  phy::DataPlanePhyChip chipWithCoreId;
  *chipWithCoreId.name() = tcvrChipName;
  *chipWithCoreId.type() = phy::DataPlanePhyChipType::TRANSCEIVER;
  *chipWithCoreId.physicalID() = 0;
  chipWithCoreId.coreId() = 2;
  mapping->setChip(tcvrChipName, chipWithCoreId);

  auto ports = mapping->getPlatformPorts();
  auto portId = PortID(ports.begin()->first);
  auto profiles = *ports.begin()->second.supportedProfiles();
  auto profileId = profiles.begin()->first;

  auto bankId = mapping->getTransceiverBankId(
      PlatformPortProfileConfigMatcher(profileId, portId, std::nullopt));
  ASSERT_TRUE(bankId.has_value());
  EXPECT_EQ(*bankId, 2);
}

} // namespace facebook::fboss::test
