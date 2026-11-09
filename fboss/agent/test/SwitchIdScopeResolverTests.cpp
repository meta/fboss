// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/state/Srv6Tunnel.h" // must precede SwSwitch.h to avoid template conflicts
#include "fboss/agent/state/VxlanTunnel.h" // must precede SwSwitch.h to avoid template conflicts

#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchInfoTable.h"
#include "fboss/agent/state/BufferPoolConfig.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/PortFlowletConfig.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

template <typename SwitchTypeT>
class SwitchIdScopeResolverTest : public ::testing::Test {
 public:
  static auto constexpr switchType = SwitchTypeT::switchType;

  void addMirrorConfig(cfg::SwitchConfig* cfg) {
    cfg::Mirror mirror;
    mirror.name() = "mirror0";
    mirror.destination()->ip() = "2401:db00:2110:3000::1";
    cfg::MirrorEgressPort egressPort;
    egressPort.logicalID() = 1;
    mirror.destination()->egressPort() = egressPort;
    cfg->mirrors()->push_back(mirror);
  }

  void SetUp() override {
    auto config = testConfigA(switchType);
    addMirrorConfig(&config);
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
  }

 protected:
  HwSwitchMatcher l3SwitchMatcher() const {
    auto l3SwitchType = switchInfo().l3SwitchType();
    auto l3SwitchIds = switchInfo().getSwitchIdsOfType(l3SwitchType);
    return HwSwitchMatcher(l3SwitchIds);
  }
  HwSwitchMatcher voqSwitchMatcher() const {
    return HwSwitchMatcher(
        switchInfo().getSwitchIdsOfType(cfg::SwitchType::VOQ));
  }
  HwSwitchMatcher allSwitchMatcher() const {
    return HwSwitchMatcher(switchInfo().getSwitchIDs());
  }
  const SwitchIdScopeResolver& scopeResolver() const {
    return *sw_->getScopeResolver();
  }
  template <typename... Args>
  void expectThrow(Args&&... args) {
    EXPECT_THROW(
        scopeResolver().scope(std::forward<Args>(args)...), FbossError);
  }
  template <typename... Args>
  void expectAll(Args&&... args) {
    EXPECT_EQ(
        scopeResolver().scope(std::forward<Args>(args)...), allSwitchMatcher());
  }
  template <typename... Args>
  void expectVoq(Args&&... args) {
    EXPECT_EQ(
        scopeResolver().scope(std::forward<Args>(args)...), voqSwitchMatcher());
  }
  template <typename... Args>
  void expectL3(Args&&... args) {
    EXPECT_EQ(
        scopeResolver().scope(std::forward<Args>(args)...), l3SwitchMatcher());
  }
  template <typename... Args>
  void expectSwitchId(Args&&... args) {
    auto switchIds = switchInfo().getSwitchIDs();
    ASSERT_EQ(switchIds.size(), 1);
    EXPECT_EQ(
        scopeResolver().scope(std::forward<Args>(args)...),
        HwSwitchMatcher(switchIds));
  }

  bool isVoq() const {
    return switchType == cfg::SwitchType::VOQ;
  }
  bool isFabric() const {
    return switchType == cfg::SwitchType::FABRIC;
  }
  bool isNpu() const {
    return switchType == cfg::SwitchType::NPU;
  }

  const SwitchInfoTable& switchInfo() const {
    return sw_->getSwitchInfoTable();
  }
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

TYPED_TEST_SUITE(SwitchIdScopeResolverTest, SwitchTypeTestTypes);

TYPED_TEST(SwitchIdScopeResolverTest, mirrorScope) {
  if (this->isFabric()) {
    return;
  } else {
    auto state = this->sw_->getState();
    auto allMirrors = state->getMirrors();
    auto mirror = allMirrors->getAllNodes()->cbegin()->second;
    this->expectSwitchId(mirror);
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, mirrorOnDropReportScope) {
  cfg::MirrorOnDropReport report;
  report.mirrorPortId() = 6;
  auto shared = std::make_shared<MirrorOnDropReport>();
  shared->setMirrorPortId(PortID(6));

  if (this->isFabric()) {
    return;
  } else {
    this->expectSwitchId(report);
    this->expectSwitchId(shared);
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, dsfNodeScope) {
  this->expectAll(cfg::DsfNode{});
  this->expectAll(std::shared_ptr<DsfNode>());
}

TYPED_TEST(SwitchIdScopeResolverTest, portScope) {
  this->expectSwitchId(PortID(6));
}

TYPED_TEST(SwitchIdScopeResolverTest, portsScope) {
  this->expectSwitchId(std::vector<PortID>({PortID(6), PortID(8)}));
}

TYPED_TEST(SwitchIdScopeResolverTest, portObjScope) {
  state::PortFields portFields;
  portFields.portId() = PortID(1);
  portFields.portName() = "port1";
  auto port = std::make_shared<Port>(std::move(portFields));
  this->expectSwitchId(port);
}

TYPED_TEST(SwitchIdScopeResolverTest, portcfgScope) {
  cfg::Port port;
  port.logicalID() = 1;
  this->expectSwitchId(port);
}

TYPED_TEST(SwitchIdScopeResolverTest, aggPortScope) {
  cfg::AggregatePort aggPort;
  aggPort.name() = "agg";
  cfg::AggregatePortMember member;
  member.memberPortID() = 6;
  aggPort.memberPorts()->push_back(member);
  if (this->isFabric()) {
    this->expectThrow(aggPort);
  } else {
    this->expectL3(aggPort);
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, sysPortScope) {
  if (this->isVoq()) {
    this->expectVoq(SystemPortID(101));
    this->expectVoq(SystemPortID(101));
    this->expectVoq(std::make_shared<SystemPort>(SystemPortID(101)));
    this->expectVoq(SystemPortID(1001));
    this->expectVoq(std::make_shared<SystemPort>(SystemPortID(1001)));
  } else {
    this->expectThrow(SystemPortID(101));
    this->expectThrow(SystemPortID(101));
    this->expectThrow(std::make_shared<SystemPort>(SystemPortID(101)));
    this->expectThrow(SystemPortID(1001));
    this->expectThrow(std::make_shared<SystemPort>(SystemPortID(1001)));
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, vlanScope) {
  auto vlan1 = std::make_shared<Vlan>(VlanID(1), std::string("Vlan1"));
  Vlan::MemberPorts ports;
  state::VlanInfo vlanInfo;
  *vlanInfo.tagged() = true;
  *vlanInfo.priorityTagged() = false;
  ports.insert(std::make_pair(0, vlanInfo));
  vlan1->setPortsInfo(ports);
  this->expectSwitchId(vlan1);
  auto vlan2 = std::make_shared<Vlan>(VlanID(2), std::string("Vlan2"));
  this->expectAll(vlan2);
}

TYPED_TEST(SwitchIdScopeResolverTest, interfaceScope) {
  if (this->isFabric()) {
    return;
  } else {
    auto state = this->sw_->getState();
    auto allIntfs = state->getInterfaces();
    auto intf = allIntfs->getAllNodes()->cbegin()->second;
    this->expectL3(intf, state);
    this->expectL3(intf, this->sw_->getConfig());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, fibContainerScope) {
  auto fibContainer = std::make_shared<ForwardingInformationBaseContainer>();
  if (this->isFabric()) {
    this->expectThrow(fibContainer);
  } else {
    this->expectSwitchId(fibContainer);
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, vrfScope) {
  cfg::VrfConfig vrfCfg;
  vrfCfg.vrfId() = 1001;
  auto vrf = std::make_shared<Vrf>(RouterID(1001));
  if (this->isFabric()) {
    this->expectThrow(vrfCfg);
    this->expectThrow(vrf);
  } else {
    this->expectL3(vrfCfg);
    this->expectL3(vrf);
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, labelFibEntry) {
  auto entry = std::make_shared<LabelForwardingEntry>();
  if (this->isFabric()) {
    this->expectThrow(entry);
  } else {
    this->expectSwitchId(entry);
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, bufferPoolCfgScope) {
  if (this->isFabric()) {
    this->expectThrow(cfg::BufferPoolConfig{});
    this->expectThrow(std::shared_ptr<BufferPoolCfg>{});
  } else {
    this->expectL3(cfg::BufferPoolConfig{});
    this->expectL3(std::shared_ptr<BufferPoolCfg>());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, aclScope) {
  if (this->isFabric()) {
    this->expectThrow(cfg::AclEntry{});
    this->expectThrow(std::shared_ptr<AclEntry>{});
  } else {
    this->expectL3(cfg::AclEntry{});
    this->expectL3(std::shared_ptr<AclEntry>());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, qosPolicyScope) {
  if (this->isFabric()) {
    this->expectThrow(cfg::QosPolicy{});
    this->expectThrow(std::shared_ptr<QosPolicy>{});
  } else {
    this->expectL3(cfg::QosPolicy{});
    this->expectL3(std::shared_ptr<QosPolicy>());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, controlPlaneScope) {
  if (this->isFabric()) {
    // Fabric switches supports CPU port and use allSwitchMatcher
    this->expectAll(std::shared_ptr<ControlPlane>{});
  } else {
    this->expectL3(std::shared_ptr<ControlPlane>());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, sflowCollectors) {
  if (this->isFabric()) {
    this->expectThrow(cfg::SflowCollector{});
    this->expectThrow(std::shared_ptr<SflowCollector>{});
  } else {
    this->expectL3(cfg::SflowCollector{});
    this->expectL3(std::shared_ptr<SflowCollector>());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, switchSettingsScope) {
  this->expectThrow(std::shared_ptr<SwitchSettings>());
}

TYPED_TEST(SwitchIdScopeResolverTest, portFlowletCfgScope) {
  if (this->isFabric()) {
    this->expectThrow(cfg::PortFlowletConfig{});
    this->expectThrow(std::shared_ptr<PortFlowletCfg>{});
  } else {
    this->expectL3(cfg::PortFlowletConfig{});
    this->expectL3(std::shared_ptr<PortFlowletCfg>());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, SwitchTypeScope) {
  std::map<int64_t, cfg::SwitchInfo> infos{};
  for (auto i = 0; i < 4; i++) {
    cfg::SwitchInfo info{};
    info.switchType() = this->switchType;
    info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
    info.switchIndex() = i;
    cfg::Range64 range{};
    range.minimum() = i * 100;
    range.maximum() = i * 100 + 64;
    info.portIdRange() = range;
    infos[i] = info;
  }
  SwitchIdScopeResolver resolver(infos);

  auto switchTypeMatcher = resolver.scope(this->switchType);
  EXPECT_EQ(switchTypeMatcher.size(), 4);
  EXPECT_THROW(resolver.scope(cfg::SwitchType::PHY), FbossError);
}

namespace {
// Multi-switch resolver with one loopback interface bound to each switch:
// switch i owns interface (kLoopbackIntfBase + i).
constexpr int32_t kLoopbackIntfBase = 10;
constexpr int kNumSwitches = 4;

std::map<int64_t, cfg::SwitchInfo> makeMultiSwitchInfos(
    cfg::SwitchType switchType,
    bool bindLoopbackIntfs) {
  std::map<int64_t, cfg::SwitchInfo> infos{};
  for (auto i = 0; i < kNumSwitches; i++) {
    cfg::SwitchInfo info{};
    info.switchType() = switchType;
    info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
    info.switchIndex() = i;
    cfg::Range64 range{};
    range.minimum() = i * 100;
    range.maximum() = i * 100 + 64;
    info.portIdRange() = range;
    if (bindLoopbackIntfs) {
      info.loopbackIntfId() = kLoopbackIntfBase + i;
    }
    infos[i] = info;
  }
  return infos;
}

std::shared_ptr<Vlan> makePortlessVlan(int32_t vlanId, int32_t intfId) {
  auto vlan = std::make_shared<Vlan>(
      VlanID(vlanId), folly::to<std::string>("Vlan", vlanId));
  vlan->setInterfaceID(InterfaceID(intfId));
  return vlan;
}

// A config in the shape coop emits: portless loopback VLANs carrying no
// intfID, with the interface->vlan mapping held on the cfg::Interface.
cfg::SwitchConfig makeLoopbackCfg() {
  cfg::SwitchConfig cfg{};
  cfg.defaultVlan() = 4094;
  for (auto i = 0; i < kNumSwitches; i++) {
    const auto intfId = kLoopbackIntfBase + i;

    cfg::Vlan vlan;
    vlan.id() = intfId;
    vlan.name() = fmt::format("fbossLoopback{}", i);
    vlan.routable() = true;
    // Deliberately no intfID.
    cfg.vlans()->push_back(vlan);

    cfg::Interface intf;
    intf.type() = cfg::InterfaceType::VLAN;
    intf.intfID() = intfId;
    intf.vlanID() = intfId;
    intf.isVirtual() = true;
    cfg.interfaces()->push_back(intf);
  }
  return cfg;
}
} // namespace

// A portless VLAN whose interface is named by loopbackIntfId resolves to that
// switch, not an arbitrary one.
TYPED_TEST(SwitchIdScopeResolverTest, portlessVlanScopeBoundToOwningSwitch) {
  SwitchIdScopeResolver resolver(
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/true));

  for (auto i = 0; i < kNumSwitches; i++) {
    EXPECT_EQ(
        resolver.scope(
            makePortlessVlan(kLoopbackIntfBase + i, kLoopbackIntfBase + i)),
        HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(i)})));
  }
}

// A RIF on a multi-l3 platform with nothing naming its owner is a
// misconfiguration: the platform must set SwitchInfo::loopbackIntfId. Fabric
// has no l3 switch, so its pseudo VLANs keep the legacy pick.
TYPED_TEST(SwitchIdScopeResolverTest, portlessVlanScopeUnboundRifIsFatal) {
  const bool isFabric = this->switchType == cfg::SwitchType::FABRIC;

  // Interface id that no switch claims, on a resolver that does bind others.
  SwitchIdScopeResolver boundResolver(
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/true));
  // No switch binds anything at all.
  SwitchIdScopeResolver unboundResolver(
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/false));

  if (isFabric) {
    auto expectSingleKnownSwitch = [](const HwSwitchMatcher& matcher) {
      EXPECT_EQ(matcher.size(), 1);
      EXPECT_LT(
          static_cast<int64_t>(*matcher.switchIds().begin()), kNumSwitches);
    };
    expectSingleKnownSwitch(boundResolver.scope(makePortlessVlan(99, 99)));
    expectSingleKnownSwitch(unboundResolver.scope(
        makePortlessVlan(kLoopbackIntfBase, kLoopbackIntfBase)));
    return;
  }

  EXPECT_DEATH(
      boundResolver.scope(makePortlessVlan(99, 99)),
      "has no SwitchInfo::loopbackIntfId");
  EXPECT_DEATH(
      unboundResolver.scope(
          makePortlessVlan(kLoopbackIntfBase, kLoopbackIntfBase)),
      "has no SwitchInfo::loopbackIntfId");
}

// Ladakh/Leh carry a default VLAN 4094 with no vlanPorts and no interface, so
// no loopbackIntfId could ever name it. Rejecting that would stop the agent
// applying config, and it is never programmed as a RIF anyway.
TYPED_TEST(SwitchIdScopeResolverTest, portlessVlanWithoutRifIsNotRejected) {
  SwitchIdScopeResolver resolver(
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/true));

  auto defaultVlan =
      std::make_shared<Vlan>(VlanID(4094), std::string("default"));
  // No setInterfaceID: there is no cfg::Interface for this VLAN.
  auto matcher = resolver.scope(defaultVlan);
  EXPECT_EQ(matcher.size(), 1);
  EXPECT_LT(static_cast<int64_t>(*matcher.switchIds().begin()), kNumSwitches);
}

// One l3 switch is unambiguous with nothing bound - the pre-existing
// single-NPU loopback case.
TYPED_TEST(SwitchIdScopeResolverTest, portlessVlanScopeUnboundSingleL3Switch) {
  auto infos = makeMultiSwitchInfos(
      this->switchType, /*bindLoopbackIntfs=*/
      false);
  std::map<int64_t, cfg::SwitchInfo> single{{0, infos[0]}};
  SwitchIdScopeResolver resolver(single);

  EXPECT_EQ(
      resolver.scope(makePortlessVlan(kLoopbackIntfBase, kLoopbackIntfBase)),
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)})));
}

// A portless VLAN with no interface at all must still prefer the l3 switch.
// allSwitchMatcher() also holds the fabric switches, which never own a RIF,
// so picking from it could hand the VLAN to one of those.
TYPED_TEST(SwitchIdScopeResolverTest, portlessVlanWithoutRifPrefersL3Switch) {
  if (this->switchType == cfg::SwitchType::FABRIC) {
    // No l3 switch to prefer; covered by portlessVlanWithoutRifIsNotRejected.
    return;
  }
  auto infos =
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/false);
  std::map<int64_t, cfg::SwitchInfo> mixed{{0, infos[0]}};
  // A fabric switch alongside the single l3 switch.
  cfg::SwitchInfo fabric{};
  fabric.switchType() = cfg::SwitchType::FABRIC;
  fabric.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
  fabric.switchIndex() = 1;
  cfg::Range64 range{};
  range.minimum() = 500;
  range.maximum() = 564;
  fabric.portIdRange() = range;
  mixed[1] = fabric;
  SwitchIdScopeResolver resolver(mixed);

  auto noRifVlan = std::make_shared<Vlan>(VlanID(4094), std::string("default"));
  EXPECT_EQ(
      resolver.scope(noRifVlan),
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)})));
}

// Interface id 0 is the thrift default for cfg::Interface::intfID, so a
// loopbackIntfId left at 0 must not capture interfaces that never set an id.
TYPED_TEST(
    SwitchIdScopeResolverTest,
    portlessVlanWithNoInterfaceIgnoresBinding) {
  auto infosBindingZero =
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/true);
  infosBindingZero[2].loopbackIntfId() = 0;
  SwitchIdScopeResolver resolver(infosBindingZero);

  auto cfg = makeLoopbackCfg();
  // An interface that never set its id reads back as 0.
  cfg::Vlan vlan;
  vlan.id() = 4094;
  vlan.name() = "vlan4094";
  vlan.routable() = true;
  cfg.vlans()->push_back(vlan);
  cfg::Interface intf;
  intf.type() = cfg::InterfaceType::VLAN;
  intf.vlanID() = 4094;
  cfg.interfaces()->push_back(intf);
  cfg.defaultVlan() = 1;

  // Switch 2 names interface 0, but interface 0 must not resolve to it. With
  // the binding correctly ignored the VLAN has no RIF, so it takes the legacy
  // fallback rather than switch 2.
  auto matcher = resolver.scope(cfg::InterfaceType::VLAN, InterfaceID(0), cfg);
  EXPECT_EQ(matcher.size(), 1);
  EXPECT_NE(
      matcher, HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(2)})));

  // A real binding still resolves, so the guard has not disabled matching.
  EXPECT_EQ(
      resolver.scope(
          cfg::InterfaceType::VLAN, InterfaceID(kLoopbackIntfBase + 1), cfg),
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(1)})));
}

// A VLAN with member ports still derives its scope from those ports, even when
// its interface id happens to be bound to a different switch.
TYPED_TEST(SwitchIdScopeResolverTest, portedVlanScopeIgnoresLoopbackIntfId) {
  SwitchIdScopeResolver resolver(
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/true));

  // Interface id is bound to switch 3, but the member port lives on switch 1.
  auto vlan = makePortlessVlan(kLoopbackIntfBase + 3, kLoopbackIntfBase + 3);
  Vlan::MemberPorts ports;
  state::VlanInfo vlanInfo;
  *vlanInfo.tagged() = true;
  *vlanInfo.priorityTagged() = false;
  ports.insert(std::make_pair(100, vlanInfo));
  vlan->setPortsInfo(ports);

  EXPECT_EQ(
      resolver.scope(vlan),
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(1)})));
}

// The cfg path builds a Vlan from cfg::Vlan, which carries no intfID because
// coop never sets it. The resolver populates it from the id it is handed.
TYPED_TEST(
    SwitchIdScopeResolverTest,
    portlessIntfScopeFromCfgWithoutVlanIntfId) {
  SwitchIdScopeResolver resolver(
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/true));

  auto cfg = makeLoopbackCfg();

  // No vlanPorts: every one of these VLANs is portless.
  for (auto i = 0; i < kNumSwitches; i++) {
    EXPECT_EQ(
        resolver.scope(
            cfg::InterfaceType::VLAN, InterfaceID(kLoopbackIntfBase + i), cfg),
        HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(i)})))
        << "interface " << (kLoopbackIntfBase + i)
        << " did not resolve to its bound switch from cfg";
  }
}

// The state path resolves through the Vlan that ApplyThriftConfig back-filled
// from the cfg::Interface claiming it.
TYPED_TEST(
    SwitchIdScopeResolverTest,
    portlessIntfScopeFromStateWithoutVlanIntfId) {
  SwitchIdScopeResolver resolver(
      makeMultiSwitchInfos(this->switchType, /*bindLoopbackIntfs=*/true));

  auto state = std::make_shared<SwitchState>();
  HwSwitchMatcher anyScope(std::unordered_set<SwitchID>({SwitchID(0)}));

  for (auto i = 0; i < kNumSwitches; i++) {
    const auto intfId = kLoopbackIntfBase + i;
    auto vlan = std::make_shared<Vlan>(
        VlanID(intfId), folly::to<std::string>("fbossLoopback", i));
    // As ApplyThriftConfig's back-fill leaves it: cfg::Vlan carried no intfID,
    // so the id came from the cfg::Interface claiming this VLAN.
    vlan->setInterfaceID(InterfaceID(intfId));
    state->getVlans()->addNode(vlan, anyScope);

    auto intf = std::make_shared<Interface>(
        InterfaceID(intfId),
        RouterID(0),
        std::optional<VlanID>(VlanID(intfId)),
        folly::StringPiece(folly::to<std::string>("fbossLoopback", i)),
        folly::MacAddress("00:02:00:00:00:55"),
        9000,
        true /*isVirtual*/,
        false,
        cfg::InterfaceType::VLAN);
    state->getInterfaces()->modify(&state)->addNode(intf, anyScope);

    EXPECT_EQ(
        resolver.scope(intf, state),
        HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(i)})))
        << "interface " << intfId
        << " did not resolve to its bound switch from state";
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, portIntfScope) {
  cfg::Port port;
  port.logicalID() = 1;
  cfg::Interface intf;
  intf.type() = cfg::InterfaceType::PORT;
  intf.intfID() = 6001;
  intf.portID() = 1;

  cfg::SwitchConfig cfg{};
  cfg.ports()->resize(1);
  cfg.ports()[0] = port;

  cfg.interfaces()->resize(1);
  cfg.interfaces()[0] = intf;

  const auto& resolver = this->scopeResolver();
  auto matcher1 = resolver.scope(PortID(1));
  auto matcher2 =
      resolver.scope(cfg::InterfaceType::PORT, InterfaceID(6001), cfg);

  EXPECT_EQ(matcher1, matcher2);

  if (this->switchType == cfg::SwitchType::NPU) {
    auto config = testConfigAWithPortInterfaces();
    this->addMirrorConfig(&config);
    this->sw_->applyConfig("applyConfig", config);
    auto state = this->sw_->getState();
    auto intf6001 = state->getInterfaces()->getNode(InterfaceID(6001));

    auto matcher3 = resolver.scope(intf6001, state);
    EXPECT_EQ(matcher3, matcher1);

    auto matcher4 = resolver.scope(intf6001, cfg);
    EXPECT_EQ(matcher3, matcher4);
  }
}

namespace {
// A port router interface bound to an aggregate port whose sole member is
// port 1, plus the port and aggregate port it refers to.
cfg::SwitchConfig makeAggPortIntfConfig() {
  cfg::Port port;
  port.logicalID() = 1;

  cfg::AggregatePortMember member;
  member.memberPortID() = 1;
  cfg::AggregatePort aggPort;
  aggPort.key() = 55;
  aggPort.name() = "agg";
  aggPort.memberPorts()->push_back(member);

  cfg::Interface intf;
  intf.type() = cfg::InterfaceType::PORT;
  intf.intfID() = 6001;
  intf.aggregatePortID() = 55;

  cfg::SwitchConfig cfg{};
  cfg.ports()->resize(1);
  cfg.ports()[0] = port;
  cfg.aggregatePorts()->resize(1);
  cfg.aggregatePorts()[0] = aggPort;
  cfg.interfaces()->resize(1);
  cfg.interfaces()[0] = intf;
  return cfg;
}
} // namespace

TYPED_TEST(SwitchIdScopeResolverTest, aggPortIntfScope) {
  auto cfg = makeAggPortIntfConfig();
  const auto& resolver = this->scopeResolver();

  if (this->isFabric()) {
    // Aggregate port scope is l3 only, as aggPortScope above asserts.
    EXPECT_THROW(
        resolver.scope(cfg::InterfaceType::PORT, InterfaceID(6001), cfg),
        FbossError);
    return;
  }

  // An aggregate port bound router interface resolves to the same scope as
  // the aggregate port, and hence as its member port.
  auto matcher1 = resolver.scope(PortID(1));
  auto matcher2 =
      resolver.scope(cfg::InterfaceType::PORT, InterfaceID(6001), cfg);
  EXPECT_EQ(matcher1, matcher2);
  EXPECT_EQ(resolver.scope(cfg.aggregatePorts()[0]), matcher2);
}

TYPED_TEST(SwitchIdScopeResolverTest, aggPortIntfScopeUnknownAggPort) {
  auto cfg = makeAggPortIntfConfig();
  cfg.interfaces()[0].aggregatePortID() = 99;
  EXPECT_THROW(
      this->scopeResolver().scope(
          cfg::InterfaceType::PORT, InterfaceID(6001), cfg),
      FbossError);
}

TYPED_TEST(SwitchIdScopeResolverTest, portIntfScopeRequiresExactlyOneBinding) {
  // Both bindings set.
  auto bothSet = makeAggPortIntfConfig();
  bothSet.interfaces()[0].portID() = 1;
  EXPECT_THROW(
      this->scopeResolver().scope(
          cfg::InterfaceType::PORT, InterfaceID(6001), bothSet),
      FbossError);

  // Neither binding set.
  auto neitherSet = makeAggPortIntfConfig();
  neitherSet.interfaces()[0].aggregatePortID().reset();
  EXPECT_THROW(
      this->scopeResolver().scope(
          cfg::InterfaceType::PORT, InterfaceID(6001), neitherSet),
      FbossError);
}

TYPED_TEST(SwitchIdScopeResolverTest, srv6TunnelCfgScope) {
  if (this->isFabric()) {
    return;
  }
  // Create a cfg::Srv6Tunnel referencing a valid interface
  cfg::Srv6Tunnel tunnel;
  tunnel.srv6TunnelId() = "tunnel0";
  tunnel.underlayIntfID() = 1;

  auto config = this->sw_->getConfig();
  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, config);

  // The scope should match that of the underlay interface
  for (const auto& intf : *config.interfaces()) {
    if (*intf.intfID() == 1) {
      auto intfMatcher = resolver.scope(*intf.type(), InterfaceID(1), config);
      EXPECT_EQ(tunnelMatcher, intfMatcher);
      break;
    }
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, srv6TunnelCfgScopePortIntf) {
  if (this->switchType != cfg::SwitchType::NPU) {
    return;
  }
  auto config = testConfigAWithPortInterfaces();
  this->addMirrorConfig(&config);
  this->sw_->applyConfig("applyConfig", config);

  cfg::Srv6Tunnel tunnel;
  tunnel.srv6TunnelId() = "tunnel0";
  tunnel.underlayIntfID() = 6001;

  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, config);
  auto portMatcher = resolver.scope(PortID(1));
  EXPECT_EQ(tunnelMatcher, portMatcher);
}

TYPED_TEST(SwitchIdScopeResolverTest, srv6TunnelStateScope) {
  if (this->isFabric()) {
    return;
  }
  auto state = this->sw_->getState();
  auto allIntfs = state->getInterfaces();
  auto intf = allIntfs->getAllNodes()->cbegin()->second;

  // Create Srv6Tunnel with underlay pointing to existing interface
  state::Srv6TunnelFields tunnelFields;
  tunnelFields.srv6TunnelId() = "tunnel0";
  tunnelFields.underlayIntfId() = static_cast<int>(intf->getID());
  auto tunnel = std::make_shared<Srv6Tunnel>(std::move(tunnelFields));

  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, state);
  auto intfMatcher = resolver.scope(intf, state);
  EXPECT_EQ(tunnelMatcher, intfMatcher);
}

TYPED_TEST(SwitchIdScopeResolverTest, srv6TunnelCfgScopeInvalidIntf) {
  if (this->isFabric()) {
    return;
  }
  cfg::Srv6Tunnel tunnel;
  tunnel.srv6TunnelId() = "tunnel0";
  tunnel.underlayIntfID() = 99999;

  auto config = this->sw_->getConfig();
  const auto& resolver = this->scopeResolver();
  EXPECT_THROW(resolver.scope(tunnel, config), FbossError);
}

TYPED_TEST(SwitchIdScopeResolverTest, srv6TunnelStateScopeCfg) {
  if (this->isFabric()) {
    return;
  }
  auto state = this->sw_->getState();
  auto allIntfs = state->getInterfaces();
  auto intf = allIntfs->getAllNodes()->cbegin()->second;

  state::Srv6TunnelFields tunnelFields;
  tunnelFields.srv6TunnelId() = "tunnel0";
  tunnelFields.underlayIntfId() = static_cast<int>(intf->getID());
  auto tunnel = std::make_shared<Srv6Tunnel>(std::move(tunnelFields));

  auto config = this->sw_->getConfig();
  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, config);
  auto intfMatcher = resolver.scope(intf, config);
  EXPECT_EQ(tunnelMatcher, intfMatcher);
}

TYPED_TEST(SwitchIdScopeResolverTest, vxlanTunnelCfgScope) {
  if (this->isFabric()) {
    return;
  }
  // Create a cfg::VxLanTunnel referencing a valid interface
  cfg::VxLanTunnel tunnel;
  tunnel.vxlanTunnelId() = "tunnel0";
  tunnel.underlayIntfID() = 1;
  tunnel.tunnelType() = TunnelType::VXLAN;

  auto config = this->sw_->getConfig();
  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, config);

  // The scope should match that of the underlay interface
  for (const auto& intf : *config.interfaces()) {
    if (*intf.intfID() == 1) {
      auto intfMatcher = resolver.scope(*intf.type(), InterfaceID(1), config);
      EXPECT_EQ(tunnelMatcher, intfMatcher);
      break;
    }
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, vxlanTunnelCfgScopePortIntf) {
  if (this->switchType != cfg::SwitchType::NPU) {
    return;
  }
  auto config = testConfigAWithPortInterfaces();
  this->addMirrorConfig(&config);
  this->sw_->applyConfig("applyConfig", config);

  cfg::VxLanTunnel tunnel;
  tunnel.vxlanTunnelId() = "tunnel0";
  tunnel.underlayIntfID() = 6001;
  tunnel.tunnelType() = TunnelType::VXLAN;

  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, config);
  auto portMatcher = resolver.scope(PortID(1));
  EXPECT_EQ(tunnelMatcher, portMatcher);
}

TYPED_TEST(SwitchIdScopeResolverTest, vxlanTunnelStateScope) {
  if (this->isFabric()) {
    return;
  }
  auto state = this->sw_->getState();
  auto allIntfs = state->getInterfaces();
  auto intf = allIntfs->getAllNodes()->cbegin()->second;

  // Create VxlanTunnel with underlay pointing to existing interface
  state::VxLanTunnelFields tunnelFields;
  tunnelFields.vxlanTunnelId() = "tunnel0";
  tunnelFields.underlayIntfId() = static_cast<int>(intf->getID());
  tunnelFields.tunnelType() = TunnelType::VXLAN;
  auto tunnel = std::make_shared<VxlanTunnel>(std::move(tunnelFields));

  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, state);
  auto intfMatcher = resolver.scope(intf, state);
  EXPECT_EQ(tunnelMatcher, intfMatcher);
}

TYPED_TEST(SwitchIdScopeResolverTest, vxlanTunnelCfgScopeInvalidIntf) {
  if (this->isFabric()) {
    return;
  }
  cfg::VxLanTunnel tunnel;
  tunnel.vxlanTunnelId() = "tunnel0";
  tunnel.underlayIntfID() = 99999;
  tunnel.tunnelType() = TunnelType::VXLAN;

  auto config = this->sw_->getConfig();
  const auto& resolver = this->scopeResolver();
  EXPECT_THROW(resolver.scope(tunnel, config), FbossError);
}

TYPED_TEST(SwitchIdScopeResolverTest, vxlanTunnelStateScopeCfg) {
  if (this->isFabric()) {
    return;
  }
  auto state = this->sw_->getState();
  auto allIntfs = state->getInterfaces();
  auto intf = allIntfs->getAllNodes()->cbegin()->second;

  state::VxLanTunnelFields tunnelFields;
  tunnelFields.vxlanTunnelId() = "tunnel0";
  tunnelFields.underlayIntfId() = static_cast<int>(intf->getID());
  tunnelFields.tunnelType() = TunnelType::VXLAN;
  auto tunnel = std::make_shared<VxlanTunnel>(std::move(tunnelFields));

  auto config = this->sw_->getConfig();
  const auto& resolver = this->scopeResolver();
  auto tunnelMatcher = resolver.scope(tunnel, config);
  auto intfMatcher = resolver.scope(intf, config);
  EXPECT_EQ(tunnelMatcher, intfMatcher);
}

TYPED_TEST(SwitchIdScopeResolverTest, mySidScope) {
  if (this->isFabric()) {
    this->expectThrow(std::shared_ptr<MySid>{});
  } else {
    this->expectL3(std::shared_ptr<MySid>());
  }
}

TYPED_TEST(SwitchIdScopeResolverTest, mySidMapScope) {
  if (this->isFabric()) {
    this->expectThrow(std::shared_ptr<MySidMap>{});
  } else {
    this->expectL3(std::shared_ptr<MySidMap>());
  }
}
