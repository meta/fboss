/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/TunManager.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockHwSwitch.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/hw/mock/MockPlatformMapping.h"
#include "fboss/agent/hw/mock/MockTestHandle.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/RouteNextHop.h"

#include "fboss/agent/single/MonolithicHwSwitchHandler.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/MockTunManager.h"

#include "fboss/agent/Constants.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"

#include "fboss/agent/Utils.h"

#include <fmt/core.h>
#include <folly/Memory.h>
#include <folly/container/Enumerate.h>
#include <chrono>
#include <memory>
#include <optional>

using folly::ByteRange;
using folly::IOBuf;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::StringPiece;
using folly::io::Cursor;
using std::make_shared;
using std::make_unique;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

using namespace facebook::fboss;

namespace {

constexpr auto kSysPortRangeMin = 1000;

void initSwSwitchWithFlags(SwSwitch* sw, SwitchFlags flags) {
  if (flags & SwitchFlags::ENABLE_TUN) {
    // TODO(aeckert): I don't think this should be a first class
    // argument to SwSwitch::init() as unit tests are the only place
    // that pass in a TunManager to init(). Let's come up with a way
    // to mock the TunManager initialization instead of passing it in
    // like this.
    //
    // TODO(aeckert): Have MockTunManager hit the real TunManager
    // implementation if testing on actual hw
    auto mockTunMgr =
        std::make_unique<MockTunManager>(sw, sw->getBackgroundEvb());
    EXPECT_CALL(*mockTunMgr.get(), doProbe(_)).Times(1);
    sw->init(std::move(mockTunMgr), mockHwSwitchInitFn(sw), flags);
  } else {
    sw->init(nullptr, mockHwSwitchInitFn(sw), flags);
  }
}

std::pair<unique_ptr<SwSwitch>, std::vector<unique_ptr<MockPlatform>>>
createMockSw(
    const shared_ptr<SwitchState>& state,
    SwitchFlags flags,
    cfg::SwitchConfig* config) {
  std::vector<unique_ptr<MockPlatform>> platforms;

  cfg::SwitchConfig switchCfg{};
  if (config) {
    switchCfg = *config;
  }

  std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo;
  if (state) {
    for (const auto& entry : std::as_const(*(state->getSwitchSettings()))) {
      auto matcher = HwSwitchMatcher(entry.first);
      auto setting = entry.second;
      for (auto switchId : matcher.switchIds()) {
        switchIdToSwitchInfo[switchId] =
            createSwitchInfo(setting->getSwitchType(switchId));
      }
    }
  } else {
    switchIdToSwitchInfo[0] = createSwitchInfo(cfg::SwitchType::NPU);
  }
  if (switchCfg.switchSettings()->switchIdToSwitchInfo()->empty()) {
    switchCfg.switchSettings()->switchIdToSwitchInfo() = switchIdToSwitchInfo;
    if (config) {
      *config = switchCfg;
    }
  }

  for (auto id2Info : *(switchCfg.switchSettings()->switchIdToSwitchInfo())) {
    auto switchId = id2Info.first;
    auto switchType = *(id2Info.second.switchType());
    platforms.emplace_back(
        createMockPlatform(switchType, switchId, &switchCfg));
  }
  auto swSwitch = setupMockSwitchWithoutHW(platforms, state, flags, &switchCfg);
  return std::make_pair(std::move(swSwitch), std::move(platforms));
}

shared_ptr<SwitchState> setAllPortState(
    const shared_ptr<SwitchState>& in,
    bool up) {
  auto newState = in->clone();
  auto newPortMaps = newState->getPorts()->modify(&newState);
  auto scopeResolver = SwitchIdScopeResolver(
      utility::getFirstNodeIf(newState->getSwitchSettings())
          ->getSwitchIdToSwitchInfo());
  for (auto portMap : *newPortMaps) {
    for (auto port : *portMap.second) {
      auto newPort = port.second->clone();
      newPort->setOperState(up);
      newPort->setAdminState(
          up ? cfg::PortState::ENABLED : cfg::PortState::DISABLED);
      newPortMaps->updateNode(newPort, scopeResolver.scope(newPort));
    }
  }
  return newState;
}

std::vector<std::string> getLoopbackIps(int64_t switchIdVal) {
  // Starts loopback IP with 200::
  static constexpr auto firstOctetLimit = 56;
  static constexpr auto switchIdLimit = firstOctetLimit * 255;
  CHECK_LE(switchIdVal, switchIdLimit)
      << "SwitchId > " << switchIdLimit << " not supported";
  auto firstOctet = 200 + switchIdVal % firstOctetLimit;
  auto secondOctet = switchIdVal / firstOctetLimit;
  auto v6 = folly::to<std::string>(firstOctet, ":", secondOctet, "::1/64");
  auto v4 = folly::to<std::string>(firstOctet, ".", secondOctet, ".0.1/24");
  return {v6, v4};
}

cfg::Interface getRecyclePortRif(
    const cfg::DsfNode& myNode,
    const cfg::SwitchConfig& cfg) {
  cfg::Interface recyclePortRif;
  recyclePortRif.intfID() = static_cast<uint32_t>(getInbandSystemPortID(
      *cfg.switchSettings()->switchIdToSwitchInfo(),
      SwitchID(*myNode.switchId())));
  recyclePortRif.type() = cfg::InterfaceType::SYSTEM_PORT;
  for (const auto& address : getLoopbackIps(*myNode.switchId())) {
    recyclePortRif.ipAddresses()->push_back(address);
  }
  return recyclePortRif;
}

void addRecyclePortRif(const cfg::DsfNode& myNode, cfg::SwitchConfig& cfg) {
  cfg::Interface recyclePortRif = getRecyclePortRif(myNode, cfg);
  cfg.interfaces()->push_back(recyclePortRif);
}

cfg::SwitchConfig testConfigAImpl(
    bool isMhnic,
    cfg::SwitchType switchType,
    cfg::AsicType asicType) {
  if (switchType == cfg::SwitchType::FABRIC) {
    return testConfigFabricSwitch();
  }
  cfg::SwitchConfig cfg;
  static constexpr auto kPortCount = 20;
  cfg.ports()->resize(kPortCount);
  // For VOQ switches reserve port id 1 for recycle port
  const auto kInterfacePortIdBegin =
      (switchType == cfg::SwitchType::VOQ ? 5 : 1);
  for (int p = 0; p < kPortCount; ++p) {
    cfg.ports()[p].logicalID() = p + kInterfacePortIdBegin;
    cfg.ports()[p].name() = folly::to<string>("port", p + 1);
    if (isMhnic) {
      cfg.ports()[p].state() = cfg::PortState::ENABLED;
      cfg.ports()[p].lookupClasses() = {
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4,
      };
      cfg.ports()[p].speed() = cfg::PortSpeed::TWENTYFIVEG;
      cfg.ports()[p].profileID() =
          cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER;
    } else {
      bool isControllingPort = (p % 4 == 0);
      if (isControllingPort) {
        cfg.ports()[p].state() = cfg::PortState::ENABLED;
        cfg.ports()[p].speed() = cfg::PortSpeed::HUNDREDG;
        cfg.ports()[p].profileID() =
            cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER;
      } else {
        cfg.ports()[p].state() = cfg::PortState::DISABLED;
        cfg.ports()[p].speed() = cfg::PortSpeed::TWENTYFIVEG;
        cfg.ports()[p].profileID() =
            cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER;
      }
    }
  }

  if (switchType == cfg::SwitchType::NPU) {
    cfg.vlans()->resize(2);
    cfg.vlans()[0].id() = 1;
    cfg.vlans()[0].name() = "Vlan1";
    cfg.vlans()[0].intfID() = 1;
    cfg.vlans()[1].id() = 55;
    cfg.vlans()[1].name() = "Vlan55";
    cfg.vlans()[1].intfID() = 55;

    cfg.vlanPorts()->resize(20);
    for (int p = 0; p < kPortCount; ++p) {
      cfg.vlanPorts()[p].logicalPort() = p + kInterfacePortIdBegin;
      cfg.vlanPorts()[p].vlanID() = p < 10 + kInterfacePortIdBegin ? 1 : 55;
    }
    cfg.interfaces()->resize(2);
    cfg.interfaces()[0].intfID() = 1;
    cfg.interfaces()[0].routerID() = 0;
    cfg.interfaces()[0].vlanID() = 1;
    cfg.interfaces()[0].name() = "eth1/5/1";
    cfg.interfaces()[0].mac() = "00:02:00:00:00:01";
    cfg.interfaces()[0].mtu() = 9000;
    cfg.interfaces()[0].ipAddresses()->resize(4);
    cfg.interfaces()[0].ipAddresses()[0] = "10.0.0.1/24";
    cfg.interfaces()[0].ipAddresses()[1] = "192.168.0.1/24";
    cfg.interfaces()[0].ipAddresses()[2] = "2401:db00:2110:3001::0001/64";
    cfg.interfaces()[0].ipAddresses()[3] = "fe80::/64"; // link local

    cfg.interfaces()[1].intfID() = 55;
    cfg.interfaces()[1].routerID() = 0;
    cfg.interfaces()[1].vlanID() = 55;
    cfg.interfaces()[1].name() = "eth1/6/1";
    cfg.interfaces()[1].mac() = "00:02:00:00:00:55";
    cfg.interfaces()[1].mtu() = 9000;
    cfg.interfaces()[1].ipAddresses()->resize(4);
    cfg.interfaces()[1].ipAddresses()[0] = "10.0.55.1/24";
    cfg.interfaces()[1].ipAddresses()[1] = "192.168.55.1/24";
    cfg.interfaces()[1].ipAddresses()[2] = "2401:db00:2110:3055::0001/64";
    cfg.interfaces()[1].ipAddresses()[3] = "169.254.0.0/16"; // link local
    cfg.switchSettings()->switchIdToSwitchInfo() = {
        std::make_pair(0, createSwitchInfo(cfg::SwitchType::NPU))};
  } else {
    if (switchType == cfg::SwitchType::VOQ) {
      // Add config for VOQ DsfNode
      cfg::DsfNode myNode = makeDsfNodeCfg(kVoqSwitchIdBegin);
      cfg.dsfNodes()->insert({*myNode.switchId(), myNode});
      cfg.interfaces()->resize(kPortCount);
      CHECK(!myNode.systemPortRanges()->systemPortRanges()->empty());
      CHECK_EQ(myNode.systemPortRanges()->systemPortRanges()->size(), 1);
      auto sysPortRange =
          *myNode.systemPortRanges()->systemPortRanges()->begin();
      cfg.switchSettings()->switchIdToSwitchInfo() = {std::make_pair(
          kVoqSwitchIdBegin,
          createSwitchInfo(
              cfg::SwitchType::VOQ,
              asicType,
              cfg::switch_config_constants::
                  DEFAULT_PORT_ID_RANGE_MIN(), /* port id range min */
              cfg::switch_config_constants::
                  DEFAULT_PORT_ID_RANGE_MAX(), /* port id range max */
              0, /* switchIndex */
              *sysPortRange.minimum(),
              *sysPortRange.maximum(),
              "02:00:00:00:0F:0B", /* switchMac */
              "68:00" /* connection handle */))};
      for (auto i = 0; i < kPortCount; ++i) {
        auto intfId = *cfg.ports()[i].logicalID() + *sysPortRange.minimum();
        cfg.interfaces()[i].intfID() = intfId;
        cfg.interfaces()[i].routerID() = 0;
        cfg.interfaces()[i].type() = cfg::InterfaceType::SYSTEM_PORT;
        cfg.interfaces()[i].name() =
            fmt::format("eth1/{}/1", *cfg.ports()[i].logicalID());
        cfg.interfaces()[i].mac() = "00:02:00:00:00:55";
        cfg.interfaces()[i].mtu() = 9000;
        cfg.interfaces()[i].ipAddresses()->resize(2);
        cfg.interfaces()[i].ipAddresses()[0] = fmt::format(
            "2401:db00:2110:30{:02d}::1/64", *cfg.ports()[i].logicalID());
        cfg.interfaces()[i].ipAddresses()[1] =
            fmt::format("10.0.{}.1/24", *cfg.ports()[i].logicalID());
      }
      cfg::Port recyclePort;
      recyclePort.logicalID() = 1;
      recyclePort.name() = "rcy1/1/1";
      recyclePort.speed() = cfg::PortSpeed::HUNDREDG;
      recyclePort.profileID() =
          cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER;
      recyclePort.portType() = cfg::PortType::RECYCLE_PORT;
      cfg.ports()->push_back(recyclePort);
      addRecyclePortRif(myNode, cfg);
      // Add a fabric node to DSF node as well. In prod DsfNode map will have
      // both IN and FN nodes
      auto fnNode =
          makeDsfNodeCfg(kFabricSwitchIdBegin, cfg::DsfNodeType::FABRIC_NODE);
      cfg.dsfNodes()->insert({*fnNode.switchId(), fnNode});
    }
  }

  return cfg;
}

cfg::SwitchConfig testConfigBImpl() {
  cfg::SwitchConfig cfg;
  static constexpr auto kPortCount = 16;
  static constexpr auto kPortCountPerNpu = 8;
  /*
   * voq1 recycle rif [ 1(2,3,4)]
   * voq1 [ 5(6,7,8), 9(10,11,12)]
   * unused [13, 14, 15, 16]
   * voq2 recycle rif [ 17(18,19,20)]
   * voq2 [21(22,23,24), 25(26,27, 28)]
   */

  for (auto p = 0; p < kPortCount; p++) {
    auto switchIndex = (p < kPortCountPerNpu) ? 0 : 1;
    auto startOffset = (switchIndex == 0) ? 5 : 21;
    auto portID = (p % kPortCountPerNpu) + startOffset;
    cfg::Port port{};
    port.logicalID() = portID;
    bool isControllingPort = (p % 4 == 0);
    if (isControllingPort) {
      port.state() = cfg::PortState::ENABLED;
      port.speed() = cfg::PortSpeed::HUNDREDG;
      port.profileID() = cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER;
    } else {
      port.state() = cfg::PortState::DISABLED;
      port.speed() = cfg::PortSpeed::TWENTYFIVEG;
      port.profileID() = cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER;
    }
    cfg.ports()->push_back(port);
  }

  std::vector<cfg::Port> recyclePorts;
  std::vector<cfg::Interface> recycleIntfs;
  for (auto switchIndex = 0; switchIndex < 2; ++switchIndex) {
    auto switchId = switchIndex + 1;
    auto minPort = (switchIndex == 0) ? 0 : 16;
    auto maxPort = (switchIndex == 0) ? 12 : 28;
    cfg::DsfNode myNode = makeDsfNodeCfg(switchId);
    myNode.inbandPortId() = minPort + 1;
    cfg.dsfNodes()->insert({*myNode.switchId(), myNode});
    CHECK(!myNode.systemPortRanges()->systemPortRanges()->empty());
    auto sysPortRange = *myNode.systemPortRanges()->systemPortRanges()->begin();
    cfg.switchSettings()->switchIdToSwitchInfo()->emplace(
        std::make_pair(
            switchId,
            createSwitchInfo(
                cfg::SwitchType::VOQ,
                cfg::AsicType::ASIC_TYPE_MOCK,
                minPort, /* port id range min */
                maxPort, /* port id range max */
                switchIndex, /* switchIndex */
                *sysPortRange.minimum(),
                *sysPortRange.maximum(),
                "02:00:00:00:0F:0B", /* switchMac */
                "68:00" /* connection handle */)));
  }

  auto switchId2SwitchInfo = *cfg.switchSettings()->switchIdToSwitchInfo();

  for (auto switchIndex = 0; switchIndex < 2; ++switchIndex) {
    auto minPort = (switchIndex == 0) ? 0 : 16;
    cfg::DsfNode myNode = cfg.dsfNodes()->at(switchIndex + 1);
    auto beginPortIndex = switchIndex * kPortCountPerNpu;
    auto endPortIndex = beginPortIndex + kPortCountPerNpu;
    for (auto index = beginPortIndex; index < endPortIndex; index++) {
      const auto& port = cfg.ports()->at(index);
      cfg::Interface intf;
      auto intfId = getSystemPortID(
          PortID(*port.logicalID()),
          *port.scope(),
          switchId2SwitchInfo,
          SwitchID(switchIndex + 1));
      XLOG(INFO) << "Port id : " << *port.logicalID()
                 << ", intf id : " << intfId;
      intf.intfID() = static_cast<int>(intfId);
      intf.routerID() = 0;
      intf.type() = cfg::InterfaceType::SYSTEM_PORT;
      intf.name() = fmt::format("fboss{}", static_cast<int>(intfId));
      intf.mac() = "00:02:00:00:00:55";
      intf.mtu() = 9000;
      intf.ipAddresses()->resize(2);
      intf.ipAddresses()[0] =
          fmt::format("2401:db00:2110:30{:02x}::1/64", *port.logicalID());
      intf.ipAddresses()[1] = fmt::format("10.0.{}.1/24", *port.logicalID());
      cfg.interfaces()->push_back(intf);
    }

    cfg::Port recyclePort;
    recyclePort.logicalID() = minPort + 1;
    recyclePort.name() = fmt::format("rcy{}/1/1", switchIndex + 1);
    recyclePort.speed() = cfg::PortSpeed::HUNDREDG;
    recyclePort.profileID() =
        cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER;
    recyclePort.portType() = cfg::PortType::RECYCLE_PORT;

    recyclePorts.push_back(recyclePort);
    auto recycleRif = getRecyclePortRif(myNode, cfg);
    recycleIntfs.push_back(recycleRif);
  }
  for (auto recyclePort : recyclePorts) {
    cfg.ports()->push_back(recyclePort);
  }
  for (auto recycleIntf : recycleIntfs) {
    cfg.interfaces()->push_back(recycleIntf);
  }

  // Add a fabric node to DSF node as well. In prod DsfNode map will have
  // both IN and FN nodes
  auto fnNode =
      makeDsfNodeCfg(kFabricSwitchIdBegin, cfg::DsfNodeType::FABRIC_NODE);
  cfg.dsfNodes()->insert({*fnNode.switchId(), fnNode});

  return cfg;
}
} // namespace

namespace facebook::fboss {

std::shared_ptr<SystemPort> makeSysPort(
    const std::optional<std::string>& qosPolicy,
    int64_t sysPortId,
    int64_t switchId) {
  auto sysPort = std::make_shared<SystemPort>(SystemPortID(sysPortId));
  sysPort->setSwitchId(SwitchID(switchId));
  sysPort->setName(fmt::format("sysPort{}", sysPortId));
  sysPort->setCoreIndex(42);
  sysPort->setCorePortIndex(24);
  sysPort->setSpeedMbps(10000);
  sysPort->setNumVoqs(8);
  sysPort->setQosPolicy(qosPolicy);
  sysPort->setScope(cfg::Scope::GLOBAL);
  return sysPort;
}

std::tuple<state::NeighborEntries, state::NeighborEntries> makeNbrs() {
  state::NeighborEntries ndpTable, arpTable;
  std::map<std::string, int> ip2Rif = {
      {"fc00::1", kSysPortRangeMin + 1},
      {"fc01::1", kSysPortRangeMin + 2},
      {"10.0.1.1", kSysPortRangeMin + 1},
      {"10.0.2.1", kSysPortRangeMin + 2},
  };
  for (const auto& [ip, rif] : ip2Rif) {
    state::NeighborEntryFields nbr;
    nbr.ipaddress() = ip;
    nbr.mac() = "01:02:03:04:05:06";
    cfg::PortDescriptor port;
    port.portId() = rif;
    port.portType() = cfg::PortDescriptorType::SystemPort;
    nbr.portId() = port;
    nbr.interfaceId() = rif;
    nbr.isLocal() = true;
    folly::IPAddress ipAddr(ip);
    if (ipAddr.isV6()) {
      ndpTable.insert({ip, nbr});
    } else {
      arpTable.insert({ip, nbr});
    }
  }
  return std::make_pair(ndpTable, arpTable);
}

cfg::DsfNode makeDsfNodeCfg(
    int64_t switchId,
    cfg::DsfNodeType type,
    std::optional<int> clusterId,
    cfg::AsicType asicType,
    std::optional<int> fabricLevel) {
  cfg::DsfNode dsfNodeCfg;
  dsfNodeCfg.switchId() = switchId;
  dsfNodeCfg.name() = fmt::format("dsfNodeCfg{}", switchId);
  dsfNodeCfg.type() = type;
  if (type == cfg::DsfNodeType::INTERFACE_NODE) {
    const auto kBlockSize{100};
    cfg::Range64 sysPortRange;
    sysPortRange.minimum() = switchId * kBlockSize;
    sysPortRange.maximum() = switchId * kBlockSize + kBlockSize;
    dsfNodeCfg.loopbackIps() = getLoopbackIps(switchId);
    dsfNodeCfg.nodeMac() = "02:00:00:00:0F:0B";
    dsfNodeCfg.localSystemPortOffset() = *sysPortRange.minimum();
    dsfNodeCfg.globalSystemPortOffset() = *sysPortRange.minimum();
    dsfNodeCfg.systemPortRanges()->systemPortRanges()->push_back(sysPortRange);
    dsfNodeCfg.localSystemPortRanges()->systemPortRanges()->push_back(
        sysPortRange);
    dsfNodeCfg.inbandPortId() = kSingleStageInbandPortId;
  }
  dsfNodeCfg.asicType() = asicType;
  dsfNodeCfg.platformType() = type == cfg::DsfNodeType::INTERFACE_NODE
      ? PlatformType::PLATFORM_MERU800BIA
      : PlatformType::PLATFORM_MERU800BFA;
  if (clusterId.has_value()) {
    dsfNodeCfg.clusterId() = clusterId.value();
  }
  if (fabricLevel.has_value()) {
    CHECK_EQ(
        static_cast<int>(type),
        static_cast<int>(cfg::DsfNodeType::FABRIC_NODE));
    dsfNodeCfg.fabricLevel() = fabricLevel.value();
  }
  return dsfNodeCfg;
}

cfg::SwitchConfig testConfigA(
    cfg::SwitchType switchType,
    cfg::AsicType asicType) {
  return testConfigAImpl(false, switchType, asicType);
}

cfg::SwitchConfig testConfigFabricSwitch(
    bool dualStage,
    int fabricLevel,
    int parallLinkPerNode,
    std::optional<int> dualStageNeighborLevel2FabricNodes) {
  static constexpr auto kPortCount = 20;
  static constexpr auto switchIdGap = 4;

  // FabricLevel: FDSW - 1, SDSW - 2. Also ensure config is dual stage if
  // fabricLevel is 2.
  CHECK(fabricLevel == 1 || (fabricLevel == 2 && dualStage));
  // If dual stage and fabricLevel == 1, dualStageLevel2NeighborFabricNodes is
  // needed, and no larger than the total number of neighbor nodes.
  if (dualStage && fabricLevel == 1) {
    CHECK(dualStageNeighborLevel2FabricNodes.has_value());
    CHECK_LE(
        dualStageNeighborLevel2FabricNodes.value(),
        kPortCount / parallLinkPerNode);
  }

  cfg::SwitchConfig cfg;
  cfg.ports()->resize(kPortCount);
  cfg.switchSettings()->switchIdToSwitchInfo() = {std::make_pair(
      kFabricSwitchIdBegin,
      createSwitchInfo(
          cfg::SwitchType::FABRIC,
          cfg::AsicType::ASIC_TYPE_MOCK,
          0, /* port id range min */
          1023, /* port id range max */
          0, /* switchIndex */
          std::nullopt, /* systemPort min */
          std::nullopt, /* systemPort max */
          std::nullopt, /* switchMac */
          "68:00" /* connection handle */))};

  for (int p = 0; p < kPortCount; ++p) {
    cfg.ports()[p].logicalID() = p + 1;
    cfg.ports()[p].name() = folly::to<string>("eth1/", p + 1, "/1");
    cfg.ports()[p].state() = cfg::PortState::ENABLED;
    cfg.ports()[p].speed() = cfg::PortSpeed::TWENTYFIVEG;
    cfg.ports()[p].profileID() =
        cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER;
    cfg.ports()[p].portType() = cfg::PortType::FABRIC_PORT;
  }
  auto myNode = makeDsfNodeCfg(
      kFabricSwitchIdBegin,
      cfg::DsfNodeType::FABRIC_NODE,
      std::nullopt,
      cfg::AsicType::ASIC_TYPE_RAMON3,
      fabricLevel);
  cfg.dsfNodes()->insert({*myNode.switchId(), myNode});

  auto nextFabricSwitchId = kFabricSwitchIdBegin + switchIdGap;
  auto nextVoqSwitchId = kVoqSwitchIdBegin;

  for (int i = 0; i < kPortCount / parallLinkPerNode; i++) {
    // Single Stage - All nodes are interface node with no clusterId
    // Dual Stage FDSW - dualStageNeighborLevel2FabricNodes fabric node and rest
    // are interface node with same cluterId. Dual Stage SDSW = all farbic node
    // with different clusterId
    std::optional<int> clusterId;
    std::optional<int> remoteFabricLevel;
    if (dualStage) {
      if (fabricLevel == 2) {
        clusterId = i + 1;
        remoteFabricLevel = 1;
      } else {
        if (i < dualStageNeighborLevel2FabricNodes.value()) {
          // Fabric nodes
          remoteFabricLevel = 2;
        } else {
          // Interface nodes
          clusterId = 1;
        }
      }
    }

    cfg::DsfNodeType remoteDsfNodeType;
    if (!dualStage) {
      remoteDsfNodeType = cfg::DsfNodeType::INTERFACE_NODE;
    } else if (fabricLevel == 1) {
      remoteDsfNodeType = i < dualStageNeighborLevel2FabricNodes.value()
          ? cfg::DsfNodeType::FABRIC_NODE
          : cfg::DsfNodeType::INTERFACE_NODE;
    } else {
      remoteDsfNodeType = cfg::DsfNodeType::FABRIC_NODE;
    }

    auto asicType = remoteDsfNodeType == cfg::DsfNodeType::FABRIC_NODE
        ? cfg::AsicType::ASIC_TYPE_RAMON3
        : cfg::AsicType::ASIC_TYPE_JERICHO3;
    cfg::DsfNode remoteNode = makeDsfNodeCfg(
        remoteDsfNodeType == cfg::DsfNodeType::FABRIC_NODE ? nextFabricSwitchId
                                                           : nextVoqSwitchId,
        remoteDsfNodeType,
        clusterId,
        asicType,
        remoteDsfNodeType == cfg::DsfNodeType::FABRIC_NODE ? remoteFabricLevel
                                                           : std::nullopt);
    cfg.dsfNodes()->insert({*remoteNode.switchId(), remoteNode});

    if (remoteDsfNodeType == cfg::DsfNodeType::FABRIC_NODE) {
      nextFabricSwitchId += switchIdGap;
    } else {
      nextVoqSwitchId += switchIdGap;
    }

    for (int j = 0; j < parallLinkPerNode; j++) {
      cfg::PortNeighbor portNeighbor;
      portNeighbor.remoteSystem() = *remoteNode.name();
      portNeighbor.remotePort() = folly::to<string>("fab1/", j + 1, "/2");
      auto portIdx = i * parallLinkPerNode + j;
      cfg.ports()[portIdx].expectedNeighborReachability() = {portNeighbor};
    }
  }

  // Insert one more node in different cluster, such that reachability group
  // will be processed as dual stage.
  if (dualStage && parallLinkPerNode == 1) {
    cfg::DsfNode cluster2Node = makeDsfNodeCfg(
        nextVoqSwitchId, cfg::DsfNodeType::INTERFACE_NODE, 2 /* clusterId */);
    cfg.dsfNodes()->insert({*cluster2Node.switchId(), cluster2Node});

    nextVoqSwitchId += switchIdGap;
  }
  return cfg;
}

cfg::SwitchConfig testConfigB() {
  return testConfigBImpl();
}

cfg::SwitchConfig testConfigAWithLookupClasses() {
  return testConfigAImpl(
      true, cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK);
}

cfg::SwitchConfig testConfigAWithPortInterfaces() {
  auto config = testConfigA(cfg::SwitchType::NPU);

  config.defaultVlan() = 1;

  cfg::Vlan vlan;
  vlan.id() = 1;
  vlan.name() = "Vlan1";

  config.vlans()->clear();
  config.vlans()->push_back(vlan);

  config.interfaces()->clear();
  config.vlanPorts()->clear();

  for (auto& port : *config.ports()) {
    cfg::Interface intf;
    intf.intfID() = 6000 + *(port.logicalID());
    intf.vlanID() = 0;
    intf.portID() = *port.logicalID();
    intf.routerID() = 0;
    intf.type() = cfg::InterfaceType::PORT;
    intf.name() = fmt::format("fboss{}", *intf.intfID());
    intf.mtu() = 9000;
    intf.mac() = "00:02:00:00:00:55";
    intf.ipAddresses()->resize(2);
    intf.ipAddresses()[0] =
        fmt::format("2601:db00:2110:30{:02x}::1/64", *port.logicalID());
    intf.ipAddresses()[1] = fmt::format("100.0.{}.1/24", *port.logicalID());

    config.interfaces()->push_back(intf);
  }
  return config;
}

cfg::SwitchConfig testConfigAWithAggregatePortInterface() {
  auto config = testConfigAWithPortInterfaces();
  auto member0 = *config.ports()[0].logicalID();
  auto member1 = *config.ports()[1].logicalID();

  cfg::AggregatePort aggPort;
  aggPort.key() = kAggregatePortKey;
  aggPort.name() = "agg";
  for (auto memberPort : {member0, member1}) {
    cfg::AggregatePortMember member;
    member.memberPortID() = memberPort;
    aggPort.memberPorts()->push_back(member);
  }
  config.aggregatePorts()->push_back(aggPort);

  auto& intfs = *config.interfaces();
  intfs.erase(
      std::remove_if(
          intfs.begin(),
          intfs.end(),
          [member0, member1](const auto& intf) {
            return intf.portID() == member0 || intf.portID() == member1;
          }),
      intfs.end());

  cfg::Interface aggIntf;
  aggIntf.intfID() = kAggregatePortInterfaceID;
  aggIntf.vlanID() = 0;
  aggIntf.aggregatePortID() = kAggregatePortKey;
  aggIntf.routerID() = 0;
  aggIntf.type() = cfg::InterfaceType::PORT;
  aggIntf.name() = "fbossAgg";
  aggIntf.mtu() = 9000;
  aggIntf.mac() = "00:02:00:00:00:66";
  aggIntf.ipAddresses()->resize(2);
  // Deliberately outside the 2601:db00:2110:30xx / 100.0.x prefixes the port
  // interfaces above take, so the aggregate is reachable on its own subnet.
  aggIntf.ipAddresses()[0] = "2601:db00:2110:3100::1/64";
  aggIntf.ipAddresses()[1] = "100.0.100.1/24";
  intfs.push_back(aggIntf);

  return config;
}

shared_ptr<SwitchState> bringAllPortsUp(const shared_ptr<SwitchState>& in) {
  return setAllPortState(in, true);
}
shared_ptr<SwitchState> bringAllPortsDown(const shared_ptr<SwitchState>& in) {
  return setAllPortState(in, false);
}

shared_ptr<SwitchState> removeVlanIPv4Address(
    const shared_ptr<SwitchState>& in,
    VlanID vlanID) {
  auto newState = in->clone();
  auto intfInVlan = newState->getInterfaces()->getInterfaceInVlanIf(vlanID);
  if (intfInVlan) {
    auto intf = intfInVlan->modify(&newState);
    if (intf->getVlanID() == vlanID) {
      Interface::Addresses newAddresses;
      for (auto iter : std::as_const(*intf->getAddresses())) {
        auto address = folly::IPAddress(iter.first);
        auto mask = iter.second->cref();
        if (address.isV6()) {
          newAddresses.emplace(address, mask);
        }
      }
      intf->setAddresses(newAddresses);
    }
  }
  return newState;
}

/*
 * Applies the config to a switch and returns the new state.
 * This helper also does fixup of config if some of the needed
 * fields such as switchInfo is missing.
 */
shared_ptr<SwitchState> publishAndApplyConfig(
    const shared_ptr<SwitchState>& state,
    cfg::SwitchConfig* config,
    const Platform* platform,
    RoutingInformationBase* rib,
    PlatformMapping* platformMapping) {
  if (config->switchSettings()->switchIdToSwitchInfo()->empty()) {
    config->switchSettings()->switchIdToSwitchInfo() = {
        {0, createSwitchInfo(cfg::SwitchType::NPU)}};
  }
  return publishAndApplyConfig(
      state, (const cfg::SwitchConfig*)config, platform, rib, platformMapping);
}

shared_ptr<SwitchState> publishAndApplyConfig(
    const shared_ptr<SwitchState>& state,
    const cfg::SwitchConfig* config,
    const Platform* platform,
    RoutingInformationBase* rib,
    PlatformMapping* platformMapping) {
  state->publish();

  // Create a temp mock platform mapping if none is passed in.
  std::unique_ptr<MockPlatformMapping> mockPlatformMapping;
  if (platformMapping == nullptr) {
    mockPlatformMapping = std::make_unique<MockPlatformMapping>();
    platformMapping = mockPlatformMapping.get();
  }

  auto hwAsicTable = HwAsicTable(
      config->switchSettings()->switchIdToSwitchInfo()->size()
          ? *config->switchSettings()->switchIdToSwitchInfo()
          : std::map<int64_t, cfg::SwitchInfo>(
                {{0, createSwitchInfo(cfg::SwitchType::NPU)}}),
      std::nullopt,
      *config->dsfNodes());
  return applyThriftConfig(
      state,
      config,
      platform->supportsAddRemovePort(),
      platformMapping,
      &hwAsicTable,
      rib);
}

std::unique_ptr<SwSwitch> setupMockSwitchWithoutHW(
    MockPlatform* platform,
    const std::shared_ptr<SwitchState>& state,
    SwitchFlags flags,
    cfg::SwitchConfig* config) {
  // Since we are just applying a initial state and to created
  // switch, set initial config to empty.
  auto platformMapping = std::make_unique<MockPlatformMapping>();
  cfg::SwitchConfig emptyConfig;
  if (!config) {
    config = &emptyConfig;
  }

  int64_t switchId;
  cfg::SwitchType switchType;

  if (config->switchSettings()->switchIdToSwitchInfo()->empty()) {
    if (state &&
        utility::getFirstNodeIf(state->getSwitchSettings())
            ->getSwitchIdToSwitchInfo()
            .size()) {
      switchId = HwSwitchMatcher(state->getSwitchSettings()->cbegin()->first)
                     .switchId();
      switchType =
          state->getSwitchSettings()->cbegin()->second->getSwitchType(switchId);
      config->switchSettings()->switchIdToSwitchInfo() =
          utility::getFirstNodeIf(state->getSwitchSettings())
              ->getSwitchIdToSwitchInfo();
    } else {
      switchType = cfg::SwitchType::NPU;
      switchId = 0;
      config->switchSettings()->switchIdToSwitchInfo() = {
          std::make_pair(switchId, createSwitchInfo(switchType))};
    }
  } else {
    switchId =
        config->switchSettings()->switchIdToSwitchInfo()->cbegin()->first;
    switchType = *config->switchSettings()
                      ->switchIdToSwitchInfo()
                      ->cbegin()
                      ->second.switchType();
  }
  auto agentConfig = createEmptyAgentConfig()->thrift;
  agentConfig.sw() = *config;
  platform->setConfig(std::make_unique<AgentConfig>(agentConfig));
  HwSwitchHandlerInitFn hwSwitchHandlerInitFn =
      [platform](
          const SwitchID& switchId, const cfg::SwitchInfo& info, SwSwitch* sw) {
        return std::make_unique<facebook::fboss::MonolithicHwSwitchHandler>(
            platform, switchId, info, sw);
      };
  HwInitResult ret;
  ret.switchState = state ? state : make_shared<SwitchState>();
  auto sw = make_unique<SwSwitch>(
      std::move(hwSwitchHandlerInitFn),
      std::move(platformMapping),
      platform->getDirectoryUtil(),
      platform->supportsAddRemovePort(),
      platform->config(),
      ret.switchState);
  sw->setConfig(std::make_unique<AgentConfig>(agentConfig));
  ret.bootType = BootType::COLD_BOOT;
  std::map<int32_t, state::RouteTableFields> routeTables{};
  auto switchInfo =
      config->switchSettings()->switchIdToSwitchInfo()->begin()->second;
  if (*switchInfo.switchType() == cfg::SwitchType::NPU ||
      *switchInfo.switchType() == cfg::SwitchType::VOQ) {
    routeTables.emplace(kDefaultVrf, state::RouteTableFields{});
  }
  ret.rib = RoutingInformationBase::fromThrift(
      routeTables, getEcmpWidth(ret.switchState));
  getMockHw(sw)->setInitialState(ret.switchState);
  EXPECT_HW_CALL(sw, initImpl(_, _, false))
      .WillOnce(Return(ByMove(std::move(ret))));
  // return same as ret.BootType
  EXPECT_HW_CALL(sw, getBootType).WillRepeatedly(Return(BootType::COLD_BOOT));
  initSwSwitchWithFlags(sw.get(), flags);
  waitForStateUpdates(sw.get());
  return sw;
}

std::unique_ptr<SwSwitch> setupMockSwitchWithoutHW(
    const std::vector<std::unique_ptr<MockPlatform>>& platforms,
    const std::shared_ptr<SwitchState>& state,
    SwitchFlags flags,
    cfg::SwitchConfig* config) {
  if (platforms.size() == 1) {
    return setupMockSwitchWithoutHW(platforms[0].get(), state, flags, config);
  }

  std::set<SwitchID> switchIds;
  for (const auto& platform : platforms) {
    switchIds.insert(SwitchID(platform->getAsic()->getSwitchId().value()));
  }
  auto swSwitch = createSwSwitchWithMultiSwitch(
      platforms[0]->getConfig(), platforms[0]->getDirectoryUtil());
  swSwitch->getHwSwitchHandler()->start();
  for (auto switchId : switchIds) {
    swSwitch->getHwSwitchHandler()->connected(switchId);
  }
  cfg::AgentConfig thrift;
  thrift.sw() = *config;
  auto multiSwitch =
      std::make_pair<std::string, std::string>("multi_switch", "true");
  thrift.defaultCommandLineArgs()->emplace(std::move(multiSwitch));
  swSwitch->setConfig(std::make_unique<AgentConfig>(thrift));
  swSwitch->init(HwWriteBehavior::WRITE, SwitchFlags::DEFAULT);
  swSwitch->applyConfig("initial config", *config);
  swSwitch->initialConfigApplied(std::chrono::steady_clock::now());
  return swSwitch;
}

unique_ptr<MockPlatform> createMockPlatform(
    cfg::SwitchType switchType,
    int64_t switchId,
    cfg::SwitchConfig* config) {
  auto mock = make_unique<testing::NiceMock<MockPlatform>>();
  cfg::AgentConfig thrift;
  if (config) {
    thrift.sw() = *config;
  } else {
    thrift.sw()->switchSettings()->switchIdToSwitchInfo() = {
        std::make_pair(switchId, createSwitchInfo(switchType))};
  }
  auto agentCfg = std::make_unique<AgentConfig>(thrift, "");
  mock->init(
      std::move(agentCfg), 0 /* features  desired*/, 0 /* switchIndex */);
  FLAGS_mac = mock->getLocalMac().toString();
  return std::move(mock);
}

unique_ptr<HwTestHandle> createTestHandle(
    const shared_ptr<SwitchState>& state,
    SwitchFlags flags,
    cfg::SwitchConfig* config) {
  auto [sw, platforms] = createMockSw(state, flags, config);
  auto handle =
      std::make_unique<MockTestHandle>(std::move(sw), std::move(platforms));
  handle->prepareForTesting();
  return handle;
}

unique_ptr<HwTestHandle> createTestHandle(
    cfg::SwitchConfig* config,
    SwitchFlags flags) {
  shared_ptr<SwitchState> initialState{nullptr};
  // Create the initial state, which only has the same number of ports with
  // the init config
  initialState = make_shared<SwitchState>();
  SwitchIdToSwitchInfo switchIdToSwitchInfo;
  if (config) {
    if (config->switchSettings()->switchIdToSwitchInfo()->size()) {
      switchIdToSwitchInfo = *config->switchSettings()->switchIdToSwitchInfo();

    } else {
      switchIdToSwitchInfo.emplace(
          std::make_pair(
              0, createSwitchInfo(*config->switchSettings()->switchType())));
    }
  } else {
    switchIdToSwitchInfo.emplace(
        std::make_pair(0, createSwitchInfo(cfg::SwitchType::NPU)));
  }

  if (config) {
    auto switchId = switchIdToSwitchInfo.begin()->first;
    for (const auto& port : *config->ports()) {
      auto id = *port.logicalID();
      registerPort(
          initialState,
          PortID(id),
          folly::to<string>("port", id),
          HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchId)})),
          *port.portType());
    }
  }

  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setSwitchIdToSwitchInfo(switchIdToSwitchInfo);
  addSwitchInfo(
      initialState,
      *switchIdToSwitchInfo.begin()->second.switchType(),
      switchIdToSwitchInfo.begin()->first);

  auto handle = createTestHandle(initialState, flags, config);
  auto sw = handle->getSw();

  if (config) {
    // Apply the thrift config
    sw->applyConfig("test_setup", *config);
  }
  return handle;
}

MockHwSwitch* getMockHw(SwSwitch* sw) {
  auto handler = boost::polymorphic_downcast<MonolithicHwSwitchHandler*>(
      sw->getHwSwitchHandler()->getHwSwitchHandlers().cbegin()->second);
  return boost::polymorphic_downcast<MockHwSwitch*>(handler->getHwSwitch());
}

MockPlatform* getMockPlatform(SwSwitch* sw) {
  auto handler = boost::polymorphic_downcast<MonolithicHwSwitchHandler*>(
      sw->getHwSwitchHandler()->getHwSwitchHandlers().cbegin()->second);
  return boost::polymorphic_downcast<MockPlatform*>(handler->getPlatform());
}

MockHwSwitch* getMockHw(std::unique_ptr<SwSwitch>& sw) {
  auto handler = boost::polymorphic_downcast<MonolithicHwSwitchHandler*>(
      sw->getHwSwitchHandler()->getHwSwitchHandlers().cbegin()->second);
  return boost::polymorphic_downcast<MockHwSwitch*>(handler->getHwSwitch());
}

MockPlatform* getMockPlatform(std::unique_ptr<SwSwitch>& sw) {
  auto handler = boost::polymorphic_downcast<MonolithicHwSwitchHandler*>(
      sw->getHwSwitchHandler()->getHwSwitchHandlers().cbegin()->second);
  return boost::polymorphic_downcast<MockPlatform*>(handler->getPlatform());
}

std::shared_ptr<SwitchState> waitForStateUpdates(SwSwitch* sw) {
  // empty the updates queue
  sw->getUpdateEvb()->runInFbossEventBaseThreadAndWait([]() {});
  // All StateUpdates scheduled from this thread will be applied in order,
  // so we can simply perform a blocking no-op update.  When it is done
  // we can be sure that all previously scheduled updates have also been
  // applied.
  std::shared_ptr<SwitchState> snapshot{nullptr};
  auto snapshotUpdate = [&snapshot](const shared_ptr<SwitchState>& state)
      -> std::shared_ptr<SwitchState> {
    // take a snapshot of the current state, but don't modify state
    snapshot = state;
    return nullptr;
  };
  sw->updateStateBlocking("waitForStateUpdates", snapshotUpdate);
  return snapshot;
}

void waitForBackgroundThread(SwSwitch* sw) {
  auto* evb = sw->getBackgroundEvb();
  evb->runInFbossEventBaseThreadAndWait([]() { return; });
}

void waitForNeighborCacheThread(SwSwitch* sw) {
  auto* evb = sw->getNeighborCacheEvb();
  evb->runInFbossEventBaseThreadAndWait([]() { return; });
}

void waitForRibUpdates(SwSwitch* sw) {
  sw->getRouteUpdater().program();
}

shared_ptr<SwitchState> testStateA(cfg::SwitchType switchType) {
  // Setup a default state object
  auto state = make_shared<SwitchState>();
  SwitchIdToSwitchInfo switchIdToSwitchInfo;
  if (switchType == cfg::SwitchType::VOQ) {
    switchIdToSwitchInfo.insert(
        std::make_pair(1, createSwitchInfo(switchType)));
  } else if (switchType == cfg::SwitchType::FABRIC) {
    switchIdToSwitchInfo.insert(
        std::make_pair(2, createSwitchInfo(switchType)));
  } else {
    switchIdToSwitchInfo.insert(
        std::make_pair(0, createSwitchInfo(switchType)));
  }
  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setHostname("test.switch");
  switchSettings->setIcmpV4UnavailableSrcAddress(
      folly::IPAddressV4("192.0.2.1"));
  switchSettings->setSwitchIdToSwitchInfo(switchIdToSwitchInfo);
  addSwitchSettingsToState(
      state, switchSettings, switchIdToSwitchInfo.begin()->first);
  HwSwitchMatcher matcher{std::unordered_set<SwitchID>(
      {SwitchID(switchIdToSwitchInfo.begin()->first)})};

  // Add VLAN 1, and ports 1-10 which belong to it.
  if (switchType != cfg::SwitchType::FABRIC) {
    auto vlan1 = make_shared<Vlan>(VlanID(1), std::string("Vlan1"));
    state->getVlans()->addNode(vlan1, matcher);
    for (int idx = 1; idx <= 10; ++idx) {
      registerPort(state, PortID(idx), folly::to<string>("port", idx), matcher);
      vlan1->addPort(PortID(idx), false, false);
      auto port = state->getPorts()->getNodeIf(PortID(idx));
      port->addVlan(vlan1->getID(), false, false);
      port->setInterfaceIDs({1});
    }
    // Add VLAN 55, and ports 11-20 which belong to it.
    auto vlan55 = make_shared<Vlan>(VlanID(55), std::string("Vlan55"));
    state->getVlans()->addNode(vlan55, matcher);
    for (int idx = 11; idx <= 20; ++idx) {
      registerPort(state, PortID(idx), folly::to<string>("port", idx), matcher);
      vlan55->addPort(PortID(idx), false, false);
      auto port = state->getPorts()->getNodeIf(PortID(idx));
      port->addVlan(vlan55->getID(), false, false);
      port->setInterfaceIDs({55});
    }
    // Add Interface 1 to VLAN 1
    auto intf1 = make_shared<Interface>(
        InterfaceID(1),
        RouterID(0),
        std::optional<VlanID>(1),
        folly::StringPiece("fboss1"),
        MacAddress("00:02:00:00:00:01"),
        9000,
        false, /* is virtual */
        false /* is state_sync disabled */);
    Interface::Addresses addrs1;
    addrs1.emplace(IPAddress("10.0.0.1"), 24);
    addrs1.emplace(IPAddress("192.168.0.1"), 24);
    addrs1.emplace(IPAddress("169.254.0.0"), 16); // link local

    addrs1.emplace(IPAddress("2401:db00:2110:3001::0001"), 64);
    addrs1.emplace(IPAddress("fe80::"), 64); // link local

    intf1->setAddresses(addrs1);
    auto allIntfs = state->getInterfaces()->modify(&state);
    allIntfs->addNode(intf1, matcher);
    vlan1->setInterfaceID(InterfaceID(1));

    // Add Interface 55 to VLAN 55
    auto intf55 = make_shared<Interface>(
        InterfaceID(55),
        RouterID(0),
        std::optional<VlanID>(55),
        folly::StringPiece("fboss55"),
        MacAddress("00:02:00:00:00:55"),
        9000,
        false, /* is virtual */
        false /* is state_sync disabled */);
    Interface::Addresses addrs55;
    addrs55.emplace(IPAddress("10.0.55.1"), 24);
    addrs55.emplace(IPAddress("192.168.55.1"), 24);
    addrs55.emplace(IPAddress("2401:db00:2110:3055::0001"), 64);
    intf55->setAddresses(addrs55);
    allIntfs->addNode(intf55, matcher);
    vlan55->setInterfaceID(InterfaceID(55));
  }
  std::optional<int> fabricPortIdxStart;
  if (switchType == cfg::SwitchType::VOQ) {
    fabricPortIdxStart = 21;
  } else if (switchType == cfg::SwitchType::FABRIC) {
    fabricPortIdxStart = 1;
  }
  if (fabricPortIdxStart.has_value()) {
    for (int idx = *fabricPortIdxStart; idx <= *fabricPortIdxStart + 20;
         ++idx) {
      registerPort(
          state,
          PortID(idx),
          folly::to<string>("port", idx),
          matcher,
          cfg::PortType::FABRIC_PORT);
    }
  }
  return state;
}

shared_ptr<SwitchState> testStateAWithPortsUp(cfg::SwitchType switchType) {
  return bringAllPortsUp(testStateA(switchType));
}

shared_ptr<SwitchState> testStateAWithLookupClasses() {
  auto newState = testStateAWithPortsUp()->clone();
  auto newPortMaps = newState->getPorts()->modify(&newState);
  auto scopeResolver = SwitchIdScopeResolver(
      utility::getFirstNodeIf(newState->getSwitchSettings())
          ->getSwitchIdToSwitchInfo());
  for (auto portMap : *newPortMaps) {
    for (auto port : *portMap.second) {
      auto newPort = port.second->clone();
      newPort->setLookupClassesToDistributeTrafficOn({
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4,
      });
      newPortMaps->updateNode(newPort, scopeResolver.scope(newPort));
    }
  }

  return newState;
}

shared_ptr<SwitchState> testStateAWithoutIpv4VlanIntf(VlanID vlanId) {
  return removeVlanIPv4Address(testStateA(), vlanId);
}

shared_ptr<SwitchState> testStateAWithoutIpv4() {
  // Removes IPv4 from EVERY interface on the switch
  auto ret = testStateA();

  auto vlans = ret->getVlans();
  for (auto multiSwitchVlanMap : *vlans) {
    for (auto vlanMap : *multiSwitchVlanMap.second) {
      auto vlan = vlanMap.second;
      ret = removeVlanIPv4Address(ret, vlan->getID());
    }
  }

  return ret;
}

std::string fbossHexDump(const IOBuf* buf) {
  Cursor cursor(buf);
  size_t length = cursor.totalLength();
  if (length == 0) {
    return "";
  }

  string result;
  result.reserve(length * 3);
  result += fmt::format("{:02x}", cursor.read<uint8_t>());
  for (size_t n = 1; n < length; ++n) {
    result += fmt::format(" {:02x}", cursor.read<uint8_t>());
  }

  return result;
}

std::string fbossHexDump(const IOBuf& buf) {
  return fbossHexDump(&buf);
}

std::string fbossHexDump(folly::ByteRange buf) {
  IOBuf iobuf(IOBuf::WRAP_BUFFER, buf);
  return fbossHexDump(&iobuf);
}

std::string fbossHexDump(folly::StringPiece buf) {
  IOBuf iobuf(IOBuf::WRAP_BUFFER, ByteRange(buf));
  return fbossHexDump(&iobuf);
}

std::string fbossHexDump(const string& buf) {
  IOBuf iobuf(IOBuf::WRAP_BUFFER, ByteRange(StringPiece(buf)));
  return fbossHexDump(&iobuf);
}

TxPacketMatcher::TxPacketMatcher(StringPiece name, TxMatchFn fn)
    : name_(name.str()), fn_(std::move(fn)) {}

::testing::Matcher<TxPacketPtr> TxPacketMatcher::createMatcher(
    folly::StringPiece name,
    TxMatchFn&& fn) {
  return ::testing::MakeMatcher(new TxPacketMatcher(name, std::move(fn)));
}

bool TxPacketMatcher::MatchAndExplain(
    TxPacketPtr pkt,
    ::testing::MatchResultListener* l) const {
  try {
    fn_(pkt);
    return true;
  } catch (const std::exception& ex) {
    *l << ex.what();
    return false;
  }
}

void TxPacketMatcher::DescribeTo(std::ostream* os) const {
  *os << name_;
}

void TxPacketMatcher::DescribeNegationTo(std::ostream* os) const {
  *os << "not " << name_;
}

RxPacketMatcher::RxPacketMatcher(
    StringPiece name,
    InterfaceID dstIfID,
    RxMatchFn fn)
    : name_(name.str()), dstIfID_(dstIfID), fn_(std::move(fn)) {}

::testing::Matcher<RxMatchFnArgs> RxPacketMatcher::createMatcher(
    folly::StringPiece name,
    InterfaceID dstIfID,
    RxMatchFn&& fn) {
  return ::testing::MakeMatcher(
      new RxPacketMatcher(name, dstIfID, std::move(fn)));
}

bool RxPacketMatcher::MatchAndExplain(
    RxMatchFnArgs args,
    ::testing::MatchResultListener* l) const {
  auto dstIfID = std::get<0>(args);
  auto pkt = std::get<1>(args);

  try {
    if (dstIfID != dstIfID_) {
      throw FbossError(
          "Mismatching dstIfID. Expected ",
          dstIfID_,
          " but received ",
          dstIfID);
    }

    fn_(pkt.get());
    return true;
  } catch (const std::exception& ex) {
    *l << ex.what();
    return false;
  }
}

void RxPacketMatcher::DescribeTo(std::ostream* os) const {
  *os << name_;
}

void RxPacketMatcher::DescribeNegationTo(std::ostream* os) const {
  *os << "not " << name_;
}

RouteNextHopSet makeNextHops(
    std::vector<std::string> ipStrs,
    std::optional<LabelForwardingAction> mplsAction) {
  RouteNextHopSet nhops;
  for (const std::string& ip : ipStrs) {
    nhops.emplace(UnresolvedNextHop(IPAddress(ip), ECMP_WEIGHT, mplsAction));
  }
  return nhops;
}
RouteNextHopSet makeResolvedNextHops(
    std::vector<std::pair<InterfaceID, std::string>> intfAndIPs,
    uint32_t weight) {
  RouteNextHopSet nhops;
  for (auto intfAndIP : intfAndIPs) {
    nhops.emplace(
        ResolvedNextHop(IPAddress(intfAndIP.second), intfAndIP.first, weight));
  }
  return nhops;
}

ResolvedNextHop makeResolvedNextHop(
    const InterfaceID& intfId,
    const std::string& nhip,
    uint32_t weight,
    std::optional<NetworkTopologyInformation> topologyInfo) {
  return ResolvedNextHop(
      IPAddress(nhip),
      intfId,
      weight,
      std::nullopt, // label action
      false, // disableTTLDecrement
      topologyInfo);
}

RouteNextHopEntry makeExpectedRouteNextHopEntry(
    const SwSwitch* sw,
    const RouteNextHopSet& nhops,
    AdminDistance distance) {
  RouteNextHopEntry entry(nhops, distance);

  CHECK(sw);
  CHECK(sw->getRib());
  auto idManager = sw->getRib()->getNextHopIDManagerCopy();
  if (idManager) {
    // Lookup resolvedNextHopSetID for the nhops
    auto resolvedId = idManager->lookupRouteNextHopSetID(nhops);
    CHECK(resolvedId);
    entry.setResolvedNextHopSetID(resolvedId);
    // Lookup normalizedResolvedNextHopSetID for the normalized nhops
    auto normalizedNhops = RouteNextHopEntry::normalizeNextHops(nhops);
    auto normalizedResolvedId =
        idManager->lookupRouteNextHopSetID(normalizedNhops);
    CHECK(normalizedResolvedId);
    entry.setNormalizedResolvedNextHopSetID(normalizedResolvedId);
  }
  return entry;
}

std::vector<RouteNextHopSet>
getUcmpNextHops(int maxWidth, int numGroups, uint32_t seed) {
  // Weight constants for alternating pattern
  constexpr auto oddWeight = 3;
  constexpr auto evenWeight = 2;
  constexpr auto kMaxOctetValue = 255;

  std::vector<RouteNextHopSet> result;
  for (int i = 0; i < numGroups; ++i) {
    RouteNextHopSet ecmpNexthopsCur;
    int groupWeight = 0;

    // We need Ip address of NextHops to be unique across calls.
    // Using 4096 separation to avoid IP collisions when called sequentially
    int totalGroupIndex = seed * 4096 + i;
    int firstOctet = ((totalGroupIndex / kMaxOctetValue) + 1) % kMaxOctetValue;
    int secondOctet = (totalGroupIndex % kMaxOctetValue) + 1;

    for (int j = 0; j < maxWidth; j++) {
      int ucmpWeight = j % 2 == 0 ? oddWeight : evenWeight;
      if (groupWeight + ucmpWeight > maxWidth) {
        ucmpWeight = 1;
      }
      groupWeight += ucmpWeight;
      ecmpNexthopsCur.insert(ResolvedNextHop(
          folly::IPAddress(
              folly::to<std::string>(
                  firstOctet, ".", secondOctet, ".1.", j + 1)),
          InterfaceID(j + 1),
          ucmpWeight));
      if (groupWeight == maxWidth) {
        break;
      }
    }
    result.push_back(std::move(ecmpNexthopsCur));
  }

  return result;
}

RoutePrefixV4 makePrefixV4(std::string str) {
  std::vector<std::string> vec;
  folly::split('/', str, vec);
  EXPECT_EQ(2, vec.size());
  auto prefix =
      RoutePrefixV4{IPAddressV4(vec.at(0)), folly::to<uint8_t>(vec.at(1))};
  return prefix;
}

RoutePrefixV6 makePrefixV6(std::string str) {
  std::vector<std::string> vec;
  folly::split('/', str, vec);
  EXPECT_EQ(2, vec.size());
  auto prefix =
      RoutePrefixV6{IPAddressV6(vec.at(0)), folly::to<uint8_t>(vec.at(1))};
  return prefix;
}

WaitForSwitchState::WaitForSwitchState(
    SwSwitch* sw,
    SwitchStatePredicate predicate,
    const std::string& name)
    : predicate_(std::move(predicate)), name_(name), sw_(sw) {
  sw_->registerStateObserver(this, name_);
}

WaitForSwitchState::~WaitForSwitchState() {
  sw_->unregisterStateObserver(this);
}

void WaitForSwitchState::stateUpdated(const StateDelta& delta) {
  if (predicate_(delta)) {
    {
      std::lock_guard<std::mutex> guard(mtx_);
      done_ = true;
    }
    cv_.notify_all();
  }
}

void programRoutes(
    const utility::RouteDistributionGenerator::RouteChunks& routeChunks,
    SwSwitch* sw) {
  auto updater = sw->getRouteUpdater();
  for (const auto& routeChunk : routeChunks) {
    for (const auto& route : routeChunk) {
      RouteNextHopSet nhops;
      for (const auto& nhop : route.nhops) {
        nhops.emplace(nhop);
      }
      updater.addRoute(
          RouterID(0),
          route.prefix.first,
          route.prefix.second,
          ClientID::BGPD,
          RouteNextHopEntry(nhops, AdminDistance::EBGP));
    }
  }
  updater.program();
}

void updateMacAddrsToBlock(
    SwSwitch* sw,
    const std::vector<std::pair<VlanID, folly::MacAddress>>& macAddrsToBlock) {
  sw->updateStateBlocking(
      "Update MAC addrs to block traffic to",
      [=](const std::shared_ptr<SwitchState>& state) {
        std::shared_ptr<SwitchState> newState{state};

        auto switchSettings =
            utility::getFirstNodeIf(state->getSwitchSettings());
        auto newSwitchSettings = switchSettings->modify(&newState);
        newSwitchSettings->setMacAddrsToBlock(macAddrsToBlock);
        return newState;
      });

  waitForStateUpdates(sw);
  sw->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(sw);
  waitForStateUpdates(sw);
}

std::vector<std::shared_ptr<Port>> getPortsInLoopbackMode(
    const std::shared_ptr<SwitchState>& state) {
  std::vector<std::shared_ptr<Port>> lbPorts;
  for (auto portMap : std::as_const(*state->getPorts())) {
    for (auto port : std::as_const(*portMap.second)) {
      if (port.second->getLoopbackMode() != cfg::PortLoopbackMode::NONE) {
        lbPorts.push_back(port.second);
      }
    }
  }
  return lbPorts;
}

void preparedMockPortConfig(
    cfg::Port& portCfg,
    int32_t id,
    std::optional<std::string> name,
    cfg::PortState state) {
  portCfg.logicalID() = id;
  portCfg.state() = state;
  portCfg.name() = name ? *name : fmt::format("port{}", id);
  // Always use 10G because all the platform ports from MockPlatform can
  // support such speed and profile
  portCfg.speed() = cfg::PortSpeed::XG;
  portCfg.profileID() = cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER;
}

state::RouteFields makeTestDropRouteFields(const std::string& prefix, bool v6) {
  if (v6) {
    return Route<folly::IPAddressV6>::makeThrift(
        makePrefixV6(prefix),
        ClientID::BGPD,
        RouteNextHopEntry(state::RouteNextHopEntry{}));
  }
  return Route<folly::IPAddressV4>::makeThrift(
      makePrefixV4(prefix),
      ClientID::BGPD,
      RouteNextHopEntry(state::RouteNextHopEntry{}));
}

std::map<std::string, state::RouteFields> makeFib(
    const std::set<std::string>& prefixes,
    bool v6) {
  std::map<std::string, state::RouteFields> fib{};
  for (const auto& prefix : prefixes) {
    fib.emplace(prefix, makeTestDropRouteFields(prefix, v6));
  }
  return fib;
}

state::FibContainerFields makeFibContainerFields(
    int vrf,
    const std::set<std::string>& v4Prefixes,
    const std::set<std::string>& v6Prefixes) {
  state::FibContainerFields fields{};

  fields.vrf() = vrf;
  fields.fibV4() = makeFib(v4Prefixes, false);
  fields.fibV4() = makeFib(v6Prefixes, true);

  return fields;
}

void addSwitchInfo(
    std::shared_ptr<SwitchState>& state,
    cfg::SwitchType switchType,
    int64_t switchId,
    cfg::AsicType asicType,
    int64_t portIdMin,
    int64_t portIdMax,
    int16_t switchIndex,
    std::optional<int64_t> sysPortMin,
    std::optional<int64_t> sysPortMax,
    std::optional<std::string> mac,
    std::optional<std::string> connectionHandle) {
  auto switchInfo = createSwitchInfo(
      switchType,
      asicType,
      portIdMin,
      portIdMax,
      switchIndex,
      sysPortMin,
      sysPortMax,
      mac,
      connectionHandle);
  auto switchSettings = utility::getFirstNodeIf(state->getSwitchSettings());
  if (switchSettings) {
    auto newSwitchSettings = switchSettings->modify(&state);
    newSwitchSettings->setSwitchIdToSwitchInfo(
        {std::make_pair(switchId, switchInfo)});
    newSwitchSettings->setSwitchInfo(switchInfo);
  } else {
    auto newSwitchSettings = std::make_shared<SwitchSettings>();
    newSwitchSettings->setSwitchIdToSwitchInfo(
        {std::make_pair(switchId, switchInfo)});
    newSwitchSettings->setSwitchInfo(switchInfo);
    addSwitchSettingsToState(state, newSwitchSettings, switchId);
  }
}

cfg::SwitchInfo createSwitchInfo(
    cfg::SwitchType switchType,
    cfg::AsicType asicType,
    int64_t portIdMin,
    int64_t portIdMax,
    int16_t switchIndex,
    std::optional<int64_t> sysPortMin,
    std::optional<int64_t> sysPortMax,
    std::optional<std::string> mac,
    std::optional<std::string> connectionHandle) {
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = switchType;
  switchInfo.asicType() = asicType;
  cfg::Range64 portIdRange;
  portIdRange.minimum() = portIdMin;
  portIdRange.maximum() = portIdMax;
  switchInfo.portIdRange() = portIdRange;
  switchInfo.switchIndex() = switchIndex;
  if (sysPortMin && sysPortMax) {
    cfg::Range64 systemPortRange;
    systemPortRange.minimum() = *sysPortMin;
    systemPortRange.maximum() = *sysPortMax;
    switchInfo.systemPortRanges()->systemPortRanges()->push_back(
        systemPortRange);
    switchInfo.localSystemPortRanges()->systemPortRanges()->push_back(
        systemPortRange);
    switchInfo.localSystemPortOffset() = *sysPortMin;
    switchInfo.globalSystemPortOffset() = *sysPortMin;
  }
  if (switchType == cfg::SwitchType::VOQ) {
    switchInfo.inbandPortId() = kSingleStageInbandPortId;
  }
  if (mac) {
    switchInfo.switchMac() = *mac;
  }
  if (connectionHandle) {
    switchInfo.connectionHandle() = *connectionHandle;
  }
  return switchInfo;
}

void registerPort(
    std::shared_ptr<SwitchState> state,
    PortID id,
    const std::string& name,
    HwSwitchMatcher scope,
    cfg::PortType portType) {
  auto port = std::make_shared<Port>(id, name);
  port->setPortType(portType);
  state->getPorts()->addNode(std::move(port), scope);
}

void setAggregatePortMemberIDs(
    std::vector<cfg::AggregatePortMember>& members,
    const std::vector<int32_t>& portIDs) {
  members.resize(portIDs.size());

  for (const auto& it : folly::enumerate(portIDs)) {
    members[it.index].memberPortID() = *it;
  }
}

template <typename T>
std::vector<int32_t> getAggregatePortMemberIDs(const std::vector<T>& members) {
  std::vector<int32_t> ports;
  ports.resize(members.size());
  for (const auto& it : folly::enumerate(members)) {
    ports[it.index] = *it->memberPortID();
  }
  return ports;
}

template std::vector<int32_t> getAggregatePortMemberIDs<
    cfg::AggregatePortMember>(const std::vector<cfg::AggregatePortMember>&);
template std::vector<int32_t> getAggregatePortMemberIDs<
    AggregatePortMemberThrift>(const std::vector<AggregatePortMemberThrift>&);

void addSwitchSettingsToState(
    std::shared_ptr<SwitchState>& state,
    std::shared_ptr<SwitchSettings> switchSettings,
    int64_t switchId) {
  auto multiSwitchSwitchSettings = std::make_unique<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(
      HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchId)}))
          .matcherString(),
      switchSettings);
  state->resetSwitchSettings(std::move(multiSwitchSwitchSettings));
}

HwSwitchInitFn mockHwSwitchInitFn(SwSwitch* sw) {
  return [sw](HwSwitchCallback* callback, bool failHwCallsOnWarmboot) {
    return getMockHw(sw)->initLight(callback, failHwCallsOnWarmboot);
  };
}

std::unique_ptr<SwSwitch> createSwSwitchWithMultiSwitch(
    const AgentConfig* config,
    const AgentDirectoryUtil* dirUtil,
    HwSwitchHandlerInitFn initFunc) {
  HwSwitchHandlerInitFn hwSwitchHandlerInitFn =
      [](const SwitchID& switchId, const cfg::SwitchInfo& info, SwSwitch* sw) {
        auto handler = std::make_unique<MockMultiSwitchHwSwitchHandler>(
            switchId, info, sw);
        // success by default
        ON_CALL(*handler, stateChanged(_, _))
            .WillByDefault(
                [=](const auto& delta, bool) { return delta.newState(); });
        ON_CALL(*handler, stateChanged(_, _, _, _, _, _))
            .WillByDefault([=](const std::vector<fsdb::OperDelta>&,
                               bool,
                               const std::shared_ptr<SwitchState>&,
                               const std::shared_ptr<SwitchState>&,
                               const HwWriteBehavior&,
                               const std::optional<StateDeltaApplication>&) {
              return std::make_pair<fsdb::OperDelta, HwSwitchStateUpdateStatus>(
                  fsdb::OperDelta{},
                  HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_SUCCEEDED);
            });
        return handler;
      };
  if (!initFunc) {
    initFunc = std::move(hwSwitchHandlerInitFn);
  }
  auto sw = make_unique<SwSwitch>(
      std::move(initFunc),
      std::make_unique<MockPlatformMapping>(),
      dirUtil,
      false,
      config,
      nullptr);
  return sw;
}

const HwAsic* checkSameAndGetAsicForTesting(
    const std::vector<const HwAsic*>& asics) {
  return checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing);
}

folly::MacAddress getMacForFirstInterfaceWithPortsForTesting(
    const std::shared_ptr<SwitchState>& state) {
  return utility::getMacForFirstInterfaceWithPorts(
      state, SwitchID(FLAGS_switch_id_for_testing));
}

InterfaceID firstInterfaceIDWithPortsForTesting(
    const std::shared_ptr<SwitchState>& state,
    std::optional<cfg::Scope> scope) {
  return utility::firstInterfaceIDWithPorts(
      state, SwitchID(FLAGS_switch_id_for_testing), scope);
}

std::shared_ptr<Interface> firstInterfaceWithPortsForTesting(
    const std::shared_ptr<SwitchState>& state) {
  return utility::firstInterfaceWithPorts(
      state, SwitchID(FLAGS_switch_id_for_testing));
}

} // namespace facebook::fboss
