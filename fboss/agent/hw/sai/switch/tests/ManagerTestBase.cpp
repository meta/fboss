/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"

#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/platforms/sai/SaiFakePlatform.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/MacEntry.h"
#include "fboss/agent/state/MacTable.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/QosPolicy.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/state/VrfMap.h"
#include "fboss/agent/test/utils/NextHopIdTestUtils.h"

#include <fmt/format.h>
#include <folly/Singleton.h>

#ifndef IS_OSS
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#endif
#endif

namespace {
constexpr auto kPrbsPolynomial = 31;

facebook::fboss::cfg::AgentConfig getDummyConfig() {
  facebook::fboss::cfg::AgentConfig config;

  config.platform()->platformSettings() = {};
  config.platform()->platformSettings()->insert(
      std::make_pair(
          facebook::fboss::cfg::PlatformAttributes::CONNECTION_HANDLE,
          "test connection handle"));
  facebook::fboss::cfg::SwitchInfo info{};
  info.switchType() = facebook::fboss::cfg::SwitchType::NPU;
  info.asicType() = facebook::fboss::cfg::AsicType::ASIC_TYPE_MOCK;
  facebook::fboss::cfg::Range64 portIdRange;
  portIdRange.minimum() = 0;
  portIdRange.maximum() = 0;
  info.portIdRange() = portIdRange;
  info.switchIndex() = 0;
  info.connectionHandle() = "test connection handle";
  config.sw()->switchSettings()->switchIdToSwitchInfo()->emplace(0, info);
  return config;
}
} // namespace

namespace facebook::fboss {

ManagerTestBase::~ManagerTestBase() {}

void ManagerTestBase::SetUp() {
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();
  setupSaiPlatform();
}

std::unique_ptr<SaiPlatform> ManagerTestBase::createSaiPlatformForTest(
    std::unique_ptr<PlatformProductInfo> productInfo) {
  return std::make_unique<SaiFakePlatform>(std::move(productInfo));
}

void ManagerTestBase::setupSaiPlatform() {
  auto productInfo = fakeProductInfo();
  saiPlatform = createSaiPlatformForTest(std::move(productInfo));
  auto thriftAgentConfig = getDummyConfig();
  auto agentConfig = std::make_unique<AgentConfig>(
      std::move(thriftAgentConfig), "dummyConfigStr");
  saiPlatform->init(
      std::move(agentConfig),
      (HwSwitch::FeaturesDesired::PACKET_RX_DESIRED |
       HwSwitch::FeaturesDesired::LINKSCAN_DESIRED),
      0);
  auto ret = saiPlatform->getHwSwitch()->init(nullptr, nullptr, false);
  auto saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  saiPlatform->initPorts();
  saiSwitch->switchRunStateChanged(SwitchRunState::INITIALIZED);
  saiApiTable = SaiApiTable::getInstance();
  saiStore = saiSwitch->getSaiStore();
  saiManagerTable = saiSwitch->managerTable();

  std::map<int64_t, cfg::SwitchInfo> switchId2SwitchInfo{};
  cfg::SwitchInfo switchInfo{};
  cfg::Range64 portIdRange;
  portIdRange.minimum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN();
  portIdRange.maximum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX();
  switchInfo.portIdRange() = portIdRange;
  switchInfo.switchIndex() = 0;
  // defaulting to NPU
  switchInfo.switchType() = cfg::SwitchType::NPU;
  switchInfo.asicType() = saiPlatform->getAsic()->getAsicType();
  switchId2SwitchInfo.emplace(SwitchID(0), switchInfo);
  resolver = std::make_unique<SwitchIdScopeResolver>(switchId2SwitchInfo);
  auto setupState = ret.switchState->clone();
  for (int i = 0; i < testInterfaces.size(); ++i) {
    if (i == 0) {
      testInterfaces[i] = TestInterface{i, 4};
    } else {
      testInterfaces[i] = TestInterface{i, 1};
    }
  }
  for (int i = 0; i < testRemoteInterfaces.size(); ++i) {
    testRemoteInterfaces[i] = TestInterface{10 + i, 1};
  }
  for (const auto& [intfId, routerId] : testInterfaceRouterIds) {
    testInterfaces.at(intfId).routerId = routerId;
  }

  HwSwitchMatcher scope(std::unordered_set<SwitchID>({SwitchID(0)}));
  TestQosPolicy testQosPolicy{{10, 0, 2}, {42, 1, 4}};
  auto qp = makeQosPolicy("default", testQosPolicy);
  auto switchSettings = setupState->getSwitchSettings()
                            ->getNode(scope.matcherString())
                            ->modify(&setupState);
  switchSettings->setDefaultDataPlaneQosPolicy(qp);

  if (setupStage & SetupStage::PORT) {
    auto* ports = setupState->getPorts()->modify(&setupState);
    for (const auto& testInterface : testInterfaces) {
      for (const auto& remoteHost : testInterface.remoteHosts) {
        auto swPort = makePort(remoteHost.port);
        ports->addNode(swPort, scopeResolver().scope(swPort));
      }
    }
  }
  if (setupStage & SetupStage::SYSTEM_PORT) {
    auto* ports = setupState->getSystemPorts()->modify(&setupState);
    for (const auto& testInterface : testInterfaces) {
      auto swPort =
          makeSystemPort("default", kSysPortOffset + testInterface.id);
      ports->addNode(swPort, scope);
    }
    auto* remoteSysports =
        setupState->getRemoteSystemPorts()->modify(&setupState);
    for (const auto& testRemoteInterface : testRemoteInterfaces) {
      auto swPort =
          makeSystemPort("default", kSysPortOffset + testRemoteInterface.id);
      remoteSysports->addNode(swPort, scope);
    }
  }
  if (setupStage & SetupStage::VLAN) {
    auto* vlans = setupState->getVlans()->modify(&setupState);
    for (const auto& testInterface : testInterfaces) {
      auto swVlan = makeVlan(testInterface);
      vlans->addNode(swVlan, scopeResolver().scope(swVlan));
    }
  }
  if (setupStage & SetupStage::VRF) {
    auto* vrfs = setupState->getVrfs()->modify(&setupState);
    for (const auto& testInterface : testInterfaces) {
      if (testInterface.routerId != RouterID(0) &&
          !vrfs->getNodeIf(testInterface.routerId)) {
        auto vrf = std::make_shared<Vrf>(testInterface.routerId);
        vrfs->addNode(vrf, scopeResolver().scope(vrf));
      }
    }
  }
  if (setupStage & SetupStage::INTERFACE) {
    auto* interfaces = setupState->getInterfaces()->modify(&setupState);
    for (const auto& testInterface : testInterfaces) {
      auto swInterface = makeInterface(testInterface);
      interfaces->addNode(swInterface, scope);
      if (setupStage & SetupStage::SYSTEM_PORT) {
        auto swPortInterface =
            makeInterface(testInterface, cfg::InterfaceType::SYSTEM_PORT);
        interfaces->addNode(swPortInterface, scope);
      }
    }
    auto* remoteInterfaces =
        setupState->getRemoteInterfaces()->modify(&setupState);
    for (const auto& testRemoteInterface : testRemoteInterfaces) {
      if (setupStage & SetupStage::SYSTEM_PORT) {
        auto swPortInterface =
            makeInterface(testRemoteInterface, cfg::InterfaceType::SYSTEM_PORT);
        remoteInterfaces->addNode(swPortInterface, scope);
      }
    }
  }
  if (setupStage & SetupStage::NEIGHBOR) {
    for (const auto& testInterface : testInterfaces) {
      for (const auto& remoteHost : testInterface.remoteHosts) {
        auto swNeighbor = makeArpEntry(testInterface.id, remoteHost);
        auto existingVlanEntry =
            setupState->getVlans()->getNode(VlanID(testInterface.id));
        auto* vlan = existingVlanEntry->modify(&setupState);
        auto arpTable = vlan->getArpTable()->modify(&vlan, &setupState);
        PortDescriptor portDesc(PortID(remoteHost.port.id));
        arpTable->addEntry(
            remoteHost.ip.asV4(),
            remoteHost.mac,
            portDesc,
            InterfaceID(testInterface.id));
        auto macTable = vlan->getMacTable();
        macTable->addEntry(
            std::make_shared<MacEntry>(
                remoteHost.mac,
                portDesc,
                std::optional<cfg::AclLookupClass>(std::nullopt),
                MacEntryType::STATIC_ENTRY));
      }
    }
  }
  applyNewState(setupState);
  if (FLAGS_enable_nexthop_id_manager) {
    nextHopIDManager_ = std::make_unique<NextHopIDManager>();
  }
}

void ManagerTestBase::TearDown() {
  if (saiPlatform) {
    // If we already reset the platform (pseudo warmboot case),
    // do nothing.
    saiPlatform->getHwSwitch()->unregisterCallbacks();
  }
  saiStore->release();
  saiPlatform.reset();
  FakeSai::clear();
  programmedState.reset();
}

void ManagerTestBase::pseudoWarmBootExitAndStoreReload() {
  saiStore->exitForWarmBoot();
#ifndef IS_OSS
#if __has_feature(address_sanitizer)
  auto* leakedPlatform = saiPlatform.release();
  __lsan_ignore_object(leakedPlatform);
#else
  saiPlatform.release();
#endif
#endif
  saiStore->reload();
}

std::shared_ptr<Port> ManagerTestBase::makePort(
    const TestPort& testPort,
    std::optional<cfg::PortSpeed> expectedSpeed,
    bool isXphyPort) const {
  state::PortFields portFields;
  portFields.portId() = testPort.id;
  portFields.portName() = fmt::format("port{}", testPort.id);
  auto swPort = std::make_shared<Port>(std::move(portFields));
  if (testPort.enabled) {
    swPort->setAdminState(cfg::PortState::ENABLED);
  }
  // Ports are assigned in starting a (vlan/interfaceId * 10 + portIdx),
  // so compute the vlanID based on that.
  VlanID vlan(testPort.id / 10);
  swPort->setIngressVlan(vlan);
  PortFields::VlanMembership vlanMemberShip{
      {vlan, PortFields::VlanInfo{false, false}}};
  swPort->setVlans(vlanMemberShip);
  swPort->setSpeed(expectedSpeed ? *expectedSpeed : testPort.portSpeed);
  switch (swPort->getSpeed()) {
    case cfg::PortSpeed::DEFAULT:
      swPort->setProfileId(cfg::PortProfileID::PROFILE_DEFAULT);
      break;
    case cfg::PortSpeed::GIGE:
    case cfg::PortSpeed::ONEPOINTSIXT:
    case cfg::PortSpeed::THREEPOINTTWOT:
      throw FbossError("profile gig, 1.6T and 3.2T ethernet is not available");
    case cfg::PortSpeed::XG:
      swPort->setProfileId(cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL);
      break;
    case cfg::PortSpeed::TWENTYG:
      swPort->setProfileId(cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC);
      break;
    case cfg::PortSpeed::TWENTYFIVEG:
      swPort->setProfileId(cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL);
      break;
    case cfg::PortSpeed::FORTYG:
      swPort->setProfileId(cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL);
      break;
    case cfg::PortSpeed::FIFTYG:
      swPort->setProfileId(cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER);
      break;
    case cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG:
      swPort->setProfileId(
          cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER);
      break;
    case cfg::PortSpeed::HUNDREDG:
      swPort->setProfileId(cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL);
      break;
    case cfg::PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG:
      swPort->setProfileId(
          cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_COPPER);
      break;
    case cfg::PortSpeed::TWOHUNDREDG:
      swPort->setProfileId(
          cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL);
      break;
    case cfg::PortSpeed::TWOHUNDREDANDTWELVEPOINTFIVEG:
      swPort->setProfileId(
          cfg::PortProfileID::PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL);
      break;
    case cfg::PortSpeed::FOURHUNDREDG:
      swPort->setProfileId(
          cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL);
      break;
    case cfg::PortSpeed::EIGHTHUNDREDG:
      swPort->setProfileId(
          cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL);
      break;
  }
  auto platformPort = saiPlatform->getPort(swPort->getID());
  PlatformPortProfileConfigMatcher matcher{
      swPort->getProfileID(), swPort->getID()};
  auto profileConfig = saiPlatform->getPortProfileConfig(matcher);
  if (!profileConfig) {
    throw FbossError(
        "No port profile config found with matcher:", matcher.toString());
  }
  if (isXphyPort) {
    // Update both system and line sides config
    const auto& pinConfigs =
        platformPort->getPortXphyPinConfig(swPort->getProfileID());
    CHECK(profileConfig->xphySystem());
    swPort->setProfileConfig(*profileConfig->xphySystem());
    CHECK(pinConfigs.xphySys());
    swPort->resetPinConfigs(*pinConfigs.xphySys());
    CHECK(profileConfig->xphyLine());
    swPort->setLineProfileConfig(*profileConfig->xphyLine());
    CHECK(pinConfigs.xphyLine());
    swPort->resetLinePinConfigs(*pinConfigs.xphyLine());
  } else {
    // Use the iphy profileConfig and pinConfigs from PlatformMapping to update
    swPort->setProfileConfig(*profileConfig->iphy());
    swPort->resetPinConfigs(
        saiPlatform->getPlatformMapping()->getPortIphyPinConfigs(matcher));
    swPort->setSerdesCustomCollection(
        saiPlatform->getPlatformMapping()->getPortSerdesCustomCollection(
            matcher));
  }
  phy::PortPrbsState prbsState;
  prbsState.enabled() = true;
  prbsState.polynominal() = kPrbsPolynomial;
  swPort->setAsicPrbs(prbsState);
  return swPort;
}

std::shared_ptr<Vlan> ManagerTestBase::makeVlan(
    const TestInterface& testInterface) const {
  std::string name = fmt::format("vlan{}", testInterface.id);
  auto swVlan = std::make_shared<Vlan>(VlanID(testInterface.id), name);
  Vlan::MemberPorts mps;
  for (const auto& remoteHost : testInterface.remoteHosts) {
    PortID portId(remoteHost.port.id);
    state::VlanInfo portInfo;
    *portInfo.tagged() = false;
    *portInfo.priorityTagged() = false;
    mps.insert(std::make_pair(portId, portInfo));
  }
  swVlan->setPortsInfo(mps);
  return swVlan;
}

std::shared_ptr<AggregatePort> ManagerTestBase::makeAggregatePort(
    const TestInterface& testInterface) const {
  using SystemPriority = uint16_t;
  using SystemID = folly::MacAddress;

  SystemPriority systemPriority{0};
  SystemID systemID("00:00:00:00:00:00");

  std::vector<AggregatePort::Subport> subports;

  for (const auto& remoteHost : testInterface.remoteHosts) {
    PortID portId(remoteHost.port.id);
    subports.emplace_back(
        portId, 0, cfg::LacpPortRate::SLOW, cfg::LacpPortActivity::PASSIVE, 0);
  }
  std::string name = fmt::format("lag{}", testInterface.id);

  return AggregatePort::fromSubportRange(
      AggregatePortID(testInterface.id),
      name,
      name,
      systemPriority,
      systemID,
      0,
      folly::range(subports.begin(), subports.end()),
      {});
}

int64_t ManagerTestBase::getSysPortId(int id) const {
  return id + kSysPortOffset;
}
InterfaceID ManagerTestBase::getIntfID(int id, cfg::InterfaceType type) const {
  switch (type) {
    case cfg::InterfaceType::VLAN:
      return InterfaceID(id);
    case cfg::InterfaceType::SYSTEM_PORT:
      return InterfaceID(getSysPortId(id));
    case cfg::InterfaceType::PORT:
      return InterfaceID(id);
  }
  XLOG(FATAL) << "Unhandled interface type";
}

std::shared_ptr<Interface> ManagerTestBase::makeInterface(
    const TestInterface& testInterface,
    cfg::InterfaceType type) const {
  std::shared_ptr<Interface> interface;
  std::optional<VlanID> vlan;

  if (type == cfg::InterfaceType::VLAN) {
    vlan = VlanID(testInterface.id);
  }
  interface = std::make_shared<Interface>(
      getIntfID(testInterface, type),
      testInterface.routerId,
      std::optional<VlanID>(vlan),
      folly::StringPiece(fmt::format("intf{}", testInterface.id)),
      testInterface.routerMac,
      1500, // mtu
      false, // isVirtual
      false, // isStateSyncDisabled
      type);
  Interface::Addresses addresses;
  addresses.emplace(
      testInterface.subnet.first.asV4(), testInterface.subnet.second);
  interface->setAddresses(addresses);
  if (type == cfg::InterfaceType::PORT) {
    interface->setPortID(PortID(testInterface.id));
  }
  return interface;
}

std::shared_ptr<Interface> ManagerTestBase::makeAggregatePortInterface(
    const TestInterface& testInterface,
    AggregatePortID aggregatePortID) const {
  auto interface = makeInterface(testInterface, cfg::InterfaceType::PORT);
  // setAggregatePortID clears the port binding makeInterface just set, so the
  // interface ends up bound to the aggregate port alone.
  interface->setAggregatePortID(aggregatePortID);
  return interface;
}

std::shared_ptr<SystemPort> ManagerTestBase::makeSystemPort(
    const std::optional<std::string>& qosPolicy,
    int64_t sysPortId,
    int64_t switchId) const {
  auto sysPort = std::make_shared<SystemPort>(SystemPortID(sysPortId));
  sysPort->setSwitchId(SwitchID(switchId));
  sysPort->setName("sysPort1");
  sysPort->setCoreIndex(42);
  sysPort->setCorePortIndex(24);
  sysPort->setSpeedMbps(10000);
  sysPort->setNumVoqs(8);
  sysPort->setQosPolicy(qosPolicy);
  std::vector<uint8_t> queueIds = {0, 2, 6, 7};
  sysPort->resetPortQueues(makeQueueConfig(queueIds));
  return sysPort;
}

std::shared_ptr<Interface> ManagerTestBase::makeInterface(
    const SystemPort& sysPort,
    const std::vector<folly::CIDRNetwork>& subnets) const {
  auto interface = std::make_shared<Interface>(
      InterfaceID(sysPort.getID()),
      RouterID(0),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece(
          fmt::format("intf{}", static_cast<int>(sysPort.getID()))),
      folly::MacAddress{
          fmt::format("42:42:42:42:42:{}", static_cast<int>(sysPort.getID()))},
      1500, // mtu
      false, // isVirtual
      false, // isStateSyncDisabled
      cfg::InterfaceType::SYSTEM_PORT);
  Interface::Addresses addresses;
  for (const auto& subnet : subnets) {
    if (subnet.first.isV4()) {
      addresses.emplace(subnet.first.asV4(), subnet.second);
    } else {
      addresses.emplace(subnet.first.asV6(), subnet.second);
    }
  }
  interface->setAddresses(addresses);
  return interface;
}

std::shared_ptr<ArpEntry> ManagerTestBase::makePendingArpEntry(
    int id,
    const TestRemoteHost& testRemoteHost,
    cfg::InterfaceType intfType) const {
  return std::make_shared<ArpEntry>(
      testRemoteHost.ip.asV4(),
      getIntfID(id, intfType),
      state::NeighborEntryType::DYNAMIC_ENTRY,
      NeighborState::PENDING);
}

std::shared_ptr<ArpEntry> ManagerTestBase::makeArpEntry(
    int id,
    const TestRemoteHost& testRemoteHost,
    std::optional<sai_uint32_t> metadata,
    std::optional<sai_uint32_t> encapIndex,
    bool isLocal,
    cfg::InterfaceType intfType,
    bool noHostRoute) const {
  auto arpEntry = std::make_shared<ArpEntry>(
      testRemoteHost.ip.asV4(),
      testRemoteHost.mac,
      PortDescriptor(PortID(testRemoteHost.port.id)),
      InterfaceID(getIntfID(id, intfType)),
      state::NeighborEntryType::DYNAMIC_ENTRY);
  if (metadata) {
    arpEntry->setClassID(static_cast<cfg::AclLookupClass>(metadata.value()));
  }
  if (encapIndex) {
    arpEntry->setEncapIndex(static_cast<int64_t>(encapIndex.value()));
  }
  arpEntry->setIsLocal(isLocal);
  arpEntry->setNoHostRoute(noHostRoute);
  return arpEntry;
}

std::shared_ptr<ArpEntry> ManagerTestBase::resolveArp(
    int id,
    const TestRemoteHost& testRemoteHost,
    cfg::InterfaceType intfType,
    std::optional<sai_uint32_t> metadata,
    std::optional<sai_uint32_t> encapIndex,
    bool isLocal,
    bool noHostRoute) {
  auto arpEntry = makeArpEntry(
      id, testRemoteHost, metadata, encapIndex, isLocal, intfType, noHostRoute);
  saiManagerTable->neighborManager().addNeighbor(arpEntry);
  if (intfType == cfg::InterfaceType::VLAN) {
    saiManagerTable->fdbManager().addFdbEntry(
        SaiPortDescriptor(arpEntry->getPort().phyPortID()),
        arpEntry->getIntfID(),
        arpEntry->getMac(),
        SAI_FDB_ENTRY_TYPE_STATIC,
        metadata);
  }
  return arpEntry;
}

std::shared_ptr<ArpEntry> ManagerTestBase::makeArpEntry(
    const SystemPort& sysPort,
    folly::IPAddressV4 ip,
    folly::MacAddress mac,
    std::optional<sai_uint32_t> encapIndex) const {
  auto arpEntry = std::make_shared<ArpEntry>(
      ip,
      mac,
      PortDescriptor(PortID(sysPort.getID())),
      InterfaceID(static_cast<int>(sysPort.getID())),
      state::NeighborEntryType::DYNAMIC_ENTRY);

  arpEntry->setEncapIndex(static_cast<int64_t>(encapIndex.value()));
  arpEntry->setIsLocal(false);
  return arpEntry;
}

std::shared_ptr<ArpEntry> ManagerTestBase::resolveArp(
    const SystemPort& sysPort,
    folly::IPAddressV4 ip,
    folly::MacAddress mac,
    std::optional<sai_uint32_t> encapIndex) const {
  auto arpEntry = makeArpEntry(sysPort, ip, mac, encapIndex);
  saiManagerTable->neighborManager().addNeighbor(arpEntry);
  return arpEntry;
}

ResolvedNextHop ManagerTestBase::makeNextHop(
    const TestInterface& testInterface) const {
  const auto& remote = testInterface.remoteHosts.at(0);
  return ResolvedNextHop{remote.ip, InterfaceID(testInterface.id), ECMP_WEIGHT};
}

ResolvedNextHop ManagerTestBase::makeMplsNextHop(
    const TestInterface& testInterface,
    const LabelForwardingAction& action) const {
  const auto& remote = testInterface.remoteHosts.at(0);
  return ResolvedNextHop{
      remote.ip, InterfaceID(testInterface.id), ECMP_WEIGHT, action};
}

std::shared_ptr<Route<folly::IPAddressV4>> ManagerTestBase::makeRoute(
    const TestRoute& route) {
  RouteFields<folly::IPAddressV4>::Prefix destination(
      route.destination.first.asV4(), route.destination.second);
  RouteNextHopEntry::NextHopSet swNextHops{};
  for (const auto& testInterface : route.nextHopInterfaces) {
    swNextHops.emplace(makeNextHop(testInterface));
  }
  RouteNextHopEntry entry(swNextHops, AdminDistance::STATIC_ROUTE);
  auto r = std::make_shared<Route<folly::IPAddressV4>>(
      Route<folly::IPAddressV4>::makeThrift(destination));
  r->update(ClientID{42}, entry);
  facebook::fboss::allocateRouteNextHopIds(nextHopIDManager_.get(), entry);
  r->setResolved(entry);
  return r;
}

std::vector<QueueSaiId> ManagerTestBase::getPortQueueSaiIds(
    const SaiPortHandle* portHandle) {
  SaiPortTraits::Attributes::QosQueueList queueListAttribute;
  auto queueSaiIdList = SaiApiTable::getInstance()->portApi().getAttribute(
      portHandle->port->adapterKey(), queueListAttribute);
  std::vector<QueueSaiId> queueSaiIds;
  for (auto queueSaiId : queueSaiIdList) {
    queueSaiIds.emplace_back(queueSaiId);
  }
  return queueSaiIds;
}

std::shared_ptr<PortQueue> ManagerTestBase::makePortQueue(
    uint8_t queueId,
    cfg::StreamType streamType,
    cfg::QueueScheduling schedType,
    uint8_t weight,
    uint64_t minPps,
    uint64_t maxPps) const {
  auto portQueue = std::make_shared<PortQueue>(queueId);
  std::string queueName = "queue";
  queueName.append(std::to_string(queueId));
  portQueue->setStreamType(streamType);
  portQueue->setName(queueName);
  cfg::PortQueueRate portQueueRate;
  cfg::Range range;
  *range.minimum() = minPps;
  *range.maximum() = maxPps;
  portQueueRate.pktsPerSec() = range;
  portQueue->setPortQueueRate(portQueueRate);
  portQueue->setWeight(weight);
  portQueue->setScheduling(schedType);
  return portQueue;
}

QueueConfig ManagerTestBase::makeQueueConfig(
    std::vector<uint8_t> queueIds,
    cfg::StreamType streamType,
    cfg::QueueScheduling schedType,
    uint8_t weight,
    uint64_t minPps,
    uint64_t maxPps) const {
  QueueConfig queueConfig;
  for (auto queueId : queueIds) {
    auto portQueue =
        makePortQueue(queueId, streamType, schedType, weight, minPps, maxPps);
    queueConfig.push_back(portQueue);
  }
  return queueConfig;
}

std::shared_ptr<QosPolicy> ManagerTestBase::makeQosPolicy(
    const std::string& name,
    const TestQosPolicy& qosPolicy) {
  DscpMap dscpMap;
  ExpMap expMap;
  std::map<int16_t, int16_t> tc2q;
  for (const auto& qosMapping : qosPolicy) {
    auto [dscp, tc, q] = qosMapping;
    dscpMap.addFromEntry(TrafficClass{tc}, DSCP{dscp});
    tc2q[TrafficClass(tc)] = q;
  }
  return std::make_shared<QosPolicy>(name, dscpMap, expMap, tc2q);
}

void ManagerTestBase::applyNewState(
    const std::shared_ptr<SwitchState>& newState) {
  auto oldState = saiPlatform->getHwSwitch()->getProgrammedState();
  std::vector<StateDelta> deltas;
  deltas.emplace_back(oldState, newState);
  EXPECT_TRUE(saiPlatform->getHwSwitch()->isValidStateUpdate(deltas.back()));
  saiPlatform->getHwSwitch()->stateChanged(deltas);
  programmedState = newState;
  programmedState->publish();
}

std::shared_ptr<SwitchState> ManagerTestBase::getProgrammedState() {
  auto state = saiPlatform->getHwSwitch()->getProgrammedState()->clone();
  facebook::fboss::populateFibInfoIdMaps(nextHopIDManager_.get(), state);
  state->publish();
  return state;
}

const SwitchIdScopeResolver& ManagerTestBase::scopeResolver() const {
  CHECK(resolver);
  return *resolver;
}
} // namespace facebook::fboss
