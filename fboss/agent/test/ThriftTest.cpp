/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "common/network/if/gen-cpp2/Address_types.h"
#include "common/stats/MonotonicCounter.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchMySidUpdater.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/ForwardingInformationBase.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Transceiver.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/RouteScaleGenerators.h"
#include "fboss/agent/test/TestPacketFactory.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/NextHopIdTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include <map>

DECLARE_bool(enable_nexthop_id_manager);
DECLARE_bool(resolve_nexthops_from_id);
DECLARE_int32(hwswitch_query_timeout);

using namespace facebook::fboss;
using namespace facebook::stats;
using apache::thrift::TEnumTraits;
using cfg::PortSpeed;
using facebook::network::toBinaryAddress;
using facebook::network::thrift::BinaryAddress;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::StringPiece;
using std::shared_ptr;
using std::unique_ptr;
using std::chrono::duration_cast;
using ::testing::_;
using ::testing::Return;
using testing::UnorderedElementsAreArray;

namespace {
static TeCounterID kCounterID("counter0");
static std::string kNhopAddrA("2401:db00:2110:3001::0002");
static std::string kNhopAddrB("2401:db00:2110:3055::0002");
static folly::MacAddress kMacAddress("01:02:03:04:05:06");
static VlanID kVlanA(1);
static VlanID kVlanB(55);
static InterfaceID kInterfaceA(1);
static InterfaceID kInterfaceB(55);
static PortID kPortIDA(1);
static PortID kPortIDB(11);

IpPrefix ipPrefix(StringPiece ip, int length) {
  IpPrefix result;
  result.ip() = toBinaryAddress(IPAddress(ip));
  result.prefixLength() = length;
  return result;
}

IpPrefix ipPrefix(const folly::CIDRNetwork& nw) {
  IpPrefix result;
  result.ip() = toBinaryAddress(nw.first);
  result.prefixLength() = nw.second;
  return result;
}

IpPrefix ipPrefix(StringPiece prefixStr) {
  return ipPrefix(IPAddress::createNetwork(prefixStr));
}

Policy makeCbfPolicy(
    const std::string& name,
    const std::string& defaultNhg,
    const std::map<ForwardingClass, NextHopGroup>& class2Nhg = {}) {
  ClassBasedPolicy cbfPolicy;
  cbfPolicy.name() = name;
  cbfPolicy.defaultNexthopGroup() = defaultNhg;
  for (const auto& [forwardingClass, nhg] : class2Nhg) {
    cbfPolicy.class2NextHopGroup()[forwardingClass] = nhg;
  }
  Policy policy;
  policy.cbfPolicy() = std::move(cbfPolicy);
  return policy;
}

NextHopGroup namedNhgRef(const std::string& name) {
  NextHopGroup nhg;
  nhg.name() = name;
  return nhg;
}

std::unique_ptr<std::vector<Policy>> onePolicy(Policy policy) {
  auto policies = std::make_unique<std::vector<Policy>>();
  policies->push_back(std::move(policy));
  return policies;
}

} // unnamed namespace

class ThriftTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto config = testConfigA();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
  }
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

// Fixture that enables NextHop ID manager before creating the switch.
// Required for MySid entries with nexthops to have their IDs allocated.
class ThriftTestWithNhopIdMgr : public ThriftTest {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    ThriftTest::SetUp();
  }
  void TearDown() override {
    FLAGS_enable_nexthop_id_manager = false;
  }
};

TEST_F(ThriftTest, getVrfs) {
  auto config = testConfigA();
  auto& vrfConfig = config.vrfs().ensure().emplace_back();
  vrfConfig.vrfId() = 1001;
  vrfConfig.name() = "red";
  config.interfaces()[1].routerID() = 1001;
  sw_->applyConfig("add vrf", config);

  ThriftHandler handler(sw_);
  std::vector<VrfInfo> vrfs;
  handler.getVrfs(vrfs);

  VrfInfo defaultVrf;
  defaultVrf.vrfId() = 0;
  defaultVrf.interfaceIds() = {*config.interfaces()[0].intfID()};
  VrfInfo red;
  red.vrfId() = 1001;
  red.name() = "red";
  red.interfaceIds() = {*config.interfaces()[1].intfID()};
  EXPECT_EQ(vrfs, (std::vector<VrfInfo>{defaultVrf, red}));
}

TEST_F(ThriftTest, getInterfaceDetail) {
  ThriftHandler handler(this->sw_);

  // Query the two interfaces configured by testStateA()
  InterfaceDetail info;
  handler.getInterfaceDetail(info, 1);
  EXPECT_EQ("eth1/5/1", *info.interfaceName());
  EXPECT_EQ(1, *info.interfaceId());
  EXPECT_EQ(1, *info.vlanId());
  EXPECT_EQ(0, *info.routerId());
  EXPECT_EQ("00:02:00:00:00:01", *info.mac());
  std::vector<IpPrefix> expectedAddrs = {
      ipPrefix("10.0.0.1", 24),
      ipPrefix("192.168.0.1", 24),
      ipPrefix("2401:db00:2110:3001::0001", 64),
      ipPrefix("fe80::202:ff:fe00:1", 64),
      ipPrefix("fe80::", 64),
  };
  EXPECT_THAT(*info.address(), UnorderedElementsAreArray(expectedAddrs));

  handler.getInterfaceDetail(info, 55);
  EXPECT_EQ("eth1/6/1", *info.interfaceName());
  EXPECT_EQ(55, *info.interfaceId());
  EXPECT_EQ(55, *info.vlanId());
  EXPECT_EQ(0, *info.routerId());
  EXPECT_EQ("00:02:00:00:00:55", *info.mac());
  expectedAddrs = {
      ipPrefix("10.0.55.1", 24),
      ipPrefix("192.168.55.1", 24),
      ipPrefix("2401:db00:2110:3055::0001", 64),
      ipPrefix("fe80::202:ff:fe00:55", 64),
      ipPrefix("169.254.0.0", 16),
  };
  EXPECT_THAT(*info.address(), UnorderedElementsAreArray(expectedAddrs));

  // Calling getInterfaceDetail() on an unknown
  // interface should throw an FbossError.
  EXPECT_THROW(handler.getInterfaceDetail(info, 123), FbossError);
}

TEST_F(ThriftTest, txPktL3WithNonzeroInterfaceIds) {
  ThriftHandler handler(sw_);
  ASSERT_EQ(
      sw_->getState()->getInterfaces()->getNodeIf(InterfaceID(0)), nullptr);
  ASSERT_NE(
      sw_->getState()->getInterfaces()->getNodeIf(InterfaceID(1)), nullptr);

  // No packets are sent: this checks interface selection without hardware I/O.
  EXPECT_NO_THROW(handler.txPktL3(std::make_unique<folly::fbstring>(), 0, 0));
}

TEST_F(ThriftTest, txPktL3WithNonemptyPayload) {
  ThriftHandler handler(sw_);
  auto packet = createV6Packet(
      IPAddressV6(kNhopAddrA),
      IPAddressV6(kNhopAddrB),
      kMacAddress,
      kMacAddress);
  // txPktL3 takes only the IP packet, without the Ethernet header.
  auto payload = std::make_unique<folly::fbstring>(
      reinterpret_cast<const char*>(packet.data() + EthHdr::SIZE),
      packet.length() - EthHdr::SIZE);
  ASSERT_FALSE(payload->empty());
  EXPECT_NO_THROW(handler.txPktL3(std::move(payload), 1, 0));
}

TEST_F(ThriftTest, getPortInfoUserMetaData) {
  ThriftHandler handler(sw_);
  constexpr int32_t kTestPortId = 1;

  // A port with no class ID configured must leave the field unset, so that
  // absence stays distinguishable from an explicit CLASS_PORT_UNCONSTRAINED.
  PortInfoThrift before;
  handler.getPortInfo(before, kTestPortId);
  EXPECT_FALSE(before.userMetaData().has_value());

  auto config = testConfigA();
  for (auto& port : *config.ports()) {
    if (*port.logicalID() == kTestPortId) {
      port.userMetaData() = cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED;
    }
  }
  sw_->applyConfig("set port user metadata", config);

  PortInfoThrift after;
  handler.getPortInfo(after, kTestPortId);
  ASSERT_TRUE(after.userMetaData().has_value());
  EXPECT_EQ(
      *after.userMetaData(), cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);

  // getAllPortInfo shares getPortInfoHelper, so it must agree.
  std::map<int32_t, PortInfoThrift> allPortInfo;
  handler.getAllPortInfo(allPortInfo);
  ASSERT_TRUE(allPortInfo.contains(kTestPortId));
  EXPECT_EQ(
      allPortInfo[kTestPortId].userMetaData(),
      cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
}

TEST_F(ThriftTest, getPortInfoCbfc) {
  ThriftHandler handler(sw_);
  constexpr int32_t kTestPortId = 1;

  PortInfoThrift before;
  handler.getPortInfo(before, kTestPortId);
  EXPECT_FALSE(before.cbfc().has_value());

  cfg::PortVcConfig rdma;
  rdma.id() = 2;
  rdma.senderEnable() = true;
  rdma.receiverEnable() = true;
  cfg::PortVcConfig monitoring;
  monitoring.id() = 6;
  monitoring.senderEnable() = true;
  monitoring.receiverEnable() = true;
  cfg::CbfcConfig cbfcConfig;
  cbfcConfig.virtualChannels() = {rdma, monitoring};
  cbfcConfig.senderCreditLimit() = 8192;
  cbfcConfig.senderCreditSize() = 56;
  cbfcConfig.senderPacketOverhead() = 16;

  auto config = testConfigA();
  config.cbfcConfigs() = {{"cbfc", cbfcConfig}};
  for (auto& port : *config.ports()) {
    if (*port.logicalID() == kTestPortId) {
      port.cbfcConfigName() = "cbfc";
    }
  }
  sw_->applyConfig("bind cbfc profile to port", config);

  PortInfoThrift after;
  handler.getPortInfo(after, kTestPortId);
  ASSERT_TRUE(after.cbfc().has_value());
  EXPECT_EQ(*after.cbfc()->configName(), "cbfc");
  EXPECT_EQ(*after.cbfc()->virtualChannels(), (std::vector<int16_t>{2, 6}));
  EXPECT_EQ(after.cbfc()->senderCreditSize(), 56);
  EXPECT_EQ(after.cbfc()->senderPacketOverhead(), 16);
  EXPECT_EQ(after.cbfc()->senderCreditLimit(), 8192);

  std::map<int32_t, PortInfoThrift> allPortInfo;
  handler.getAllPortInfo(allPortInfo);
  ASSERT_TRUE(allPortInfo.contains(kTestPortId));
  EXPECT_EQ(allPortInfo[kTestPortId].cbfc(), after.cbfc());
}

TEST_F(ThriftTest, getPortInfoIngressAclTableName) {
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  FLAGS_enable_acl_table_group = true;
  ThriftHandler handler(sw_);
  constexpr int32_t kTestPortId = 1;
  const std::string kPortTable = "port-ingress-table";

  PortInfoThrift before;
  handler.getPortInfo(before, kTestPortId);
  EXPECT_FALSE(before.ingressAclTableName().has_value());

  auto makeTable = [](const std::string& name, int priority) {
    cfg::AclTable table;
    table.name() = name;
    table.priority() = priority;
    return table;
  };

  auto config = testConfigA();

  // A switch-bound group alongside the port-bound one, mirroring how the
  // access policy tables are published in production.
  cfg::AclTableGroup switchGroup;
  switchGroup.name() = "switch-ingress-group";
  switchGroup.stage() = cfg::AclStage::INGRESS;
  switchGroup.bindPoint() = cfg::AclTableGroupBindPoint::SWITCH;
  switchGroup.aclTables() = {makeTable("switch-ingress-table", 1)};

  cfg::AclTableGroup portGroup;
  portGroup.name() = "port-ingress-group";
  portGroup.stage() = cfg::AclStage::INGRESS;
  portGroup.bindPoint() = cfg::AclTableGroupBindPoint::PORT;
  portGroup.aclTables() = {makeTable(kPortTable, 1)};

  config.aclTableGroups() = {switchGroup, portGroup};
  for (auto& port : *config.ports()) {
    if (*port.logicalID() == kTestPortId) {
      port.ingressAclTableName() = kPortTable;
    }
  }
  sw_->applyConfig("bind ingress acl table to port", config);

  PortInfoThrift after;
  handler.getPortInfo(after, kTestPortId);
  ASSERT_TRUE(after.ingressAclTableName().has_value());
  EXPECT_EQ(*after.ingressAclTableName(), kPortTable);

  std::map<int32_t, PortInfoThrift> allPortInfo;
  handler.getAllPortInfo(allPortInfo);
  ASSERT_TRUE(allPortInfo.contains(kTestPortId));
  EXPECT_EQ(allPortInfo[kTestPortId].ingressAclTableName(), kPortTable);
}

TEST_F(ThriftTest, getAclTableLookupClassPort) {
  ThriftHandler handler(sw_);

  auto config = testConfigA();
  config.acls()->resize(2);
  config.acls()[0].name() = "acl-with-class-id";
  config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].lookupClassPort() =
      cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED;
  config.acls()[1].name() = "acl-without-class-id";
  config.acls()[1].actionType() = cfg::AclActionType::DENY;
  config.acls()[1].dscp() = 8;
  sw_->applyConfig("acls matching ingress port class id", config);

  std::vector<AclEntryThrift> aclTable;
  handler.getAclTable(aclTable);

  const AclEntryThrift* withClassId = nullptr;
  const AclEntryThrift* withoutClassId = nullptr;
  for (const auto& entry : aclTable) {
    if (*entry.name() == "acl-with-class-id") {
      withClassId = &entry;
    } else if (*entry.name() == "acl-without-class-id") {
      withoutClassId = &entry;
    }
  }

  ASSERT_NE(withClassId, nullptr);
  ASSERT_TRUE(withClassId->lookupClassPort().has_value());
  EXPECT_EQ(
      *withClassId->lookupClassPort(),
      cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);

  // An entry that does not qualify on the ingress port class ID must leave the
  // field unset rather than defaulting to CLASS_PORT_UNCONSTRAINED.
  ASSERT_NE(withoutClassId, nullptr);
  EXPECT_FALSE(withoutClassId->lookupClassPort().has_value());
}

class ThriftTestAggregatePortInterface : public ::testing::Test {
 public:
  void SetUp() override {
    auto config = testConfigAWithAggregatePortInterface();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
  }
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

TEST_F(ThriftTestAggregatePortInterface, getInterfaceDetail) {
  ThriftHandler handler(this->sw_);
  auto state = this->sw_->getState();

  InterfaceDetail aggInfo;
  handler.getInterfaceDetail(aggInfo, kAggregatePortInterfaceID);
  EXPECT_EQ(kAggregatePortInterfaceID, *aggInfo.interfaceId());
  EXPECT_EQ(cfg::InterfaceType::PORT, *aggInfo.interfaceType());

  // The interface is reported against the aggregate port. portId names a
  // physical port, so it stays unset for an aggregate bound interface.
  ASSERT_TRUE(aggInfo.aggregatePortId().has_value());
  EXPECT_EQ(kAggregatePortKey, *aggInfo.aggregatePortId());
  // portId is unqualified, so it cannot be distinguished as unset. It is left
  // at its default, and no port carries id 0.
  EXPECT_EQ(0, *aggInfo.portId());

  // Every member port of the aggregate is named.
  auto aggPort =
      state->getAggregatePorts()->getNode(AggregatePortID(kAggregatePortKey));
  std::vector<std::string> memberPortNames;
  for (const auto& subport : aggPort->sortedSubports()) {
    memberPortNames.push_back(
        state->getPorts()->getNode(subport.portID)->getName());
  }
  ASSERT_EQ(2, memberPortNames.size());
  EXPECT_THAT(*aggInfo.portNames(), UnorderedElementsAreArray(memberPortNames));

  // An interface bound to a physical port is unaffected: it reports portId and
  // no aggregatePortId.
  std::shared_ptr<Port> nonMember;
  for (const auto& [_, portMap] : std::as_const(*state->getPorts())) {
    for (const auto& [_, port] : std::as_const(*portMap)) {
      if (!aggPort->isMemberPort(port->getID())) {
        nonMember = port;
        break;
      }
    }
  }
  ASSERT_NE(nullptr, nonMember);
  InterfaceDetail portInfo;
  handler.getInterfaceDetail(portInfo, nonMember->getInterfaceID());
  EXPECT_EQ(static_cast<int32_t>(nonMember->getID()), *portInfo.portId());
  EXPECT_FALSE(portInfo.aggregatePortId().has_value());
  EXPECT_THAT(
      *portInfo.portNames(),
      UnorderedElementsAreArray(
          std::vector<std::string>{nonMember->getName()}));
}

template <typename SwitchTypeT>
class ThriftTestAllSwitchTypes : public ::testing::Test {
 public:
  static auto constexpr switchType = SwitchTypeT::switchType;
  void SetUp() override {
    FLAGS_dsf_num_parallel_sessions_per_remote_interface_node =
        std::numeric_limits<uint32_t>::max();
    auto config = testConfigA(switchType);
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
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
  int interfaceIdBegin() const {
    auto switchId = getSwitchIdAndType().first;
    if (isVoq()) {
      auto dsfNode = sw_->getState()->getDsfNodes()->getNodeIf(switchId);
      CHECK(dsfNode->getGlobalSystemPortOffset().has_value());
      return *dsfNode->getGlobalSystemPortOffset() + 5;
    }
    return 1;
  }

  std::pair<SwitchID, cfg::SwitchType> getSwitchIdAndType() const {
    if (isNpu()) {
      return {SwitchID(kNpuSwitchIdBegin), cfg::SwitchType::NPU};
    } else if (isFabric()) {
      return {SwitchID(kFabricSwitchIdBegin), cfg::SwitchType::FABRIC};
    } else if (isVoq()) {
      return {SwitchID(kVoqSwitchIdBegin), cfg::SwitchType::VOQ};
    }
    throw FbossError("Invalid switch type");
  }
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

TYPED_TEST_SUITE(ThriftTestAllSwitchTypes, SwitchTypeTestTypes);

TYPED_TEST(ThriftTestAllSwitchTypes, checkSwitchId) {
  auto switchInfoTable = this->sw_->getSwitchInfoTable();
  auto switchIdAndType = this->getSwitchIdAndType();
  for (const auto& type :
       {cfg::SwitchType::NPU, cfg::SwitchType::VOQ, cfg::SwitchType::FABRIC}) {
    if (type == switchIdAndType.second) {
      EXPECT_EQ(
          *switchInfoTable.getSwitchIdsOfType(switchIdAndType.second).begin(),
          switchIdAndType.first);
    } else {
      EXPECT_EQ(switchInfoTable.getSwitchIdsOfType(type).size(), 0);
    }
  }
  auto hwAsicTable = this->sw_->getHwAsicTable();
  EXPECT_NE(hwAsicTable, nullptr);
  auto hwAsic = hwAsicTable->getHwAsicIf(switchIdAndType.first);
  EXPECT_NE(hwAsic, nullptr);
  if (this->isVoq()) {
    EXPECT_EQ(hwAsic->getAsicMac(), folly::MacAddress("02:00:00:00:0F:0B"));
  }
  if (this->isFabric() || this->isVoq()) {
    EXPECT_EQ(
        *(switchInfoTable.getSwitchIdToSwitchInfo()
              .at(switchIdAndType.first)

              .connectionHandle()),
        "68:00");
  }
  EXPECT_EQ(SwitchID(*hwAsic->getSwitchId()), switchIdAndType.first);
  EXPECT_EQ(hwAsic->getSwitchType(), switchIdAndType.second);
  auto config = testConfigA(switchIdAndType.second);
  EXPECT_EQ(
      *(this->sw_->getScopeResolver()
            ->scope(PortID(*(config.ports()[0].logicalID())))
            .switchIds()
            .begin()),
      switchIdAndType.first);
}

TYPED_TEST(ThriftTestAllSwitchTypes, listHwObjects) {
  ThriftHandler handler(this->sw_);
  std::string out;
  std::vector<HwObjectType> in{HwObjectType::PORT};
  EXPECT_HW_CALL(this->sw_, listObjects(in, testing::_)).Times(1);
  handler.listHwObjects(
      out, std::make_unique<std::vector<HwObjectType>>(in), false);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getHwDebugDump) {
  ThriftHandler handler(this->sw_);
  std::string out;
  EXPECT_HW_CALL(this->sw_, dumpDebugState(testing::_)).Times(1);
  // Mock getHwDebugDump doesn't write any thing so expect FbossError
  EXPECT_THROW(handler.getHwDebugDump(out), FbossError);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getL2Table) {
  ThriftHandler handler(this->sw_);
  std::string out;
  EXPECT_HW_CALL(this->sw_, fetchL2Table(testing::_, testing::_))
      .Times(this->isNpu() ? 1 : 0);

  std::vector<L2EntryThrift> l2Entries;
  if (this->isNpu()) {
    handler.getL2Table(l2Entries);
  } else {
    EXPECT_THROW(handler.getL2Table(l2Entries), FbossError);
  }
}

TEST(ThriftEnum, assertPortSpeeds) {
  // We rely on the exact value of the port speeds for some
  // logic, so we want to ensure that these values don't change.
  for (const auto key : TEnumTraits<cfg::PortSpeed>::values) {
    switch (key) {
      case PortSpeed::DEFAULT:
        continue;
      case PortSpeed::GIGE:
        EXPECT_EQ(static_cast<int>(key), 1000);
        break;
      case PortSpeed::XG:
        EXPECT_EQ(static_cast<int>(key), 10000);
        break;
      case PortSpeed::TWENTYG:
        EXPECT_EQ(static_cast<int>(key), 20000);
        break;
      case PortSpeed::TWENTYFIVEG:
        EXPECT_EQ(static_cast<int>(key), 25000);
        break;
      case PortSpeed::FORTYG:
        EXPECT_EQ(static_cast<int>(key), 40000);
        break;
      case PortSpeed::FIFTYG:
        EXPECT_EQ(static_cast<int>(key), 50000);
        break;
      case PortSpeed::FIFTYTHREEPOINTONETWOFIVEG:
        EXPECT_EQ(static_cast<int>(key), 53125);
        break;
      case PortSpeed::HUNDREDG:
        EXPECT_EQ(static_cast<int>(key), 100000);
        break;
      case PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG:
        EXPECT_EQ(static_cast<int>(key), 106250);
        break;
      case PortSpeed::TWOHUNDREDG:
        EXPECT_EQ(static_cast<int>(key), 200000);
        break;
      case PortSpeed::TWOHUNDREDANDTWELVEPOINTFIVEG:
        EXPECT_EQ(static_cast<int>(key), 212500);
        break;
      case PortSpeed::FOURHUNDREDG:
        EXPECT_EQ(static_cast<int>(key), 400000);
        break;
      case PortSpeed::EIGHTHUNDREDG:
        EXPECT_EQ(static_cast<int>(key), 800000);
        break;
      case PortSpeed::ONEPOINTSIXT:
        EXPECT_EQ(static_cast<int>(key), 1600000);
        break;
      case PortSpeed::THREEPOINTTWOT:
        EXPECT_EQ(static_cast<int>(key), 3200000);
        break;
    }
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, LinkLocalRoutes) {
  // Link local addr.
  auto ip = IPAddressV6("fe80::");
  // Find longest match to link local addr.
  auto longestMatchRoute = findLongestMatchRoute(
      this->sw_->getRib(), RouterID(0), ip, this->sw_->getState());
  if (this->isFabric()) {
    ASSERT_EQ(nullptr, longestMatchRoute);
  } else {
    // Verify that a route is found. Link local route should always
    // be present
    ASSERT_NE(nullptr, longestMatchRoute);
    // Verify that the route is to link local addr.
    ASSERT_EQ(longestMatchRoute->prefix().network(), ip);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, flushNonExistentNeighbor) {
  ThriftHandler handler(this->sw_);
  auto v4Addr = std::make_unique<BinaryAddress>(
      toBinaryAddress(IPAddress("100.100.100.1")));
  auto v6Addr =
      std::make_unique<BinaryAddress>(toBinaryAddress(IPAddress("100::100")));
  if (this->isNpu()) {
    EXPECT_EQ(handler.flushNeighborEntry(std::move(v4Addr), 1), 0);
    EXPECT_EQ(handler.flushNeighborEntry(std::move(v6Addr), 1), 0);
  } else {
    EXPECT_THROW(
        handler.flushNeighborEntry(std::move(v4Addr), 1001), FbossError);
    EXPECT_THROW(
        handler.flushNeighborEntry(std::move(v6Addr), 1001), FbossError);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, flushNeighborEntriesEmpty) {
  ThriftHandler handler(this->sw_);
  auto entries = std::make_unique<std::vector<IfAndIP>>();
  EXPECT_EQ(handler.flushNeighborEntries(std::move(entries)), 0);
}

TYPED_TEST(ThriftTestAllSwitchTypes, flushNonExistentNeighborEntries) {
  ThriftHandler handler(this->sw_);
  auto entries = std::make_unique<std::vector<IfAndIP>>();

  IfAndIP v4Entry;
  v4Entry.interfaceID() = 1;
  v4Entry.ip() = toBinaryAddress(IPAddress("100.100.100.1"));
  entries->push_back(v4Entry);

  IfAndIP v6Entry;
  v6Entry.interfaceID() = 1;
  v6Entry.ip() = toBinaryAddress(IPAddress("100::100"));
  entries->push_back(v6Entry);

  if (this->isNpu()) {
    // Non-existent entries on a valid vlan return 0 flushed
    EXPECT_EQ(handler.flushNeighborEntries(std::move(entries)), 0);
  } else {
    // On non-NPU switches, flushNeighborEntry throws for each entry,
    // but flushNeighborEntries catches exceptions and continues
    EXPECT_EQ(handler.flushNeighborEntries(std::move(entries)), 0);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, flushNeighborEntriesWithInvalidVlan) {
  ThriftHandler handler(this->sw_);
  auto entries = std::make_unique<std::vector<IfAndIP>>();

  // Entry with invalid vlan - flushNeighborEntry will throw,
  // but flushNeighborEntries should catch and continue
  IfAndIP invalidEntry;
  invalidEntry.interfaceID() = 9999;
  invalidEntry.ip() = toBinaryAddress(IPAddress("100.100.100.1"));
  entries->push_back(invalidEntry);

  // Should not throw, failures are caught internally
  EXPECT_EQ(handler.flushNeighborEntries(std::move(entries)), 0);
}

TYPED_TEST(ThriftTestAllSwitchTypes, flushNeighborEntriesMixedValidity) {
  ThriftHandler handler(this->sw_);

  if (this->isNpu()) {
    // Add actual neighbor entries so they can be flushed
    this->sw_->getNeighborUpdater()->receivedArpMineForIntf(
        InterfaceID(1),
        folly::IPAddressV4("10.0.0.22"),
        folly::MacAddress("02:09:00:00:00:22"),
        PortDescriptor(PortID(1)),
        ArpOpCode::ARP_OP_REPLY);

    this->sw_->getNeighborUpdater()->receivedNdpMineForIntf(
        InterfaceID(1),
        folly::IPAddressV6("2401:db00:2110:3001::22"),
        folly::MacAddress("02:09:00:00:00:23"),
        PortDescriptor(PortID(1)),
        ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
        0);

    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(this->sw_);
    waitForStateUpdates(this->sw_);

    auto entries = std::make_unique<std::vector<IfAndIP>>();

    // Existing ARP entry - should flush successfully
    IfAndIP existingArpEntry;
    existingArpEntry.interfaceID() = 1;
    existingArpEntry.ip() = toBinaryAddress(IPAddress("10.0.0.22"));
    entries->push_back(existingArpEntry);

    // Invalid vlan - will throw inside flushNeighborEntry, caught internally
    IfAndIP invalidVlanEntry;
    invalidVlanEntry.interfaceID() = 9999;
    invalidVlanEntry.ip() = toBinaryAddress(IPAddress("200.200.200.1"));
    entries->push_back(invalidVlanEntry);

    // Existing NDP entry - should flush successfully
    IfAndIP existingNdpEntry;
    existingNdpEntry.interfaceID() = 1;
    existingNdpEntry.ip() =
        toBinaryAddress(IPAddress("2401:db00:2110:3001::22"));
    entries->push_back(existingNdpEntry);

    // Non-existent neighbor on valid vlan - returns 0
    IfAndIP nonExistentEntry;
    nonExistentEntry.interfaceID() = 1;
    nonExistentEntry.ip() = toBinaryAddress(IPAddress("100.100.100.1"));
    entries->push_back(nonExistentEntry);

    // 2 entries flushed (ARP + NDP), invalid vlan caught, non-existent is 0
    EXPECT_EQ(handler.flushNeighborEntries(std::move(entries)), 2);
  } else {
    // On non-NPU switches, all entries throw but are caught
    auto entries = std::make_unique<std::vector<IfAndIP>>();

    IfAndIP validVlanEntry;
    validVlanEntry.interfaceID() = 1;
    validVlanEntry.ip() = toBinaryAddress(IPAddress("100.100.100.1"));
    entries->push_back(validVlanEntry);

    IfAndIP invalidVlanEntry;
    invalidVlanEntry.interfaceID() = 9999;
    invalidVlanEntry.ip() = toBinaryAddress(IPAddress("200.200.200.1"));
    entries->push_back(invalidVlanEntry);

    IfAndIP anotherValidEntry;
    anotherValidEntry.interfaceID() = 1;
    anotherValidEntry.ip() = toBinaryAddress(IPAddress("100::100"));
    entries->push_back(anotherValidEntry);

    EXPECT_EQ(handler.flushNeighborEntries(std::move(entries)), 0);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, setPortState) {
  const PortID port5{5};
  ThriftHandler handler(this->sw_);
  handler.setPortState(port5, true);
  this->sw_->linkStateChanged(port5, true, cfg::PortType::INTERFACE_PORT);
  waitForStateUpdates(this->sw_);

  auto port = this->sw_->getState()->getPorts()->getNodeIf(port5);
  EXPECT_TRUE(port->isUp());
  EXPECT_TRUE(port->isEnabled());

  this->sw_->linkStateChanged(port5, false, cfg::PortType::INTERFACE_PORT);
  handler.setPortState(port5, false);
  waitForStateUpdates(this->sw_);

  port = this->sw_->getState()->getPorts()->getNodeIf(port5);
  EXPECT_FALSE(port->isUp());
  EXPECT_FALSE(port->isEnabled());
}

TYPED_TEST(ThriftTestAllSwitchTypes, setPortDrainState) {
  const PortID port5{5};
  auto port = this->sw_->getState()->getPorts()->getNodeIf(port5);
  EXPECT_FALSE(port->isDrained());

  ThriftHandler handler(this->sw_);
  if (this->isFabric()) {
    handler.setPortDrainState(port5, true);
    waitForStateUpdates(this->sw_);
    port = this->sw_->getState()->getPorts()->getNodeIf(port5);
    EXPECT_TRUE(port->isDrained());

    handler.setPortDrainState(port5, false);
    waitForStateUpdates(this->sw_);
    port = this->sw_->getState()->getPorts()->getNodeIf(port5);
    EXPECT_FALSE(port->isDrained());
  } else {
    EXPECT_THROW(handler.setPortDrainState(port5, true), FbossError);
    EXPECT_THROW(handler.setPortDrainState(port5, false), FbossError);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getPortStatus) {
  const PortID port5{5};
  auto port = this->sw_->getState()->getPorts()->getNodeIf(port5);
  ThriftHandler handler(this->sw_);

  std::map<int32_t, PortStatus> statusMap;
  std::vector<int> ports{5};
  handler.getPortStatus(statusMap, std::make_unique<std::vector<int>>(ports));
  auto portStatus = statusMap.find(5);
  EXPECT_FALSE(*portStatus->second.drained());

  if (this->isFabric()) {
    handler.setPortDrainState(port5, true);
    waitForStateUpdates(this->sw_);
    handler.getPortStatus(statusMap, std::make_unique<std::vector<int>>(ports));
    portStatus = statusMap.find(5);
    EXPECT_TRUE(*portStatus->second.drained());
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getSetSwitchDrainState) {
  ThriftHandler handler(this->sw_);
  auto switchDrainFn =
      [this](const shared_ptr<SwitchState>& state) -> shared_ptr<SwitchState> {
    shared_ptr<SwitchState> newState{state};
    auto oldSwitchSettings =
        utility::getFirstNodeIf(state->getSwitchSettings());
    auto newSwitchSettings = oldSwitchSettings->modify(&newState);
    newSwitchSettings->setSwitchDrainState(cfg::SwitchDrainState::DRAINED);
    return newState;
  };

  if (this->isFabric()) {
    EXPECT_FALSE(handler.isSwitchDrained());
    this->sw_->updateStateBlocking("Switch drain", switchDrainFn);
    EXPECT_TRUE(handler.isSwitchDrained());
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getDsfNodes) {
  ThriftHandler handler(this->sw_);
  std::map<int64_t, cfg::DsfNode> dsfNodes;
  if (this->isNpu()) {
    EXPECT_THROW(handler.getDsfNodes(dsfNodes), FbossError);
  } else {
    handler.getDsfNodes(dsfNodes);
    EXPECT_EQ(
        dsfNodes.size(), this->sw_->getState()->getDsfNodes()->numNodes());
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getSysPorts) {
  ThriftHandler handler(this->sw_);
  std::map<int64_t, SystemPortThrift> sysPorts;
  handler.getSystemPorts(sysPorts);
  if (this->isVoq()) {
    EXPECT_GT(sysPorts.size(), 1);
    EXPECT_EQ(
        sysPorts.size(),
        this->sw_->getState()->getSystemPorts()->numNodes() +
            this->sw_->getState()->getRemoteSystemPorts()->numNodes());
  } else {
    EXPECT_EQ(sysPorts.size(), 0);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getSysPortStats) {
  ThriftHandler handler(this->sw_);
  std::map<std::string, HwSysPortStats> sysPortStats;
  EXPECT_HW_CALL(this->sw_, getSysPortStats()).Times(1);
  this->sw_->updateStats();
  handler.getSysPortStats(sysPortStats);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getHwPortStats) {
  ThriftHandler handler(this->sw_);
  std::map<std::string, HwPortStats> hwPortStats;
  EXPECT_HW_CALL(this->sw_, getPortStats()).Times(1);
  this->sw_->updateStats();
  handler.getHwPortStats(hwPortStats);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getFabricReachabilityStats) {
  ThriftHandler handler(this->sw_);
  FabricReachabilityStats stats;
  handler.getFabricReachabilityStats(stats);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getFabricConnectivityHwSwitchFailure) {
  ThriftHandler handler(this->sw_);
  std::map<std::string, FabricEndpoint> connectivity;
  auto oldTimeout = FLAGS_hwswitch_query_timeout;
  FLAGS_hwswitch_query_timeout = 1;
  SCOPE_EXIT {
    FLAGS_hwswitch_query_timeout = oldTimeout;
  };

  EXPECT_THROW(handler.getFabricConnectivity(connectivity), FbossError);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getCpuPortStats) {
  ThriftHandler handler(this->sw_);
  CpuPortStats cpuPortStats;
  EXPECT_HW_CALL(this->sw_, getCpuPortStats()).Times(1);
  this->sw_->updateStats();
  handler.getCpuPortStats(cpuPortStats);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getAllEcmpDetails) {
  ThriftHandler handler(this->sw_);
  std::vector<EcmpDetails> ecmpDetails;
  EXPECT_HW_CALL(this->sw_, getAllEcmpDetails()).Times(1);
  handler.getAllEcmpDetails(ecmpDetails);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getAclTableGroup) {
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  FLAGS_enable_acl_table_group = true;
  ThriftHandler handler(this->sw_);

  auto switchIdAndType = this->getSwitchIdAndType();

  AclTableThrift aclTables;
  handler.getAclTableGroup(aclTables);
  EXPECT_EQ(aclTables.aclTableEntries()->size(), 0);
  // No ACLs on fabric switches
  if (!this->isFabric()) {
    cfg::SwitchConfig config = testConfigA(switchIdAndType.second);
    cfg::AclTableGroup tableGroup;
    tableGroup.name() = "test-table-group";
    tableGroup.stage() = cfg::AclStage::INGRESS;

    auto createAclTable = [](auto tableNum) {
      cfg::AclTable cfgTable;

      cfgTable.name() = folly::to<std::string>("test-table-", tableNum);
      cfgTable.priority() = tableNum;
      cfgTable.aclEntries()->resize(2);
      cfgTable.aclEntries()[0].name() =
          folly::to<std::string>("table", tableNum, "_acl1");
      cfgTable.aclEntries()[0].actionType() = cfg::AclActionType::DENY;
      cfgTable.aclEntries()[0].srcIp() = "192.168.0.1";
      cfgTable.aclEntries()[0].dstIp() = "192.168.0.0/24";
      cfgTable.aclEntries()[0].srcPort() = 5;
      cfgTable.aclEntries()[0].dstPort() = 8;
      cfgTable.aclEntries()[1].name() =
          folly::to<std::string>("table", tableNum, "_acl2");
      cfgTable.aclEntries()[1].actionType() = cfg::AclActionType::DENY;
      cfgTable.aclEntries()[1].srcIp() = "192.168.1.1";
      cfgTable.aclEntries()[1].dstIp() = "192.168.1.0/24";
      cfgTable.aclEntries()[1].srcPort() = 5;
      cfgTable.aclEntries()[1].dstPort() = 8;

      return cfgTable;
    };

    tableGroup.aclTables()->push_back(createAclTable(1));
    tableGroup.aclTables()->push_back(createAclTable(2));

    config.aclTableGroups() = {tableGroup};
    this->sw_->applyConfig("New config with acl table group", config);
    auto state = this->sw_->getState();
    handler.getAclTableGroup(aclTables);
    EXPECT_EQ(aclTables.aclTableEntries()->size(), 2);
    int tableNum = 1;
    for (auto& [aclTableName, aclEntries] : *aclTables.aclTableEntries()) {
      EXPECT_EQ(aclTableName, folly::to<std::string>("test-table-", tableNum));
      EXPECT_EQ(aclEntries.size(), 2);
      for (int i = 0; i < 2; i++) {
        EXPECT_EQ(
            *aclEntries[i].name(),
            folly::to<std::string>("table", tableNum, "_acl", i + 1));
      }
      tableNum++;
    }
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getAclTable) {
  ThriftHandler handler(this->sw_);

  auto switchIdAndType = this->getSwitchIdAndType();

  std::vector<AclEntryThrift> aclTable;
  handler.getAclTable(aclTable);
  EXPECT_EQ(aclTable.size(), 0);
  // No ACLs on fabric switches
  if (!this->isFabric()) {
    cfg::SwitchConfig config = testConfigA(switchIdAndType.second);
    config.acls()->resize(2);
    config.acls()[0].name() = "acl1";
    config.acls()[0].actionType() = cfg::AclActionType::DENY;
    config.acls()[0].srcIp() = "192.168.0.1";
    config.acls()[0].dstIp() = "192.168.0.0/24";
    config.acls()[0].srcPort() = 5;
    config.acls()[0].dstPort() = 8;
    config.acls()[1].name() = "acl2";
    config.acls()[1].actionType() = cfg::AclActionType::DENY;
    config.acls()[1].srcIp() = "192.168.1.1";
    config.acls()[1].dstIp() = "192.168.1.0/24";
    config.acls()[1].srcPort() = 5;
    config.acls()[1].dstPort() = 8;
    this->sw_->applyConfig("New config with acls", config);
    auto state = this->sw_->getState();
    handler.getAclTable(aclTable);
    EXPECT_EQ(aclTable.size(), 2);
    EXPECT_EQ(*aclTable[0].name(), "acl1");
    EXPECT_EQ(*aclTable[0].srcPort(), 5);
    EXPECT_EQ(*aclTable[0].dstPort(), 8);
    EXPECT_EQ(*aclTable[0].actionType(), "deny");
    EXPECT_EQ(*aclTable[1].name(), "acl2");
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getSwitchReachability) {
  ThriftHandler handler(this->sw_);
  std::unique_ptr<std::vector<std::string>> switchNames =
      std::make_unique<std::vector<std::string>>();
  switchNames->push_back("dsfNodeCfg0");
  std::map<std::string, std::vector<std::string>> reachabilityMatrix;
  EXPECT_HW_CALL(this->sw_, getSwitchReachability(testing::_)).Times(0);
  if (this->isNpu()) {
    EXPECT_THROW(
        handler.getSwitchReachability(
            reachabilityMatrix, std::move(switchNames)),
        FbossError);
  } else {
    handler.getSwitchReachability(reachabilityMatrix, std::move(switchNames));
    EXPECT_EQ(reachabilityMatrix.size(), 1);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getDsfSubscriptions) {
  ThriftHandler handler(this->sw_);
  std::vector<FsdbSubscriptionThrift> subscriptions;
  if (this->isNpu()) {
    EXPECT_THROW(handler.getDsfSubscriptions(subscriptions), FbossError);
  } else if (this->isFabric()) {
    handler.getDsfSubscriptions(subscriptions);
    EXPECT_EQ(subscriptions.size(), 0);
  } else {
    // VOQ
    handler.getDsfSubscriptions(subscriptions);
    EXPECT_EQ(subscriptions.size(), 0);

    // Add 1 IN node to DSF config
    cfg::SwitchConfig config = testConfigA(cfg::SwitchType::VOQ);
    auto dsfNodeCfg = makeDsfNodeCfg(5);
    config.dsfNodes()->insert({5, dsfNodeCfg});
    this->sw_->applyConfig("Config with 1 more IN node", config);

    handler.getDsfSubscriptions(subscriptions);
    EXPECT_EQ(subscriptions.size(), 2);
    for (const auto& subscription : subscriptions) {
      EXPECT_EQ(*subscription.name(), *dsfNodeCfg.name());
      EXPECT_EQ((*subscription.paths()).size(), 3);
      EXPECT_EQ(*subscription.state(), "DISCONNECTED");
    }
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getDsfSubscriptionClientId) {
  ThriftHandler handler(this->sw_);
  std::string ret;
  if (this->isNpu()) {
    EXPECT_THROW(handler.getDsfSubscriptionClientId(ret), FbossError);
  } else {
    handler.getDsfSubscriptionClientId(ret);
    EXPECT_TRUE(ret.find(":agent") != std::string::npos);
  }
}

std::unique_ptr<UnicastRoute> makeEcmpUnicastRoute(
    std::string prefixStr,
    const std::vector<std::string>& nxtHops,
    AdminDistance distance = AdminDistance::MAX_ADMIN_DISTANCE,
    std::optional<RouteCounterID> counterID = std::nullopt,
    std::optional<cfg::AclLookupClass> classID = std::nullopt) {
  std::vector<std::string> vec;
  folly::split('/', prefixStr, vec);
  EXPECT_EQ(2, vec.size());
  auto nr = std::make_unique<UnicastRoute>();
  *nr->dest()->ip() = toBinaryAddress(IPAddress(vec.at(0)));
  *nr->dest()->prefixLength() = folly::to<uint8_t>(vec.at(1));
  for (const auto& nxtHop : nxtHops) {
    nr->nextHopAddrs()->push_back(toBinaryAddress(IPAddress(nxtHop)));
  }
  nr->adminDistance() = distance;
  if (counterID.has_value()) {
    nr->counterID() = *counterID;
  }
  if (classID.has_value()) {
    nr->classID() = *classID;
  }
  return nr;
}

std::unique_ptr<UnicastRoute> makeUnicastRoute(
    const std::string& prefixStr,
    const std::string& nxtHop,
    AdminDistance distance = AdminDistance::MAX_ADMIN_DISTANCE,
    std::optional<RouteCounterID> counterID = std::nullopt,
    std::optional<cfg::AclLookupClass> classID = std::nullopt) {
  return makeEcmpUnicastRoute(
      prefixStr,
      std::vector<std::string>({nxtHop}),
      distance,
      counterID,
      classID);
}

TEST_F(ThriftTest, addUnicastRouteWithNonexistentLinkLocalInterfaceThrows) {
  ThriftHandler handler(sw_);
  auto route = std::make_unique<UnicastRoute>();
  route->dest() = ipPrefix("2001:db8::/64");
  route->adminDistance() = AdminDistance::OPENR;

  for (auto i = 0; i < 4; ++i) {
    NextHopThrift nexthop;
    nexthop.address() = toBinaryAddress(IPAddress("fe80:face:b00c::1"));
    nexthop.address()->ifName() = i == 2 ? "fboss9999" : "fboss1";
    nexthop.weight() = ECMP_WEIGHT;
    route->nextHops()->push_back(std::move(nexthop));
  }

  EXPECT_THROW(
      handler.addUnicastRoute(
          static_cast<int16_t>(ClientID::OPENR), std::move(route)),
      FbossError);
}

TEST_F(ThriftTest, addUnicastRouteWithLinkLocalInterfaceSucceeds) {
  ThriftHandler handler(sw_);
  auto route = std::make_unique<UnicastRoute>();
  route->dest() = ipPrefix("2001:db8::/64");
  route->adminDistance() = AdminDistance::OPENR;

  NextHopThrift nexthop;
  nexthop.address() = toBinaryAddress(IPAddress("fe80:face:b00c::1"));
  nexthop.address()->ifName() = "fboss1";
  nexthop.weight() = ECMP_WEIGHT;
  route->nextHops()->push_back(std::move(nexthop));

  EXPECT_NO_THROW(handler.addUnicastRoute(
      static_cast<int16_t>(ClientID::OPENR), std::move(route)));
}

TEST_F(ThriftTest, addUnicastRouteWithNonLinkLocalInterfaceThrows) {
  ThriftHandler handler(sw_);
  auto route = std::make_unique<UnicastRoute>();
  route->dest() = ipPrefix("2001:db8::/64");
  route->adminDistance() = AdminDistance::OPENR;

  NextHopThrift nexthop;
  nexthop.address() = toBinaryAddress(IPAddress("2001:db8::1"));
  nexthop.address()->ifName() = "fboss1";
  nexthop.weight() = ECMP_WEIGHT;
  route->nextHops()->push_back(std::move(nexthop));

  EXPECT_THROW(
      handler.addUnicastRoute(
          static_cast<int16_t>(ClientID::OPENR), std::move(route)),
      FbossError);
}

// Test for the ThriftHandler::syncFib method
TYPED_TEST(ThriftTestAllSwitchTypes, multipleClientSyncFib) {
  if (this->isFabric()) {
    // no FIB on fabric or phy
    GTEST_SKIP();
  }
  RouterID rid = RouterID(0);

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(this->sw_);

  auto kIntf1 = InterfaceID(this->interfaceIdBegin());

  // Two clients - BGP and OPENR
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto openrClient = static_cast<int16_t>(ClientID::OPENR);
  auto bgpClientAdmin = this->sw_->clientIdToAdminDistance(bgpClient);
  auto openrClientAdmin = this->sw_->clientIdToAdminDistance(openrClient);

  // nhops to use
  std::string nhop4, nhop6;
  if (this->isVoq()) {
    nhop4 = "10.0.5.2";
    nhop6 = "2401:db00:2110:3005::0002";
  } else {
    nhop4 = "10.0.0.2";
    nhop6 = "2401:db00:2110:3001::0002";
  }

  // resolve the nexthops
  auto nh1 = makeResolvedNextHops({{kIntf1, nhop4}});
  auto nh2 = makeResolvedNextHops({{kIntf1, nhop6}});

  // prefixes to add
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";
  auto prefixB4 = "7.2.0.0/16";
  auto prefixB6 = "aaaa:2::0/64";
  auto prefixC4 = "7.3.0.0/16";
  auto prefixC6 = "aaaa:3::0/64";
  auto prefixD4 = "7.4.0.0/16";
  auto prefixD6 = "aaaa:4::0/64";

  auto addRoutesForClient = [&](const auto& prefix4,
                                const auto& prefix6,
                                const auto& client,
                                const auto& clientAdmin) {
    if (this->isFabric()) {
      EXPECT_THROW(
          handler.addUnicastRoute(
              client, makeUnicastRoute(prefix4, nhop4, clientAdmin)),
          FbossError);
      EXPECT_THROW(
          handler.addUnicastRoute(
              client, makeUnicastRoute(prefix6, nhop6, clientAdmin)),
          FbossError);
    } else {
      handler.addUnicastRoute(
          client, makeUnicastRoute(prefix4, nhop4, clientAdmin));
      handler.addUnicastRoute(
          client, makeUnicastRoute(prefix6, nhop6, clientAdmin));
    }
  };

  addRoutesForClient(prefixA4, prefixA6, bgpClient, bgpClientAdmin);
  addRoutesForClient(prefixB4, prefixB6, openrClient, openrClientAdmin);

  auto verifyPrefixesPresent =
      [&](const auto& prefix4, const auto& prefix6, AdminDistance distance) {
        if (this->isFabric()) {
          return;
        }
        auto state = this->sw_->getState();
        auto rtA4 = findRoute<folly::IPAddressV4>(
            rid, IPAddress::createNetwork(prefix4), state);
        EXPECT_NE(nullptr, rtA4);
        EXPECT_EQ(
            rtA4->getForwardInfo(),
            makeExpectedRouteNextHopEntry(
                this->sw_, makeResolvedNextHops({{kIntf1, nhop4}}), distance));

        auto rtA6 = findRoute<folly::IPAddressV6>(
            rid, IPAddress::createNetwork(prefix6), state);
        EXPECT_NE(nullptr, rtA6);
        EXPECT_EQ(
            rtA6->getForwardInfo(),
            makeExpectedRouteNextHopEntry(
                this->sw_, makeResolvedNextHops({{kIntf1, nhop6}}), distance));
      };
  verifyPrefixesPresent(prefixA4, prefixA6, AdminDistance::EBGP);
  verifyPrefixesPresent(prefixB4, prefixB6, AdminDistance::OPENR);

  auto verifyPrefixesRemoved = [&](const auto& prefix4, const auto& prefix6) {
    auto state = this->sw_->getState();
    auto rtA4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefix4), state);
    EXPECT_EQ(nullptr, rtA4);
    auto rtA6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefix6), state);
    EXPECT_EQ(nullptr, rtA6);
  };

  // Call syncFib for BGP. Remove all BGP routes and add some new routes
  auto newBgpRoutes = std::make_unique<std::vector<UnicastRoute>>();
  newBgpRoutes->push_back(
      *makeUnicastRoute(prefixC6, nhop6, bgpClientAdmin).get());
  newBgpRoutes->push_back(
      *makeUnicastRoute(prefixC4, nhop4, bgpClientAdmin).get());
  if (this->isFabric()) {
    EXPECT_THROW(
        handler.syncFib(bgpClient, std::move(newBgpRoutes)), FbossError);
  } else {
    handler.syncFib(bgpClient, std::move(newBgpRoutes));
  }

  // verify that old BGP prefixes are removed
  verifyPrefixesRemoved(prefixA4, prefixA6);
  // verify that OPENR prefixes exist
  verifyPrefixesPresent(prefixB4, prefixB6, AdminDistance::OPENR);
  // verify new BGP prefixes are added
  verifyPrefixesPresent(prefixC4, prefixC6, AdminDistance::EBGP);

  // Call syncFib for OPENR. Remove all OPENR routes and add some new routes
  auto newOpenrRoutes = std::make_unique<std::vector<UnicastRoute>>();
  newOpenrRoutes->push_back(
      *makeUnicastRoute(prefixD4, nhop4, openrClientAdmin).get());
  newOpenrRoutes->push_back(
      *makeUnicastRoute(prefixD6, nhop6, openrClientAdmin).get());
  if (this->isFabric()) {
    EXPECT_THROW(
        handler.syncFib(openrClient, std::move(newOpenrRoutes)), FbossError);
  } else {
    handler.syncFib(openrClient, std::move(newOpenrRoutes));
  }

  // verify that old OPENR prefixes are removed
  verifyPrefixesRemoved(prefixB4, prefixB6);
  // verify that new OPENR prefixes are added
  verifyPrefixesPresent(prefixD4, prefixD6, AdminDistance::OPENR);

  // Add back BGP and OPENR routes
  addRoutesForClient(prefixA4, prefixA6, bgpClient, bgpClientAdmin);
  addRoutesForClient(prefixB4, prefixB6, openrClient, openrClientAdmin);

  // verify routes added
  verifyPrefixesPresent(prefixA4, prefixA6, AdminDistance::EBGP);
  verifyPrefixesPresent(prefixB4, prefixB6, AdminDistance::OPENR);
}

TYPED_TEST(ThriftTestAllSwitchTypes, getVlanAddresses) {
  ThriftHandler handler(this->sw_);
  using Addresses = std::vector<facebook::network::thrift::Address>;
  using BinaryAddresses = std::vector<facebook::network::thrift::BinaryAddress>;
  auto constexpr kVlanId = 1;
  auto constexpr kVlanName = "Vlan1";
  if (this->isNpu()) {
    {
      Addresses addrs;
      handler.getVlanAddresses(addrs, kVlanId);
      EXPECT_GT(addrs.size(), 0);
    }
    {
      Addresses addrs;
      handler.getVlanAddressesByName(
          addrs, std::make_unique<std::string>(kVlanName));
      EXPECT_GT(addrs.size(), 0);
    }
    {
      BinaryAddresses addrs;
      handler.getVlanBinaryAddresses(addrs, kVlanId);
      EXPECT_GT(addrs.size(), 0);
    }
    {
      BinaryAddresses addrs;
      handler.getVlanBinaryAddressesByName(
          addrs, std::make_unique<std::string>(kVlanName));
      EXPECT_GT(addrs.size(), 0);
    }
  } else {
    {
      Addresses addrs;
      EXPECT_THROW(handler.getVlanAddresses(addrs, 1), FbossError);
      EXPECT_THROW(
          handler.getVlanAddressesByName(
              addrs, std::make_unique<std::string>(kVlanName)),
          FbossError);
    }
    {
      BinaryAddresses addrs;
      EXPECT_THROW(handler.getVlanBinaryAddresses(addrs, 1), FbossError);
      EXPECT_THROW(
          handler.getVlanBinaryAddressesByName(
              addrs, std::make_unique<std::string>(kVlanName)),
          FbossError);
    }
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getAllInterfaces) {
  ThriftHandler handler(this->sw_);
  std::map<int32_t, InterfaceDetail> intfs;
  handler.getAllInterfaces(intfs);
  if (this->isFabric()) {
    EXPECT_TRUE(intfs.empty());
  } else {
    EXPECT_FALSE(intfs.empty());
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getInterfaceList) {
  ThriftHandler handler(this->sw_);
  std::vector<std::string> intfs;
  handler.getInterfaceList(intfs);
  if (this->isFabric()) {
    EXPECT_TRUE(intfs.empty());
  } else {
    EXPECT_FALSE(intfs.empty());
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getInterfaceDetail) {
  ThriftHandler handler(this->sw_);
  InterfaceDetail intfDetail;
  auto intfId = 1;
  if (this->isFabric()) {
    EXPECT_THROW(handler.getInterfaceDetail(intfDetail, intfId), FbossError);
  } else {
    handler.getInterfaceDetail(intfDetail, intfId);
    EXPECT_EQ(*intfDetail.interfaceId(), intfId);
  }
}

TYPED_TEST(ThriftTestAllSwitchTypes, getAggregatePorts) {
  if (this->isFabric()) {
    // no agg ports on fabric
    GTEST_SKIP();
  }
  auto switchIdAndType = this->getSwitchIdAndType();
  ThriftHandler handler(this->sw_);
  auto startState = this->sw_->getState();

  auto config = testConfigA(switchIdAndType.second);
  config.aggregatePorts()->resize(2);
  *config.aggregatePorts()[0].key() = 55;
  *config.aggregatePorts()[0].name() = "lag55";
  *config.aggregatePorts()[0].description() = "upwards facing link-bundle";
  setAggregatePortMemberIDs(
      *config.aggregatePorts()[0].memberPorts(),
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  *config.aggregatePorts()[1].key() = 155;
  *config.aggregatePorts()[1].name() = "lag155";
  *config.aggregatePorts()[1].description() = "downwards facing link-bundle";
  config.aggregatePorts()[1].memberPorts()->resize(10);
  setAggregatePortMemberIDs(
      *config.aggregatePorts()[1].memberPorts(),
      {11, 12, 13, 14, 15, 16, 17, 18, 19, 20});
  this->sw_->applyConfig("add agg ports", config);
  std::vector<AggregatePortThrift> aggPorts;
  handler.getAggregatePortTable(aggPorts);
  EXPECT_EQ(aggPorts.size(), 2);
  EXPECT_EQ(*aggPorts[0].key(), 55);
  EXPECT_EQ(*aggPorts[1].key(), 155);
  EXPECT_EQ(
      getAggregatePortMemberIDs(*aggPorts[0].memberPorts()),
      getAggregatePortMemberIDs(*config.aggregatePorts()[0].memberPorts()));
  EXPECT_EQ(
      getAggregatePortMemberIDs(*aggPorts[1].memberPorts()),
      getAggregatePortMemberIDs(*config.aggregatePorts()[1].memberPorts()));
}

TYPED_TEST(ThriftTestAllSwitchTypes, getSwitchIndicesForInterfaces) {
  ThriftHandler handler(this->sw_);
  std::map<int16_t, std::vector<std::string>> switchIndicesForInterfaces;
  std::vector<std::string> interfaces;
  handler.getInterfaceList(interfaces);
  if (this->isFabric()) {
    EXPECT_TRUE(interfaces.empty());
    return;
  }
  // Remove dummy recycle port interface if switch type is VOQ
  if (this->isVoq()) {
    interfaces.erase(interfaces.begin());
  }
  handler.getSwitchIndicesForInterfaces(
      switchIndicesForInterfaces,
      std::make_unique<std::vector<std::string>>(interfaces));
  EXPECT_EQ(switchIndicesForInterfaces.size(), 1);
  EXPECT_EQ(switchIndicesForInterfaces.begin()->first, 0);
  auto fetchedInterfaces = switchIndicesForInterfaces.begin()->second;
  for (auto i = 0; i < interfaces.size(); i++) {
    EXPECT_EQ(interfaces[i], fetchedInterfaces[i]);
  }
}

TEST_F(ThriftTest, getMyMacs) {
  ThriftHandler handler(sw_);

  std::vector<cfg::MacAndVlan> myMacs;
  handler.getMyMacs(myMacs);
  EXPECT_TRUE(myMacs.empty());

  auto config = testConfigA();
  std::vector<cfg::MacAndVlan> cfgMyMacs;
  for (const auto& macAddress : {"02:00:00:00:00:01", "02:00:00:00:00:02"}) {
    cfg::MacAndVlan myMac;
    myMac.vlanID() = 1;
    myMac.macAddress() = macAddress;
    cfgMyMacs.push_back(myMac);
  }
  config.switchSettings()->myMacs() = cfgMyMacs;
  sw_->applyConfig("add myMacs", config);

  handler.getMyMacs(myMacs);
  EXPECT_EQ(myMacs, cfgMyMacs);

  config.switchSettings()->myMacs()->clear();
  sw_->applyConfig("remove myMacs", config);

  myMacs.clear();
  handler.getMyMacs(myMacs);
  EXPECT_TRUE(myMacs.empty());
}

TEST_F(ThriftTest, getAndSetMacAddrsToBlock) {
  ThriftHandler handler(sw_);

  auto cfgMacAddrsToBlock = std::make_unique<std::vector<cfg::MacAndVlan>>();
  cfgMacAddrsToBlock->resize(FLAGS_max_mac_address_to_block + 1);
  EXPECT_THROW(
      handler.setMacAddrsToBlock(std::move(cfgMacAddrsToBlock)), FbossError);

  auto blockListVerify =
      [&handler](
          std::vector<std::pair<VlanID, folly::MacAddress>> macAddrsToBlock) {
        auto lambdaCfgMacAddrsToBlock =
            std::make_unique<std::vector<cfg::MacAndVlan>>();

        for (const auto& [vlanID, macAddress] : macAddrsToBlock) {
          cfg::MacAndVlan macAndVlan;
          macAndVlan.vlanID() = vlanID;
          macAndVlan.macAddress() = macAddress.toString();
          lambdaCfgMacAddrsToBlock->emplace_back(macAndVlan);
        }
        auto expectedCfgMacAddrsToBlock = *lambdaCfgMacAddrsToBlock;
        handler.setMacAddrsToBlock(std::move(lambdaCfgMacAddrsToBlock));
        waitForStateUpdates(handler.getSw());

        auto gotMacAddrsToBlock =
            utility::getFirstNodeIf(
                handler.getSw()->getState()->getSwitchSettings())
                ->getMacAddrsToBlock_DEPRECATED();
        EXPECT_EQ(macAddrsToBlock, gotMacAddrsToBlock);

        std::vector<cfg::MacAndVlan> gotMacAddrsToBlockViaThrift;
        handler.getMacAddrsToBlock(gotMacAddrsToBlockViaThrift);
        EXPECT_EQ(gotMacAddrsToBlockViaThrift, expectedCfgMacAddrsToBlock);
      };

  // set blockneighbor1
  blockListVerify({{VlanID(2000), folly::MacAddress("00:11:22:33:44:55")}});

  // set blockneighbor1, blockNeighbor2
  blockListVerify(
      {{VlanID(2000), folly::MacAddress("00:11:22:33:44:55")},
       {VlanID(2000), folly::MacAddress("00:11:22:33:44:66")}});

  // set blockNeighbor2
  blockListVerify({{VlanID(2000), folly::MacAddress("00:11:22:33:44:66")}});

  // set null list (clears block list)
  std::vector<cfg::MacAndVlan> macAddrsToBlock;
  handler.setMacAddrsToBlock({});
  waitForStateUpdates(sw_);
  EXPECT_EQ(
      0,
      utility::getFirstNodeIf(sw_->getState()->getSwitchSettings())
          ->getMacAddrsToBlock_DEPRECATED()
          .size());
  handler.getMacAddrsToBlock(macAddrsToBlock);
  EXPECT_TRUE(macAddrsToBlock.empty());

  // set empty list (clears block list)
  auto macAddrsToBlock2 = std::make_unique<std::vector<cfg::MacAndVlan>>();
  handler.setMacAddrsToBlock(std::move(macAddrsToBlock2));
  waitForStateUpdates(sw_);
  EXPECT_EQ(
      0,
      utility::getFirstNodeIf(sw_->getState()->getSwitchSettings())
          ->getMacAddrsToBlock_DEPRECATED()
          .size());
  handler.getMacAddrsToBlock(macAddrsToBlock);
  EXPECT_TRUE(macAddrsToBlock.empty());

  auto invalidMacAddrToBlock =
      "twshared12345.06.abc7"; // only MACs are supported
  auto invalidMacAddrsToBlock =
      std::make_unique<std::vector<cfg::MacAndVlan>>();
  cfg::MacAndVlan macAndVlan;
  macAndVlan.vlanID() = 2000;
  macAndVlan.macAddress() = invalidMacAddrToBlock;
  invalidMacAddrsToBlock->emplace_back(macAndVlan);
  EXPECT_THROW(
      handler.setMacAddrsToBlock(std::move(invalidMacAddrsToBlock)),
      FbossError);
  handler.getMacAddrsToBlock(macAddrsToBlock);
  EXPECT_TRUE(macAddrsToBlock.empty());
}

// Test for the ThriftHandler::syncFib method
TEST_F(ThriftTest, syncFib) {
  RouterID rid = RouterID(0);

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);

  auto randomClient = 500;
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto staticClient = static_cast<int16_t>(ClientID::STATIC_ROUTE);
  auto randomClientAdmin = sw_->clientIdToAdminDistance(randomClient);
  auto bgpAdmin = sw_->clientIdToAdminDistance(bgpClient);
  auto staticAdmin = sw_->clientIdToAdminDistance(staticClient);
  // STATIC_ROUTE > BGPD > RANDOM_CLIENT
  //
  // Add a few BGP routes
  //

  auto cli1_nhop4 = "10.0.0.11";
  auto cli1_nhop6 = "2401:db00:2110:3001::0011";
  auto cli2_nhop4 = "10.0.0.22";
  auto cli2_nhop6 = "2401:db00:2110:3001::0022";
  auto cli3_nhop6 = "2401:db00:2110:3001::0033";

  // These routes will include nexthops from client 10 only
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA6, cli1_nhop6, randomClientAdmin));

  // This route will include nexthops from clients randomClient and bgpClient
  auto prefixB4 = "7.2.0.0/16";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixB4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      bgpClient, makeUnicastRoute(prefixB4, cli2_nhop4, bgpAdmin));

  // This route will include nexthops from clients randomClient and bgpClient
  // and staticClient
  auto prefixC6 = "aaaa:3::0/64";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixC6, cli1_nhop6, randomClientAdmin));
  handler.addUnicastRoute(
      bgpClient, makeUnicastRoute(prefixC6, cli2_nhop6, bgpAdmin));
  handler.addUnicastRoute(
      staticClient, makeUnicastRoute(prefixC6, cli3_nhop6, staticAdmin));

  // These routes will not be used until fibSync happens.
  auto prefixD4 = "7.4.0.0/16";
  auto prefixD6 = "aaaa:4::0/64";

  //
  // Test the state of things before calling syncFib
  //

  // Make sure all the static and link-local routes are there
  auto ensureConfigRoutes = [this, rid]() {
    auto state = sw_->getState();
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV4>(
            rid, IPAddress::createNetwork("10.0.0.0/24"), state));
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV4>(
            rid, IPAddress::createNetwork("192.168.0.0/24"), state));
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV6>(
            rid, IPAddress::createNetwork("2401:db00:2110:3001::/64"), state));
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV6>(
            rid, IPAddress::createNetwork("fe80::/64"), state));
  };
  auto kIntf1 = InterfaceID(1);
  ensureConfigRoutes();
  // Make sure the lowest adming distance route is installed in FIB routes are
  // there.

  {
    auto state = sw_->getState();
    // Only random client routes
    auto rtA4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixA4), state);
    EXPECT_NE(nullptr, rtA4);
    EXPECT_EQ(
        rtA4->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop4}}),
            AdminDistance::MAX_ADMIN_DISTANCE));

    auto rtA6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixA6), state);
    EXPECT_NE(nullptr, rtA6);
    EXPECT_EQ(
        rtA6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop6}}),
            AdminDistance::MAX_ADMIN_DISTANCE));
    // Random client and BGP routes - bgp should win
    auto rtB4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixB4), state);
    EXPECT_NE(nullptr, rtB4);
    EXPECT_EQ(
        rtB4->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli2_nhop4}}),
            AdminDistance::EBGP));
    // Random client, bgp, static routes. Static shouold win
    auto rtC6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixC6), state);
    EXPECT_NE(nullptr, rtC6);
    EXPECT_EQ(
        rtC6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli3_nhop6}}),
            AdminDistance::STATIC_ROUTE));
    auto [v4Routes, v6Routes] = getRouteCount(state);
    EXPECT_EQ(
        8, v4Routes); // 5 intf routes + 2 routes from above + 1 default routes
    EXPECT_EQ(
        6, v6Routes); // 2 intf routes + 2 routes from above + 1 link local
                      // + 1 default route
    // Unistalled routes. These should not be found
    EXPECT_EQ(
        nullptr,
        findRoute<folly::IPAddressV4>(
            rid, IPAddress::createNetwork(prefixD4), state));
    EXPECT_EQ(
        nullptr,
        findRoute<folly::IPAddressV6>(
            rid, IPAddress::createNetwork(prefixD6), state));
  }
  //
  // Now use syncFib to remove all the routes for randomClient and add
  // some new ones
  // Statics, link-locals, and clients bgp and static should remain unchanged.
  //

  auto newRoutes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute nr1 =
      *makeUnicastRoute(prefixC6, cli1_nhop6, randomClientAdmin).get();
  UnicastRoute nr2 =
      *makeUnicastRoute(prefixD6, cli1_nhop6, randomClientAdmin).get();
  UnicastRoute nr3 =
      *makeUnicastRoute(prefixD4, cli1_nhop4, randomClientAdmin).get();
  newRoutes->push_back(nr1);
  newRoutes->push_back(nr2);
  newRoutes->push_back(nr3);
  handler.syncFib(randomClient, std::move(newRoutes));

  //
  // Test the state of things after syncFib
  //
  {
    // Make sure all the static and link-local routes are still there
    auto state = sw_->getState();
    ensureConfigRoutes();
    // Only random client routes from before are gone, since we did not syncFib
    // with them
    auto rtA4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixA4), state);
    EXPECT_EQ(nullptr, rtA4);

    auto rtA6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixA6), state);
    EXPECT_EQ(nullptr, rtA6);
    // random client and bgp routes. Bgp should continue to win
    auto rtB4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixB4), state);
    EXPECT_TRUE(rtB4->getForwardInfo().isSame(makeExpectedRouteNextHopEntry(
        sw_,
        makeResolvedNextHops({{InterfaceID(1), cli2_nhop4}}),
        AdminDistance::EBGP)));

    // Random client, bgp, static routes. Static should win
    auto rtC6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixC6), state);
    EXPECT_NE(nullptr, rtC6);
    EXPECT_EQ(
        rtC6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli3_nhop6}}),
            AdminDistance::STATIC_ROUTE));
    // D6 and D4 should now be found and resolved by random client nhops
    auto rtD4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixD4), state);
    EXPECT_NE(nullptr, rtD4);
    EXPECT_EQ(
        rtD4->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop4}}),
            AdminDistance::MAX_ADMIN_DISTANCE));
    auto rtD6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixD6), state);
    EXPECT_NE(nullptr, rtD6);
    EXPECT_EQ(
        rtD6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop6}}),
            AdminDistance::MAX_ADMIN_DISTANCE));
    // A4, A6 removed, D4, D6 added. Count should remain same
    auto [v4Routes, v6Routes] = getRouteCount(state);
    EXPECT_EQ(8, v4Routes);
    EXPECT_EQ(6, v6Routes);
  }
}

// Test for the ThriftHandler::add/del Unicast routes methods
// This test is a replica of syncFib test from above, except that
// when adding, deleting routes for a client it uses add, del
// UnicastRoute APIs instead of syncFib
TEST_F(ThriftTest, addDelUnicastRoutes) {
  RouterID rid = RouterID(0);

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);

  auto randomClient = 500;
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto staticClient = static_cast<int16_t>(ClientID::STATIC_ROUTE);
  auto randomClientAdmin = sw_->clientIdToAdminDistance(randomClient);
  auto bgpAdmin = sw_->clientIdToAdminDistance(bgpClient);
  auto staticAdmin = sw_->clientIdToAdminDistance(staticClient);
  // STATIC_ROUTE > BGPD > RANDOM_CLIENT
  //
  // Add a few BGP routes
  //

  auto cli1_nhop4 = "10.0.0.11";
  auto cli1_nhop6 = "2401:db00:2110:3001::0011";
  auto cli2_nhop4 = "10.0.0.22";
  auto cli2_nhop6 = "2401:db00:2110:3001::0022";
  auto cli3_nhop6 = "2401:db00:2110:3001::0033";

  // These routes will include nexthops from client 10 only
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA6, cli1_nhop6, randomClientAdmin));

  // This route will include nexthops from clients randomClient and bgpClient
  auto prefixB4 = "7.2.0.0/16";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixB4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      bgpClient, makeUnicastRoute(prefixB4, cli2_nhop4, bgpAdmin));

  // This route will include nexthops from clients randomClient and bgpClient
  // and staticClient
  auto prefixC6 = "aaaa:3::0/64";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixC6, cli1_nhop6, randomClientAdmin));
  handler.addUnicastRoute(
      bgpClient, makeUnicastRoute(prefixC6, cli2_nhop6, bgpAdmin));
  handler.addUnicastRoute(
      staticClient, makeUnicastRoute(prefixC6, cli3_nhop6, staticAdmin));

  // These routes will not be used until fibSync happens.
  auto prefixD4 = "7.4.0.0/16";
  auto prefixD6 = "aaaa:4::0/64";

  //
  // Test the state of things before calling syncFib
  //

  // Make sure all the static and link-local routes are there
  auto ensureConfigRoutes = [this, rid]() {
    auto state = sw_->getState();
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV4>(

            rid, IPAddress::createNetwork("10.0.0.0/24"), state));
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV4>(

            rid, IPAddress::createNetwork("192.168.0.0/24"), state));
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV6>(

            rid, IPAddress::createNetwork("2401:db00:2110:3001::/64"), state));
    EXPECT_NE(
        nullptr,
        findRoute<folly::IPAddressV6>(

            rid, IPAddress::createNetwork("fe80::/64"), state));
  };
  auto kIntf1 = InterfaceID(1);
  ensureConfigRoutes();
  // Make sure the lowest admin distance route is installed in FIB routes are
  // there.

  {
    auto state = sw_->getState();
    // Only random client routes
    auto rtA4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixA4), state);
    EXPECT_NE(nullptr, rtA4);
    EXPECT_EQ(
        rtA4->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop4}}),
            AdminDistance::MAX_ADMIN_DISTANCE));

    auto rtA6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixA6), state);
    EXPECT_NE(nullptr, rtA6);
    EXPECT_EQ(
        rtA6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop6}}),
            AdminDistance::MAX_ADMIN_DISTANCE));
    // Random client and BGP routes - bgp should win
    auto rtB4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixB4), state);
    EXPECT_NE(nullptr, rtB4);
    EXPECT_EQ(
        rtB4->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli2_nhop4}}),
            AdminDistance::EBGP));
    // Random client, bgp, static routes. Static should win
    auto rtC6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixC6), state);
    EXPECT_NE(nullptr, rtC6);
    EXPECT_EQ(
        rtC6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli3_nhop6}}),
            AdminDistance::STATIC_ROUTE));
    auto [v4Routes, v6Routes] = getRouteCount(state);
    EXPECT_EQ(
        8, v4Routes); // 5 intf routes + 2 routes from above + 1 default routes
    EXPECT_EQ(
        6, v6Routes); // 2 intf routes + 2 routes from above + 1 link local
                      // + 1 default route
    // Unistalled routes. These should not be found
    EXPECT_EQ(
        nullptr,
        findRoute<folly::IPAddressV4>(
            rid, IPAddress::createNetwork(prefixD4), state));
    EXPECT_EQ(
        nullptr,
        findRoute<folly::IPAddressV6>(
            rid, IPAddress::createNetwork(prefixD6), state));
  }
  //
  // Now use deleteUnicastRoute to remove all the routes for randomClient and
  // add some new ones Statics, link-locals, and clients bgp and static should
  // remain unchanged.
  //
  std::vector<IpPrefix> delRoutes = {
      ipPrefix(IPAddress::createNetwork(prefixA4)),
      ipPrefix(IPAddress::createNetwork(prefixA6)),
      ipPrefix(IPAddress::createNetwork(prefixB4)),
  };
  handler.deleteUnicastRoutes(
      randomClient, std::make_unique<std::vector<IpPrefix>>(delRoutes));
  auto newRoutes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute nr1 =
      *makeUnicastRoute(prefixC6, cli1_nhop6, randomClientAdmin).get();
  UnicastRoute nr2 =
      *makeUnicastRoute(prefixD6, cli1_nhop6, randomClientAdmin).get();
  UnicastRoute nr3 =
      *makeUnicastRoute(prefixD4, cli1_nhop4, randomClientAdmin).get();
  newRoutes->push_back(nr1);
  newRoutes->push_back(nr2);
  newRoutes->push_back(nr3);
  handler.addUnicastRoutes(randomClient, std::move(newRoutes));

  //
  // Test the state of things after syncFib
  //
  {
    // Make sure all the static and link-local routes are still there
    auto state = sw_->getState();
    ensureConfigRoutes();
    // Only random client routes from before are gone, since we did not syncFib
    // with them
    auto rtA4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixA4), state);
    EXPECT_EQ(nullptr, rtA4);

    auto rtA6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixA6), state);
    EXPECT_EQ(nullptr, rtA6);
    // random client and bgp routes. Bgp should continue to win
    auto rtB4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixB4), state);
    EXPECT_TRUE(rtB4->getForwardInfo().isSame(makeExpectedRouteNextHopEntry(
        sw_,
        makeResolvedNextHops({{InterfaceID(1), cli2_nhop4}}),
        AdminDistance::EBGP)));

    // Random client, bgp, static routes. Static should win
    auto rtC6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixC6), state);
    EXPECT_NE(nullptr, rtC6);
    EXPECT_EQ(
        rtC6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli3_nhop6}}),
            AdminDistance::STATIC_ROUTE));
    // D6 and D4 should now be found and resolved by random client nhops
    auto rtD4 = findRoute<folly::IPAddressV4>(
        rid, IPAddress::createNetwork(prefixD4), state);
    EXPECT_NE(nullptr, rtD4);
    EXPECT_EQ(
        rtD4->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop4}}),
            AdminDistance::MAX_ADMIN_DISTANCE));
    auto rtD6 = findRoute<folly::IPAddressV6>(
        rid, IPAddress::createNetwork(prefixD6), state);
    EXPECT_NE(nullptr, rtD6);
    EXPECT_EQ(
        rtD6->getForwardInfo(),
        makeExpectedRouteNextHopEntry(
            sw_,
            makeResolvedNextHops({{kIntf1, cli1_nhop6}}),
            AdminDistance::MAX_ADMIN_DISTANCE));
    // A4, A6 removed, D4, D6 added. Count should remain same
    auto [v4Routes, v6Routes] = getRouteCount(state);
    EXPECT_EQ(8, v4Routes);
    EXPECT_EQ(6, v6Routes);
  }
}

TEST_F(ThriftTest, addUnicastRoutesWithOverrides) {
  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);

  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpAdmin = sw_->clientIdToAdminDistance(bgpClient);
  auto prefixA6 = "aaaa:1::0/64";
  auto prefixA4 = "7.1.0.0/16";

  auto makeV4Route = [prefixA4, bgpAdmin](bool withOverrides = false) {
    auto cli1_nhop4 = "10.0.0.11";
    auto cli2_nhop4 = "10.0.0.22";
    auto v4Route =
        makeEcmpUnicastRoute(prefixA4, {cli1_nhop4, cli2_nhop4}, bgpAdmin);
    if (withOverrides) {
      v4Route->overrideEcmpSwitchingMode() =
          cfg::SwitchingMode::PER_PACKET_RANDOM;
    }
    return v4Route;
  };
  auto makeV4RouteWithOverride = [&makeV4Route]() { return makeV4Route(true); };
  auto makeV6Route = [prefixA6, bgpAdmin](bool withOverrides = false) {
    auto cli1_nhop6 = "2401:db00:2110:3001::0011";
    auto cli2_nhop6 = "2401:db00:2110:3001::0022";
    auto cli3_nhop6 = "2401:db00:2110:3001::0033";
    auto v6Route = makeEcmpUnicastRoute(
        prefixA6, {cli1_nhop6, cli2_nhop6, cli3_nhop6}, bgpAdmin);
    if (withOverrides) {
      v6Route->overrideNextHops() = std::vector<NextHopThrift>();
    }
    return v6Route;
  };
  auto makeV6RouteWithOverride = [&makeV6Route]() { return makeV6Route(true); };
  auto makeUnicastRoutes = [](const std::unique_ptr<UnicastRoute>& routeOne,
                              const std::unique_ptr<UnicastRoute>& routeTwo) {
    auto routes = std::make_unique<std::vector<UnicastRoute>>();
    routes->emplace_back(*routeOne);
    routes->emplace_back(*routeTwo);
    return routes;
  };

  handler.addUnicastRoute(bgpClient, makeV4Route());
  handler.addUnicastRoute(bgpClient, makeV6Route());
  // update routes to add overrides, should get rejected
  EXPECT_THROW(
      handler.addUnicastRoute(bgpClient, makeV4RouteWithOverride()),
      FbossError);
  auto v6Route = makeV6Route();
  EXPECT_THROW(
      handler.addUnicastRoute(bgpClient, makeV6RouteWithOverride()),
      FbossError);
  // Update multiple routes together with overrides, should get rejected
  EXPECT_THROW(
      handler.addUnicastRoutes(
          bgpClient,
          makeUnicastRoutes(makeV4Route(), makeV6RouteWithOverride())),
      FbossError);
  EXPECT_THROW(
      handler.addUnicastRoutes(
          bgpClient,
          makeUnicastRoutes(makeV4RouteWithOverride(), makeV6Route())),
      FbossError);
  EXPECT_THROW(
      handler.addUnicastRoutes(
          bgpClient,
          makeUnicastRoutes(
              makeV4RouteWithOverride(), makeV6RouteWithOverride())),
      FbossError);
  // Delete routes. Later we will add them with overrides and assert for
  // rejection
  std::vector<IpPrefix> delRoutes = {ipPrefix(prefixA4), ipPrefix(prefixA6)};
  handler.deleteUnicastRoutes(
      bgpClient, std::make_unique<std::vector<IpPrefix>>(delRoutes));
  EXPECT_THROW(
      handler.addUnicastRoutes(
          bgpClient,
          makeUnicastRoutes(
              makeV4RouteWithOverride(), makeV6RouteWithOverride())),
      FbossError);
  EXPECT_THROW(
      handler.addUnicastRoutes(
          bgpClient,
          makeUnicastRoutes(makeV4Route(), makeV6RouteWithOverride())),
      FbossError);
  EXPECT_THROW(
      handler.addUnicastRoutes(
          bgpClient,
          makeUnicastRoutes(makeV4RouteWithOverride(), makeV6Route())),
      FbossError);
}

TEST_F(ThriftTest, addUnicastRoutesRejectsMplsAndSrv6) {
  ThriftHandler handler(sw_);

  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpAdmin = sw_->clientIdToAdminDistance(bgpClient);
  auto prefix = "7.1.0.0/16";
  auto nhopAddr = "10.0.0.11";

  // Helper: create a route with a NextHopThrift that has both mplsAction and
  // srv6SegmentList set
  auto makeBothRoute = [&]() {
    auto route = makeEcmpUnicastRoute(prefix, {nhopAddr}, bgpAdmin);
    NextHopThrift nh;
    nh.address() = toBinaryAddress(IPAddress(nhopAddr));
    MplsAction mplsAction;
    mplsAction.action() = MplsActionCode::PUSH;
    mplsAction.pushLabels() = {101};
    nh.mplsAction() = mplsAction;
    nh.srv6SegmentList() = {toBinaryAddress(IPAddress("2001:db8::1"))};
    nh.tunnelType() = TunnelType::SRV6_ENCAP;
    nh.tunnelId() = "tunnel1";
    route->nextHops() = {nh};
    return route;
  };

  // Single route with both should be rejected
  EXPECT_THROW(handler.addUnicastRoute(bgpClient, makeBothRoute()), FbossError);

  // Batch route with both should be rejected
  auto routes = std::make_unique<std::vector<UnicastRoute>>();
  routes->emplace_back(*makeBothRoute());
  EXPECT_THROW(
      handler.addUnicastRoutes(bgpClient, std::move(routes)), FbossError);

  // Route with only mplsAction should be accepted
  auto mplsOnlyRoute = makeEcmpUnicastRoute(prefix, {nhopAddr}, bgpAdmin);
  NextHopThrift mplsNh;
  mplsNh.address() = toBinaryAddress(IPAddress(nhopAddr));
  MplsAction mplsAction;
  mplsAction.action() = MplsActionCode::PUSH;
  mplsAction.pushLabels() = {101};
  mplsNh.mplsAction() = mplsAction;
  mplsOnlyRoute->nextHops() = {mplsNh};
  EXPECT_NO_THROW(handler.addUnicastRoute(bgpClient, std::move(mplsOnlyRoute)));

  // Route with only srv6SegmentList should be accepted
  auto srv6OnlyRoute = makeEcmpUnicastRoute("8.1.0.0/16", {nhopAddr}, bgpAdmin);
  NextHopThrift srv6Nh;
  srv6Nh.address() = toBinaryAddress(IPAddress(nhopAddr));
  srv6Nh.srv6SegmentList() = {toBinaryAddress(IPAddress("2001:db8::1"))};
  srv6Nh.tunnelType() = TunnelType::SRV6_ENCAP;
  srv6Nh.tunnelId() = "tunnel1";
  srv6OnlyRoute->nextHops() = {srv6Nh};
  EXPECT_NO_THROW(handler.addUnicastRoute(bgpClient, std::move(srv6OnlyRoute)));
}

TEST_F(ThriftTest, addUnicastRoutesRejectsSrv6WithInvalidTunnelType) {
  ThriftHandler handler(sw_);

  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpAdmin = sw_->clientIdToAdminDistance(bgpClient);
  auto nhopAddr = "10.0.0.11";

  // Next hop with srv6SegmentList and wrong tunnelType should be rejected
  {
    auto route = makeEcmpUnicastRoute("9.1.0.0/16", {nhopAddr}, bgpAdmin);
    NextHopThrift nh;
    nh.address() = toBinaryAddress(IPAddress(nhopAddr));
    nh.srv6SegmentList() = {toBinaryAddress(IPAddress("2001:db8::1"))};
    nh.tunnelId() = "tunnel1";
    nh.tunnelType() = TunnelType::IP_IN_IP_DECAP;
    route->nextHops() = {nh};
    EXPECT_THROW(
        handler.addUnicastRoute(bgpClient, std::move(route)), FbossError);
  }

  // Next hop with srv6SegmentList, tunnelId, and SRV6_ENCAP tunnelType should
  // be accepted
  {
    auto route = makeEcmpUnicastRoute("9.2.0.0/16", {nhopAddr}, bgpAdmin);
    NextHopThrift nh;
    nh.address() = toBinaryAddress(IPAddress(nhopAddr));
    nh.srv6SegmentList() = {toBinaryAddress(IPAddress("2001:db8::1"))};
    nh.tunnelId() = "tunnel1";
    nh.tunnelType() = TunnelType::SRV6_ENCAP;
    route->nextHops() = {nh};
    EXPECT_NO_THROW(handler.addUnicastRoute(bgpClient, std::move(route)));
  }

  // Next hop with srv6SegmentList, no tunnelId, and no SRV6_ENCAP tunnel in
  // config should be rejected
  {
    auto route = makeEcmpUnicastRoute("9.3.0.0/16", {nhopAddr}, bgpAdmin);
    NextHopThrift nh;
    nh.address() = toBinaryAddress(IPAddress(nhopAddr));
    nh.srv6SegmentList() = {toBinaryAddress(IPAddress("2001:db8::1"))};
    // tunnelId not set and no SRV6_ENCAP tunnel in config
    route->nextHops() = {nh};
    EXPECT_THROW(
        handler.addUnicastRoute(bgpClient, std::move(route)), FbossError);
  }

  // Next hop with srv6SegmentList and no tunnelId should default to first
  // SRV6_ENCAP tunnel from config
  {
    // Add an SRv6 tunnel to the config so the defaulting logic can find it
    auto config = sw_->getConfig();
    cfg::Srv6Tunnel srv6Tunnel;
    srv6Tunnel.srv6TunnelId() = "srv6Tunnel0";
    srv6Tunnel.underlayIntfID() = 1;
    srv6Tunnel.tunnelType() = TunnelType::SRV6_ENCAP;
    srv6Tunnel.srcIp() = "2001:db8::100";
    config.srv6Tunnels() = {srv6Tunnel};
    sw_->applyConfig("Add SRv6 tunnel", config);

    auto route = makeEcmpUnicastRoute("9.4.0.0/16", {nhopAddr}, bgpAdmin);
    NextHopThrift nh;
    nh.address() = toBinaryAddress(IPAddress(nhopAddr));
    nh.srv6SegmentList() = {toBinaryAddress(IPAddress("2001:db8::1"))};
    // tunnelId not set — should default to "srv6Tunnel0" from config
    route->nextHops() = {nh};
    EXPECT_NO_THROW(handler.addUnicastRoute(bgpClient, std::move(route)));
  }
}

TEST_F(ThriftTest, delUnicastRoutes) {
  RouterID rid = RouterID(0);

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);

  auto randomClient = 500;
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto staticClient = static_cast<int16_t>(ClientID::STATIC_ROUTE);
  auto randomClientAdmin = sw_->clientIdToAdminDistance(randomClient);
  auto bgpAdmin = sw_->clientIdToAdminDistance(bgpClient);
  auto staticAdmin = sw_->clientIdToAdminDistance(staticClient);
  // STATIC_ROUTE > BGPD > RANDOM_CLIENT
  //
  // Add a few BGP routes
  //

  auto cli1_nhop4 = "10.0.0.11";
  auto cli1_nhop6 = "2401:db00:2110:3001::0011";
  auto cli2_nhop4 = "10.0.0.22";
  auto cli2_nhop6 = "2401:db00:2110:3001::0022";
  auto cli3_nhop6 = "2401:db00:2110:3001::0033";

  // These routes will include nexthops from client 10 only
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA6, cli1_nhop6, randomClientAdmin));

  // This route will include nexthops from clients randomClient and bgpClient
  auto prefixB4 = "7.2.0.0/16";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixB4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      bgpClient, makeUnicastRoute(prefixB4, cli2_nhop4, bgpAdmin));

  // This route will include nexthops from clients randomClient and bgpClient
  // and staticClient
  auto prefixC6 = "aaaa:3::0/64";
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixC6, cli1_nhop6, randomClientAdmin));
  handler.addUnicastRoute(
      bgpClient, makeUnicastRoute(prefixC6, cli2_nhop6, bgpAdmin));
  handler.addUnicastRoute(
      staticClient, makeUnicastRoute(prefixC6, cli3_nhop6, staticAdmin));
  auto assertRoute =
      [rid, prefixC6, this](
          bool expectPresent, const std::string& nhop, AdminDistance distance) {
        auto kIntf1 = InterfaceID(1);
        auto rtC6 = findRoute<folly::IPAddressV6>(
            rid, IPAddress::createNetwork(prefixC6), sw_->getState());
        if (!expectPresent) {
          EXPECT_EQ(nullptr, rtC6);
          return;
        }
        ASSERT_NE(nullptr, rtC6);
        EXPECT_EQ(
            rtC6->getForwardInfo(),
            makeExpectedRouteNextHopEntry(
                sw_, makeResolvedNextHops({{kIntf1, nhop}}), distance));
      };
  // Random client, bgp, static routes. Static should win
  assertRoute(true, cli3_nhop6, AdminDistance::STATIC_ROUTE);
  std::vector<IpPrefix> delRoutes = {
      ipPrefix(IPAddress::createNetwork(prefixC6)),
  };
  // Now delete prefixC6 for Static client. BGP should win
  handler.deleteUnicastRoutes(
      staticClient, std::make_unique<std::vector<IpPrefix>>(delRoutes));
  // Random client, bgp, routes. BGP should win
  assertRoute(true, cli2_nhop6, AdminDistance::EBGP);
  // Now delete prefixC6 for BGP client. random client should win
  // For good measure - use the single route delete API here
  handler.deleteUnicastRoute(
      bgpClient,
      std::make_unique<IpPrefix>(ipPrefix(IPAddress::createNetwork(prefixC6))));
  // Random client routes only
  assertRoute(true, cli1_nhop6, AdminDistance::MAX_ADMIN_DISTANCE);
  // Now delete prefixC6 for random client. Route should be dropped now
  handler.deleteUnicastRoutes(
      randomClient, std::make_unique<std::vector<IpPrefix>>(delRoutes));
  // Random client, bgp, routes. BGP should win
  assertRoute(false, "none", AdminDistance::EBGP);
  // Add routes back and see that lowest admin distance route comes in
  // again
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixC6, cli1_nhop6, randomClientAdmin));
  // Random client routes only
  assertRoute(true, cli1_nhop6, AdminDistance::MAX_ADMIN_DISTANCE);
  handler.addUnicastRoute(
      bgpClient, makeUnicastRoute(prefixC6, cli2_nhop6, bgpAdmin));
  // Random client, bgp, routes. BGP should win
  assertRoute(true, cli2_nhop6, AdminDistance::EBGP);
  handler.addUnicastRoute(
      staticClient, makeUnicastRoute(prefixC6, cli3_nhop6, staticAdmin));
  // Random client, bgp, static routes. Static should win
  assertRoute(true, cli3_nhop6, AdminDistance::STATIC_ROUTE);
}

TEST_F(ThriftTest, syncFibIsHwProtected) {
  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);
  CounterCache counters(sw_);
  auto addRoutes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute nr1 =
      *makeUnicastRoute("aaaa::/64", "2401:db00:2110:3001::1").get();
  addRoutes->push_back(nr1);
  EXPECT_STATE_UPDATE(sw_);
  handler.addUnicastRoutes(10, std::move(addRoutes));
  auto newRoutes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute nr2 = *makeUnicastRoute("bbbb::/64", "42::42").get();
  newRoutes->push_back(nr2);
  // Fail HW update by returning current state
  EXPECT_HW_CALL(sw_, stateChangedImpl(_, _))
      .Times(::testing::AtLeast(1))
      .WillOnce(Return(sw_->getState()));
  EXPECT_THROW(
      {
        try {
          handler.syncFib(10, std::move(newRoutes));
        } catch (const FbossFibUpdateError& fibError) {
          EXPECT_EQ(fibError.vrf2failedAddUpdatePrefixes()->size(), 1);
          auto itr = fibError.vrf2failedAddUpdatePrefixes()->find(0);
          EXPECT_EQ(itr->second.size(), 1);
          itr = fibError.vrf2failedDeletePrefixes()->find(0);
          EXPECT_EQ(itr->second.size(), 1);
          throw;
        }
      },
      FbossFibUpdateError);
  counters.update();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(sw_->stats()->getRouteProgrammingUpdateFailures(), 0);
  });
}

TEST_F(ThriftTest, addUnicastRoutesIsHwProtected) {
  ThriftHandler handler(sw_);
  CounterCache counters(sw_);
  auto newRoutes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute nr1 = *makeUnicastRoute("aaaa::/64", "42::42").get();
  newRoutes->push_back(nr1);
  // Fail HW update by returning current state
  EXPECT_HW_CALL(sw_, stateChangedImpl(_, _)).WillOnce(Return(sw_->getState()));
  EXPECT_THROW(
      {
        try {
          handler.addUnicastRoutes(10, std::move(newRoutes));
        } catch (const FbossFibUpdateError& fibError) {
          EXPECT_EQ(fibError.vrf2failedAddUpdatePrefixes()->size(), 1);
          auto itr = fibError.vrf2failedAddUpdatePrefixes()->find(0);
          EXPECT_EQ(itr->second.size(), 1);
          throw;
        }
      },
      FbossFibUpdateError);
  counters.update();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(sw_->stats()->getRouteProgrammingUpdateFailures(), 0);
  });
}

TEST_F(ThriftTest, getRouteTable) {
  ThriftHandler handler(sw_);
  auto [v4Routes, v6Routes] = getRouteCount(sw_->getState());
  std::vector<UnicastRoute> routeTable;
  handler.getRouteTable(routeTable);
  // 7 intf routes + 2 default routes + 1 link local route
  EXPECT_EQ(10, v4Routes + v6Routes);
  EXPECT_EQ(10, routeTable.size());
}

TEST_F(ThriftTest, getRouteDetails) {
  ThriftHandler handler(sw_);
  auto [v4Routes, v6Routes] = getRouteCount(sw_->getState());
  std::vector<RouteDetails> routeDetails;
  handler.getRouteTableDetails(routeDetails);
  // 7 intf routes + 2 default routes + 1 link local route
  EXPECT_EQ(10, v4Routes + v6Routes);
  EXPECT_EQ(10, routeDetails.size());
}

// Covers the resolver being threaded through the route-details thrift APIs
// backing `fboss2 show route details`. Two clients, so resolving one through
// another's entry would be caught.
TEST_F(ThriftTestWithNhopIdMgr, routeDetailsResolveClientNextHops) {
  // Pinned: other tests leave the flag false without restoring it.
  auto savedResolve = FLAGS_resolve_nexthops_from_id;
  SCOPE_EXIT {
    FLAGS_resolve_nexthops_from_id = savedResolve;
  };
  FLAGS_resolve_nexthops_from_id = true;

  ThriftHandler handler(sw_);
  constexpr int32_t kClientA = 10;
  constexpr int32_t kClientB = 11;
  const std::string kNhopA = "2401:db00:2110:3001::1";
  const std::string kNhopB = "2401:db00:2110:3001::2";
  const std::set<std::string> expectedA{kNhopA};
  const std::set<std::string> expectedB{kNhopB};

  handler.addUnicastRoute(kClientA, makeUnicastRoute("aaaa::/64", kNhopA));
  handler.addUnicastRoute(kClientB, makeUnicastRoute("aaaa::/64", kNhopB));

  std::vector<RouteDetails> all;
  handler.getRouteTableDetails(all);
  bool found = false;
  for (const auto& rd : all) {
    if (*rd.dest()->prefixLength() != 64 ||
        facebook::network::toIPAddress(*rd.dest()->ip()).str() != "aaaa::") {
      continue;
    }
    EXPECT_EQ(clientNextHops(rd, kClientA), expectedA);
    EXPECT_EQ(clientNextHops(rd, kClientB), expectedB);
    found = true;
  }
  EXPECT_TRUE(found) << "aaaa::/64 missing from route table details";

  RouteDetails single;
  handler.getIpRouteDetails(
      single,
      std::make_unique<facebook::network::thrift::Address>(
          facebook::network::toAddress(IPAddress("aaaa::1"))),
      0);
  EXPECT_EQ(clientNextHops(single, kClientA), expectedA);
  EXPECT_EQ(clientNextHops(single, kClientB), expectedB);
}

TEST_F(ThriftTest, getRouteTableSize) {
  ThriftHandler handler(sw_);
  auto [expectedV4, expectedV6] = getRouteCount(sw_->getState());

  RouteCount routeCount;
  handler.getRouteTableSize(routeCount);

  EXPECT_EQ(*routeCount.v4Count(), expectedV4);
  EXPECT_EQ(*routeCount.v6Count(), expectedV6);
  // 7 intf routes + 2 default routes + 1 link local route
  EXPECT_EQ(10, *routeCount.v4Count() + *routeCount.v6Count());
}

TEST_F(ThriftTest, getRouteTableByClient) {
  ThriftHandler handler(sw_);
  std::vector<UnicastRoute> routeTable;
  handler.getRouteTableByClient(
      routeTable, static_cast<int16_t>(ClientID::INTERFACE_ROUTE));
  // 6 intf routes + 2 default routes + 1 link local route
  EXPECT_EQ(7, routeTable.size());
}
std::unique_ptr<MplsRoute> makeMplsRoute(
    int32_t mplsLabel,
    std::string nxtHop,
    AdminDistance distance = AdminDistance::MAX_ADMIN_DISTANCE) {
  auto nr = std::make_unique<MplsRoute>();
  nr->topLabel() = mplsLabel;
  NextHopThrift nh;
  MplsAction mplsAction;
  mplsAction.action() = MplsActionCode::POP_AND_LOOKUP;
  nh.address() = toBinaryAddress(IPAddress(nxtHop));
  nh.mplsAction() = mplsAction;
  nr->nextHops()->push_back(nh);
  nr->adminDistance() = distance;
  return nr;
}

TEST_F(ThriftTest, addMplsRoutesRejectsSrv6SegmentList) {
  ThriftHandler handler(sw_);

  auto mplsRoute = makeMplsRoute(101, "10.0.0.2");
  // Add a srv6SegmentList to the next hop — should be rejected
  mplsRoute->nextHops()[0].srv6SegmentList() = {
      toBinaryAddress(IPAddress("2001:db8::1"))};
  mplsRoute->nextHops()[0].tunnelType() = TunnelType::SRV6_ENCAP;
  mplsRoute->nextHops()[0].tunnelId() = "tunnel1";

  auto routes = std::make_unique<std::vector<MplsRoute>>();
  routes->emplace_back(*mplsRoute);
  EXPECT_THROW(
      handler.addMplsRoutes(
          static_cast<int16_t>(ClientID::BGPD), std::move(routes)),
      FbossError);

  // Route without srv6SegmentList should be accepted
  auto validRoute = makeMplsRoute(102, "10.0.0.3");
  auto validRoutes = std::make_unique<std::vector<MplsRoute>>();
  validRoutes->emplace_back(*validRoute);
  EXPECT_NO_THROW(handler.addMplsRoutes(
      static_cast<int16_t>(ClientID::BGPD), std::move(validRoutes)));
}

// Label-route IDs reach SwitchState via the same FibInfo id-map sync as
// v4/v6, so the state-side resolver can resolve them.
TEST_F(ThriftTestWithNhopIdMgr, mplsRouteDetailsResolveClientNextHops) {
  // Pinned: other tests leave the flag false without restoring it.
  auto savedResolve = FLAGS_resolve_nexthops_from_id;
  SCOPE_EXIT {
    FLAGS_resolve_nexthops_from_id = savedResolve;
  };
  FLAGS_resolve_nexthops_from_id = true;

  ThriftHandler handler(sw_);
  constexpr int32_t kClient = static_cast<int32_t>(ClientID::BGPD);
  const std::string kNhop = "10.0.0.2";
  const std::set<std::string> expected{kNhop};

  auto routes = std::make_unique<std::vector<MplsRoute>>();
  routes->emplace_back(*makeMplsRoute(101, kNhop));
  handler.addMplsRoutes(kClient, std::move(routes));

  MplsRouteDetails details;
  handler.getMplsRouteDetails(details, 101);
  EXPECT_EQ(clientNextHops(details, kClient), expected);
}

TEST_F(ThriftTest, syncMplsFibIsHwProtected) {
  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);
  CounterCache counters(sw_);
  auto newRoutes = std::make_unique<std::vector<MplsRoute>>();
  MplsRoute nr1 = *makeMplsRoute(101, "10.0.0.2").get();
  newRoutes->push_back(nr1);
  // Fail HW update by returning current state
  EXPECT_HW_CALL(sw_, stateChangedImpl(_, _))
      .WillRepeatedly(Return(sw_->getState()));
  EXPECT_THROW(
      {
        try {
          handler.syncMplsFib(10, std::move(newRoutes));
        } catch (const FbossFibUpdateError& fibError) {
          EXPECT_EQ(fibError.failedAddUpdateMplsLabels()->size(), 1);
          EXPECT_EQ(*fibError.failedAddUpdateMplsLabels()->begin(), 101);
          throw;
        }
      },
      FbossFibUpdateError);
  counters.update();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(sw_->stats()->getRouteProgrammingUpdateFailures(), 0);
  });
}

TEST_F(ThriftTest, addMplsRoutesIsHwProtected) {
  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);
  CounterCache counters(sw_);
  auto newRoutes = std::make_unique<std::vector<MplsRoute>>();
  MplsRoute nr1 = *makeMplsRoute(101, "10.0.0.2").get();
  newRoutes->push_back(nr1);
  // Fail HW update by returning current state
  EXPECT_HW_CALL(sw_, stateChangedImpl(_, _))
      .WillRepeatedly(Return(sw_->getState()));
  EXPECT_THROW(
      {
        try {
          handler.addMplsRoutes(10, std::move(newRoutes));
        } catch (const FbossFibUpdateError& fibError) {
          EXPECT_EQ(fibError.failedAddUpdateMplsLabels()->size(), 1);
          EXPECT_EQ(*fibError.failedAddUpdateMplsLabels()->begin(), 101);
          throw;
        }
      },
      FbossFibUpdateError);
  counters.update();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(sw_->stats()->getRouteProgrammingUpdateFailures(), 0);
  });
}

TEST_F(ThriftTest, hwUpdateErrorAfterPartialUpdate) {
  ThriftHandler handler(sw_);
  UnicastRoute nr1 =
      *makeUnicastRoute("aaaa::/64", "2401:db00:2110:3001::1").get();
  UnicastRoute nr2 =
      *makeUnicastRoute("bbbb::/64", "2401:db00:2110:3001::1").get();
  std::vector<UnicastRoute> routes;
  routes.push_back(nr1);
  routes.push_back(nr2);
  // Simulate partial HW update: HW only programs aaaa::/64, not bbbb::/64.
  // Derive the applied state from the desired state by removing bbbb::/64.
  // This ensures aaaa::/64's internal state (including resolvedNextHopSetID
  // when NextHopID generation is enabled) matches the desired state exactly,
  // so only bbbb::/64 is reported as a failed prefix.
  EXPECT_HW_CALL(sw_, stateChangedImpl(_, _))
      .Times(::testing::AtLeast(1))
      .WillOnce(
          ::testing::WithArg<0>(
              ::testing::Invoke([](const std::vector<StateDelta>& deltas) {
                auto partialState = deltas.back().newState()->clone();
                auto* fib = partialState->getFibsInfoMap()
                                ->getFibContainer(RouterID(0))
                                ->getFibV6()
                                ->modify(RouterID(0), &partialState);
                fib->removeNode("bbbb::/64");
                partialState->publish();
                return partialState;
              })));
  EXPECT_THROW(
      {
        try {
          handler.addUnicastRoutes(
              10, std::make_unique<std::vector<UnicastRoute>>(routes));
        } catch (const FbossFibUpdateError& fibError) {
          EXPECT_EQ(fibError.vrf2failedAddUpdatePrefixes()->size(), 1);
          auto itr = fibError.vrf2failedAddUpdatePrefixes()->find(0);
          EXPECT_EQ(itr->second.size(), 1);
          itr = fibError.vrf2failedDeletePrefixes()->find(0);
          EXPECT_EQ(itr->second.size(), 0);
          throw;
        }
      },
      FbossFibUpdateError);
}

TEST_F(ThriftTest, routeUpdatesWithConcurrentReads) {
  auto thriftHgridRoutes =
      utility::HgridDuRouteScaleGenerator(
          sw_->getState(), sw_->needL2EntryForNeighbor(), 100000)
          .getThriftRoutes()[0];
  auto thriftRswRoutes =
      utility::RSWRouteScaleGenerator(
          sw_->getState(), sw_->needL2EntryForNeighbor(), 10000)
          .getThriftRoutes()[0];
  std::atomic<bool> done{false};

  ThriftHandler handler(sw_);
  std::thread routeReads([&handler, &done]() {
    while (!done) {
      std::vector<RouteDetails> details;
      handler.getRouteTableDetails(details);
      UnicastRoute route;
      handler.getIpRoute(
          route,
          std::make_unique<facebook::network::thrift::Address>(
              facebook::network::toAddress(folly::IPAddress("2001::"))),
          RouterID(0));
    }
  });
  handler.addUnicastRoutes(
      10, std::make_unique<std::vector<UnicastRoute>>(thriftHgridRoutes));
  handler.addUnicastRoutes(
      11, std::make_unique<std::vector<UnicastRoute>>(thriftRswRoutes));
  done = true;
  routeReads.join();
}

TEST_F(ThriftTest, UnicastRoutesWithClassID) {
  RouterID rid = RouterID(0);

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);

  auto randomClient = 500;
  auto randomClientAdmin = sw_->clientIdToAdminDistance(randomClient);
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpClientAdmin = sw_->clientIdToAdminDistance(bgpClient);

  auto cli1_nhop4 = "10.0.0.11";
  auto cli1_nhop6 = "2401:db00:2110:3001::0011";

  // These routes will include nexthops from client 10 only
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";

  std::optional<cfg::AclLookupClass> classID1(
      cfg::AclLookupClass::DST_CLASS_L3_DPR);
  std::optional<cfg::AclLookupClass> classID2(
      cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2);

  // Add BGP routes with class ID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(
          prefixA4, cli1_nhop4, bgpClientAdmin, std::nullopt, classID1));
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(
          prefixA6, cli1_nhop6, bgpClientAdmin, std::nullopt, classID2));

  // Add random client routes with no class ID
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA6, cli1_nhop6, randomClientAdmin));

  // BGP route should get selected and class ID set
  auto state = sw_->getState();
  auto rtA4 = findRoute<folly::IPAddressV4>(
      rid, IPAddress::createNetwork(prefixA4), state);
  EXPECT_NE(nullptr, rtA4);
  EXPECT_EQ(rtA4->getClassID(), classID1);
  EXPECT_EQ(rtA4->getForwardInfo().getClassID(), classID1);
  if (auto classID = rtA4->getEntryForClient(ClientID::BGPD)->getClassID()) {
    EXPECT_EQ(*classID, classID1);
  }
  EXPECT_TRUE(!(rtA4->getEntryForClient(static_cast<ClientID>(randomClient))
                    ->getClassID()));
  auto rtA6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixA6), state);
  EXPECT_NE(nullptr, rtA6);
  EXPECT_EQ(rtA6->getClassID(), classID2);
  EXPECT_EQ(rtA6->getForwardInfo().getClassID(), classID2);
  EXPECT_EQ(*(rtA6->getEntryForClient(ClientID::BGPD)->getClassID()), classID2);
  EXPECT_FALSE(rtA6->getEntryForClient(static_cast<ClientID>(randomClient))
                   ->getClassID());

  // delete BGP routes
  std::vector<IpPrefix> delRoutes = {
      ipPrefix(IPAddress::createNetwork(prefixA4)),
      ipPrefix(IPAddress::createNetwork(prefixA6)),
  };
  XLOG(DBG2) << "Deleting routes";
  handler.deleteUnicastRoutes(
      bgpClient, std::make_unique<std::vector<IpPrefix>>(delRoutes));

  // class IDs should not be active
  state = sw_->getState();
  rtA4 = findRoute<folly::IPAddressV4>(
      rid, IPAddress::createNetwork(prefixA4), state);
  EXPECT_NE(nullptr, rtA4);
  EXPECT_EQ(rtA4->getClassID(), std::nullopt);
  EXPECT_EQ(rtA4->getForwardInfo().getClassID(), std::nullopt);
  rtA6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixA6), state);
  EXPECT_NE(nullptr, rtA6);
  EXPECT_EQ(rtA6->getClassID(), std::nullopt);
  EXPECT_EQ(rtA6->getForwardInfo().getClassID(), std::nullopt);

  // Add back BGP routes with class ID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(
          prefixA4, cli1_nhop4, bgpClientAdmin, std::nullopt, classID1));
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(
          prefixA6, cli1_nhop6, bgpClientAdmin, std::nullopt, classID2));
  state = sw_->getState();
  rtA4 = findRoute<folly::IPAddressV4>(
      rid, IPAddress::createNetwork(prefixA4), state);

  // class id should get set again
  EXPECT_NE(nullptr, rtA4);
  EXPECT_EQ(rtA4->getClassID(), classID1);
  EXPECT_EQ(rtA4->getForwardInfo().getClassID(), classID1);
  EXPECT_EQ(*(rtA4->getEntryForClient(ClientID::BGPD)->getClassID()), classID1);
  EXPECT_FALSE(rtA4->getEntryForClient(static_cast<ClientID>(randomClient))
                   ->getClassID());
  rtA6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixA6), state);
  EXPECT_NE(nullptr, rtA6);
  EXPECT_EQ(rtA6->getClassID(), classID2);
  EXPECT_EQ(rtA6->getForwardInfo().getClassID(), classID2);
  EXPECT_EQ(*(rtA6->getEntryForClient(ClientID::BGPD)->getClassID()), classID2);
  EXPECT_FALSE(rtA6->getEntryForClient(static_cast<ClientID>(randomClient))
                   ->getClassID());
}

TEST_F(ThriftTest, UnicastRoutesWithCounterID) {
  RouterID rid = RouterID(0);

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);

  auto randomClient = 500;
  auto randomClientAdmin = sw_->clientIdToAdminDistance(randomClient);
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpClientAdmin = sw_->clientIdToAdminDistance(bgpClient);

  auto cli1_nhop4 = "10.0.0.11";
  auto cli1_nhop6 = "2401:db00:2110:3001::0011";

  // These routes will include nexthops from client 10 only
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";

  std::optional<RouteCounterID> counterID1("route.counter.0");
  std::optional<RouteCounterID> counterID2("route.counter.1");

  // Add BGP routes with counter ID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixA4, cli1_nhop4, bgpClientAdmin, counterID1));
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixA6, cli1_nhop6, bgpClientAdmin, counterID2));

  // Add random client routes with no counter ID
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA4, cli1_nhop4, randomClientAdmin));
  handler.addUnicastRoute(
      randomClient, makeUnicastRoute(prefixA6, cli1_nhop6, randomClientAdmin));

  // BGP route should get selected and counter ID set
  auto state = sw_->getState();
  auto rtA4 = findRoute<folly::IPAddressV4>(
      rid, IPAddress::createNetwork(prefixA4), state);
  EXPECT_NE(nullptr, rtA4);
  EXPECT_EQ(rtA4->getForwardInfo().getCounterID(), counterID1);
  EXPECT_EQ(
      *(rtA4->getEntryForClient(ClientID::BGPD)->getCounterID()), counterID1);
  EXPECT_FALSE(rtA4->getEntryForClient(static_cast<ClientID>(randomClient))
                   ->getCounterID());
  auto rtA6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixA6), state);
  EXPECT_NE(nullptr, rtA6);
  EXPECT_EQ(rtA6->getForwardInfo().getCounterID(), counterID2);
  EXPECT_EQ(
      *(rtA6->getEntryForClient(ClientID::BGPD)->getCounterID()), counterID2);
  EXPECT_FALSE(rtA6->getEntryForClient(static_cast<ClientID>(randomClient))
                   ->getCounterID());

  // delete BGP routes
  std::vector<IpPrefix> delRoutes = {
      ipPrefix(IPAddress::createNetwork(prefixA4)),
      ipPrefix(IPAddress::createNetwork(prefixA6)),
  };
  XLOG(DBG2) << "Deleting routes";
  handler.deleteUnicastRoutes(
      bgpClient, std::make_unique<std::vector<IpPrefix>>(delRoutes));

  // counter IDs should not be active
  state = sw_->getState();
  rtA4 = findRoute<folly::IPAddressV4>(
      rid, IPAddress::createNetwork(prefixA4), state);
  EXPECT_NE(nullptr, rtA4);
  EXPECT_EQ(rtA4->getForwardInfo().getCounterID(), std::nullopt);
  rtA6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixA6), state);
  EXPECT_NE(nullptr, rtA6);
  EXPECT_EQ(rtA6->getForwardInfo().getCounterID(), std::nullopt);

  // Add back BGP routes with counter ID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixA4, cli1_nhop4, bgpClientAdmin, counterID1));
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixA6, cli1_nhop6, bgpClientAdmin, counterID2));
  state = sw_->getState();
  rtA4 = findRoute<folly::IPAddressV4>(
      rid, IPAddress::createNetwork(prefixA4), state);

  // counter id should get set again
  EXPECT_NE(nullptr, rtA4);
  EXPECT_EQ(rtA4->getForwardInfo().getCounterID(), counterID1);
  EXPECT_EQ(
      *(rtA4->getEntryForClient(ClientID::BGPD)->getCounterID()), counterID1);
  EXPECT_FALSE(rtA4->getEntryForClient(static_cast<ClientID>(randomClient))
                   ->getCounterID());
  rtA6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixA6), state);
  EXPECT_NE(nullptr, rtA6);
  EXPECT_EQ(rtA6->getForwardInfo().getCounterID(), counterID2);
  EXPECT_EQ(
      *(rtA6->getEntryForClient(ClientID::BGPD)->getCounterID()), counterID2);
  EXPECT_FALSE(rtA6->getEntryForClient(static_cast<ClientID>(randomClient))
                   ->getCounterID());
}

TEST_F(ThriftTest, CounterIDThriftReadTest) {
  RouterID rid = RouterID(0);

  // Create a mock SwSwitch using the config, and wrap it in a ThriftHandler
  ThriftHandler handler(sw_);

  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpClientAdmin = sw_->clientIdToAdminDistance(bgpClient);

  auto cli1_nhop4 = "10.0.0.11";
  auto cli1_nhop6 = "2401:db00:2110:3001::0011";

  // These routes will include nexthops from client 10 only
  auto prefixA4 = "7.1.0.0/16";
  auto prefixA6 = "aaaa:1::0/64";
  auto prefixB6 = "aaaa:2::0/64";

  std::optional<RouteCounterID> counterID1("route.counter.0");
  std::optional<RouteCounterID> counterID2("route.counter.1");

  // Add BGP routes with counter ID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixA4, cli1_nhop4, bgpClientAdmin, counterID1));
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixA6, cli1_nhop6, bgpClientAdmin, counterID2));
  // This route shares counterID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixB6, cli1_nhop6, bgpClientAdmin, counterID2));

  auto state = sw_->getState();
  auto rtA4 = findRoute<folly::IPAddressV4>(
      rid, IPAddress::createNetwork(prefixA4), state);
  EXPECT_NE(nullptr, rtA4);
  auto rtA6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixA6), state);
  EXPECT_NE(nullptr, rtA6);
  auto rtB6 = findRoute<folly::IPAddressV6>(
      rid, IPAddress::createNetwork(prefixB6), state);
  EXPECT_NE(nullptr, rtB6);

  auto updateCounter = [](auto counterID) {
    auto counter = std::make_unique<MonotonicCounter>(
        *counterID, facebook::fb303::SUM, facebook::fb303::RATE);
    auto now = duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    counter->updateValue(now, 0);
    now = duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    counter->updateValue(now, 64);
  };
  // create and update route counters
  updateCounter(counterID1);
  updateCounter(counterID2);

  std::map<std::string, std::int64_t> routeCounters;
  auto counterIDs = std::make_unique<std::vector<std::string>>();
  auto verifyCounters = [&routeCounters]() {
    EXPECT_EQ(routeCounters.size(), 2);
    for (const auto& counterAndBytes : routeCounters) {
      EXPECT_EQ(counterAndBytes.second, 64);
    }
  };
  handler.getAllRouteCounterBytes(routeCounters);
  verifyCounters();

  routeCounters.clear();
  counterIDs->emplace_back(*counterID1);
  counterIDs->emplace_back(*counterID2);
  handler.getRouteCounterBytes(routeCounters, std::move(counterIDs));
  verifyCounters();

  routeCounters.clear();
  counterIDs = std::make_unique<std::vector<std::string>>();
  // invalid counter should return 0
  counterIDs->emplace_back("invalid");
  handler.getRouteCounterBytes(routeCounters, std::move(counterIDs));
  EXPECT_EQ(routeCounters.size(), 1);
  auto counterBytes = routeCounters.find("invalid");
  EXPECT_NE(counterBytes, routeCounters.end());
  EXPECT_EQ(counterBytes->second, 0);
}

TEST_F(ThriftTest, getRouteTableVerifyCounterID) {
  ThriftHandler handler(sw_);
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpClientAdmin = sw_->clientIdToAdminDistance(bgpClient);

  auto cli1_nhop6 = "2401:db00:2110:3001::0011";
  auto prefixA6 = "aaaa:1::0/64";
  auto addrA6 = folly::IPAddress("aaaa:1::0");
  std::optional<RouteCounterID> counterID1("route.counter.0");

  // Add BGP routes with counter ID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(prefixA6, cli1_nhop6, bgpClientAdmin, counterID1));

  std::vector<UnicastRoute> routeTable;
  bool found = false;
  handler.getRouteTable(routeTable);
  for (const auto& rt : routeTable) {
    if (rt.dest()->ip() == toBinaryAddress(addrA6)) {
      EXPECT_EQ(*rt.counterID(), *counterID1);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  routeTable.resize(0);
  found = false;
  handler.getRouteTableByClient(routeTable, bgpClient);
  for (const auto& rt : routeTable) {
    if (rt.dest()->ip() == toBinaryAddress(addrA6)) {
      EXPECT_EQ(*rt.counterID(), *counterID1);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  std::vector<RouteDetails> routeDetails;
  found = false;
  handler.getRouteTableDetails(routeDetails);
  for (const auto& route : routeDetails) {
    if (route.dest()->ip() == toBinaryAddress(addrA6)) {
      EXPECT_EQ(*route.counterID(), *counterID1);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  UnicastRoute route;
  auto addr = std::make_unique<facebook::network::thrift::Address>(
      facebook::network::toAddress(IPAddress("aaaa:1::")));
  handler.getIpRoute(route, std::move(addr), RouterID(0));
  EXPECT_EQ(*route.counterID(), *counterID1);
}

TEST_F(ThriftTest, getRouteTableVerifyClassID) {
  ThriftHandler handler(sw_);
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);
  auto bgpClientAdmin = sw_->clientIdToAdminDistance(bgpClient);

  auto cli1_nhop6 = "2401:db00:2110:3001::0011";
  auto prefixA6 = "aaaa:1::0/64";
  auto addrA6 = folly::IPAddress("aaaa:1::0");
  std::optional<cfg::AclLookupClass> classID1(
      cfg::AclLookupClass::DST_CLASS_L3_DPR);

  // Add BGP routes with class ID
  handler.addUnicastRoute(
      bgpClient,
      makeUnicastRoute(
          prefixA6, cli1_nhop6, bgpClientAdmin, std::nullopt, classID1));

  std::vector<UnicastRoute> routeTable;
  bool found = false;
  handler.getRouteTable(routeTable);
  for (const auto& rt : routeTable) {
    if (rt.dest()->ip() == toBinaryAddress(addrA6)) {
      EXPECT_EQ(*rt.classID(), *classID1);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  routeTable.resize(0);
  found = false;
  handler.getRouteTableByClient(routeTable, bgpClient);
  for (const auto& rt : routeTable) {
    if (rt.dest()->ip() == toBinaryAddress(addrA6)) {
      EXPECT_EQ(*rt.classID(), *classID1);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  std::vector<RouteDetails> routeDetails;
  found = false;
  handler.getRouteTableDetails(routeDetails);
  for (const auto& route : routeDetails) {
    if (route.dest()->ip() == toBinaryAddress(addrA6)) {
      EXPECT_EQ(*route.classID(), *classID1);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  UnicastRoute route;
  auto addr = std::make_unique<facebook::network::thrift::Address>(
      facebook::network::toAddress(IPAddress("aaaa:1::")));
  handler.getIpRoute(route, std::move(addr), RouterID(0));
  EXPECT_EQ(*route.classID(), *classID1);
}

TEST_F(ThriftTest, getLoopbackMode) {
  ThriftHandler handler(sw_);
  std::map<int32_t, PortLoopbackMode> port2LoopbackMode;
  handler.getAllPortLoopbackMode(port2LoopbackMode);
  EXPECT_EQ(port2LoopbackMode.size(), sw_->getState()->getPorts()->numNodes());
  std::for_each(
      port2LoopbackMode.begin(),
      port2LoopbackMode.end(),
      [](auto& portAndLbMode) {
        EXPECT_EQ(portAndLbMode.second, PortLoopbackMode::NONE);
      });
}

TEST_F(ThriftTest, setLoopbackMode) {
  ThriftHandler handler(sw_);
  std::map<int32_t, PortLoopbackMode> port2LoopbackMode;
  auto firstPort =
      sw_->getState()->getPorts()->cbegin()->second->cbegin()->second->getID();
  auto otherPortsUnchanged = [firstPort, this]() {
    for (auto& portMap : std::as_const(*sw_->getState()->getPorts())) {
      for (auto& port : std::as_const(*portMap.second)) {
        if (port.second->getID() != firstPort) {
          EXPECT_EQ(
              port.second->getLoopbackMode(), cfg::PortLoopbackMode::NONE);
        }
      }
    }
  };

  for (auto lbMode :
       {PortLoopbackMode::NIF,
        PortLoopbackMode::MAC,
        PortLoopbackMode::PHY,
        PortLoopbackMode::NONE}) {
    // MAC
    handler.setPortLoopbackMode(firstPort, lbMode);
    handler.getAllPortLoopbackMode(port2LoopbackMode);
    EXPECT_EQ(
        port2LoopbackMode.size(), sw_->getState()->getPorts()->numNodes());
    EXPECT_EQ(port2LoopbackMode.find(firstPort)->second, lbMode);
    otherPortsUnchanged();
  }
}

TEST_F(ThriftTest, programLedExternalState) {
  ThriftHandler handler(sw_);
  auto firstPort =
      sw_->getState()->getPorts()->cbegin()->second->cbegin()->second->getID();
  handler.setExternalLedState(
      firstPort, PortLedExternalState::EXTERNAL_FORCE_ON);
  auto port = sw_->getState()->getPorts()->getNode(firstPort);
  EXPECT_EQ(
      port->getLedPortExternalState(), PortLedExternalState::EXTERNAL_FORCE_ON);
}

TEST_F(ThriftTest, programInternalPhyPorts) {
  ThriftHandler handler(sw_);
  // Using Wedge100PlatformMapping in MockPlatform. Transceiver 4 is used by
  // eth1/5/[1-4], which is software port 1/2/3/4
  TransceiverID id = TransceiverID(4);
  constexpr auto kEnabledPort = 1;
  constexpr auto kCableLength = 3.5;
  constexpr auto kMediaInterface = MediaInterfaceCode::CWDM4_100G;
  constexpr auto kComplianceCode = ExtendedSpecComplianceCode::CWDM4_100G;
  constexpr auto kManagementInterface = TransceiverManagementInterface::CMIS;
  auto preparedTcvrInfo = [id,
                           kMediaInterface,
                           kComplianceCode,
                           kManagementInterface](double cableLength) {
    auto tcvrInfo = std::make_unique<TransceiverInfo>();
    tcvrInfo->tcvrState()->port() = id;
    tcvrInfo->tcvrState()->present() = true;
    Cable cable;
    cable.length() = cableLength;
    tcvrInfo->tcvrState()->cable() = cable;
    tcvrInfo->tcvrState()->transceiverManagementInterface() =
        kManagementInterface;
    TransceiverSettings tcvrSettings;
    std::vector<MediaInterfaceId> mediaInterfaces;
    for (int i = 0; i < 4; i++) {
      MediaInterfaceId intf;
      intf.lane() = i;
      intf.code() = kMediaInterface;
      mediaInterfaces.push_back(intf);
    }
    tcvrSettings.mediaInterface() = std::move(mediaInterfaces);
    tcvrInfo->tcvrState()->settings() = tcvrSettings;
    return tcvrInfo;
  };

  // Only enabled ports should be return
  auto checkProgrammedPorts =
      [&](const std::map<int32_t, cfg::PortProfileID>& programmedPorts) {
        EXPECT_EQ(programmedPorts.size(), 1);
        EXPECT_TRUE(
            programmedPorts.find(kEnabledPort) != programmedPorts.end());
        // Controlling port should be 100G
        EXPECT_EQ(
            programmedPorts.find(kEnabledPort)->second,
            cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER);

        // Make sure the enabled ports using the new profile config/pin configs
        auto tcvr = sw_->getState()->getTransceivers()->getNodeIf(id);
        std::optional<cfg::PlatformPortConfigOverrideFactor> factor;
        if (tcvr != nullptr) {
          factor = tcvr->toPlatformPortConfigOverrideFactor();
        }
        sw_->getPlatformMapping()->customizePlatformPortConfigOverrideFactor(
            factor);
        // Port must exist in the SwitchState
        const auto port =
            sw_->getState()->getPorts()->getNodeIf(PortID(kEnabledPort));
        EXPECT_TRUE(port->isEnabled());
        PlatformPortProfileConfigMatcher matcher{
            port->getProfileID(), port->getID(), factor};
        auto portProfileCfg =
            sw_->getPlatformMapping()->getPortProfileConfig(matcher);
        CHECK(portProfileCfg) << "No port profile config found with matcher:"
                              << matcher.toString();
        auto expectedProfileConfig = *portProfileCfg->iphy();
        const auto& expectedPinConfigs =
            sw_->getPlatformMapping()->getPortIphyPinConfigs(matcher);

        EXPECT_TRUE(expectedProfileConfig == port->getProfileConfig());
        EXPECT_TRUE(expectedPinConfigs == port->getPinConfigs());
      };

  std::map<int32_t, cfg::PortProfileID> programmedPorts;
  handler.programInternalPhyPorts(
      programmedPorts, preparedTcvrInfo(kCableLength), false);

  checkProgrammedPorts(programmedPorts);
  auto tcvr = sw_->getState()->getTransceivers()->getNode(id);
  EXPECT_EQ(tcvr->getID(), id);
  EXPECT_EQ(*tcvr->getCableLength(), kCableLength);
  EXPECT_EQ(*tcvr->getMediaInterface(), kMediaInterface);
  EXPECT_EQ(*tcvr->getManagementInterface(), kManagementInterface);

  // Now change the cable length
  const auto oldPort =
      sw_->getState()->getPorts()->getNodeIf(PortID(kEnabledPort));
  constexpr auto kCableLength2 = 1;
  std::map<int32_t, cfg::PortProfileID> programmedPorts2;
  handler.programInternalPhyPorts(
      programmedPorts2, preparedTcvrInfo(kCableLength2), false);

  checkProgrammedPorts(programmedPorts2);
  tcvr = sw_->getState()->getTransceivers()->getNode(id);
  EXPECT_EQ(tcvr->getID(), id);
  EXPECT_EQ(*tcvr->getCableLength(), kCableLength2);
  EXPECT_EQ(*tcvr->getMediaInterface(), kMediaInterface);
  EXPECT_EQ(*tcvr->getManagementInterface(), kManagementInterface);
  const auto newPort =
      sw_->getState()->getPorts()->getNodeIf(PortID(kEnabledPort));
  // Because we're using Wedge100PlatformMapping here, we should see pinConfig
  // change due to the cable length change
  EXPECT_TRUE(oldPort->getProfileConfig() == newPort->getProfileConfig());
  EXPECT_TRUE(oldPort->getPinConfigs() != newPort->getPinConfigs());

  // Using the same transceiver info to program and no new state created
  auto beforeGen = sw_->getState()->getGeneration();
  programmedPorts2.clear();
  handler.programInternalPhyPorts(
      programmedPorts2, preparedTcvrInfo(kCableLength2), false);
  checkProgrammedPorts(programmedPorts2);
  EXPECT_EQ(beforeGen, sw_->getState()->getGeneration());

  // Finally remove the transceiver
  auto unpresentTcvr = std::make_unique<TransceiverInfo>();
  unpresentTcvr->tcvrState()->port() = id;
  unpresentTcvr->tcvrState()->present() = false;
  std::map<int32_t, cfg::PortProfileID> programmedPorts3;
  handler.programInternalPhyPorts(
      programmedPorts3, std::move(unpresentTcvr), false);

  // Still return programmed ports even though no transceiver there
  checkProgrammedPorts(programmedPorts3);
  tcvr = sw_->getState()->getTransceivers()->getNodeIf(id);
  EXPECT_TRUE(tcvr == nullptr);

  // Remove the same Transceiver again, and make sure no new state created.
  beforeGen = sw_->getState()->getGeneration();
  programmedPorts3.clear();
  unpresentTcvr = std::make_unique<TransceiverInfo>();
  unpresentTcvr->tcvrState()->port() = id;
  unpresentTcvr->tcvrState()->present() = false;
  handler.programInternalPhyPorts(
      programmedPorts3, std::move(unpresentTcvr), false);
  // Still return programmed ports even though no transceiver there
  checkProgrammedPorts(programmedPorts3);
  tcvr = sw_->getState()->getTransceivers()->getNodeIf(id);
  EXPECT_TRUE(tcvr == nullptr);
  EXPECT_EQ(beforeGen, sw_->getState()->getGeneration());
}

TEST_F(ThriftTest, getConfigAppliedInfo) {
  ThriftHandler handler(sw_);
  // The SetUp() will applied an initialed config, so we should verify the
  // lastConfigAppliedInMs should be > 0 and < now.
  ConfigAppliedInfo initConfigAppliedInfo;
  handler.getConfigAppliedInfo(initConfigAppliedInfo);
  auto initConfigAppliedInMs = *initConfigAppliedInfo.lastAppliedInMs();
  EXPECT_GT(initConfigAppliedInMs, 0);
  // Thrift test should always trigger coldboot
  auto coldbootConfigAppliedTime =
      initConfigAppliedInfo.lastColdbootAppliedInMs();
  if (coldbootConfigAppliedTime) {
    EXPECT_EQ(*coldbootConfigAppliedTime, initConfigAppliedInMs);
  } else {
    throw FbossError("No coldboot config applied time");
  }

  // Adding sleep in case we immediately check the last config applied time
  /* sleep override */
  usleep(1000);
  auto currentInMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  EXPECT_LT(initConfigAppliedInMs, currentInMs);

  // Try to apply a new config, the lastConfigAppliedTime should changed
  // Adding sleep in case we apply a new config immediately after the last
  // config
  /* sleep override */
  usleep(1000);
  sw_->applyConfig(
      "New config with new speed profile", testConfigAWithLookupClasses());

  ConfigAppliedInfo newConfigAppliedInfo;
  handler.getConfigAppliedInfo(newConfigAppliedInfo);
  auto newConfigAppliedInMs = *newConfigAppliedInfo.lastAppliedInMs();
  EXPECT_GT(newConfigAppliedInMs, initConfigAppliedInMs);
  // Coldboot time should not change
  if (auto newColdbootConfigAppliedTime =
          newConfigAppliedInfo.lastColdbootAppliedInMs()) {
    EXPECT_EQ(*newColdbootConfigAppliedTime, *coldbootConfigAppliedTime);
  } else {
    throw FbossError("No coldboot config applied time");
  }
}

TEST_F(ThriftTest, applySpeedAndProfileMismatchConfig) {
  ThriftHandler handler(sw_);
  auto mismatchConfig = testConfigA();
  // Change the speed mismatch with the profile
  CHECK(!mismatchConfig.ports()->empty());
  mismatchConfig.ports()[0].state() = cfg::PortState::ENABLED;
  mismatchConfig.ports()[0].speed() = cfg::PortSpeed::TWENTYFIVEG;
  mismatchConfig.ports()[0].profileID() =
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER;

  EXPECT_THROW(
      sw_->applyConfig(
          "Mismatch config with wrong speed and profile", mismatchConfig),
      FbossError);
}

TEST_F(ThriftTest, getCurrentStateJSON) {
  ThriftHandler handler(sw_);
  std::string out;
  std::string in = "portMaps/id=0/1";
  handler.getCurrentStateJSON(out, std::make_unique<std::string>(in));
  auto dyn = folly::parseJson(out);
  EXPECT_EQ(dyn["portId"], 1);
  EXPECT_EQ(dyn["portName"], "port1");
  EXPECT_EQ(dyn["portState"], "ENABLED");

  in = "portMaps/id=0/1/portOperState";
  handler.getCurrentStateJSON(out, std::make_unique<std::string>(in));
  EXPECT_EQ(out, "false");

  // Empty thrift path
  in = "";
  EXPECT_THROW(
      handler.getCurrentStateJSON(out, std::make_unique<std::string>(in)),
      FbossError);

  // Invalid thrift path
  in = "invalid/path";
  EXPECT_THROW(
      handler.getCurrentStateJSON(out, std::make_unique<std::string>(in)),
      FbossError);
}

TEST_F(ThriftTest, getCurrentStateJSONForPaths) {
  ThriftHandler handler(sw_);
  std::map<std::string, std::string> pathToState;
  std::string in = "portMaps/id=0/1";
  std::vector<std::string> paths = {in};
  handler.getCurrentStateJSONForPaths(
      pathToState, std::make_unique<std::vector<std::string>>(paths));
  ASSERT_TRUE(pathToState.find(in) != pathToState.end());
  auto dyn = folly::parseJson(pathToState[in]);
  EXPECT_EQ(dyn["portId"], 1);
  EXPECT_EQ(dyn["portName"], "port1");
  EXPECT_EQ(dyn["portState"], "ENABLED");

  in = "portMaps/id=0/1/portOperState";
  paths = {in};
  handler.getCurrentStateJSONForPaths(
      pathToState, std::make_unique<std::vector<std::string>>(paths));
  ASSERT_TRUE(pathToState.find(in) != pathToState.end());
  EXPECT_EQ(pathToState[in], "false");

  // Empty thrift path
  paths = {""};
  EXPECT_THROW(
      handler.getCurrentStateJSONForPaths(
          pathToState, std::make_unique<std::vector<std::string>>(paths)),
      FbossError);

  // Invalid thrift path
  paths = {"invalid/path"};
  EXPECT_THROW(
      handler.getCurrentStateJSONForPaths(
          pathToState, std::make_unique<std::vector<std::string>>(paths)),
      FbossError);
}

class ThriftTeFlowTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto config = testConfigA();
    cfg::ExactMatchTableConfig tableConfig;
    tableConfig.name() = "TeFlowTable";
    tableConfig.dstPrefixLength() = 64;
    config.switchSettings()->exactMatchTableConfigs() = {tableConfig};
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());

    sw_->getNeighborUpdater()->receivedNdpMineForIntf(
        kInterfaceA,
        folly::IPAddressV6(kNhopAddrA),
        kMacAddress,
        PortDescriptor(kPortIDA),
        ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
        0);

    sw_->getNeighborUpdater()->receivedNdpMineForIntf(
        kInterfaceB,
        folly::IPAddressV6(kNhopAddrB),
        kMacAddress,
        PortDescriptor(kPortIDB),
        ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
        0);
    sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(sw_);
    waitForStateUpdates(sw_);
  }
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

namespace {

MySidEntry makeMySidEntry(
    const std::string& addr,
    uint8_t len,
    MySidType type = MySidType::DECAPSULATE_AND_LOOKUP) {
  MySidEntry entry;
  entry.type() = type;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddressV6(addr));
  prefix.prefixLength() = len;
  entry.mySid() = prefix;
  return entry;
}

IpPrefix toMySidIpPrefix(const std::string& addr, uint8_t len) {
  IpPrefix prefix;
  prefix.ip() = facebook::network::toBinaryAddress(folly::IPAddressV6(addr));
  prefix.prefixLength() = len;
  return prefix;
}

facebook::network::thrift::IPPrefix toFrrMySidIpPrefix(
    const std::string& addr,
    uint8_t len) {
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddressV6(addr));
  prefix.prefixLength() = len;
  return prefix;
}

MySidEntry makeMySidEntryWithNextHops(
    const std::string& addr,
    uint8_t len,
    MySidType type,
    const std::vector<std::string>& nhAddrs) {
  MySidEntry entry = makeMySidEntry(addr, len, type);
  std::vector<NextHopThrift> nextHops;
  for (const auto& nhAddr : nhAddrs) {
    NextHopThrift nh;
    nh.address() =
        facebook::network::toBinaryAddress(folly::IPAddressV6(nhAddr));
    if (type == MySidType::BINDING_MICRO_SID) {
      nh.srv6SegmentList() = {facebook::network::toBinaryAddress(
          folly::IPAddressV6("2001:db8::10"))};
      nh.tunnelType() = TunnelType::SRV6_ENCAP;
      nh.tunnelId() = "tunnel1";
    }
    nextHops.push_back(std::move(nh));
  }
  entry.nextHops() = std::move(nextHops);
  return entry;
}

} // namespace

TEST_F(ThriftTest, addAdjacencyFrrRejectsMissingMySid) {
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  NextHopThrift backupNextHop;
  backupNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::ff"));
  backupNextHop.role() = NextHopRole::BACKUP;
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
  backupNextHops->push_back(std::move(backupNextHop));

  EXPECT_THROW(
      handler.addAdjacencyFrr(
          std::move(protectedObject), std::move(backupNextHops)),
      FbossError);
}

TEST_F(ThriftTestWithNhopIdMgr, addAdjacencyFrrRejectsEmptyBackupNextHops) {
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  state::MySidFields fields;
  fields.type() = MySidType::ADJACENCY_MICRO_SID;
  fields.mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  fields.adjacencyInterfaceId() = static_cast<int32_t>(kInterfaceA);
  fields.isV6() = true;
  fields.clientId() = ClientID::STATIC_ROUTE;
  auto mySid = std::make_shared<MySid>(fields);
  const RouteNextHopSet nextHops{
      ResolvedNextHop(folly::IPAddress(kNhopAddrA), kInterfaceA, ECMP_WEIGHT)};
  sw_->getRib()->update(
      sw_->getScopeResolver(),
      std::vector<MySidWithNextHops>{{mySid, nextHops, std::nullopt}},
      {},
      {},
      "add adjacency mysid via rib",
      ribMySidToSwitchStateFunc,
      sw_);

  ThriftHandler handler(sw_);
  auto protectedObjectWithBackup = std::make_unique<FrrProtectedObject>();
  protectedObjectWithBackup->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  NextHopThrift backupNextHop;
  backupNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::ff"));
  auto backupNextHopsWithBackup =
      std::make_unique<std::vector<NextHopThrift>>();
  backupNextHopsWithBackup->push_back(std::move(backupNextHop));
  handler.addAdjacencyFrr(
      std::move(protectedObjectWithBackup),
      std::move(backupNextHopsWithBackup));

  const folly::CIDRNetworkV6 prefix{folly::IPAddressV6("2001:db8::1"), 64};
  const auto mySidBefore = sw_->getRib()->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySidBefore.backupUnresolveNextHopsId().has_value());
  const auto backupId = *mySidBefore.backupUnresolveNextHopsId();
  const auto managerBefore = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(managerBefore, nullptr);
  const auto backupNextHopsBefore =
      managerBefore->getNextHops(NextHopSetID(backupId));

  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();

  EXPECT_THROW(
      handler.addAdjacencyFrr(
          std::move(protectedObject), std::move(backupNextHops)),
      FbossError);

  const auto mySidAfter = sw_->getRib()->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySidAfter.backupUnresolveNextHopsId().has_value());
  EXPECT_EQ(*mySidAfter.backupUnresolveNextHopsId(), backupId);
  const auto managerAfter = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(managerAfter, nullptr);
  EXPECT_EQ(
      managerAfter->getNextHops(NextHopSetID(backupId)), backupNextHopsBefore);
}

TEST_F(ThriftTest, addAdjacencyFrrRejectsSrv6NextHopWithoutTunnel) {
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  NextHopThrift backupNextHop;
  backupNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::ff"));
  backupNextHop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"))};
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
  backupNextHops->push_back(std::move(backupNextHop));

  // No SRV6_ENCAP tunnel in config, so no tunnelId can be defaulted.
  EXPECT_THROW(
      handler.addAdjacencyFrr(
          std::move(protectedObject), std::move(backupNextHops)),
      FbossError);
}

TEST_F(ThriftTest, addAdjacencyFrrRejectsBadLinkLocalInterface) {
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  NextHopThrift backupNextHop;
  backupNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::1"));
  backupNextHop.address()->ifName() = "fboss9999";
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
  backupNextHops->push_back(std::move(backupNextHop));

  EXPECT_THROW(
      handler.addAdjacencyFrr(
          std::move(protectedObject), std::move(backupNextHops)),
      FbossError);
}

TEST_F(ThriftTestWithNhopIdMgr, addAdjacencyFrrDefaultsSrv6Tunnel) {
  // Add an SRv6 tunnel to the config so the defaulting logic can find it.
  auto config = sw_->getConfig();
  cfg::Srv6Tunnel srv6Tunnel;
  srv6Tunnel.srv6TunnelId() = "srv6Tunnel0";
  srv6Tunnel.underlayIntfID() = 1;
  srv6Tunnel.tunnelType() = TunnelType::SRV6_ENCAP;
  srv6Tunnel.srcIp() = "2001:db8::100";
  config.srv6Tunnels() = {srv6Tunnel};
  sw_->applyConfig("Add SRv6 tunnel", config);

  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  state::MySidFields fields;
  fields.type() = MySidType::ADJACENCY_MICRO_SID;
  fields.mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  fields.adjacencyInterfaceId() = static_cast<int32_t>(kInterfaceA);
  fields.isV6() = true;
  fields.clientId() = ClientID::STATIC_ROUTE;
  auto mySid = std::make_shared<MySid>(fields);
  const RouteNextHopSet nextHops{
      ResolvedNextHop(folly::IPAddress(kNhopAddrA), kInterfaceA, ECMP_WEIGHT)};
  sw_->getRib()->update(
      sw_->getScopeResolver(),
      std::vector<MySidWithNextHops>{{mySid, nextHops, std::nullopt}},
      {},
      {},
      "add adjacency mysid via rib",
      ribMySidToSwitchStateFunc,
      sw_);

  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  NextHopThrift backupNextHop;
  backupNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::ff"));
  backupNextHop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"))};
  // tunnelId not set — should default to "srv6Tunnel0" from config.
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
  backupNextHops->push_back(std::move(backupNextHop));
  EXPECT_NO_THROW(handler.addAdjacencyFrr(
      std::move(protectedObject), std::move(backupNextHops)));

  const folly::CIDRNetworkV6 prefix{folly::IPAddressV6("2001:db8::1"), 64};
  const auto mySidEntry = sw_->getRib()->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySidEntry.backupUnresolveNextHopsId().has_value());
  const auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  const auto storedBackupNextHops = manager->getNextHops(
      NextHopSetID(*mySidEntry.backupUnresolveNextHopsId()));
  ASSERT_EQ(storedBackupNextHops.size(), 1);
  EXPECT_EQ(storedBackupNextHops.begin()->tunnelId(), "srv6Tunnel0");
}

TEST_F(ThriftTest, deleteAdjacencyFrrRejectsMissingMySid) {
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);

  EXPECT_THROW(
      handler.deleteAdjacencyFrr(std::move(protectedObject)), FbossError);
}

TEST_F(ThriftTest, addAdjacencyFrrRejectsMplsLabel) {
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mplsLabel() = 100;
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();

  EXPECT_THROW(
      handler.addAdjacencyFrr(
          std::move(protectedObject), std::move(backupNextHops)),
      FbossError);
}

TEST_F(ThriftTest, deleteAdjacencyFrrRejectsMplsLabel) {
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mplsLabel() = 100;

  EXPECT_THROW(
      handler.deleteAdjacencyFrr(std::move(protectedObject)), FbossError);
}

TEST_F(ThriftTest, addAdjacencyFrrRejectsUnsetProtectedObject) {
  // An unset union names no protected object. Rejecting it is what keeps the
  // call from reaching the RIB as an empty update, which would install no
  // protection and still report success.
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  NextHopThrift backupNextHop;
  backupNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::ff"));
  backupNextHop.role() = NextHopRole::BACKUP;
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
  backupNextHops->push_back(std::move(backupNextHop));

  EXPECT_THROW(
      handler.addAdjacencyFrr(
          std::move(protectedObject), std::move(backupNextHops)),
      FbossError);
}

TEST_F(ThriftTest, deleteAdjacencyFrrRejectsUnsetProtectedObject) {
  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();

  EXPECT_THROW(
      handler.deleteAdjacencyFrr(std::move(protectedObject)), FbossError);
}

TEST_F(ThriftTest, addMySidEntries) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::1", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::2", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  handler.addMySidEntries(std::move(entries));

  auto state = sw_->getState();
  auto mySids = state->getMySids();
  EXPECT_NE(nullptr, mySids);
  EXPECT_NE(nullptr, mySids->getNodeIf("2001:db8::1/64"));
  EXPECT_NE(nullptr, mySids->getNodeIf("2001:db8::2/64"));
}

TEST_F(ThriftTest, deleteMySidEntries) {
  ThriftHandler handler(sw_);

  // First add entries
  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::1", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::2", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  handler.addMySidEntries(std::move(entries));

  // Delete one
  auto prefixes = std::make_unique<std::vector<IpPrefix>>();
  prefixes->push_back(toMySidIpPrefix("2001:db8::1", 64));
  handler.deleteMySidEntries(std::move(prefixes));

  auto state = sw_->getState();
  auto mySids = state->getMySids();
  EXPECT_EQ(nullptr, mySids->getNodeIf("2001:db8::1/64"));
  EXPECT_NE(nullptr, mySids->getNodeIf("2001:db8::2/64"));
}

TEST_F(ThriftTest, addMySidEntryRejectsNodeType) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(
      makeMySidEntry("2001:db8::1", 64, MySidType::NODE_MICRO_SID));
  EXPECT_THROW(handler.addMySidEntries(std::move(entries)), FbossError);
}

TEST_F(ThriftTest, addMySidEntryRejectsAdjacencyType) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(
      makeMySidEntry("2001:db8::1", 64, MySidType::ADJACENCY_MICRO_SID));
  EXPECT_THROW(handler.addMySidEntries(std::move(entries)), FbossError);
}

TEST_F(ThriftTest, addMySidEntryRejectsDecapsulateType) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(makeMySidEntry("2001:db8::1", 64));
  EXPECT_THROW(handler.addMySidEntries(std::move(entries)), FbossError);
}

TEST_F(ThriftTest, addAndDeleteMySidEntriesInSequence) {
  ThriftHandler handler(sw_);

  // Add
  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::1", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  handler.addMySidEntries(std::move(entries));

  auto state = sw_->getState();
  EXPECT_NE(nullptr, state->getMySids()->getNodeIf("2001:db8::1/64"));

  // Delete
  auto prefixes = std::make_unique<std::vector<IpPrefix>>();
  prefixes->push_back(toMySidIpPrefix("2001:db8::1", 64));
  handler.deleteMySidEntries(std::move(prefixes));

  state = sw_->getState();
  EXPECT_EQ(nullptr, state->getMySids()->getNodeIf("2001:db8::1/64"));
}

TEST_F(ThriftTest, deleteNonExistentMySidEntryIsNoOp) {
  ThriftHandler handler(sw_);

  auto stateBefore = sw_->getState();

  // Delete a prefix that doesn't exist
  auto prefixes = std::make_unique<std::vector<IpPrefix>>();
  prefixes->push_back(toMySidIpPrefix("2001:db8::99", 64));
  handler.deleteMySidEntries(std::move(prefixes));

  auto stateAfter = sw_->getState();
  // MySids map should be the same (both empty)
  EXPECT_EQ(
      stateBefore->getMySids()->toThrift(),
      stateAfter->getMySids()->toThrift());
}

TEST_F(ThriftTest, mySidEntryReflectedInRib) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::1", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  handler.addMySidEntries(std::move(entries));

  // Verify RIB has the entry
  auto rib = sw_->getRib();
  auto ribMySidTable = rib->getMySidTableCopy();
  auto cidr = std::make_pair(folly::IPAddressV6("2001:db8::1"), 64);
  EXPECT_EQ(ribMySidTable.count(cidr), 1);

  // Verify SwitchState has the entry
  auto state = sw_->getState();
  EXPECT_NE(nullptr, state->getMySids()->getNodeIf("2001:db8::1/64"));
}

TEST_F(ThriftTest, addMySidEntryRejectsDecapTypeWithNamedNextHops) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  auto entry =
      makeMySidEntry("2001:db8::1", 64, MySidType::DECAPSULATE_AND_LOOKUP);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;
  entries->push_back(entry);
  EXPECT_THROW(handler.addMySidEntries(std::move(entries)), FbossError);
}

TEST_F(ThriftTest, getMySidEntriesEmpty) {
  ThriftHandler handler(sw_);

  std::vector<MySidEntry> result;
  handler.getMySidEntries(result);
  EXPECT_TRUE(result.empty());
}

TEST_F(ThriftTest, getMySidEntriesReturnsAddedEntries) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::1", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::2", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  handler.addMySidEntries(std::move(entries));

  std::vector<MySidEntry> result;
  handler.getMySidEntries(result);
  EXPECT_EQ(result.size(), 2);

  std::set<std::string> prefixes;
  for (const auto& e : result) {
    EXPECT_EQ(*e.type(), MySidType::BINDING_MICRO_SID);
    auto ip = facebook::network::toIPAddress(*e.mySid()->prefixAddress());
    prefixes.insert(
        folly::IPAddress::networkToString(
            {ip, (uint8_t)*e.mySid()->prefixLength()}));
  }
  EXPECT_THAT(
      prefixes,
      testing::UnorderedElementsAre("2001:db8::1/64", "2001:db8::2/64"));
}

TEST_F(ThriftTest, getMySidEntriesReflectsDelete) {
  ThriftHandler handler(sw_);

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::1", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  entries->push_back(makeMySidEntryWithNextHops(
      "2001:db8::2", 64, MySidType::BINDING_MICRO_SID, {"2001:db8::ff"}));
  handler.addMySidEntries(std::move(entries));

  auto prefixes = std::make_unique<std::vector<IpPrefix>>();
  prefixes->push_back(toMySidIpPrefix("2001:db8::1", 64));
  handler.deleteMySidEntries(std::move(prefixes));

  std::vector<MySidEntry> result;
  handler.getMySidEntries(result);
  EXPECT_EQ(result.size(), 1);
  auto ip = facebook::network::toIPAddress(*result[0].mySid()->prefixAddress());
  EXPECT_EQ(
      folly::IPAddress::networkToString(
          {ip, (uint8_t)*result[0].mySid()->prefixLength()}),
      "2001:db8::2/64");
}

TEST_F(ThriftTestWithNhopIdMgr, getMySidEntriesReportsBackupNextHops) {
  // Add an ADJACENCY MySid directly via the RIB (bypassing ThriftHandler
  // validation), then protect it via addAdjacencyFrr. The backup resolves
  // against connected routes configured in testConfigA().
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  state::MySidFields fields;
  fields.type() = MySidType::ADJACENCY_MICRO_SID;
  fields.mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  fields.adjacencyInterfaceId() = static_cast<int32_t>(kInterfaceA);
  fields.isV6() = true;
  fields.clientId() = ClientID::STATIC_ROUTE;
  auto mySid = std::make_shared<MySid>(fields);
  const RouteNextHopSet nextHops{
      ResolvedNextHop(folly::IPAddress(kNhopAddrA), kInterfaceA, ECMP_WEIGHT)};
  sw_->getRib()->update(
      sw_->getScopeResolver(),
      std::vector<MySidWithNextHops>{{mySid, nextHops, std::nullopt}},
      {},
      {},
      "add adjacency mysid via rib",
      ribMySidToSwitchStateFunc,
      sw_);

  ThriftHandler handler(sw_);
  auto protectedObject = std::make_unique<FrrProtectedObject>();
  protectedObject->mySid() = toFrrMySidIpPrefix("2001:db8::1", 64);
  NextHopThrift backupNextHop;
  backupNextHop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6(kNhopAddrB));
  auto backupNextHops = std::make_unique<std::vector<NextHopThrift>>();
  backupNextHops->push_back(std::move(backupNextHop));
  handler.addAdjacencyFrr(
      std::move(protectedObject), std::move(backupNextHops));

  std::vector<MySidEntry> result;
  handler.getMySidEntries(result);
  ASSERT_EQ(result.size(), 1);
  const auto& entry = result[0];
  ASSERT_EQ(entry.backupUnresolvedNextHops()->size(), 1);
  EXPECT_EQ(
      facebook::network::toIPAddress(
          *entry.backupUnresolvedNextHops()[0].address()),
      folly::IPAddress(kNhopAddrB));
  ASSERT_EQ(entry.backupResolvedNextHops()->size(), 1);
  EXPECT_EQ(
      facebook::network::toIPAddress(
          *entry.backupResolvedNextHops()[0].address()),
      folly::IPAddress(kNhopAddrB));
}

TEST_F(ThriftTestWithNhopIdMgr, getMySidEntriesNodeAndAdjacencyTypesViaRib) {
  // Add NODE and ADJACENCY MySid entries directly via the RIB (bypassing
  // ThriftHandler validation). Their nexthops resolve against connected routes
  // configured in testConfigA().
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);

  std::vector<MySidEntry> nodeToAdd = {
      makeMySidEntryWithNextHops(
          "2001:db8::1", 64, MySidType::NODE_MICRO_SID, {kNhopAddrA}),
  };
  sw_->getRib()->update(
      sw_->getScopeResolver(),
      nodeToAdd,
      {} /* toDelete */,
      "add node mysid via rib",
      ribMySidToSwitchStateFunc,
      sw_);

  // ADJACENCY_MICRO_SID — must be added via the `MySidWithNextHops`
  // overload so we can populate `adjacencyInterfaceId` + `isV6` and
  // pass a `ResolvedNextHop(neighborIP, intf)`.
  state::MySidFields adjFields;
  adjFields.type() = MySidType::ADJACENCY_MICRO_SID;
  facebook::network::thrift::IPPrefix adjPrefix;
  adjPrefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8::2"));
  adjPrefix.prefixLength() = 64;
  adjFields.mySid() = adjPrefix;
  adjFields.adjacencyInterfaceId() = static_cast<int32_t>(kInterfaceB);
  adjFields.isV6() = true;
  adjFields.clientId() = ClientID::STATIC_ROUTE;
  auto adjMySid = std::make_shared<MySid>(adjFields);
  RouteNextHopSet adjNhops{
      ResolvedNextHop(folly::IPAddress(kNhopAddrB), kInterfaceB, ECMP_WEIGHT)};
  std::vector<MySidWithNextHops> adjToAdd = {
      {adjMySid, adjNhops, std::nullopt}};
  sw_->getRib()->update(
      sw_->getScopeResolver(),
      std::move(adjToAdd),
      {} /* toUnresolveIfMatch */,
      {} /* toDelete */,
      "add adjacency mysid via rib",
      ribMySidToSwitchStateFunc,
      sw_);

  ThriftHandler handler(sw_);
  std::vector<MySidEntry> result;
  handler.getMySidEntries(result);
  ASSERT_EQ(result.size(), 2);

  // Sort by prefix for deterministic assertions
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return facebook::network::toIPAddress(*a.mySid()->prefixAddress()) <
        facebook::network::toIPAddress(*b.mySid()->prefixAddress());
  });

  // Entry 0: NODE_MICRO_SID with kNhopAddrA
  {
    const auto& entry = result[0];
    EXPECT_EQ(*entry.type(), MySidType::NODE_MICRO_SID);
    auto ip = facebook::network::toIPAddress(*entry.mySid()->prefixAddress());
    EXPECT_EQ(
        folly::IPAddress::networkToString(
            {ip, (uint8_t)*entry.mySid()->prefixLength()}),
        "2001:db8::1/64");
    ASSERT_EQ(entry.nextHops()->size(), 1);
    EXPECT_EQ(
        facebook::network::toIPAddress(*entry.nextHops()[0].address()),
        folly::IPAddress(kNhopAddrA));
    ASSERT_EQ(entry.resolvedNextHops()->size(), 1);
    EXPECT_EQ(
        facebook::network::toIPAddress(*entry.resolvedNextHops()[0].address()),
        folly::IPAddress(kNhopAddrA));
  }

  // Entry 1: ADJACENCY_MICRO_SID with kNhopAddrB
  {
    const auto& entry = result[1];
    EXPECT_EQ(*entry.type(), MySidType::ADJACENCY_MICRO_SID);
    auto ip = facebook::network::toIPAddress(*entry.mySid()->prefixAddress());
    EXPECT_EQ(
        folly::IPAddress::networkToString(
            {ip, (uint8_t)*entry.mySid()->prefixLength()}),
        "2001:db8::2/64");
    ASSERT_EQ(entry.nextHops()->size(), 1);
    EXPECT_EQ(
        facebook::network::toIPAddress(*entry.nextHops()[0].address()),
        folly::IPAddress(kNhopAddrB));
    ASSERT_EQ(entry.resolvedNextHops()->size(), 1);
    EXPECT_EQ(
        facebook::network::toIPAddress(*entry.resolvedNextHops()[0].address()),
        folly::IPAddress(kNhopAddrB));
  }
}

namespace {

NextHopThrift makeNextHopThrift(const std::string& ip, int weight = 0) {
  NextHopThrift nhop;
  nhop.address() = toBinaryAddress(folly::IPAddress(ip));
  nhop.weight() = weight;
  return nhop;
}

NextHopThrift makeSrv6NextHopThrift(const std::string& ip) {
  auto nhop = makeNextHopThrift(ip);
  nhop.srv6SegmentList() = {toBinaryAddress(folly::IPAddress("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = "tunnel1";
  return nhop;
}

NextHopGroup makeGroup(
    const std::string& name,
    const std::vector<std::string>& nhopIps) {
  NextHopGroup group;
  group.name() = name;
  std::vector<NextHopThrift> nhops;
  nhops.reserve(nhopIps.size());
  for (const auto& ip : nhopIps) {
    nhops.push_back(makeNextHopThrift(ip));
  }
  group.nexthops() = std::move(nhops);
  return group;
}

NextHopGroup makeSrv6Group(
    const std::string& name,
    const std::vector<std::string>& nhopIps) {
  NextHopGroup group;
  group.name() = name;
  std::vector<NextHopThrift> nhops;
  nhops.reserve(nhopIps.size());
  for (const auto& ip : nhopIps) {
    nhops.push_back(makeSrv6NextHopThrift(ip));
  }
  group.nexthops() = std::move(nhops);
  return group;
}

void addUnicastRouteWithNextHops(
    ThriftHandler& handler,
    const std::string& prefix,
    const std::vector<std::string>& nhopIps) {
  auto network = IPAddress::createNetwork(prefix);
  auto route = std::make_unique<UnicastRoute>();
  route->dest()->ip() = toBinaryAddress(network.first);
  route->dest()->prefixLength() = network.second;
  for (const auto& ip : nhopIps) {
    route->nextHopAddrs()->push_back(toBinaryAddress(IPAddress(ip)));
  }
  route->adminDistance() = AdminDistance::EBGP;
  handler.addUnicastRoute(
      static_cast<int16_t>(ClientID::BGPD), std::move(route));
}

// Unlike addUnicastRouteWithNextHops, this carries whole NextHopThrifts, so
// the route keeps per-next-hop attributes such as ifName and the SID list.
void addUnicastRouteWithNextHopThrifts(
    ThriftHandler& handler,
    const std::string& prefix,
    const std::vector<NextHopThrift>& nhops,
    ClientID client = ClientID::BGPD,
    AdminDistance adminDistance = AdminDistance::EBGP) {
  auto network = IPAddress::createNetwork(prefix);
  auto route = std::make_unique<UnicastRoute>();
  route->dest()->ip() = toBinaryAddress(network.first);
  route->dest()->prefixLength() = network.second;
  route->nextHops() = nhops;
  route->adminDistance() = adminDistance;
  handler.addUnicastRoute(static_cast<int16_t>(client), std::move(route));
}

void addUnicastRouteWithNamedNextHopGroup(
    ThriftHandler& handler,
    const std::string& prefix,
    const std::string& nhgName,
    ClientID client = ClientID::BGPD,
    AdminDistance adminDistance = AdminDistance::EBGP) {
  auto network = IPAddress::createNetwork(prefix);
  auto route = std::make_unique<UnicastRoute>();
  route->dest()->ip() = toBinaryAddress(network.first);
  route->dest()->prefixLength() = network.second;
  // Assign the whole NamedRouteDestination: namedRouteDestination is an
  // optional field, so reaching through operator-> writes the value without
  // ever marking it set, and the route reaches the RIB with no named group.
  NamedRouteDestination namedDest;
  namedDest.nextHopGroup() = nhgName;
  route->namedRouteDestination() = namedDest;
  route->adminDistance() = adminDistance;
  handler.addUnicastRoute(static_cast<int16_t>(client), std::move(route));
}

// Plain link-local next hop on an interface that exists in testConfigA.
NextHopThrift makeLinkLocalNextHopThrift(const std::string& ip) {
  auto nhop = makeNextHopThrift(ip);
  nhop.address()->ifName() = "fboss1";
  return nhop;
}

// Link-local next hop carrying an SRv6 SID list, on an interface that exists
// in testConfigA. Repeating one of these verbatim is what combining collapses
// into a single weighted next hop.
NextHopThrift makeLinkLocalSrv6NextHopThrift(
    const std::string& ip,
    const std::string& sid) {
  auto nhop = makeNextHopThrift(ip);
  nhop.address()->ifName() = "fboss1";
  nhop.srv6SegmentList() = {toBinaryAddress(folly::IPAddress(sid))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = "tunnel1";
  return nhop;
}

} // unnamed namespace

class NamedNextHopGroupThriftTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    auto config = testConfigA();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
  }

  void TearDown() override {
    FLAGS_enable_nexthop_id_manager = false;
  }

  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

TEST_F(NamedNextHopGroupThriftTest, addAndGetNextHopGroup) {
  ThriftHandler handler(sw_);

  // Add a named next-hop group
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1", {"2401:db00:2110:3001::2", "2401:db00:2110:3001::3"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  // Get group by name filter
  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back("group1");
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(*result[0].name(), "group1");
  EXPECT_EQ(result[0].nexthops()->size(), 2);
}

TEST_F(NamedNextHopGroupThriftTest, rejectMixedSrv6AndPlainNextHops) {
  ThriftHandler handler(sw_);

  NextHopGroup group;
  group.name() = "mixed_group";
  group.nexthops() = {
      makeLinkLocalNextHopThrift("fe80:face:b00c::1"),
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::2", "2001:db8::2")};
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(std::move(group));

  EXPECT_THROW(
      handler.addOrUpdateNamedNextHopGroups(std::move(groups)), FbossError);

  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back("mixed_group");
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  EXPECT_TRUE(result.empty());
}

namespace {
// Weight each next hop of a named group came back with, keyed by address.
std::map<std::string, int32_t> readGroupWeights(
    ThriftHandler& handler,
    const std::string& name) {
  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back(name);
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  CHECK_EQ(result.size(), 1);

  std::map<std::string, int32_t> weights;
  for (const auto& nhop : *result[0].nexthops()) {
    weights[facebook::network::toIPAddress(*nhop.address()).str()] =
        *nhop.weight();
  }
  return weights;
}
} // namespace

TEST_F(NamedNextHopGroupThriftTest, duplicateNextHopsCombinedWhenRequested) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1",
      {"2401:db00:2110:3001::2",
       "2401:db00:2110:3001::2",
       "2401:db00:2110:3001::3"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups), true);

  // The repeated next hop becomes one weighted next hop. Since something was
  // combined, the one listed once is given an explicit share of 1 rather than
  // ECMP_WEIGHT, so the weights survive route resolution.
  const std::map<std::string, int32_t> expected{
      {"2401:db00:2110:3001::2", 2}, {"2401:db00:2110:3001::3", 1}};
  EXPECT_EQ(readGroupWeights(handler, "group1"), expected);
}

TEST_F(NamedNextHopGroupThriftTest, addNamedNextHopGroupsCombinesDuplicates) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1",
      {"2401:db00:2110:3001::2",
       "2401:db00:2110:3001::2",
       "2401:db00:2110:3001::3"}));
  handler.addNamedNextHopGroups(std::move(groups), true);

  const std::map<std::string, int32_t> expected{
      {"2401:db00:2110:3001::2", 2}, {"2401:db00:2110:3001::3", 1}};
  EXPECT_EQ(readGroupWeights(handler, "group1"), expected);
}

TEST_F(NamedNextHopGroupThriftTest, combineFlagLeavesDistinctNextHopsAlone) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1",
      {"2401:db00:2110:3001::2",
       "2401:db00:2110:3001::3",
       "2401:db00:2110:3001::4"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups), true);

  // Nothing is duplicated, so every next hop keeps ECMP_WEIGHT and the group
  // is not promoted to UCMP.
  const std::map<std::string, int32_t> expected{
      {"2401:db00:2110:3001::2", 0},
      {"2401:db00:2110:3001::3", 0},
      {"2401:db00:2110:3001::4", 0}};
  EXPECT_EQ(readGroupWeights(handler, "group1"), expected);
}

TEST_F(
    NamedNextHopGroupThriftTest,
    addNamedNextHopGroupsCombineFlagLeavesDistinctNextHopsAlone) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1",
      {"2401:db00:2110:3001::2",
       "2401:db00:2110:3001::3",
       "2401:db00:2110:3001::4"}));
  handler.addNamedNextHopGroups(std::move(groups), true);

  const std::map<std::string, int32_t> expected{
      {"2401:db00:2110:3001::2", 0},
      {"2401:db00:2110:3001::3", 0},
      {"2401:db00:2110:3001::4", 0}};
  EXPECT_EQ(readGroupWeights(handler, "group1"), expected);
}

TEST_F(NamedNextHopGroupThriftTest, duplicateNextHopsDroppedByDefault) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1",
      {"2401:db00:2110:3001::2",
       "2401:db00:2110:3001::2",
       "2401:db00:2110:3001::3"}));
  // No flag passed, so this exercises the default.
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  // Repeat is dropped rather than combined, leaving both next hops at
  // ECMP_WEIGHT.
  const std::map<std::string, int32_t> expected{
      {"2401:db00:2110:3001::2", 0}, {"2401:db00:2110:3001::3", 0}};
  EXPECT_EQ(readGroupWeights(handler, "group1"), expected);
}

TEST_F(NamedNextHopGroupThriftTest, teAgentRouteOverridesOpenrWithWeights) {
  ThriftHandler handler(sw_);

  constexpr auto kPrefix = "fdad:ffff::4/128";
  constexpr auto kPrefixAddr = "fdad:ffff::4";

  // Both clients program the same prefix over the same three link-local next
  // hop addresses. OpenR's carry no SID list; the TE agent's are SRv6 next
  // hops reached through a named group that repeats one of them and is added
  // with combineDuplicatedNextHops.
  const auto openrNhop1 = makeLinkLocalNextHopThrift("fe80:face:b00c::1");
  const auto openrNhop2 = makeLinkLocalNextHopThrift("fe80:face:b00c::2");
  const auto openrNhop3 = makeLinkLocalNextHopThrift("fe80:face:b00c::3");

  const auto nhop1 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::1", "2001:db8::1");
  const auto nhop2 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::2", "2001:db8::2");
  const auto nhop3 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::3", "2001:db8::3");

  // Resolved next hops straight off the FIB, as weight and SID list keyed by
  // address. Going through getIpRouteDetails instead would report every
  // ECMP_WEIGHT next hop as 1 and hide the difference between the two clients.
  auto resolvedNextHops = [this, kPrefixAddr]() {
    auto state = sw_->getState();
    auto route = findRoute<folly::IPAddressV6>(
        RouterID(0), {folly::IPAddress(kPrefixAddr), 128}, state);
    CHECK(route) << "route " << kPrefixAddr << " not in the FIB";
    std::map<std::string, NextHopWeight> weights;
    std::map<std::string, std::vector<std::string>> sids;
    for (const auto& nhop : getNextHops(state, route->getForwardInfo())) {
      const auto addr = nhop.addr().str();
      weights[addr] = nhop.weight();
      for (const auto& sid : nhop.srv6SegmentList()) {
        sids[addr].push_back(sid.str());
      }
    }
    return std::make_pair(weights, sids);
  };

  // OpenR gets there first, carrying its next hops on the route itself. With
  // no named group there is nothing to combine, so the route is plain ECMP,
  // and its next hops carry no SID list.
  addUnicastRouteWithNextHopThrifts(
      handler,
      kPrefix,
      {openrNhop1, openrNhop2, openrNhop3},
      ClientID::OPENR,
      AdminDistance::OPENR);

  auto [openrWeights, openrSids] = resolvedNextHops();
  const std::map<std::string, NextHopWeight> expectedOpenrWeights{
      {"fe80:face:b00c::1", ECMP_WEIGHT},
      {"fe80:face:b00c::2", ECMP_WEIGHT},
      {"fe80:face:b00c::3", ECMP_WEIGHT}};
  EXPECT_EQ(openrWeights, expectedOpenrWeights);
  EXPECT_TRUE(openrSids.empty());

  // The TE agent then programs the same prefix through a combined group.
  // AdminDistance::TE_AGENT (2) beats OPENR (10), so its entry becomes the
  // route's best entry and the combined weights take effect. The comparison
  // in RouteNextHopsMulti is strictly less-than, so this only displaces OpenR
  // because the distance is genuinely lower, not because it arrived later.
  NextHopGroup teAgentGroup;
  teAgentGroup.name() = "te_agent_group";
  teAgentGroup.nexthops() = {nhop1, nhop1, nhop2, nhop3};
  auto teAgentGroups = std::make_unique<std::vector<NextHopGroup>>();
  teAgentGroups->push_back(std::move(teAgentGroup));
  handler.addOrUpdateNamedNextHopGroups(
      std::move(teAgentGroups), /*combineDuplicatedNextHops=*/true);

  addUnicastRouteWithNamedNextHopGroup(
      handler,
      kPrefix,
      "te_agent_group",
      ClientID::TE_AGENT,
      AdminDistance::TE_AGENT);

  std::vector<NextHopGroup> programmedGroups;
  auto groupNames = std::make_unique<std::vector<std::string>>();
  groupNames->push_back("te_agent_group");
  handler.getNamedNextHopGroups(programmedGroups, std::move(groupNames));
  ASSERT_EQ(programmedGroups.size(), 1);
  ASSERT_TRUE(programmedGroups[0].isProgrammed().has_value());
  EXPECT_TRUE(*programmedGroups[0].isProgrammed());

  auto [teAgentWeights, teAgentSids] = resolvedNextHops();
  const std::map<std::string, NextHopWeight> expectedTeAgentWeights{
      {"fe80:face:b00c::1", 2},
      {"fe80:face:b00c::2", 1},
      {"fe80:face:b00c::3", 1}};
  // Same next hop addresses as OpenR's, but now each carries its SID list.
  const std::map<std::string, std::vector<std::string>> expectedTeAgentSids{
      {"fe80:face:b00c::1", {"2001:db8::1"}},
      {"fe80:face:b00c::2", {"2001:db8::2"}},
      {"fe80:face:b00c::3", {"2001:db8::3"}}};
  EXPECT_EQ(teAgentWeights, expectedTeAgentWeights);
  EXPECT_EQ(teAgentSids, expectedTeAgentSids);
}

TEST_F(
    NamedNextHopGroupThriftTest,
    teAgentRouteOverridesOpenrWithNormalizedWeights) {
  ThriftHandler handler(sw_);

  constexpr auto kPrefix = "fdad:ffff::4/128";
  constexpr auto kPrefixAddr = "fdad:ffff::4";

  const auto openrNhop1 = makeLinkLocalNextHopThrift("fe80:face:b00c::1");
  const auto openrNhop2 = makeLinkLocalNextHopThrift("fe80:face:b00c::2");
  const auto nhop1 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::1", "2001:db8::1");
  const auto nhop2 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::2", "2001:db8::2");

  addUnicastRouteWithNextHopThrifts(
      handler,
      kPrefix,
      {openrNhop1, openrNhop2},
      ClientID::OPENR,
      AdminDistance::OPENR);

  NextHopGroup teAgentGroup;
  teAgentGroup.name() = "te_agent_group";
  teAgentGroup.nexthops() = {nhop1, nhop1, nhop2, nhop2};
  auto teAgentGroups = std::make_unique<std::vector<NextHopGroup>>();
  teAgentGroups->push_back(std::move(teAgentGroup));
  handler.addOrUpdateNamedNextHopGroups(
      std::move(teAgentGroups), /*combineDuplicatedNextHops=*/true);

  addUnicastRouteWithNamedNextHopGroup(
      handler,
      kPrefix,
      "te_agent_group",
      ClientID::TE_AGENT,
      AdminDistance::TE_AGENT);

  std::vector<NextHopGroup> programmedGroups;
  auto groupNames = std::make_unique<std::vector<std::string>>();
  groupNames->push_back("te_agent_group");
  handler.getNamedNextHopGroups(programmedGroups, std::move(groupNames));
  ASSERT_EQ(programmedGroups.size(), 1);
  ASSERT_TRUE(programmedGroups[0].isProgrammed().has_value());
  EXPECT_TRUE(*programmedGroups[0].isProgrammed());

  auto state = sw_->getState();
  auto route = findRoute<folly::IPAddressV6>(
      RouterID(0), {folly::IPAddress(kPrefixAddr), 128}, state);
  ASSERT_NE(route, nullptr);
  std::map<std::string, NextHopWeight> weights;
  for (const auto& nhop : getNextHops(state, route->getForwardInfo())) {
    weights[nhop.addr().str()] = nhop.weight();
  }
  const std::map<std::string, NextHopWeight> expectedWeights{
      {"fe80:face:b00c::1", 1}, {"fe80:face:b00c::2", 1}};
  EXPECT_EQ(weights, expectedWeights);

  std::vector<RouteDetails> routeDetails;
  handler.getRouteTableDetails(routeDetails);
  const auto detail = std::find_if(
      routeDetails.begin(), routeDetails.end(), [](const auto& routeDetail) {
        return facebook::network::toIPAddress(*routeDetail.dest()->ip()) ==
            folly::IPAddress("fdad:ffff::4");
      });
  ASSERT_NE(detail, routeDetails.end());
  ASSERT_TRUE(detail->resolvedNextHops().has_value());

  std::map<std::string, NextHopWeight> postNormalizationWeights;
  for (const auto& nhop : *detail->nextHops()) {
    postNormalizationWeights[facebook::network::toIPAddress(*nhop.address())
                                 .str()] = *nhop.weight();
  }
  std::map<std::string, NextHopWeight> preNormalizationWeights;
  for (const auto& nhop : *detail->resolvedNextHops()) {
    preNormalizationWeights[facebook::network::toIPAddress(*nhop.address())
                                .str()] = *nhop.weight();
  }
  const std::map<std::string, NextHopWeight> expectedPreNormalizationWeights{
      {"fe80:face:b00c::1", 2}, {"fe80:face:b00c::2", 2}};
  const std::map<std::string, NextHopWeight> expectedPostNormalizationWeights{
      {"fe80:face:b00c::1", 1}, {"fe80:face:b00c::2", 1}};
  EXPECT_EQ(preNormalizationWeights, expectedPreNormalizationWeights);
  EXPECT_EQ(postNormalizationWeights, expectedPostNormalizationWeights);
  EXPECT_NE(preNormalizationWeights, postNormalizationWeights);
}

TEST_F(NamedNextHopGroupThriftTest, bgpRouteRecursesOntoTeAgentWeights) {
  ThriftHandler handler(sw_);

  constexpr auto kTePrefix = "fdad:ffff::4/128";
  constexpr auto kTePrefixAddr = "fdad:ffff::4";
  constexpr auto kBgpPrefix = "100::/64";
  // findRoute is an exact prefix match, so this is the network address, not
  // an address inside the prefix.
  constexpr auto kBgpPrefixAddr = "100::";

  const auto openrNhop1 = makeLinkLocalNextHopThrift("fe80:face:b00c::1");
  const auto openrNhop2 = makeLinkLocalNextHopThrift("fe80:face:b00c::2");
  const auto openrNhop3 = makeLinkLocalNextHopThrift("fe80:face:b00c::3");

  const auto nhop1 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::1", "2001:db8::1");
  const auto nhop2 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::2", "2001:db8::2");
  const auto nhop3 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::3", "2001:db8::3");

  auto resolvedWeights = [this](const std::string& addr, uint8_t mask) {
    auto state = sw_->getState();
    auto route = findRoute<folly::IPAddressV6>(
        RouterID(0), {folly::IPAddress(addr), mask}, state);
    CHECK(route) << "route " << addr << "/" << static_cast<int>(mask)
                 << " not in the FIB";
    std::map<std::string, NextHopWeight> weights;
    for (const auto& nhop : getNextHops(state, route->getForwardInfo())) {
      weights[nhop.addr().str()] = nhop.weight();
    }
    return weights;
  };

  // Same two steps as teAgentRouteOverridesOpenrWithWeights: OpenR programs
  // the prefix unweighted, then the TE agent takes it over with a combined
  // group and wins on admin distance.
  addUnicastRouteWithNextHopThrifts(
      handler,
      kTePrefix,
      {openrNhop1, openrNhop2, openrNhop3},
      ClientID::OPENR,
      AdminDistance::OPENR);

  NextHopGroup teAgentGroup;
  teAgentGroup.name() = "te_agent_group";
  teAgentGroup.nexthops() = {nhop1, nhop1, nhop2, nhop3};
  auto teAgentGroups = std::make_unique<std::vector<NextHopGroup>>();
  teAgentGroups->push_back(std::move(teAgentGroup));
  handler.addOrUpdateNamedNextHopGroups(
      std::move(teAgentGroups), /*combineDuplicatedNextHops=*/true);

  addUnicastRouteWithNamedNextHopGroup(
      handler,
      kTePrefix,
      "te_agent_group",
      ClientID::TE_AGENT,
      AdminDistance::TE_AGENT);

  std::vector<NextHopGroup> programmedGroups;
  auto groupNames = std::make_unique<std::vector<std::string>>();
  groupNames->push_back("te_agent_group");
  handler.getNamedNextHopGroups(programmedGroups, std::move(groupNames));
  ASSERT_EQ(programmedGroups.size(), 1);
  ASSERT_TRUE(programmedGroups[0].isProgrammed().has_value());
  EXPECT_TRUE(*programmedGroups[0].isProgrammed());

  const std::map<std::string, NextHopWeight> expectedWeights{
      {"fe80:face:b00c::1", 2},
      {"fe80:face:b00c::2", 1},
      {"fe80:face:b00c::3", 1}};
  EXPECT_EQ(resolvedWeights(kTePrefixAddr, 128), expectedWeights);

  // A BGP route recursing through that prefix inherits the same weighted next
  // hops. Its own next hop is at ECMP_WEIGHT, as an ordinary BGP next hop is:
  // stating no weight of its own, it does not override the weights the TE
  // agent programmed underneath it.
  addUnicastRouteWithNextHops(handler, kBgpPrefix, {kTePrefixAddr});

  EXPECT_EQ(resolvedWeights(kBgpPrefixAddr, 64), expectedWeights);

  std::vector<RouteDetails> routeDetails;
  handler.getRouteTableDetails(routeDetails);
  const auto detail = std::find_if(
      routeDetails.begin(), routeDetails.end(), [](const auto& routeDetail) {
        return facebook::network::toIPAddress(*routeDetail.dest()->ip()) ==
            folly::IPAddress("100::");
      });
  ASSERT_NE(detail, routeDetails.end());
  ASSERT_TRUE(detail->resolvedNextHops().has_value());

  std::map<std::string, NextHopWeight> preNormalizationWeights;
  for (const auto& nhop : *detail->resolvedNextHops()) {
    preNormalizationWeights[facebook::network::toIPAddress(*nhop.address())
                                .str()] = *nhop.weight();
  }
  std::map<std::string, NextHopWeight> postNormalizationWeights;
  for (const auto& nhop : *detail->nextHops()) {
    postNormalizationWeights[facebook::network::toIPAddress(*nhop.address())
                                 .str()] = *nhop.weight();
  }
  // The inherited weights have a GCD of 1, so normalization preserves them.
  const std::map<std::string, NextHopWeight> expectedPreNormalizationWeights{
      {"fe80:face:b00c::1", 2},
      {"fe80:face:b00c::2", 1},
      {"fe80:face:b00c::3", 1}};
  const std::map<std::string, NextHopWeight> expectedPostNormalizationWeights{
      {"fe80:face:b00c::1", 2},
      {"fe80:face:b00c::2", 1},
      {"fe80:face:b00c::3", 1}};
  EXPECT_EQ(preNormalizationWeights, expectedPreNormalizationWeights);
  EXPECT_EQ(postNormalizationWeights, expectedPostNormalizationWeights);
}

TEST_F(
    NamedNextHopGroupThriftTest,
    bgpRouteRecursesOntoTeAgentNormalizedWeights) {
  ThriftHandler handler(sw_);

  constexpr auto kTePrefix = "fdad:ffff::4/128";
  constexpr auto kTePrefixAddr = "fdad:ffff::4";
  constexpr auto kBgpPrefix = "100::/64";
  constexpr auto kBgpPrefixAddr = "100::";

  const auto openrNhop1 = makeLinkLocalNextHopThrift("fe80:face:b00c::1");
  const auto openrNhop2 = makeLinkLocalNextHopThrift("fe80:face:b00c::2");
  const auto nhop1 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::1", "2001:db8::1");
  const auto nhop2 =
      makeLinkLocalSrv6NextHopThrift("fe80:face:b00c::2", "2001:db8::2");

  auto resolvedWeights = [this](const std::string& addr, uint8_t mask) {
    auto state = sw_->getState();
    auto route = findRoute<folly::IPAddressV6>(
        RouterID(0), {folly::IPAddress(addr), mask}, state);
    CHECK(route) << "route " << addr << "/" << static_cast<int>(mask)
                 << " not in the FIB";
    std::map<std::string, NextHopWeight> weights;
    for (const auto& nhop : getNextHops(state, route->getForwardInfo())) {
      weights[nhop.addr().str()] = nhop.weight();
    }
    return weights;
  };

  addUnicastRouteWithNextHopThrifts(
      handler,
      kTePrefix,
      {openrNhop1, openrNhop2},
      ClientID::OPENR,
      AdminDistance::OPENR);

  NextHopGroup teAgentGroup;
  teAgentGroup.name() = "te_agent_group";
  teAgentGroup.nexthops() = {nhop1, nhop1, nhop2, nhop2};
  auto teAgentGroups = std::make_unique<std::vector<NextHopGroup>>();
  teAgentGroups->push_back(std::move(teAgentGroup));
  handler.addOrUpdateNamedNextHopGroups(
      std::move(teAgentGroups), /*combineDuplicatedNextHops=*/true);

  addUnicastRouteWithNamedNextHopGroup(
      handler,
      kTePrefix,
      "te_agent_group",
      ClientID::TE_AGENT,
      AdminDistance::TE_AGENT);

  const std::map<std::string, NextHopWeight> expectedTeWeights{
      {"fe80:face:b00c::1", 1}, {"fe80:face:b00c::2", 1}};
  EXPECT_EQ(resolvedWeights(kTePrefixAddr, 128), expectedTeWeights);

  addUnicastRouteWithNextHops(handler, kBgpPrefix, {kTePrefixAddr});

  const std::map<std::string, NextHopWeight> expectedBgpWeights{
      {"fe80:face:b00c::1", ECMP_WEIGHT}, {"fe80:face:b00c::2", ECMP_WEIGHT}};
  EXPECT_EQ(resolvedWeights(kBgpPrefixAddr, 64), expectedBgpWeights);

  std::vector<NextHopGroup> programmedGroups;
  auto groupNames = std::make_unique<std::vector<std::string>>();
  groupNames->push_back("te_agent_group");
  handler.getNamedNextHopGroups(programmedGroups, std::move(groupNames));
  ASSERT_EQ(programmedGroups.size(), 1);
  ASSERT_TRUE(programmedGroups[0].isProgrammed().has_value());
  EXPECT_TRUE(*programmedGroups[0].isProgrammed());

  std::vector<RouteDetails> routeDetails;
  handler.getRouteTableDetails(routeDetails);
  const auto detail = std::find_if(
      routeDetails.begin(), routeDetails.end(), [](const auto& routeDetail) {
        return facebook::network::toIPAddress(*routeDetail.dest()->ip()) ==
            folly::IPAddress("100::");
      });
  ASSERT_NE(detail, routeDetails.end());
  ASSERT_TRUE(detail->resolvedNextHops().has_value());

  std::map<std::string, NextHopWeight> postNormalizationWeights;
  for (const auto& nhop : *detail->nextHops()) {
    postNormalizationWeights[facebook::network::toIPAddress(*nhop.address())
                                 .str()] = *nhop.weight();
  }
  std::map<std::string, NextHopWeight> preNormalizationWeights;
  for (const auto& nhop : *detail->resolvedNextHops()) {
    preNormalizationWeights[facebook::network::toIPAddress(*nhop.address())
                                .str()] = *nhop.weight();
  }
  // Recursing through equal TE weights produces unweighted ECMP next hops.
  // Route details preserve ECMP_WEIGHT (0) before normalization and report
  // explicit weights of 1 after normalization.
  const std::map<std::string, NextHopWeight> expectedPreNormalizationWeights{
      {"fe80:face:b00c::1", ECMP_WEIGHT}, {"fe80:face:b00c::2", ECMP_WEIGHT}};
  const std::map<std::string, NextHopWeight> expectedPostNormalizationWeights{
      {"fe80:face:b00c::1", 1}, {"fe80:face:b00c::2", 1}};
  EXPECT_EQ(preNormalizationWeights, expectedPreNormalizationWeights);
  EXPECT_EQ(postNormalizationWeights, expectedPostNormalizationWeights);
  EXPECT_NE(preNormalizationWeights, postNormalizationWeights);
}

TEST_F(NamedNextHopGroupThriftTest, combinedGroupWeightsFollowGroupUpdates) {
  ThriftHandler handler(sw_);

  constexpr auto kNhop1 = "2401:db00:2110:3001::2";
  constexpr auto kNhop2 = "2401:db00:2110:3001::3";
  constexpr auto kNhop3 = "2401:db00:2110:3055::2";
  constexpr auto kPrefix = "2401::100/128";
  constexpr auto kPrefixAddr = "2401::100";

  // Every add below passes combineDuplicatedNextHops, so the only thing that
  // varies across the three steps is whether a next hop is repeated. Both
  // this and the route add below come back with the FIB already updated:
  // the RIB call blocks on its own thread and the rib-to-switch-state
  // function it runs blocks on the state update, so no waiting is needed.
  auto setGroupNextHops = [&handler](const std::vector<std::string>& ips) {
    auto groups = std::make_unique<std::vector<NextHopGroup>>();
    groups->push_back(makeGroup("group1", ips));
    handler.addOrUpdateNamedNextHopGroups(std::move(groups), true);
  };

  auto resolvedWeights = [this, kPrefixAddr]() {
    auto state = sw_->getState();
    auto route = findRoute<folly::IPAddressV6>(
        RouterID(0), {folly::IPAddress(kPrefixAddr), 128}, state);
    CHECK(route) << "route " << kPrefixAddr << " not in the FIB";
    std::map<std::string, NextHopWeight> weights;
    for (const auto& nhop : getNextHops(state, route->getForwardInfo())) {
      weights[nhop.addr().str()] = nhop.weight();
    }
    return weights;
  };

  // Nothing repeated, so there is nothing to combine and the group stays a
  // plain ECMP group.
  setGroupNextHops({kNhop1, kNhop2, kNhop3});
  addUnicastRouteWithNamedNextHopGroup(handler, kPrefix, "group1");

  // Nothing was combined, so every next hop stays at ECMP_WEIGHT in both the
  // group and the resolved route.
  const std::map<std::string, int32_t> unweightedGroup{
      {kNhop1, 0}, {kNhop2, 0}, {kNhop3, 0}};
  const std::map<std::string, NextHopWeight> unweightedResolved{
      {kNhop1, ECMP_WEIGHT}, {kNhop2, ECMP_WEIGHT}, {kNhop3, ECMP_WEIGHT}};
  EXPECT_EQ(readGroupWeights(handler, "group1"), unweightedGroup);
  EXPECT_EQ(resolvedWeights(), unweightedResolved);

  // Repeat one next hop. It takes the pair's weight and, because the set can
  // no longer carry any ECMP_WEIGHT member without resolution downgrading the
  // whole thing, the other two are given an explicit share of 1. Group and
  // resolved route agree once everything is weighted.
  setGroupNextHops({kNhop1, kNhop1, kNhop2, kNhop3});

  const std::map<std::string, int32_t> weightedGroup{
      {kNhop1, 2}, {kNhop2, 1}, {kNhop3, 1}};
  const std::map<std::string, NextHopWeight> weightedResolved{
      {kNhop1, 2}, {kNhop2, 1}, {kNhop3, 1}};
  EXPECT_EQ(readGroupWeights(handler, "group1"), weightedGroup);
  EXPECT_EQ(resolvedWeights(), weightedResolved);

  // Drop the repeat again and both views go back to plain ECMP.
  setGroupNextHops({kNhop1, kNhop2, kNhop3});
  EXPECT_EQ(readGroupWeights(handler, "group1"), unweightedGroup);
  EXPECT_EQ(resolvedWeights(), unweightedResolved);
}

namespace {
// Addresses of a group's next hops, sorted, with repeats preserved.
std::vector<std::string> nextHopAddrs(const NextHopGroup& group) {
  std::vector<std::string> addrs;
  for (const auto& nhop : *group.nexthops()) {
    addrs.push_back(facebook::network::toIPAddress(*nhop.address()).str());
  }
  std::sort(addrs.begin(), addrs.end());
  return addrs;
}

std::vector<std::string> readGroupNextHopAddrs(
    ThriftHandler& handler,
    const std::string& name,
    bool replicateWeightedNexthops) {
  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back(name);
  handler.getNamedNextHopGroups(
      result, std::move(nameFilter), replicateWeightedNexthops);
  CHECK_EQ(result.size(), 1);
  return nextHopAddrs(result[0]);
}

// A group whose repeated next hop was combined into a single weighted one.
void addCombinedDuplicateGroup(ThriftHandler& handler) {
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1",
      {"2401:db00:2110:3001::2",
       "2401:db00:2110:3001::2",
       "2401:db00:2110:3001::3"}));
  handler.addOrUpdateNamedNextHopGroups(
      std::move(groups), true /* combineDuplicatedNextHops */);
}
} // namespace

TEST_F(NamedNextHopGroupThriftTest, weightedNextHopsReplicatedWhenRequested) {
  ThriftHandler handler(sw_);
  addCombinedDuplicateGroup(handler);

  // The weight-2 next hop comes back as the two next hops it was built from.
  const std::vector<std::string> expected{
      "2401:db00:2110:3001::2",
      "2401:db00:2110:3001::2",
      "2401:db00:2110:3001::3"};
  EXPECT_EQ(
      readGroupNextHopAddrs(
          handler, "group1", true /* replicateWeightedNexthops */),
      expected);
}

TEST_F(NamedNextHopGroupThriftTest, weightedNextHopsNotReplicatedByDefault) {
  ThriftHandler handler(sw_);
  addCombinedDuplicateGroup(handler);

  const std::vector<std::string> expected{
      "2401:db00:2110:3001::2", "2401:db00:2110:3001::3"};
  EXPECT_EQ(
      readGroupNextHopAddrs(
          handler, "group1", false /* replicateWeightedNexthops */),
      expected);
}

TEST_F(
    NamedNextHopGroupThriftTest,
    getNextHopGroupsReplicatesWeightedNextHops) {
  ThriftHandler handler(sw_);
  addCombinedDuplicateGroup(handler);

  std::vector<NextHopGroup> result;
  handler.getNextHopGroups(result, true /* replicateWeightedNexthops */);

  const NextHopGroup* group = nullptr;
  for (const auto& g : result) {
    if (g.name().has_value() && *g.name() == "group1") {
      group = &g;
    }
  }
  ASSERT_NE(group, nullptr);

  const std::vector<std::string> expected{
      "2401:db00:2110:3001::2",
      "2401:db00:2110:3001::2",
      "2401:db00:2110:3001::3"};
  EXPECT_EQ(nextHopAddrs(*group), expected);
}

TEST_F(NamedNextHopGroupThriftTest, addNamedNextHopGroups) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "group1", {"2401:db00:2110:3001::2", "2401:db00:2110:3001::3"}));
  handler.addNamedNextHopGroups(std::move(groups));

  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back("group1");
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(*result[0].name(), "group1");
  EXPECT_EQ(result[0].nexthops()->size(), 2);
}

TEST_F(NamedNextHopGroupThriftTest, addMultipleGroups) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("groupA", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("groupB", {"2401:db00:2110:3055::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  // Query both named groups by filter
  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back("groupA");
  nameFilter->push_back("groupB");
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  ASSERT_EQ(result.size(), 2);

  std::set<std::string> names;
  for (const auto& g : result) {
    names.insert(*g.name());
  }
  EXPECT_EQ(names.count("groupA"), 1);
  EXPECT_EQ(names.count("groupB"), 1);
}

TEST_F(NamedNextHopGroupThriftTest, getNextHopGroupsByName) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("groupA", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("groupB", {"2401:db00:2110:3055::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  // Query only groupA
  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back("groupA");
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(*result[0].name(), "groupA");
}

TEST_F(NamedNextHopGroupThriftTest, getEmptyResult) {
  ThriftHandler handler(sw_);

  // Query a specific name that doesn't exist
  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back("nonexistent");
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  EXPECT_TRUE(result.empty());
}

TEST_F(NamedNextHopGroupThriftTest, getNonExistentGroupByName) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("groupA", {"2401:db00:2110:3001::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  // Query a name that doesn't exist
  std::vector<NextHopGroup> result;
  auto nameFilter = std::make_unique<std::vector<std::string>>();
  nameFilter->push_back("nonexistent");
  handler.getNamedNextHopGroups(result, std::move(nameFilter));
  EXPECT_TRUE(result.empty());
}

TEST_F(NamedNextHopGroupThriftTest, namedGroupNotProgrammedByDefault) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("orphan", {"2401:db00:2110:3001::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  std::vector<NextHopGroup> result;
  auto names = std::make_unique<std::vector<std::string>>();
  names->push_back("orphan");
  handler.getNamedNextHopGroups(result, std::move(names));
  ASSERT_EQ(result.size(), 1);
  ASSERT_TRUE(result[0].isProgrammed().has_value());
  EXPECT_FALSE(*result[0].isProgrammed());
}

TEST_F(NamedNextHopGroupThriftTest, getAllNamedGroups) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("groupA", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("groupB", {"2401:db00:2110:3055::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  std::vector<NextHopGroup> result;
  auto emptyNames = std::make_unique<std::vector<std::string>>();
  handler.getNamedNextHopGroups(result, std::move(emptyNames));
  ASSERT_EQ(result.size(), 2);

  std::set<std::string> foundNames;
  for (const auto& g : result) {
    foundNames.insert(*g.name());
    EXPECT_TRUE(g.isProgrammed().has_value());
  }
  EXPECT_EQ(foundNames.count("groupA"), 1);
  EXPECT_EQ(foundNames.count("groupB"), 1);
}

TEST_F(
    NamedNextHopGroupThriftTest,
    getNextHopGroupsIncludesNamedAndFiltersUnnamedSingletons) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(
      makeGroup("used", {"2401:db00:2110:3001::2", "2401:db00:2110:3001::3"}));
  groups->push_back(makeGroup("orphan", {"2401:db00:2110:3055::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));
  addUnicastRouteWithNextHops(handler, "2401::101/128", {kNhopAddrA});
  addUnicastRouteWithNextHops(
      handler, "2401::102/128", {kNhopAddrA, kNhopAddrB});

  std::vector<NextHopGroup> result;
  handler.getNextHopGroups(result);
  ASSERT_FALSE(result.empty());

  const NextHopGroup* usedGroup = nullptr;
  const NextHopGroup* orphanGroup = nullptr;
  for (const auto& g : result) {
    if (g.name().has_value()) {
      if (*g.name() == "used") {
        usedGroup = &g;
      } else if (*g.name() == "orphan") {
        orphanGroup = &g;
      }
      continue;
    }

    EXPECT_TRUE(g.isProgrammed().has_value());
    EXPECT_TRUE(*g.isProgrammed());
    EXPECT_GE(g.nexthops()->size(), 2);
  }

  ASSERT_NE(usedGroup, nullptr);
  ASSERT_TRUE(usedGroup->isProgrammed().has_value());
  EXPECT_FALSE(*usedGroup->isProgrammed());

  ASSERT_NE(orphanGroup, nullptr);
  ASSERT_TRUE(orphanGroup->isProgrammed().has_value());
  EXPECT_FALSE(*orphanGroup->isProgrammed());
  EXPECT_EQ(orphanGroup->nexthops()->size(), 1);
}

TEST_F(NamedNextHopGroupThriftTest, nextHopGroupsReportNextHopSetId) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("group1", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("group2", {"2401:db00:2110:3055::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  std::map<std::string, int64_t> expected;
  auto fibInfo = sw_->getState()->getFibsInfoMap()->cbegin()->second;
  for (const auto& [name, setId] : fibInfo->getNameToNextHopSetId()) {
    expected[name] = setId;
  }
  ASSERT_EQ(expected.size(), 2);

  auto toNameToId = [](const std::vector<NextHopGroup>& result) {
    std::map<std::string, int64_t> nameToId;
    for (const auto& group : result) {
      if (group.name().has_value() && group.id().has_value()) {
        nameToId[*group.name()] = *group.id();
      }
    }
    return nameToId;
  };

  std::vector<NextHopGroup> namedGroups;
  handler.getNamedNextHopGroups(
      namedGroups, std::make_unique<std::vector<std::string>>());
  EXPECT_EQ(toNameToId(namedGroups), expected);

  std::vector<NextHopGroup> allGroups;
  handler.getNextHopGroups(allGroups);
  EXPECT_EQ(toNameToId(allGroups), expected);
}

TEST_F(NamedNextHopGroupThriftTest, getNextHopGroupsNamedIsProgrammed) {
  ThriftHandler handler(sw_);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("multi_ref", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("single_ref", {"2401:db00:2110:3055::2"}));
  groups->push_back(makeGroup("orphan", {"2401:db00:2110:3001::3"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  addUnicastRouteWithNamedNextHopGroup(handler, "2401::100/128", "multi_ref");

  std::vector<NextHopGroup> allGroups;
  handler.getNextHopGroups(allGroups);

  std::vector<NextHopGroup> namedGroups;
  auto emptyNames = std::make_unique<std::vector<std::string>>();
  handler.getNamedNextHopGroups(namedGroups, std::move(emptyNames));

  std::map<std::string, bool> nameToProgrammedState;
  for (const auto& group : allGroups) {
    if (group.name().has_value()) {
      ASSERT_TRUE(group.isProgrammed().has_value());
      nameToProgrammedState[*group.name()] = *group.isProgrammed();
    }
  }

  std::map<std::string, bool> namedApiProgrammedState;
  for (const auto& group : namedGroups) {
    ASSERT_TRUE(group.name().has_value());
    ASSERT_TRUE(group.isProgrammed().has_value());
    namedApiProgrammedState[*group.name()] = *group.isProgrammed();
  }

  ASSERT_EQ(nameToProgrammedState.count("multi_ref"), 1);
  ASSERT_EQ(nameToProgrammedState.count("single_ref"), 1);
  ASSERT_EQ(nameToProgrammedState.count("orphan"), 1);
  EXPECT_FALSE(nameToProgrammedState.at("single_ref"));
  EXPECT_FALSE(nameToProgrammedState.at("orphan"));
  EXPECT_EQ(nameToProgrammedState, namedApiProgrammedState);
}

TEST_F(NamedNextHopGroupThriftTest, pbrRedirectGroupIsProgrammed) {
  SCOPE_EXIT {
    FLAGS_enable_acl_table_group = false;
  };
  FLAGS_enable_acl_table_group = true;
  cfg::AclTable pbrTable;
  pbrTable.name() = cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE();
  pbrTable.priority() = 1;
  pbrTable.actionTypes() = {
      cfg::AclTableActionType::COUNTER, cfg::AclTableActionType::REDIRECT};
  pbrTable.qualifiers() = {
      cfg::AclTableQualifier::TC, cfg::AclTableQualifier::NEXT_HOP_GROUP_ID};
  cfg::AclTableGroup tableGroup;
  tableGroup.name() = "ingress-group";
  tableGroup.stage() = cfg::AclStage::INGRESS;
  tableGroup.aclTables() = {pbrTable};
  auto config = testConfigA();
  config.aclTableGroups() = {tableGroup};
  sw_->applyConfig("add PBR ACL table", config);

  ThriftHandler handler(sw_);
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("defaultNhg", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("classNhg", {"2401:db00:2110:3001::3"}));
  groups->push_back(makeGroup("aliasNhg", {"2401:db00:2110:3001::3"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));
  handler.addPolicies(onePolicy(makeCbfPolicy(
      "pol1",
      "defaultNhg",
      {{ForwardingClass::CLASS_1, namedNhgRef("classNhg")}})));

  auto namedGroups = [&handler]() {
    std::vector<NextHopGroup> named;
    handler.getNamedNextHopGroups(
        named, std::make_unique<std::vector<std::string>>());
    std::map<std::string, NextHopGroup> byName;
    for (auto& group : named) {
      const auto name = *group.name();
      byName[name] = std::move(group);
    }
    return byName;
  };

  // 1. No route names the default group, so the policy programs no ACL entry.
  EXPECT_FALSE(*namedGroups().at("classNhg").isProgrammed());

  // 2. A route on the default group makes the rule redirect to classNhg.
  // aliasNhg has the same members, so it shares classNhg's next-hop set id.
  addUnicastRouteWithNamedNextHopGroup(handler, "2401::100/128", "defaultNhg");
  auto byName = namedGroups();
  ASSERT_EQ(*byName.at("aliasNhg").id(), *byName.at("classNhg").id());
  for (const auto* name : {"defaultNhg", "classNhg", "aliasNhg"}) {
    EXPECT_TRUE(*byName.at(name).isProgrammed()) << name;
  }

  // 3. getNextHopGroups agrees, under whichever name the shared id kept.
  std::vector<NextHopGroup> allGroups;
  handler.getNextHopGroups(allGroups);
  for (const auto& group : allGroups) {
    if (group.name().has_value()) {
      EXPECT_TRUE(*group.isProgrammed()) << *group.name();
    }
  }
}

TEST_F(NamedNextHopGroupThriftTest, removeNextHopGroup) {
  ThriftHandler handler(sw_);

  // Add two groups
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("groupA", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("groupB", {"2401:db00:2110:3055::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  // Remove groupA
  auto names = std::make_unique<std::vector<std::string>>();
  names->push_back("groupA");
  handler.deleteNamedNextHopGroups(std::move(names));

  // groupA should be gone, groupB should remain
  std::vector<NextHopGroup> resultA;
  auto filterA = std::make_unique<std::vector<std::string>>();
  filterA->push_back("groupA");
  handler.getNamedNextHopGroups(resultA, std::move(filterA));
  EXPECT_TRUE(resultA.empty());

  std::vector<NextHopGroup> resultB;
  auto filterB = std::make_unique<std::vector<std::string>>();
  filterB->push_back("groupB");
  handler.getNamedNextHopGroups(resultB, std::move(filterB));
  ASSERT_EQ(resultB.size(), 1);
  EXPECT_EQ(*resultB[0].name(), "groupB");
}

TEST_F(NamedNextHopGroupThriftTest, removeNonExistentGroup) {
  ThriftHandler handler(sw_);

  // Removing a non-existent group should not throw
  auto names = std::make_unique<std::vector<std::string>>();
  names->push_back("nonexistent");
  EXPECT_NO_THROW(handler.deleteNamedNextHopGroups(std::move(names)));
}

TEST_F(NamedNextHopGroupThriftTest, addRemoveAddGroup) {
  ThriftHandler handler(sw_);

  // Add
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("group1", {"2401:db00:2110:3001::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  // Remove
  auto names = std::make_unique<std::vector<std::string>>();
  names->push_back("group1");
  handler.deleteNamedNextHopGroups(std::move(names));

  // Verify removed
  std::vector<NextHopGroup> result;
  auto filter = std::make_unique<std::vector<std::string>>();
  filter->push_back("group1");
  handler.getNamedNextHopGroups(result, std::move(filter));
  EXPECT_TRUE(result.empty());

  // Re-add with different nexthops
  auto groups2 = std::make_unique<std::vector<NextHopGroup>>();
  groups2->push_back(makeGroup("group1", {"2401:db00:2110:3055::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups2));

  // Verify re-added
  result.clear();
  auto filter2 = std::make_unique<std::vector<std::string>>();
  filter2->push_back("group1");
  handler.getNamedNextHopGroups(result, std::move(filter2));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(*result[0].name(), "group1");
}

TEST_F(NamedNextHopGroupThriftTest, rejectGroupNameExceedingMaxLength) {
  ThriftHandler handler(sw_);

  // 31 chars should succeed
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(
      makeGroup(std::string(31, 'a'), {"2401:db00:2110:3001::2"}));
  EXPECT_NO_THROW(handler.addOrUpdateNamedNextHopGroups(std::move(groups)));

  // 32 chars should fail
  auto groups2 = std::make_unique<std::vector<NextHopGroup>>();
  groups2->push_back(
      makeGroup(std::string(32, 'b'), {"2401:db00:2110:3001::2"}));
  EXPECT_THROW(
      handler.addOrUpdateNamedNextHopGroups(std::move(groups2)), FbossError);
}

TEST_F(
    NamedNextHopGroupThriftTest,
    addGroupWithNonexistentLinkLocalInterfaceThrows) {
  ThriftHandler handler(sw_);

  NextHopGroup group;
  group.name() = "group1";
  std::vector<NextHopThrift> nhops;
  for (auto i = 0; i < 4; ++i) {
    auto nh = makeNextHopThrift("fe80:face:b00c::1");
    nh.address()->ifName() = i == 2 ? "fboss9999" : "fboss1";
    nhops.push_back(std::move(nh));
  }
  group.nexthops() = std::move(nhops);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(group);
  EXPECT_THROW(
      handler.addOrUpdateNamedNextHopGroups(std::move(groups)), FbossError);
}

TEST_F(NamedNextHopGroupThriftTest, rejectsMplsAndSrv6) {
  ThriftHandler handler(sw_);

  NextHopGroup group;
  group.name() = "grp1";
  NextHopThrift nh;
  nh.address() = toBinaryAddress(folly::IPAddress("2401:db00:2110:3001::2"));
  MplsAction mplsAction;
  mplsAction.action() = MplsActionCode::PUSH;
  mplsAction.pushLabels() = {101};
  nh.mplsAction() = mplsAction;
  nh.srv6SegmentList() = {toBinaryAddress(folly::IPAddress("2001:db8::1"))};
  nh.tunnelType() = TunnelType::SRV6_ENCAP;
  nh.tunnelId() = "tunnel1";
  group.nexthops() = {nh};

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(group);
  EXPECT_THROW(
      handler.addOrUpdateNamedNextHopGroups(std::move(groups)), FbossError);
}

TEST_F(NamedNextHopGroupThriftTest, rejectsSrv6WithInvalidTunnelType) {
  ThriftHandler handler(sw_);

  NextHopGroup group;
  group.name() = "grp1";
  NextHopThrift nh;
  nh.address() = toBinaryAddress(folly::IPAddress("2401:db00:2110:3001::2"));
  nh.srv6SegmentList() = {toBinaryAddress(folly::IPAddress("2001:db8::1"))};
  nh.tunnelId() = "tunnel1";
  nh.tunnelType() = TunnelType::IP_IN_IP_DECAP;
  group.nexthops() = {nh};

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(group);
  EXPECT_THROW(
      handler.addOrUpdateNamedNextHopGroups(std::move(groups)), FbossError);
}

TEST_F(NamedNextHopGroupThriftTest, acceptsSrv6WithValidTunnelType) {
  ThriftHandler handler(sw_);

  NextHopGroup group;
  group.name() = "grp1";
  NextHopThrift nh;
  nh.address() = toBinaryAddress(folly::IPAddress("2401:db00:2110:3001::2"));
  nh.srv6SegmentList() = {toBinaryAddress(folly::IPAddress("2001:db8::1"))};
  nh.tunnelType() = TunnelType::SRV6_ENCAP;
  nh.tunnelId() = "tunnel1";
  group.nexthops() = {nh};

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(group);
  EXPECT_NO_THROW(handler.addOrUpdateNamedNextHopGroups(std::move(groups)));
}

TEST_F(
    NamedNextHopGroupThriftTest,
    bindingSidWithNamedNextHopGroupResolvesInSwitchState) {
  ThriftHandler handler(sw_);
  constexpr auto kNamedNhg = "bindingSidNHG";

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeSrv6Group(kNamedNhg, {kNhopAddrA, kNhopAddrB}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  auto entries = std::make_unique<std::vector<MySidEntry>>();
  auto entry = makeMySidEntry("2001:db8::1", 64, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = kNamedNhg;
  entry.namedNextHops() = named;
  entries->push_back(std::move(entry));
  handler.addMySidEntries(std::move(entries));

  auto state = sw_->getState();
  auto mySid = state->getMySids()->getNodeIf("2001:db8::1/64");
  ASSERT_NE(mySid, nullptr);
  ASSERT_TRUE(mySid->getNamedNextHopGroup().has_value());
  EXPECT_EQ(*mySid->getNamedNextHopGroup(), kNamedNhg);
  ASSERT_TRUE(mySid->getResolvedNextHopsId().has_value());

  auto fibsInfoMap = state->getFibsInfoMap();
  ASSERT_NE(fibsInfoMap, nullptr);
  ASSERT_FALSE(fibsInfoMap->empty());
  const auto& fibInfo = fibsInfoMap->cbegin()->second;
  auto resolvedNextHops = fibInfo->resolveNextHopSetFromId(
      static_cast<NextHopSetId>(*mySid->getResolvedNextHopsId()));
  ASSERT_EQ(resolvedNextHops.size(), 2);

  std::map<folly::IPAddress, InterfaceID> expectedNhops = {
      {folly::IPAddress(kNhopAddrA), kInterfaceA},
      {folly::IPAddress(kNhopAddrB), kInterfaceB},
  };
  const std::vector<folly::IPAddressV6> expectedSidList = {
      folly::IPAddressV6("2001:db8::10"),
  };

  for (const auto& nextHop : resolvedNextHops) {
    auto expected = expectedNhops.find(nextHop.addr());
    ASSERT_NE(expected, expectedNhops.end());
    ASSERT_TRUE(nextHop.intfID().has_value());
    EXPECT_EQ(*nextHop.intfID(), expected->second);
    EXPECT_EQ(nextHop.srv6SegmentList(), expectedSidList);
    ASSERT_TRUE(nextHop.tunnelType().has_value());
    EXPECT_EQ(*nextHop.tunnelType(), TunnelType::SRV6_ENCAP);
    ASSERT_TRUE(nextHop.tunnelId().has_value());
    EXPECT_EQ(*nextHop.tunnelId(), "tunnel1");
    expectedNhops.erase(expected);
  }
  EXPECT_TRUE(expectedNhops.empty());
}

TEST_F(NamedNextHopGroupThriftTest, rejectsSrv6WithoutTunnelIdAndNoConfig) {
  ThriftHandler handler(sw_);

  NextHopGroup group;
  group.name() = "grp1";
  NextHopThrift nh;
  nh.address() = toBinaryAddress(folly::IPAddress("2401:db00:2110:3001::2"));
  nh.srv6SegmentList() = {toBinaryAddress(folly::IPAddress("2001:db8::1"))};
  group.nexthops() = {nh};

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(group);
  EXPECT_THROW(
      handler.addOrUpdateNamedNextHopGroups(std::move(groups)), FbossError);
}

TEST_F(NamedNextHopGroupThriftTest, defaultsSrv6TunnelIdFromConfig) {
  ThriftHandler handler(sw_);

  auto config = sw_->getConfig();
  cfg::Srv6Tunnel srv6Tunnel;
  srv6Tunnel.srv6TunnelId() = "srv6Tunnel0";
  srv6Tunnel.underlayIntfID() = 1;
  srv6Tunnel.tunnelType() = TunnelType::SRV6_ENCAP;
  srv6Tunnel.srcIp() = "2001:db8::100";
  config.srv6Tunnels() = {srv6Tunnel};
  sw_->applyConfig("Add SRv6 tunnel", config);

  NextHopGroup group;
  group.name() = "grp1";
  NextHopThrift nh;
  nh.address() = toBinaryAddress(folly::IPAddress("2401:db00:2110:3001::2"));
  nh.srv6SegmentList() = {toBinaryAddress(folly::IPAddress("2001:db8::1"))};
  group.nexthops() = {nh};

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(group);
  EXPECT_NO_THROW(handler.addOrUpdateNamedNextHopGroups(std::move(groups)));
}

TEST_F(
    NamedNextHopGroupThriftTest,
    getRouteTableByClientVerifyNamedRouteDestination) {
  ThriftHandler handler(sw_);
  auto bgpClient = static_cast<int16_t>(ClientID::BGPD);

  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup(
      "nhg_foo", {"2401:db00:2110:3001::2", "2401:db00:2110:3001::3"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  UnicastRoute route;
  route.dest()->ip() = toBinaryAddress(IPAddress("2401::1"));
  route.dest()->prefixLength() = 128;
  NamedRouteDestination namedDest;
  namedDest.nextHopGroup() = "nhg_foo";
  route.namedRouteDestination() = namedDest;

  auto updater = sw_->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  updater.program();

  // Verify via getRouteTableByClient
  std::vector<UnicastRoute> routeTable;
  bool found = false;
  handler.getRouteTableByClient(routeTable, bgpClient);
  for (const auto& rt : routeTable) {
    if (rt.dest()->ip() == toBinaryAddress(IPAddress("2401::1"))) {
      ASSERT_TRUE(rt.namedRouteDestination()->nextHopGroup().has_value());
      EXPECT_EQ(*rt.namedRouteDestination()->nextHopGroup(), "nhg_foo");
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(ThriftTest, routeCounterSetForNamedNhg) {
  FLAGS_enable_route_counters_for_named_nhg = true;
  SCOPE_EXIT {
    FLAGS_enable_route_counters_for_named_nhg = false;
  };
  ThriftHandler handler(sw_);

  auto route = std::make_unique<UnicastRoute>();
  *route->dest()->ip() = toBinaryAddress(IPAddress("2401::1"));
  *route->dest()->prefixLength() = 128;
  route->namedRouteDestination()->nextHopGroup() = "nhg_foo";

  auto client = static_cast<int16_t>(ClientID::BGPD);
  handler.addUnicastRoute(client, std::move(route));

  auto state = sw_->getState();
  auto rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::1/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_EQ(rt->getForwardInfo().getCounterID(), "nhg_foo");
}

TEST_F(ThriftTest, routeCounterNotSetForNamedNhgWhenDisabled) {
  FLAGS_enable_route_counters_for_named_nhg = false;
  ThriftHandler handler(sw_);

  auto route = std::make_unique<UnicastRoute>();
  *route->dest()->ip() = toBinaryAddress(IPAddress("2401::2"));
  *route->dest()->prefixLength() = 128;
  route->namedRouteDestination()->nextHopGroup() = "nhg_bar";

  auto client = static_cast<int16_t>(ClientID::BGPD);
  handler.addUnicastRoute(client, std::move(route));

  auto state = sw_->getState();
  auto rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::2/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_FALSE(rt->getForwardInfo().getCounterID().has_value());
}

TEST_F(ThriftTest, syncFibSetsRouteCounterForNamedNhg) {
  FLAGS_enable_route_counters_for_named_nhg = true;
  SCOPE_EXIT {
    FLAGS_enable_route_counters_for_named_nhg = false;
  };
  ThriftHandler handler(sw_);

  auto routes = std::make_unique<std::vector<UnicastRoute>>();

  UnicastRoute route1;
  *route1.dest()->ip() = toBinaryAddress(IPAddress("2401::10"));
  *route1.dest()->prefixLength() = 128;
  route1.namedRouteDestination()->nextHopGroup() = "nhg_alpha";

  UnicastRoute route2;
  *route2.dest()->ip() = toBinaryAddress(IPAddress("2401::20"));
  *route2.dest()->prefixLength() = 128;
  route2.namedRouteDestination()->nextHopGroup() = "nhg_beta";

  // Route without named NHG — counterID should not be set
  UnicastRoute route3;
  *route3.dest()->ip() = toBinaryAddress(IPAddress("2401::30"));
  *route3.dest()->prefixLength() = 128;
  route3.nextHopAddrs()->push_back(toBinaryAddress(IPAddress("10.0.0.2")));
  route3.adminDistance() = AdminDistance::EBGP;

  routes->push_back(route1);
  routes->push_back(route2);
  routes->push_back(route3);

  auto client = static_cast<int16_t>(ClientID::BGPD);
  handler.syncFib(client, std::move(routes));

  auto state = sw_->getState();

  auto rt1 = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::10/128"), state);
  ASSERT_NE(nullptr, rt1);
  EXPECT_EQ(rt1->getForwardInfo().getCounterID(), "nhg_alpha");

  auto rt2 = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::20/128"), state);
  ASSERT_NE(nullptr, rt2);
  EXPECT_EQ(rt2->getForwardInfo().getCounterID(), "nhg_beta");

  auto rt3 = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::30/128"), state);
  ASSERT_NE(nullptr, rt3);
  EXPECT_FALSE(rt3->getForwardInfo().getCounterID().has_value());
}

TEST_F(ThriftTest, syncFibDoesNotSetRouteCounterForNamedNhgWhenDisabled) {
  FLAGS_enable_route_counters_for_named_nhg = false;
  ThriftHandler handler(sw_);

  auto routes = std::make_unique<std::vector<UnicastRoute>>();

  UnicastRoute route1;
  *route1.dest()->ip() = toBinaryAddress(IPAddress("2401::40"));
  *route1.dest()->prefixLength() = 128;
  route1.namedRouteDestination()->nextHopGroup() = "nhg_gamma";

  routes->push_back(route1);

  auto client = static_cast<int16_t>(ClientID::BGPD);
  handler.syncFib(client, std::move(routes));

  auto state = sw_->getState();
  auto rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::40/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_FALSE(rt->getForwardInfo().getCounterID().has_value());
}

TEST_F(ThriftTest, routeCounterUpdatedWhenNamedNhgAddedToExistingRoute) {
  FLAGS_enable_route_counters_for_named_nhg = true;
  SCOPE_EXIT {
    FLAGS_enable_route_counters_for_named_nhg = false;
  };
  ThriftHandler handler(sw_);
  auto client = static_cast<int16_t>(ClientID::BGPD);

  // First add a regular route without named NHG
  auto route1 = std::make_unique<UnicastRoute>();
  *route1->dest()->ip() = toBinaryAddress(IPAddress("2401::50"));
  *route1->dest()->prefixLength() = 128;
  route1->nextHopAddrs()->push_back(toBinaryAddress(IPAddress("10.0.0.2")));
  route1->adminDistance() = AdminDistance::EBGP;
  handler.addUnicastRoute(client, std::move(route1));

  auto state = sw_->getState();
  auto rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::50/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_FALSE(rt->getForwardInfo().getCounterID().has_value());

  // Now update same prefix via syncFib with a named NHG
  auto routes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute route2;
  *route2.dest()->ip() = toBinaryAddress(IPAddress("2401::50"));
  *route2.dest()->prefixLength() = 128;
  route2.namedRouteDestination()->nextHopGroup() = "nhg_delta";
  routes->push_back(route2);
  handler.syncFib(client, std::move(routes));

  state = sw_->getState();
  rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::50/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_EQ(rt->getForwardInfo().getCounterID(), "nhg_delta");
}

TEST_F(ThriftTest, routeCounterRemovedWhenNamedNhgRemovedFromRoute) {
  FLAGS_enable_route_counters_for_named_nhg = true;
  SCOPE_EXIT {
    FLAGS_enable_route_counters_for_named_nhg = false;
  };
  ThriftHandler handler(sw_);
  auto client = static_cast<int16_t>(ClientID::BGPD);

  // First add a route with named NHG
  auto route1 = std::make_unique<UnicastRoute>();
  *route1->dest()->ip() = toBinaryAddress(IPAddress("2401::60"));
  *route1->dest()->prefixLength() = 128;
  route1->namedRouteDestination()->nextHopGroup() = "nhg_epsilon";
  handler.addUnicastRoute(client, std::move(route1));

  auto state = sw_->getState();
  auto rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::60/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_EQ(rt->getForwardInfo().getCounterID(), "nhg_epsilon");

  // Now update same prefix via syncFib without named NHG
  auto routes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute route2;
  *route2.dest()->ip() = toBinaryAddress(IPAddress("2401::60"));
  *route2.dest()->prefixLength() = 128;
  route2.nextHopAddrs()->push_back(toBinaryAddress(IPAddress("10.0.0.2")));
  route2.adminDistance() = AdminDistance::EBGP;
  routes->push_back(route2);
  handler.syncFib(client, std::move(routes));

  state = sw_->getState();
  rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::60/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_FALSE(rt->getForwardInfo().getCounterID().has_value());
}

TEST_F(ThriftTest, routeCounterUpdatedWhenNamedNhgChanges) {
  FLAGS_enable_route_counters_for_named_nhg = true;
  SCOPE_EXIT {
    FLAGS_enable_route_counters_for_named_nhg = false;
  };
  ThriftHandler handler(sw_);
  auto client = static_cast<int16_t>(ClientID::BGPD);

  // Add route with named NHG "nhg_one"
  auto route1 = std::make_unique<UnicastRoute>();
  *route1->dest()->ip() = toBinaryAddress(IPAddress("2401::70"));
  *route1->dest()->prefixLength() = 128;
  route1->namedRouteDestination()->nextHopGroup() = "nhg_one";
  handler.addUnicastRoute(client, std::move(route1));

  auto state = sw_->getState();
  auto rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::70/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_EQ(rt->getForwardInfo().getCounterID(), "nhg_one");

  // Update same prefix with different named NHG "nhg_two"
  auto routes = std::make_unique<std::vector<UnicastRoute>>();
  UnicastRoute route2;
  *route2.dest()->ip() = toBinaryAddress(IPAddress("2401::70"));
  *route2.dest()->prefixLength() = 128;
  route2.namedRouteDestination()->nextHopGroup() = "nhg_two";
  routes->push_back(route2);
  handler.syncFib(client, std::move(routes));

  state = sw_->getState();
  rt = findRoute<folly::IPAddressV6>(
      RouterID(0), IPAddress::createNetwork("2401::70/128"), state);
  ASSERT_NE(nullptr, rt);
  EXPECT_EQ(rt->getForwardInfo().getCounterID(), "nhg_two");
}

TEST_F(NamedNextHopGroupThriftTest, addPoliciesRejectsInlineNexthopsInPolicy) {
  ThriftHandler handler(sw_);
  auto inlineNhops = makeGroup("classNhg", {"2401:db00:2110:3001::2"});
  EXPECT_THAT(
      [&] {
        handler.addPolicies(onePolicy(makeCbfPolicy(
            "pol1", "defaultNhg", {{ForwardingClass::CLASS_1, inlineNhops}})));
      },
      ::testing::ThrowsMessage<FbossError>(
          ::testing::HasSubstr("supplies inline next hops")));
}

TEST_F(NamedNextHopGroupThriftTest, addPoliciesRejectsTheTemporaryEntryPrefix) {
  ThriftHandler handler(sw_);
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("defaultNhg", {"2401:db00:2110:3001::2"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  EXPECT_THAT(
      [&] {
        handler.addPolicies(onePolicy(makeCbfPolicy("#pol1", "defaultNhg")));
      },
      ::testing::ThrowsMessage<FbossError>(
          ::testing::HasSubstr("cannot start with")));
}

// testConfigA has no PBR ACL table, so a policy that passes validation fails
// the HW update.
TEST_F(NamedNextHopGroupThriftTest, addPoliciesPassesValidationWhenWellFormed) {
  ThriftHandler handler(sw_);
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(makeGroup("defaultNhg", {"2401:db00:2110:3001::2"}));
  groups->push_back(makeGroup("classNhg", {"2401:db00:2110:3001::3"}));
  handler.addOrUpdateNamedNextHopGroups(std::move(groups));

  EXPECT_THROW(
      handler.addPolicies(onePolicy(makeCbfPolicy(
          "pol1",
          "defaultNhg",
          {{ForwardingClass::CLASS_1, namedNhgRef("classNhg")}}))),
      FbossHwUpdateError);
}
