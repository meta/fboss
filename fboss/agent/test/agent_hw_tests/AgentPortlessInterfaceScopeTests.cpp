// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/core.h>

#include <map>
#include <set>

#include "fboss/agent/HwSwitchThriftClientTable.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/lib/config/agent/PortConfigUtils.h"

namespace facebook::fboss {

namespace {
// Portless loopback interface -> the ASIC it must land on.
const std::map<int32_t, int64_t> kLoopbackIntfToSwitchId = {
    {utility::kNpu0LoopbackIntfId, 0},
    {utility::kNpu1LoopbackIntfId, 1},
};
} // namespace

/*
 * A portless interface has no member ports, so the owning ASIC cannot be
 * inferred from port membership the way it is for a regular VLAN RIF. On
 * dual-NPU SUSW platforms each NPU owns one loopback interface, bound via
 * SwitchInfo::loopbackIntfId. This test proves the binding survives all
 * the way into hardware: each RIF is programmed on its own ASIC and nowhere
 * else.
 */
class AgentPortlessInterfaceScopeTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    addPortlessLoopbacks(config);
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    // VLAN keeps this off VOQ platforms, where the VLAN RIFs this test builds
    // are invalid and initialConfig() would fail before any assertion runs.
    return {ProductionFeature::L3_FORWARDING, ProductionFeature::VLAN};
  }

 protected:
  // Interface IDs present in the given ASIC's programmed state. Issues one
  // thrift round trip, so callers cache the result per switch rather than
  // re-querying for every interface.
  std::set<int32_t> programmedIntfIds(int64_t switchId) {
    auto hwState = getSw()->getHwSwitchThriftClientTable()->getProgrammedState(
        SwitchID(switchId));
    std::set<int32_t> intfIds;
    for (const auto& [_mapName, intfMap] : *hwState.interfaceMaps()) {
      for (const auto& [intfId, _intf] : intfMap) {
        intfIds.insert(static_cast<int32_t>(intfId));
      }
    }
    return intfIds;
  }

 private:
  static void addPortlessLoopbacks(cfg::SwitchConfig& config) {
    // The shared config helper parks the default VLAN's interface ID on 10,
    // which is a real loopback interface here. Give any VLAN that collides
    // with one of our loopback ids an id of its own. Keyed off the map so it
    // stays correct if the loopback set changes.
    for (auto& vlan : *config.vlans()) {
      if (kLoopbackIntfToSwitchId.count(vlan.intfID().value_or(0))) {
        vlan.intfID() = *vlan.id();
      }
    }

    for (const auto& [intfId, switchId] : kLoopbackIntfToSwitchId) {
      auto& switchInfoMap = *config.switchSettings()->switchIdToSwitchInfo();
      auto switchInfo = switchInfoMap.find(switchId);
      if (switchInfo == switchInfoMap.end()) {
        // Platform has no such NPU, so there is nothing to bind the loopback
        // to. verify() scopes its assertions to the same set.
        continue;
      }

      cfg::Vlan vlan;
      vlan.id() = intfId;
      vlan.name() =
          fmt::format("fbossLoopback{}", intfId - utility::kNpu0LoopbackIntfId);
      vlan.intfID() = intfId;
      vlan.routable() = true;
      config.vlans()->push_back(vlan);

      cfg::Interface intf;
      intf.intfID() = intfId;
      intf.vlanID() = intfId;
      intf.name() = *vlan.name();
      intf.mac() = utility::getLocalCpuMacStr();
      intf.type() = cfg::InterfaceType::VLAN;
      intf.routerID() = 0;
      intf.mtu() = 9000;
      intf.isVirtual() = true;
      // v6 only, matching what GSC models on loop0/loop1.
      intf.ipAddresses() = {fmt::format("2401:db00:ffff::{:x}/128", intfId)};
      config.interfaces()->push_back(intf);

      // Without this the interface has no ports to scope from and falls back
      // to an arbitrary switch.
      switchInfo->second.loopbackIntfId() = intfId;
    }
  }
};

TEST_F(AgentPortlessInterfaceScopeTest, LoopbacksBindToOwningAsic) {
  auto setup = []() {};
  auto verify = [this]() {
    // Scope to the NPUs this platform actually has. On a single-NPU box that
    // leaves one loopback to check, which is still a real assertion; on Ladakh
    // it checks both. Nothing is skipped either way.
    const auto& switchIds = getSw()->getSwitchInfoTable().getSwitchIDs();
    std::map<int32_t, int64_t> expected;
    for (const auto& [intfId, switchId] : kLoopbackIntfToSwitchId) {
      if (switchIds.find(SwitchID(switchId)) != switchIds.end()) {
        expected[intfId] = switchId;
      }
    }
    ASSERT_FALSE(expected.empty())
        << "no loopback maps to a switchId on this platform";

    const auto state = getProgrammedState();

    // One thrift round trip per switch, rather than one per (interface,
    // switch) pair as the assertions below are evaluated.
    std::map<int64_t, std::set<int32_t>> programmedPerSwitch;
    for (const auto& switchId : switchIds) {
      programmedPerSwitch[static_cast<int64_t>(switchId)] =
          programmedIntfIds(static_cast<int64_t>(switchId));
    }

    for (const auto& [intfId, expectedSwitchId] : expected) {
      const auto intf = state->getInterfaces()->getNodeIf(InterfaceID(intfId));
      ASSERT_NE(intf, nullptr) << "interface " << intfId << " not programmed";

      EXPECT_EQ(
          scopeResolver().scope(intf, state).switchId(),
          SwitchID(expectedSwitchId))
          << "interface " << intfId << " scoped to the wrong switch";

      // The RIF must exist on its own ASIC and be absent from every other one.
      for (const auto& switchId : switchIds) {
        const auto id = static_cast<int64_t>(switchId);
        EXPECT_EQ(
            programmedPerSwitch.at(id).count(intfId) > 0,
            id == expectedSwitchId)
            << "interface " << intfId << " unexpectedly "
            << (id == expectedSwitchId ? "absent from" : "present on")
            << " switch " << id;
      }
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
