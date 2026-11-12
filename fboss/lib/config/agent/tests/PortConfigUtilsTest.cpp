/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/platform_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/lib/config/agent/PortConfigUtils.h"

namespace facebook::fboss {

namespace {
const PortID kPortId{1};
constexpr auto kPortName = "eth1/1/1";

cfg::PlatformPortEntry makeEntry(
    int32_t id,
    const std::string& name,
    cfg::PortType portType = cfg::PortType::INTERFACE_PORT) {
  cfg::PlatformPortEntry entry;
  entry.mapping()->id() = id;
  entry.mapping()->name() = name;
  entry.mapping()->controllingPort() = id;
  entry.mapping()->portType() = portType;
  entry.mapping()->scope() = cfg::Scope::LOCAL;
  entry.supportedProfiles()[cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91] =
      cfg::PlatformPortConfig{};
  entry.supportedProfiles()[cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N] =
      cfg::PlatformPortConfig{};
  return entry;
}

// Build a PlatformMapping with a single port. Only the 100G profile has a
// resolvable PortProfileConfig (speed); the 400G profile deliberately has none.
PlatformMapping makeMapping() {
  cfg::PlatformMapping thriftMapping;
  thriftMapping.ports()[kPortId] = makeEntry(kPortId, kPortName);

  cfg::PlatformPortProfileConfigEntry profileEntry;
  profileEntry.factor()->profileID() =
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91;
  profileEntry.profile()->speed() = cfg::PortSpeed::HUNDREDG;
  thriftMapping.platformSupportedProfiles()->push_back(profileEntry);

  return PlatformMapping(thriftMapping);
}

void addProfile(
    cfg::PlatformMapping& mapping,
    cfg::PortProfileID profileID,
    cfg::PortSpeed speed) {
  cfg::PlatformPortProfileConfigEntry profileEntry;
  profileEntry.factor()->profileID() = profileID;
  profileEntry.profile()->speed() = speed;
  mapping.platformSupportedProfiles()->push_back(profileEntry);
}

cfg::PlatformPortEntry makePortGroupEntry(
    int32_t id,
    int32_t controllingPort,
    const std::map<cfg::PortProfileID, std::vector<int32_t>>& profiles) {
  auto entry = makeEntry(id, "eth1/" + std::to_string(id) + "/1");
  entry.mapping()->controllingPort() = controllingPort;
  entry.supportedProfiles()->clear();
  for (const auto& [profileID, subsumedPorts] : profiles) {
    cfg::PlatformPortConfig profile;
    if (!subsumedPorts.empty()) {
      profile.subsumedPorts() = subsumedPorts;
    }
    entry.supportedProfiles()[profileID] = std::move(profile);
  }
  return entry;
}

PlatformMapping makeTwoPortGroupMapping(bool includeSafeProfile) {
  cfg::PlatformMapping mapping;
  std::map<cfg::PortProfileID, std::vector<int32_t>> controllingProfiles{
      {cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N, {2}}};
  if (includeSafeProfile) {
    controllingProfiles.emplace(
        cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91, std::vector<int32_t>{});
  }
  mapping.ports()[1] = makePortGroupEntry(1, 1, controllingProfiles);
  mapping.ports()[2] = makePortGroupEntry(
      2,
      1,
      includeSafeProfile
          ? std::map<
                cfg::PortProfileID,
                std::vector<
                    int32_t>>{{cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91, {}}}
          : std::map<cfg::PortProfileID, std::vector<int32_t>>{});
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      cfg::PortSpeed::HUNDREDG);
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N,
      cfg::PortSpeed::FOURHUNDREDG);
  return PlatformMapping(mapping);
}

PlatformMapping makeSpecialPolicyMapping() {
  cfg::PlatformMapping mapping;
  mapping.ports()[1] = makePortGroupEntry(
      1,
      1,
      {
          {cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91, {}},
          {cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL, {}},
          {cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL, {}},
      });
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      cfg::PortSpeed::HUNDREDG);
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL,
      cfg::PortSpeed::FOURHUNDREDG);
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL,
      cfg::PortSpeed::EIGHTHUNDREDG);
  return PlatformMapping(mapping);
}

PlatformMapping makeIndependentProfileMapping() {
  cfg::PlatformMapping mapping;
  mapping.ports()[1] = makePortGroupEntry(
      1, 1, {{cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91, {}}});
  mapping.ports()[2] = makePortGroupEntry(
      2, 2, {{cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N, {}}});
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      cfg::PortSpeed::HUNDREDG);
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N,
      cfg::PortSpeed::FOURHUNDREDG);
  return PlatformMapping(mapping);
}

PlatformMapping makeMediumPreferenceMapping(
    cfg::PortType portType,
    bool includeOptical = true) {
  cfg::PlatformMapping mapping;
  std::map<cfg::PortProfileID, std::vector<int32_t>> profiles{
      {cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER, {}}};
  if (includeOptical) {
    profiles.emplace(
        cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
        std::vector<int32_t>{});
  }
  mapping.ports()[1] = makePortGroupEntry(1, 1, profiles);
  mapping.ports()[1].mapping()->portType() = portType;
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER,
      cfg::PortSpeed::HUNDREDG);
  if (includeOptical) {
    addProfile(
        mapping,
        cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL,
        cfg::PortSpeed::HUNDREDG);
  }
  return PlatformMapping(mapping);
}

// Hyper port 8 supports only PROFILE_DEFAULT, subsuming members 10 and 11.
PlatformMapping makeHyperPortMapping() {
  cfg::PlatformMapping mapping;
  mapping.ports()[8] = makePortGroupEntry(
      8, 8, {{cfg::PortProfileID::PROFILE_DEFAULT, {10, 11}}});
  mapping.ports()[8].mapping()->portType() = cfg::PortType::HYPER_PORT;
  for (const auto member : {10, 11}) {
    mapping.ports()[member] = makePortGroupEntry(
        member,
        member,
        {{cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL, {}}});
    mapping.ports()[member].mapping()->portType() =
        cfg::PortType::HYPER_PORT_MEMBER;
  }
  addProfile(
      mapping, cfg::PortProfileID::PROFILE_DEFAULT, cfg::PortSpeed::DEFAULT);
  addProfile(
      mapping,
      cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL,
      cfg::PortSpeed::EIGHTHUNDREDG);
  return PlatformMapping(mapping);
}
} // namespace

TEST(PortConfigUtilsTest, safeProfilesKeepEveryPortOnFixedPortPlatforms) {
  const auto mapping = makeTwoPortGroupMapping(true);
  const std::map<PortID, std::vector<PortID>> groups{
      {PortID(1), {PortID(1), PortID(2)}}};
  const utility::SafeProfileSelectionOptions options{
      .asicType = cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
      .supportsAddRemovePort = false,
  };

  const utility::PortProfileMap expected{
      {PortID(1), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91},
      {PortID(2), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91}};
  EXPECT_EQ(utility::getSafeProfileIDs(mapping, groups, options), expected);
}

TEST(PortConfigUtilsTest, safeProfilesMaySubsumeUnrequiredPorts) {
  const auto mapping = makeTwoPortGroupMapping(true);
  const std::map<PortID, std::vector<PortID>> groups{
      {PortID(1), {PortID(1), PortID(2)}}};
  const utility::SafeProfileSelectionOptions options{
      .asicType = cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
      .supportsAddRemovePort = true,
      .requiredPorts = std::set<PortID>{PortID(1)},
  };

  const utility::PortProfileMap expected{
      {PortID(1), cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N}};
  EXPECT_EQ(utility::getSafeProfileIDs(mapping, groups, options), expected);
}

TEST(PortConfigUtilsTest, safeProfilesPreserveAsicSpecificPolicy) {
  const auto mapping = makeSpecialPolicyMapping();
  const std::map<PortID, std::vector<PortID>> groups{{PortID(1), {PortID(1)}}};
  const utility::SafeProfileSelectionOptions jerichoOptions{
      .asicType = cfg::AsicType::ASIC_TYPE_JERICHO3,
      .dualStageRdsw3q2q = true,
  };
  const utility::SafeProfileSelectionOptions chenabOptions{
      .asicType = cfg::AsicType::ASIC_TYPE_CHENAB,
  };

  const utility::PortProfileMap expected{
      {PortID(1), cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL}};
  EXPECT_EQ(
      utility::getSafeProfileIDs(mapping, groups, jerichoOptions), expected);
  EXPECT_EQ(
      utility::getSafeProfileIDs(mapping, groups, chenabOptions), expected);
}

TEST(PortConfigUtilsTest, safeProfilesPreferOpticalForRoutedPorts) {
  const std::map<PortID, std::vector<PortID>> groups{{PortID(1), {PortID(1)}}};
  const utility::SafeProfileSelectionOptions options{
      .asicType = cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
      .preferOpticalProfiles = true,
  };

  for (const auto portType :
       {cfg::PortType::INTERFACE_PORT, cfg::PortType::MANAGEMENT_PORT}) {
    const auto mapping = makeMediumPreferenceMapping(portType);
    const utility::PortProfileMap expected{
        {PortID(1), cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL}};
    EXPECT_EQ(utility::getSafeProfileIDs(mapping, groups, options), expected);
  }

  const auto copperOnlyMapping =
      makeMediumPreferenceMapping(cfg::PortType::INTERFACE_PORT, false);
  const utility::PortProfileMap copperFallback{
      {PortID(1), cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER}};
  EXPECT_EQ(
      utility::getSafeProfileIDs(copperOnlyMapping, groups, options),
      copperFallback);
}

TEST(PortConfigUtilsTest, safeProfilesDoNotPreferOpticalByDefault) {
  const auto mapping =
      makeMediumPreferenceMapping(cfg::PortType::INTERFACE_PORT);
  const std::map<PortID, std::vector<PortID>> groups{{PortID(1), {PortID(1)}}};
  const utility::SafeProfileSelectionOptions options{
      .asicType = cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
  };

  const utility::PortProfileMap expected{
      {PortID(1), cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER}};
  EXPECT_EQ(utility::getSafeProfileIDs(mapping, groups, options), expected);
}

TEST(PortConfigUtilsTest, safeProfilesKeepDefaultProfileForHyperPorts) {
  const auto mapping = makeHyperPortMapping();
  const std::map<PortID, std::vector<PortID>> groups{
      {PortID(8), {PortID(8)}},
      {PortID(10), {PortID(10)}},
      {PortID(11), {PortID(11)}}};
  const utility::SafeProfileSelectionOptions options{
      .asicType = cfg::AsicType::ASIC_TYPE_JERICHO3,
      .preferOpticalProfiles = true,
  };

  const utility::PortProfileMap expected{
      {PortID(8), cfg::PortProfileID::PROFILE_DEFAULT},
      {PortID(10), cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL},
      {PortID(11), cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL}};
  EXPECT_EQ(utility::getSafeProfileIDs(mapping, groups, options), expected);
}

TEST(PortConfigUtilsTest, safeProfilesRejectConflictingPortGroup) {
  const auto mapping = makeTwoPortGroupMapping(false);
  const std::map<PortID, std::vector<PortID>> groups{
      {PortID(1), {PortID(1), PortID(2)}}};
  const utility::SafeProfileSelectionOptions options{
      .asicType = cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
  };

  try {
    utility::getSafeProfileIDs(mapping, groups, options);
    FAIL() << "Expected safe profile selection to fail";
  } catch (const FbossError& error) {
    EXPECT_NE(
        std::string(error.what())
            .find("Can't find safe profiles for ports: 1, 2"),
        std::string::npos);
  }
}

TEST(PortConfigUtilsTest, safeProfilesRejectUnresolvedProfileSpeed) {
  const auto mapping = makeMapping();
  const std::map<PortID, std::vector<PortID>> groups{{kPortId, {kPortId}}};
  const utility::SafeProfileSelectionOptions options{
      .asicType = cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
  };

  try {
    utility::getSafeProfileIDs(mapping, groups, options);
    FAIL() << "Expected safe profile selection to fail";
  } catch (const FbossError& error) {
    EXPECT_NE(
        std::string(error.what()).find("Can't resolve speed for profile"),
        std::string::npos);
    EXPECT_NE(
        std::string(error.what()).find("controlling-port group 1"),
        std::string::npos);
  }
}

TEST(PortConfigUtilsTest, applyPortProfilesSupportsHeterogeneousBatch) {
  const auto mapping = makeIndependentProfileMapping();
  cfg::SwitchConfig config;
  config.ports() = {
      utility::createRoutedPortConfig(
          mapping,
          PortID(1),
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(2001)),
      utility::createRoutedPortConfig(
          mapping,
          PortID(2),
          cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N,
          VlanID(2002)),
  };

  const auto result = utility::applyPortProfileChanges(
      config,
      mapping,
      {
          {.portName = "eth1/1/1",
           .profileID = cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91},
          {.portName = "eth1/2/1",
           .profileID = cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N},
      });

  ASSERT_EQ(result.requestedPorts.size(), 2);
  EXPECT_EQ(result.requestedPorts.at(0).speed, cfg::PortSpeed::HUNDREDG);
  EXPECT_EQ(result.requestedPorts.at(1).speed, cfg::PortSpeed::FOURHUNDREDG);
  EXPECT_EQ(
      *config.ports()->at(0).profileID(),
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
  EXPECT_EQ(
      *config.ports()->at(1).profileID(),
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N);
}

TEST(PortConfigUtilsTest, applyPortProfilesReturnsSubsumedPorts) {
  const auto mapping = makeTwoPortGroupMapping(true);
  cfg::SwitchConfig config;
  config.ports() = {
      utility::createRoutedPortConfig(
          mapping,
          PortID(1),
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(2001)),
      utility::createRoutedPortConfig(
          mapping,
          PortID(2),
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(2002)),
  };
  for (auto& port : *config.ports()) {
    port.state() = cfg::PortState::ENABLED;
  }

  const auto result = utility::applyPortProfileChanges(
      config,
      mapping,
      {{.portName = "eth1/1/1",
        .profileID = cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N}},
      {{"eth1/1/1", {}}});

  EXPECT_EQ(
      result.subsumedPorts,
      (std::map<PortID, std::string>{{PortID(2), "eth1/2/1"}}));
  ASSERT_EQ(config.ports()->size(), 2);
  EXPECT_EQ(
      *config.ports()->at(0).profileID(),
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N);
  EXPECT_EQ(*config.ports()->at(0).speed(), cfg::PortSpeed::FOURHUNDREDG);
  EXPECT_EQ(*config.ports()->at(1).state(), cfg::PortState::ENABLED);
}

TEST(PortConfigUtilsTest, applyPortProfilesEnforcesQsfpSupportAtomically) {
  const auto mapping = makeMapping();
  cfg::SwitchConfig config;
  config.ports() = {
      utility::createRoutedPortConfig(
          mapping,
          kPortId,
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(2001)),
  };
  const auto originalConfig = config;
  const utility::PortProfileSupportMap qsfpSupportedProfiles{
      {kPortName, {cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91}}};

  EXPECT_THROW(
      utility::applyPortProfileChanges(
          config,
          mapping,
          {{.portName = kPortName,
            .profileID = cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N}},
          qsfpSupportedProfiles),
      FbossError);
  EXPECT_EQ(config, originalConfig);
}

TEST(PortConfigUtilsTest, applyPortProfilesAlwaysRequiresPlatformSupport) {
  const auto mapping = makeTwoPortGroupMapping(true);
  cfg::SwitchConfig config;
  config.ports() = {
      utility::createRoutedPortConfig(
          mapping,
          PortID(2),
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(2002)),
  };
  const auto originalConfig = config;
  const utility::PortProfileSupportMap qsfpSupportedProfiles{
      {"eth1/2/1", {cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N}}};

  EXPECT_THROW(
      utility::applyPortProfileChanges(
          config,
          mapping,
          {{.portName = "eth1/2/1",
            .profileID = cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N}},
          qsfpSupportedProfiles),
      FbossError);
  EXPECT_EQ(config, originalConfig);
}

TEST(PortConfigUtilsTest, createDefaultPortConfigSetsAllFields) {
  auto mapping = makeMapping();

  auto port = utility::createDefaultPortConfig(
      &mapping, kPortId, cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91, 2001);

  EXPECT_EQ(*port.name(), kPortName);
  EXPECT_EQ(PortID(*port.logicalID()), kPortId);
  EXPECT_EQ(*port.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
  EXPECT_EQ(*port.portType(), cfg::PortType::INTERFACE_PORT);
  EXPECT_EQ(*port.scope(), cfg::Scope::LOCAL);
  EXPECT_EQ(*port.state(), cfg::PortState::DISABLED);
  EXPECT_EQ(*port.ingressVlan(), 2001);
  EXPECT_EQ(*port.speed(), cfg::PortSpeed::HUNDREDG);
}

TEST(PortConfigUtilsTest, createDefaultPortConfigDefaultsSpeedWhenUnresolved) {
  auto mapping = makeMapping();

  auto port = utility::createDefaultPortConfig(
      &mapping,
      kPortId,
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N,
      2001);

  EXPECT_EQ(*port.speed(), cfg::PortSpeed::DEFAULT);
}

TEST(PortConfigUtilsTest, createDefaultPortConfigRejectsNullMapping) {
  EXPECT_THROW(
      utility::createDefaultPortConfig(
          nullptr, kPortId, cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91, 2001),
      FbossError);
}

TEST(PortConfigUtilsTest, createInterfacePortConfigSetsRoutedDefaults) {
  const auto mapping = makeMapping();

  const auto port = utility::createInterfacePortConfig(
      mapping,
      kPortId,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      VlanID(2001));

  EXPECT_EQ(*port.logicalID(), static_cast<int32_t>(kPortId));
  EXPECT_EQ(*port.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
  EXPECT_EQ(*port.state(), cfg::PortState::DISABLED);
  EXPECT_EQ(*port.ingressVlan(), 2001);
  EXPECT_TRUE(*port.routable());
  EXPECT_EQ(*port.parserType(), cfg::ParserType::L3);
}

TEST(PortConfigUtilsTest, createRoutedPortConfigSupportsManagementPort) {
  cfg::PlatformMapping thriftMapping;
  thriftMapping.ports()[kPortId] =
      makeEntry(kPortId, kPortName, cfg::PortType::MANAGEMENT_PORT);
  addProfile(
      thriftMapping,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      cfg::PortSpeed::HUNDREDG);
  const PlatformMapping mapping(thriftMapping);

  const auto port = utility::createRoutedPortConfig(
      mapping,
      kPortId,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      VlanID(2999));

  EXPECT_EQ(*port.portType(), cfg::PortType::MANAGEMENT_PORT);
  EXPECT_EQ(*port.ingressVlan(), 2999);
  EXPECT_TRUE(*port.routable());
  EXPECT_EQ(*port.parserType(), cfg::ParserType::L3);
}

TEST(PortConfigUtilsTest, createRoutedPortConfigRejectsNonRoutedPortTypes) {
  cfg::PlatformMapping thriftMapping;
  thriftMapping.ports()[kPortId] =
      makeEntry(kPortId, kPortName, cfg::PortType::FABRIC_PORT);
  addProfile(
      thriftMapping,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      cfg::PortSpeed::HUNDREDG);
  const PlatformMapping mapping(thriftMapping);

  EXPECT_THROW(
      utility::createRoutedPortConfig(
          mapping,
          kPortId,
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(2001)),
      FbossError);
}

TEST(PortConfigUtilsTest, createInterfacePortConfigRejectsManagementPort) {
  cfg::PlatformMapping thriftMapping;
  thriftMapping.ports()[kPortId] =
      makeEntry(kPortId, kPortName, cfg::PortType::MANAGEMENT_PORT);
  addProfile(
      thriftMapping,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      cfg::PortSpeed::HUNDREDG);
  const PlatformMapping mapping(thriftMapping);

  EXPECT_THROW(
      utility::createInterfacePortConfig(
          mapping,
          kPortId,
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(2999)),
      FbossError);
}

TEST(PortConfigUtilsTest, createVlanPortConfigSetsBasicFields) {
  const auto vlanPort = utility::createVlanPortConfig(kPortId, VlanID(2001));
  EXPECT_EQ(*vlanPort.logicalPort(), static_cast<int32_t>(kPortId));
  EXPECT_EQ(*vlanPort.vlanID(), 2001);
  EXPECT_EQ(*vlanPort.spanningTreeState(), cfg::SpanningTreeState::FORWARDING);
  EXPECT_FALSE(*vlanPort.emitTags());
}

TEST(PortConfigUtilsTest, allocateFreeVlanIdSkipsVlanAndInterfaceIds) {
  cfg::SwitchConfig config;
  cfg::Vlan vlan2001;
  vlan2001.id() = 2001;
  cfg::Vlan vlan2003;
  vlan2003.id() = 2003;
  config.vlans() = {vlan2001, vlan2003};
  cfg::Interface intf;
  intf.intfID() = 2002;
  config.interfaces() = {intf};

  EXPECT_EQ(utility::allocateFreeVlanId(config), 2004);
}

TEST(PortConfigUtilsTest, allocateFreeVlanIdReturnsNextAfterSingleVlan) {
  cfg::SwitchConfig config;
  cfg::Vlan vlan2001;
  vlan2001.id() = 2001;
  config.vlans() = {vlan2001};

  EXPECT_EQ(utility::allocateFreeVlanId(config), 2002);
}

TEST(PortConfigUtilsTest, allocateFreeVlanIdThrowsWhenExhausted) {
  cfg::SwitchConfig config;
  cfg::Vlan vlan;
  vlan.id() = 2001;
  config.vlans() = {vlan};

  EXPECT_THROW(utility::allocateFreeVlanId(config, 2001, 2001), FbossError);
}

TEST(PortConfigUtilsTest, addInterfacePortToConfigAppendsAllEntities) {
  auto mapping = makeMapping();
  cfg::SwitchConfig config;
  // Occupy 2001 so the allocation must skip it.
  cfg::Vlan existing;
  existing.id() = 2001;
  config.vlans() = {existing};

  const int32_t n = utility::addInterfacePortToConfig(
      config, &mapping, kPortId, cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);

  EXPECT_EQ(n, 2002);

  ASSERT_EQ(config.ports()->size(), 1);
  const auto& port = config.ports()->at(0);
  EXPECT_TRUE(*port.routable());
  EXPECT_EQ(*port.parserType(), cfg::ParserType::L3);
  EXPECT_EQ(*port.ingressVlan(), n);
  EXPECT_EQ(*port.state(), cfg::PortState::DISABLED);
  EXPECT_EQ(PortID(*port.logicalID()), kPortId);

  ASSERT_EQ(config.vlans()->size(), 2);
  const auto& vlan = config.vlans()->at(1);
  EXPECT_EQ(*vlan.id(), n);
  EXPECT_TRUE(*vlan.routable());
  EXPECT_TRUE(*vlan.recordStats());

  ASSERT_EQ(config.vlanPorts()->size(), 1);
  const auto& vlanPort = config.vlanPorts()->at(0);
  EXPECT_EQ(*vlanPort.vlanID(), n);
  EXPECT_EQ(*vlanPort.logicalPort(), static_cast<int32_t>(kPortId));
  EXPECT_EQ(*vlanPort.spanningTreeState(), cfg::SpanningTreeState::FORWARDING);
  EXPECT_FALSE(*vlanPort.emitTags());

  ASSERT_EQ(config.interfaces()->size(), 1);
  const auto& intf = config.interfaces()->at(0);
  EXPECT_EQ(*intf.intfID(), n);
  EXPECT_EQ(*intf.vlanID(), n);
  EXPECT_EQ(*intf.type(), cfg::InterfaceType::VLAN);
  EXPECT_EQ(*intf.routerID(), 0);
  EXPECT_EQ(*intf.scope(), cfg::Scope::LOCAL);
  EXPECT_EQ(*intf.name(), std::to_string(n));
  EXPECT_EQ(*intf.mtu(), 9000);
  EXPECT_FALSE(intf.mac().has_value());
  EXPECT_TRUE(intf.ipAddresses()->empty());
}

TEST(PortConfigUtilsTest, addInterfacePortToConfigUsesExplicitVlanId) {
  const auto mapping = makeMapping();
  cfg::SwitchConfig config;

  utility::addInterfacePortToConfig(
      config,
      &mapping,
      kPortId,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
      VlanID(2500));

  ASSERT_EQ(config.ports()->size(), 1);
  EXPECT_EQ(*config.ports()->at(0).ingressVlan(), 2500);
  ASSERT_EQ(config.vlans()->size(), 1);
  EXPECT_EQ(*config.vlans()->at(0).id(), 2500);
  ASSERT_EQ(config.vlanPorts()->size(), 1);
  EXPECT_EQ(*config.vlanPorts()->at(0).vlanID(), 2500);
  ASSERT_EQ(config.interfaces()->size(), 1);
  EXPECT_EQ(*config.interfaces()->at(0).intfID(), 2500);
}

TEST(PortConfigUtilsTest, addInterfacePortToConfigRejectsReservedVlan) {
  const auto mapping = makeMapping();
  cfg::SwitchConfig config;

  EXPECT_THROW(
      utility::addInterfacePortToConfig(
          config,
          &mapping,
          kPortId,
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91,
          VlanID(0)),
      FbossError);
  EXPECT_EQ(config, cfg::SwitchConfig());
}

namespace {
cfg::Port makePort(int id, int ingressVlan) {
  cfg::Port p;
  p.logicalID() = id;
  p.name() = std::string("eth1/") + std::to_string(id) + "/1";
  p.ingressVlan() = ingressVlan;
  return p;
}
cfg::Vlan makeVlan(int id) {
  cfg::Vlan v;
  v.id() = id;
  v.name() = std::string("vlan") + std::to_string(id);
  return v;
}
cfg::VlanPort makeVlanPort(int logicalPort, int vlan) {
  cfg::VlanPort vp;
  vp.logicalPort() = logicalPort;
  vp.vlanID() = vlan;
  return vp;
}
cfg::Interface makeVlanIntf(int intfId, int vlan) {
  cfg::Interface i;
  i.intfID() = intfId;
  i.vlanID() = vlan;
  return i;
}
} // namespace

// Removing a subsumed port drops the port, its vlanPort, the vlan it emptied,
// and the VLAN interface on that vlan; while keeping a surviving port's vlan,
// an originally-empty vlan, and the default vlan.
TEST(RemovePortsFromConfigTest, prunesPortVlanPortVlanAndInterface) {
  cfg::SwitchConfig config;
  config.defaultVlan() = 1;
  config.ports() = {makePort(1, 1), makePort(2, 2)}; // port 2 will be subsumed
  config.vlans() = {makeVlan(1), makeVlan(2), makeVlan(99)}; // 99 is empty
  config.vlanPorts() = {makeVlanPort(1, 1), makeVlanPort(2, 2)};
  config.interfaces() = {makeVlanIntf(1, 1), makeVlanIntf(2, 2)};

  utility::removePortsFromConfig(config, {PortID(2)});

  cfg::SwitchConfig expected;
  expected.defaultVlan() = 1;
  expected.ports() = {makePort(1, 1)};
  expected.vlans() = {makeVlan(1), makeVlan(99)};
  expected.vlanPorts() = {makeVlanPort(1, 1)};
  expected.interfaces() = {makeVlanIntf(1, 1)};

  EXPECT_EQ(config, expected);
}

// Vlan membership is judged by vlanPorts, not by ports' ingressVlan: a vlan
// owned as ingressVlan by a removed port survives if a surviving port is still
// a (tagged) member of it via a vlanPort -- so no vlanPort is left pointing at
// a dropped vlan.
TEST(RemovePortsFromConfigTest, keepsVlanReferencedBySurvivingTaggedPort) {
  cfg::SwitchConfig config;
  config.defaultVlan() = 1;
  config.ports() = {makePort(1, 1), makePort(2, 2)}; // port 2 removed
  config.vlans() = {makeVlan(1), makeVlan(2)};
  // port 1 is untagged on vlan 1 and a tagged member of vlan 2 (port 2's
  // ingressVlan).
  config.vlanPorts() = {
      makeVlanPort(1, 1), makeVlanPort(1, 2), makeVlanPort(2, 2)};
  config.interfaces() = {makeVlanIntf(1, 1), makeVlanIntf(2, 2)};

  utility::removePortsFromConfig(config, {PortID(2)});

  // vlan 2 (and its interface) survive because port 1 still references it via a
  // vlanPort; only port 2's own vlanPort is dropped -- nothing dangles.
  cfg::SwitchConfig expected;
  expected.defaultVlan() = 1;
  expected.ports() = {makePort(1, 1)};
  expected.vlans() = {makeVlan(1), makeVlan(2)};
  expected.vlanPorts() = {makeVlanPort(1, 1), makeVlanPort(1, 2)};
  expected.interfaces() = {makeVlanIntf(1, 1), makeVlanIntf(2, 2)};

  EXPECT_EQ(config, expected);
}

// The default vlan is preserved even when the port that used it is removed and
// no other vlanPort references it.
TEST(RemovePortsFromConfigTest, keepsDefaultVlanEvenWhenEmptied) {
  cfg::SwitchConfig config;
  config.defaultVlan() = 1;
  config.ports() = {makePort(1, 1), makePort(2, 2)}; // port 1 uses default vlan
  config.vlans() = {makeVlan(1), makeVlan(2)};
  config.vlanPorts() = {makeVlanPort(1, 1), makeVlanPort(2, 2)};
  config.interfaces() = {makeVlanIntf(1, 1), makeVlanIntf(2, 2)};

  utility::removePortsFromConfig(config, {PortID(1)});

  // Default vlan 1 survives despite losing its only vlanPort; vlan 2 (still
  // used by surviving port 2) also survives.
  cfg::SwitchConfig expected;
  expected.defaultVlan() = 1;
  expected.ports() = {makePort(2, 2)};
  expected.vlans() = {makeVlan(1), makeVlan(2)};
  expected.vlanPorts() = {makeVlanPort(2, 2)};
  expected.interfaces() = {makeVlanIntf(1, 1), makeVlanIntf(2, 2)};

  EXPECT_EQ(config, expected);
}

// VLAN-only for now: port-router (PORT-type) interfaces bind to a port via
// portID, not a vlan (their vlanID defaults to 0), so removePortsFromConfig
// leaves them untouched -- both on a surviving port and (for now) on the
// removed port. Full port-router cleanup is deferred until platform support is
// verified. This guards against the vlan-survival check erroneously erasing an
// L3 interface whose vlanID reads as 0.
TEST(RemovePortsFromConfigTest, leavesPortRouterInterfacesUntouched) {
  cfg::SwitchConfig config;
  config.defaultVlan() = 1;
  config.ports() = {makePort(1, 1), makePort(2, 2)}; // port 2 removed
  config.vlans() = {makeVlan(1), makeVlan(2)};
  config.vlanPorts() = {makeVlanPort(1, 1), makeVlanPort(2, 2)};
  cfg::Interface portIntf1; // port-router interface on surviving port 1
  portIntf1.intfID() = 101;
  portIntf1.portID() = 1;
  cfg::Interface portIntf2; // port-router interface on removed port 2
  portIntf2.intfID() = 102;
  portIntf2.portID() = 2;
  config.interfaces() = {portIntf1, portIntf2};

  utility::removePortsFromConfig(config, {PortID(2)});

  // Both port-router interfaces remain (not pruned by the vlan-survival check).
  ASSERT_EQ(config.interfaces()->size(), 2);
  EXPECT_EQ(*config.interfaces()->at(0).intfID(), 101);
  EXPECT_EQ(*config.interfaces()->at(1).intfID(), 102);
}

// An empty subsumed set leaves the config untouched.
TEST(RemovePortsFromConfigTest, emptySubsumedSetIsNoOp) {
  cfg::SwitchConfig config;
  config.defaultVlan() = 1;
  config.ports() = {makePort(1, 1)};
  config.vlans() = {makeVlan(1)};
  config.vlanPorts() = {makeVlanPort(1, 1)};
  config.interfaces() = {makeVlanIntf(1, 1)};

  const cfg::SwitchConfig expected = config;
  utility::removePortsFromConfig(config, {});
  EXPECT_EQ(config, expected);
}

// Disable mode keeps the subsumed port (and its vlan/vlanPort/interface) in
// place but marks the port DISABLED.
TEST(RemovePortsFromConfigTest, disableModeDisablesPortInPlace) {
  cfg::SwitchConfig config;
  config.defaultVlan() = 1;
  config.ports() = {makePort(1, 1), makePort(2, 2)};
  config.vlans() = {makeVlan(1), makeVlan(2)};
  config.vlanPorts() = {makeVlanPort(1, 1), makeVlanPort(2, 2)};
  config.interfaces() = {makeVlanIntf(1, 1), makeVlanIntf(2, 2)};

  utility::removePortsFromConfig(
      config, {PortID(2)}, utility::PortRemovalMode::Disable);

  ASSERT_EQ(config.ports()->size(), 2);
  for (const auto& p : *config.ports()) {
    if (*p.logicalID() == 2) {
      EXPECT_EQ(*p.state(), cfg::PortState::DISABLED);
    }
  }
  EXPECT_EQ(config.vlanPorts()->size(), 2);
  EXPECT_EQ(config.vlans()->size(), 2);
  EXPECT_EQ(config.interfaces()->size(), 2);
}

// Erase mode with pruning disabled drops the port and its vlanPort but leaves
// the now-empty vlan and the interface untouched.
TEST(RemovePortsFromConfigTest, eraseWithoutPruneKeepsVlansAndIntfs) {
  cfg::SwitchConfig config;
  config.defaultVlan() = 1;
  config.ports() = {makePort(1, 1), makePort(2, 2)};
  config.vlans() = {makeVlan(1), makeVlan(2)};
  config.vlanPorts() = {makeVlanPort(1, 1), makeVlanPort(2, 2)};
  config.interfaces() = {makeVlanIntf(1, 1), makeVlanIntf(2, 2)};

  utility::removePortsFromConfig(
      config,
      {PortID(2)},
      utility::PortRemovalMode::Erase,
      /*pruneEmptyVlansAndInterfaces=*/false);

  ASSERT_EQ(config.ports()->size(), 1);
  EXPECT_EQ(*config.ports()->at(0).logicalID(), 1);
  EXPECT_EQ(config.vlanPorts()->size(), 1);
  EXPECT_EQ(config.vlans()->size(), 2);
  EXPECT_EQ(config.interfaces()->size(), 2);
}

} // namespace facebook::fboss
