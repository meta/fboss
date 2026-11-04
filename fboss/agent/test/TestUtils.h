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

#include <gmock/gmock.h>
#include <functional>
#include <memory>

#include <folly/Function.h>
#include <folly/MacAddress.h>
#include <folly/Range.h>
#include <folly/synchronization/Baton.h>
#include <optional>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwSwitchThriftClientTable.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/StateObserver.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/hw/mock/MockHwSwitch.h"
#include "fboss/agent/mnpu/MultiSwitchHwSwitchHandler.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/RouteDistributionGenerator.h"

namespace facebook::fboss {

class MockHwSwitch;
class MockPlatform;
class MockTunManager;
class Platform;
class RxPacket;
class SwitchState;
class SwSwitch;
class TxPacket;
class HwTestHandle;
class RoutingInformationBase;

namespace cfg {
class SwitchConfig;
}

class MockMultiSwitchHwSwitchHandler : public MultiSwitchHwSwitchHandler {
 public:
  using MultiSwitchHwSwitchHandler::MultiSwitchHwSwitchHandler;
  MOCK_METHOD2(
      stateChanged,
      std::shared_ptr<SwitchState>(const StateDelta&, bool));
  MOCK_METHOD6(
      stateChanged,
      std::pair<fsdb::OperDelta, HwSwitchStateUpdateStatus>(
          std::vector<fsdb::OperDelta>,
          bool,
          const std::shared_ptr<SwitchState>&,
          const std::shared_ptr<SwitchState>&,
          const HwWriteBehavior&,
          const std::optional<StateDeltaApplication>&));
};

template <cfg::SwitchType type, bool enableIntfNbrTable, int count = 1>
struct SwitchTypeAndEnableIntfNbrTableT {
  static constexpr auto switchType = type;
  static constexpr auto intfNbrTable = enableIntfNbrTable;
  static constexpr auto numSwitches = count;
};

/*
 * VOQ switches don't support VLANs. Thus, we are in the process of migrating
 * from using VLAN neighbor tables to Interface neighbor tables.
 *
 * Thus, test all switches with Interface neighbor tables enabled.
 * TODO(skhare) Once migration is complete, stop testing for VLAN neighbor
 * tables i.e. remove <cfg::SwitchType::NPU, false>.
 */
using SwitchTypeAndEnableIntfNbrTable = ::testing::Types<
    SwitchTypeAndEnableIntfNbrTableT<cfg::SwitchType::NPU, false>,
    SwitchTypeAndEnableIntfNbrTableT<cfg::SwitchType::NPU, true>,
    SwitchTypeAndEnableIntfNbrTableT<cfg::SwitchType::VOQ, true, 2>,
    SwitchTypeAndEnableIntfNbrTableT<cfg::SwitchType::FABRIC, true>>;

template <cfg::SwitchType type>
struct SwitchTypeT {
  static constexpr auto switchType = type;
};

using SwitchTypeTestTypes = ::testing::Types<
    SwitchTypeT<cfg::SwitchType::NPU>,
    SwitchTypeT<cfg::SwitchType::VOQ>,
    SwitchTypeT<cfg::SwitchType::FABRIC>>;

using DsfSwitchTypeTestTypes = ::testing::Types<
    SwitchTypeT<cfg::SwitchType::VOQ>,
    SwitchTypeT<cfg::SwitchType::FABRIC>>;

constexpr int64_t kNpuSwitchIdBegin = 0;
constexpr int64_t kVoqSwitchIdBegin = 0;
constexpr int64_t kFabricSwitchIdBegin = 1024;
/*
 * In the non unit test code state passed to apply*Config is the state
 * returned from SwSwitch init, which is always published. However this
 * is not the case for unit test code where we are constructing config
 * by hand and applying it to a (often empty) initial state. Now in a rare
 * case if such a config had only route updates in it, since the previous
 * state was not published, we would apply these route updates inline
 * and now when we compare the old and new route tables to determine
 * if anything changed we would come up empty. The end result in such
 * a case would be to return a null new state falsely signifying no change.
 * To avoid this, provide convenience wrappers for unit test code to call.
 */
std::shared_ptr<SwitchState> publishAndApplyConfig(
    const std::shared_ptr<SwitchState>& state,
    const cfg::SwitchConfig* config,
    const Platform* platform,
    RoutingInformationBase* rib = nullptr,
    PlatformMapping* platformMapping = nullptr);

std::shared_ptr<SwitchState> publishAndApplyConfig(
    const std::shared_ptr<SwitchState>& state,
    cfg::SwitchConfig* config,
    const Platform* platform,
    RoutingInformationBase* rib = nullptr,
    PlatformMapping* platformMapping = nullptr);

/*
 * Create a SwSwitch for testing purposes, with the specified initial state.
 *
 * This will use a MockHwSwitch for the HwSwitch implementation, with the
 * specified initial state.  The returned SwSwitch will have already been
 * initialized.
 */
std::unique_ptr<HwTestHandle> createTestHandle(
    const std::shared_ptr<SwitchState>& = nullptr,
    SwitchFlags flags = SwitchFlags::DEFAULT,
    cfg::SwitchConfig* cfg = nullptr);
std::unique_ptr<HwTestHandle> createTestHandle(
    cfg::SwitchConfig* cfg,
    SwitchFlags flags = SwitchFlags::DEFAULT);

std::unique_ptr<MockPlatform> createMockPlatform(
    cfg::SwitchType switchType = cfg::SwitchType::NPU,
    int64_t switchId = 0,
    cfg::SwitchConfig* config = nullptr);
std::unique_ptr<SwSwitch> setupMockSwitchWithoutHW(
    MockPlatform* platform,
    const std::shared_ptr<SwitchState>& state,
    SwitchFlags flags,
    cfg::SwitchConfig* config = nullptr);

std::unique_ptr<SwSwitch> setupMockSwitchWithoutHW(
    const std::vector<std::unique_ptr<MockPlatform>>& platforms,
    const std::shared_ptr<SwitchState>& state,
    SwitchFlags flags,
    cfg::SwitchConfig* config = nullptr);

std::unique_ptr<SwSwitch> setupMockSwitchWithHW(
    std::unique_ptr<MockPlatform> platform,
    const std::shared_ptr<SwitchState>& state,
    SwitchFlags flags);
cfg::DsfNode makeDsfNodeCfg(
    int64_t switchId = 0,
    cfg::DsfNodeType type = cfg::DsfNodeType::INTERFACE_NODE,
    std::optional<int> clusterId = std::nullopt,
    cfg::AsicType asicType = cfg::AsicType::ASIC_TYPE_MOCK,
    std::optional<int> fabricLevel = std::nullopt);

std::shared_ptr<SystemPort> makeSysPort(
    const std::optional<std::string>& qosPolicy,
    int64_t sysPortId = 1,
    int64_t switchId = 1);
std::tuple<state::NeighborEntries, state::NeighborEntries> makeNbrs();

/*
 * Get the MockHwSwitch from a SwSwitch.
 */
MockHwSwitch* getMockHw(SwSwitch* sw);
MockHwSwitch* getMockHw(std::unique_ptr<SwSwitch>& sw);

/*
 * Get the MockPlatform from a SwSwitch.
 */
MockPlatform* getMockPlatform(SwSwitch* sw);
MockPlatform* getMockPlatform(std::unique_ptr<SwSwitch>& sw);

/*
 * Wait until all pending StateUpdates have been applied. Take a
 * snapshot and return the state at the time all previous updates have
 * been processed.
 */
std::shared_ptr<SwitchState> waitForStateUpdates(SwSwitch* sw);

/*
 * Wait until all pending actions on the background thread
 * have been processed
 */
void waitForBackgroundThread(SwSwitch* sw);

/*
 * Wait until all pending actions on the neighbor cache thread
 * have been processed
 */
void waitForNeighborCacheThread(SwSwitch* sw);

/*
 * Wait until all currently queued lambdas on ribUpdateThread are
 * done
 */
void waitForRibUpdates(SwSwitch* sw);

/*
 * Update Blocked MAC addr list and wait for updates
 */
void updateMacAddrsToBlock(
    SwSwitch* sw,
    const std::vector<std::pair<VlanID, folly::MacAddress>>& macAddrsToBlock);

/**
 * check the field value
 */
template <typename ExpectedType, typename ActualType>
void checkField(
    const ExpectedType& expected,
    const ActualType& actual,
    folly::StringPiece fieldName) {
  if (expected != actual) {
    throw FbossError(
        "expected ", fieldName, " to be ", expected, ", but found ", actual);
    return;
  }
}

/*
 * Create a SwitchState for testing.
 *
 * Profile A:
 *   - 20 ports, 2 VLANS, 2 interfaces
 *   - Ports 1-10 are in VLAN 1
 *   - Ports 11-20 are in VLAN 55
 *   - Interface 1:
 *     - VLAN 1
 *     - MAC 00:02:00:00:00:01
 *     - IPs:
 *       10.0.0.1/24
 *       192.168.0.1/24
 *       2401:db00:2110:3001::0001/64
 *   - Interface 55:
 *     - VLAN 55
 *     - MAC 00:02:00:00:00:55
 *     - IPs:
 *       10.0.55.1/24
 *       192.168.55.1/24
 *       2401:db00:2110:3055::0001/64
 */
std::shared_ptr<SwitchState> testStateA(
    cfg::SwitchType switchType = cfg::SwitchType::NPU);
/*
 * Same as testStateA but with all ports
 * enabled and up
 */
std::shared_ptr<SwitchState> testStateAWithPortsUp(
    cfg::SwitchType switchType = cfg::SwitchType::NPU);

/*
 * Same as testStateA but with AclLookupClass associated with every port.
 * (MH-NIC case queue-per-host configuration).
 */
std::shared_ptr<SwitchState> testStateAWithLookupClasses();

std::shared_ptr<SwitchState> testStateAWithoutIpv4VlanIntf(VlanID vlanId);

std::shared_ptr<SwitchState> testStateAWithoutIpv4();

/*
 * Bring all ports up for a given input state
 */
std::shared_ptr<SwitchState> bringAllPortsUp(
    const std::shared_ptr<SwitchState>& in);

/*
 * Bring all ports down for a given input state
 */
std::shared_ptr<SwitchState> bringAllPortsDown(
    const std::shared_ptr<SwitchState>& in);

/*
 * Fabric switch test config
 */
cfg::SwitchConfig testConfigFabricSwitch(
    bool dualStage = false,
    int fabricLevel = 1,
    int parallLinkPerNode = 4,
    std::optional<int> dualStageLevel2FabricNodes = std::nullopt);
/*
 * The returned configuration object, if applied to a SwitchState with ports
 * 1-20, will yield the same SwitchState as that returned by testStateA().
 */
cfg::SwitchConfig testConfigA(
    cfg::SwitchType switchType = cfg::SwitchType::NPU,
    cfg::AsicType asicType = cfg::AsicType::ASIC_TYPE_MOCK);

cfg::SwitchConfig testConfigB();
/*
 * Same as testConfgA but with AclLookupClass associated with every port.
 * (MH-NIC case queue-per-host configuration).
 */
cfg::SwitchConfig testConfigAWithLookupClasses();

/*
 * NPU switch configuration with only port interfaces
 */
cfg::SwitchConfig testConfigAWithPortInterfaces();

/*
 * Identifiers of the aggregate port and of the router interface bound to it in
 * testConfigAWithAggregatePortInterface().
 */
constexpr int32_t kAggregatePortInterfaceID = 6500;
constexpr int32_t kAggregatePortKey = 55;

/*
 * Same as testConfigAWithPortInterfaces, except that the first two ports are
 * members of a single aggregate port and share one router interface bound to
 * that aggregate. Every other port keeps a router interface of its own, so a
 * single config exercises both flavours of port router interface.
 */
cfg::SwitchConfig testConfigAWithAggregatePortInterface();

/*
 * Convenience macro that wraps EXPECT_CALL() on the underlying MockHwSwitch
 */
#define EXPECT_HW_CALL(sw, method) EXPECT_CALL(*getMockHw(sw), method)

#define EXPECT_MANY_HW_CALLS(sw, method) \
  EXPECT_CALL(*getMockHw(sw), method).Times(testing::AtLeast(1))

/*
 * Convenience macro that wraps EXPECT_CALL() on the underlying MockPlatform
 */
#define EXPECT_PLATFORM_CALL(sw, method) \
  EXPECT_CALL(*getMockPlatform(sw), method)

/*
 * Helper functions for comparing buffer-like objects
 */
#define EXPECT_BUF_EQ(a, b) EXPECT_EQ(fbossHexDump(a), fbossHexDump(b))
#define ASSERT_BUF_EQ(a, b) ASSERT_EQ(fbossHexDump(a), fbossHexDump(b))

std::string fbossHexDump(const folly::IOBuf* buf);
std::string fbossHexDump(const folly::IOBuf& buf);
std::string fbossHexDump(folly::ByteRange buf);
std::string fbossHexDump(folly::StringPiece buf);
std::string fbossHexDump(const std::string& buf);

RouteNextHopSet makeNextHops(
    std::vector<std::string> ipStrs,
    std::optional<LabelForwardingAction> mplsAction = std::nullopt);

template <typename AddrT>
inline RouteNextHopSet makeNextHops(const std::vector<AddrT>& ips) {
  std::vector<std::string> ipStrs;
  std::for_each(ips.begin(), ips.end(), [&ipStrs](const auto& ip) {
    ipStrs.push_back(ip.str());
  });
  return makeNextHops(ipStrs);
}

RouteNextHopSet makeResolvedNextHops(
    std::vector<std::pair<InterfaceID, std::string>> intfAndIP,
    uint32_t weight = ECMP_WEIGHT);

ResolvedNextHop makeResolvedNextHop(
    const InterfaceID& intfId,
    const std::string& nhip,
    uint32_t weight = 1,
    std::optional<NetworkTopologyInformation> topologyInfo = std::nullopt);

/**
 * Create an expected RouteNextHopEntry with the correct IDs (ResolvedNextHopID
 * and NormalizedResolvedNextHopID) by looking up the ID from the
 * NextHopIDManager in the RIB.
 *
 * This is useful in tests where we need to compare actual route forward info
 * with expected values, and the expected RouteNextHopEntry needs to have the
 * correct resolvedNextHopSetID populated.
 */
RouteNextHopEntry makeExpectedRouteNextHopEntry(
    const SwSwitch* sw,
    const RouteNextHopSet& nhops,
    AdminDistance distance);

/*
 * Generate UCMP next hop groups with weighted next hops.
 *
 * This utility function creates UCMP (Unequal Cost Multi-Path) next hop groups
 * with alternating weights. Each group's total weight will not exceed maxWidth.
 *
 */
std::vector<RouteNextHopSet>
getUcmpNextHops(int maxWidth, int numGroups, uint32_t seed = 0);

RoutePrefixV4 makePrefixV4(std::string str);

RoutePrefixV6 makePrefixV6(std::string str);

/*
 * Convenience macro for expecting a packet to be transmitted by the switch
 *
 * The name should be a brief description of the match being performed.
 *
 * The matchFn should be a function that accepts a single "const TxPacket*"
 * argument.  If the packet matches it should return void, and if it doesn't
 * match it should throw an exception describing why the packet did not match.
 *
 * Note that you should not use gtest EXPECT_* macros when checking the
 * packet.  Just because one packet is not a match does not mean that another
 * packet will not arrive and match the filter.  For instance, consider the
 * following code:
 *
 *   EXPECT_SWITCHED_PKT(sw, "ARP reply", arpReplyChecker).Times(1);
 *   EXPECT_SWITCHED_PKT(sw, "ICMP packet", icmpChecker).Times(1);
 *
 * The code expects to see one ARP reply and one ICMP packet, but it may have
 * to call both filters on a packet to see which one it is.
 */
#define EXPECT_SWITCHED_PKT(sw, name, matchFn) \
  EXPECT_HW_CALL(                              \
      sw,                                      \
      sendPacketSwitchedAsync_(TxPacketMatcher::createMatcher(name, matchFn)))

#define EXPECT_MANY_SWITCHED_PKTS(sw, name, matchFn) \
  EXPECT_MANY_HW_CALLS(                              \
      sw,                                            \
      sendPacketSwitchedAsync_(TxPacketMatcher::createMatcher(name, matchFn)))

#define EXPECT_OUT_OF_PORT_PKT(sw, name, matchFn, portID, cos) \
  EXPECT_HW_CALL(                                              \
      sw,                                                      \
      sendPacketOutOfPortAsync_(                               \
          TxPacketMatcher::createMatcher(name, matchFn), portID, cos))

/**
 * Convenience macro for expecting RxPacket to be transmitted by TunManager.
 * usage:
 *  EXPECT_TUN_PKT(tunMgr, "Unicast Packet", packetChecker).Times(1)
 */
#define EXPECT_TUN_PKT(tun, name, dstIfID, matchFn) \
  EXPECT_CALL(                                      \
      *tun,                                         \
      sendPacketToHost_(                            \
          RxPacketMatcher::createMatcher(name, dstIfID, matchFn)))

/**
 * Convenience macro for expecting stateChanged
 * usage:
 *  EXPECT_STATE_UPDATE(sw)
 */
#define EXPECT_STATE_UPDATE(sw) EXPECT_HW_CALL(sw, stateChangedImpl(_, _));

#define EXPECT_STATE_UPDATE_TIMES(sw, times) \
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(times);

#define EXPECT_STATE_UPDATE_TIMES_ATLEAST(sw, times) \
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(::testing::AtLeast(times));

/**
 * Templatized version of Matching function for Tx/Rx packet.
 */
template <typename T>
using MatchFn = std::function<void(const T* pkt)>;
using RxMatchFn = MatchFn<RxPacket>;
using TxMatchFn = MatchFn<TxPacket>;

/*
 * A gmock MatcherInterface for matching TxPacket objects.
 */
using TxPacketPtr = TxPacket*;
class TxPacketMatcher : public ::testing::MatcherInterface<TxPacketPtr> {
 public:
  TxPacketMatcher(folly::StringPiece name, TxMatchFn fn);

  static ::testing::Matcher<TxPacket*> createMatcher(
      folly::StringPiece name,
      TxMatchFn&& fn);

  bool MatchAndExplain(TxPacketPtr pkt, ::testing::MatchResultListener* l)
      const override;

  void DescribeTo(std::ostream* os) const override;
  void DescribeNegationTo(std::ostream* os) const override;

 private:
  const std::string name_;
  const TxMatchFn fn_;
};

/*
 * A gmock MatcherInterface for matching RxPacket objects.
 */
using RxMatchFnArgs = std::tuple<InterfaceID, std::shared_ptr<RxPacket>>;
class RxPacketMatcher : public ::testing::MatcherInterface<RxMatchFnArgs> {
 public:
  RxPacketMatcher(folly::StringPiece name, InterfaceID dstIfID, RxMatchFn fn);

  static ::testing::Matcher<RxMatchFnArgs>
  createMatcher(folly::StringPiece name, InterfaceID dstIfID, RxMatchFn&& fn);

  bool MatchAndExplain(RxMatchFnArgs args, ::testing::MatchResultListener* l)
      const override;

  void DescribeTo(std::ostream* os) const override;
  void DescribeNegationTo(std::ostream* os) const override;

 private:
  const std::string name_;
  const InterfaceID dstIfID_;
  const RxMatchFn fn_;
};

using SwitchStatePredicate = folly::Function<bool(const StateDelta&)>;

class WaitForSwitchState : public StateObserver {
 public:
  WaitForSwitchState(
      SwSwitch* sw,
      SwitchStatePredicate predicate,
      const std::string& name);
  ~WaitForSwitchState();

  void stateUpdated(const StateDelta& delta) override;
  template <class Rep = int64_t, class Period = std::ratio<1>>
  bool wait(
      const std::chrono::duration<Rep, Period>& timeout =
          std::chrono::seconds(5)) {
    std::unique_lock<std::mutex> lock(mtx_);
    while (!done_) {
      if (cv_.wait_for(lock, timeout) == std::cv_status::timeout) {
        XLOG(ERR) << "timed out on " << name_;
        return false;
      }
    }
    done_ = false;
    // TSAN seems not smart enough to figure out that ~WaitForSwitchState() will
    // only be called after wait() is successfully done. Thus, suppress TSAN
    // data race warning starting from now until WaitForSwitchState is
    // destroyed.
    tsanGuard_ =
        std::make_unique<folly::annotate_ignore_thread_sanitizer_guard>(
            __FILE__, __LINE__);
    return true;
  }

 private:
  SwitchStatePredicate predicate_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool done_{false};
  std::string name_;
  SwSwitch* sw_;
  std::unique_ptr<folly::annotate_ignore_thread_sanitizer_guard> tsanGuard_;
};

class WaitForMacEntryAddedOrDeleted : public WaitForSwitchState {
 public:
  WaitForMacEntryAddedOrDeleted(
      SwSwitch* sw,
      folly::MacAddress mac,
      VlanID vlan,
      bool added)
      : WaitForSwitchState(
            sw,
            [mac, vlan, added](const StateDelta& delta) {
              const auto& newVlan =
                  delta.getVlansDelta().getNew()->getNodeIf(vlan);

              auto newEntry = newVlan->getMacTable()->getMacIf(mac);
              if (added) {
                return (newEntry != nullptr);
              }
              return (newEntry == nullptr);
            },
            "WaitForMacEntryAddedOrDeleted") {}
  ~WaitForMacEntryAddedOrDeleted() {}
};

void programRoutes(
    const utility::RouteDistributionGenerator::RouteChunks& routeChunks,
    SwSwitch* sw);

std::vector<std::shared_ptr<Port>> getPortsInLoopbackMode(
    const std::shared_ptr<SwitchState>& state);

void preparedMockPortConfig(
    cfg::Port& portCfg,
    int32_t id,
    std::optional<std::string> name = std::nullopt,
    cfg::PortState state = cfg::PortState::ENABLED);

template <typename Node, bool areFields = false>
void validateNodeSerialization(const Node& node) {
  if constexpr (!areFields) {
    if constexpr (!kIsThriftCowNode<Node>) {
      // Thrifty Nodes
      auto nodeBack = Node::fromThrift(node.toThrift());
      EXPECT_EQ(node, *nodeBack);
    } else {
      // Thrift-cow Nodes
      auto nodeBack = std::make_shared<Node>();
      nodeBack->fromThrift(node.toThrift());
      EXPECT_EQ(node, *nodeBack);
    }
  } else {
    auto nodeBack = Node::fromThrift(node.toThrift());
    EXPECT_EQ(node, nodeBack);
  }
}

template <typename Node>
void validateThriftStructNodeSerialization(const Node& node) {
  auto nodeBack = std::make_shared<Node>(node.toThrift());
  EXPECT_EQ(node, *nodeBack);
  nodeBack = std::make_shared<Node>();
  nodeBack->fromThrift(node.toThrift());
  EXPECT_EQ(node, *nodeBack);
}

template <typename NodeMap>
bool isSameNodeMap(const NodeMap& lhs, const NodeMap& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (const auto& [key, value] : lhs.getAllNodes()) {
    if (auto other = rhs.getNodeIf(key); !other || *value != *other) {
      return false;
    }
  }
  return true;
}

template <typename NodeMap>
void validateNodeMapSerialization(const NodeMap& nodeMap) {
  auto nodeMapBack = NodeMap::fromThrift(nodeMap.toThrift());
  EXPECT_TRUE(isSameNodeMap(nodeMap, *nodeMapBack));
}

template <typename NodeMap>
void validateThriftMapMapSerialization(const NodeMap& nodeMap) {
  auto nodeMapBack = std::make_shared<NodeMap>(nodeMap.toThrift());
  EXPECT_TRUE(nodeMap.toThrift() == nodeMapBack->toThrift());
}

state::RouteFields makeTestDropRouteFields(const std::string& prefix, bool v6);

std::map<std::string, state::RouteFields> makeFib(
    const std::set<std::string>& prefixes,
    bool v6);

state::FibContainerFields makeFibContainerFields(
    int vrf,
    const std::set<std::string>& v4Prefixes,
    const std::set<std::string>& v6Prefixes);

void addSwitchInfo(
    std::shared_ptr<SwitchState>& state,
    cfg::SwitchType switchType = cfg::SwitchType::NPU,
    int64_t SwitchId = 0,
    cfg::AsicType asicType = cfg::AsicType::ASIC_TYPE_MOCK,
    int64_t portIdMin =
        cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
    int64_t portIdMax =
        cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
    int16_t switchIndex = 0,
    std::optional<int64_t> sysPortMin = std::nullopt,
    std::optional<int64_t> sysPortMax = std::nullopt,
    std::optional<std::string> mac = std::nullopt,
    std::optional<std::string> connectionHandle = std::nullopt);

cfg::SwitchInfo createSwitchInfo(
    cfg::SwitchType switchType,
    cfg::AsicType asicType = cfg::AsicType::ASIC_TYPE_MOCK,
    int64_t portIdMin =
        cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
    int64_t portIdMax =
        cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
    int16_t switchIndex = 0,
    std::optional<int64_t> sysPortMin = std::nullopt,
    std::optional<int64_t> sysPortMax = std::nullopt,
    std::optional<std::string> mac = std::nullopt,
    std::optional<std::string> connectionHandle = std::nullopt);

void registerPort(
    std::shared_ptr<SwitchState> state,
    PortID id,
    const std::string& name,
    HwSwitchMatcher scope,
    cfg::PortType portType = cfg::PortType::INTERFACE_PORT);

void setAggregatePortMemberIDs(
    std::vector<cfg::AggregatePortMember>& members,
    const std::vector<int32_t>& portIDs);

template <typename T>
std::vector<int32_t> getAggregatePortMemberIDs(const std::vector<T>& members);

void addSwitchSettingsToState(
    std::shared_ptr<SwitchState>& state,
    std::shared_ptr<SwitchSettings> switchSettings =
        std::make_shared<SwitchSettings>(),
    int64_t switchId = 0);

HwSwitchInitFn mockHwSwitchInitFn(SwSwitch* sw);

/*
 * Test-only wrappers that use FLAGS_switch_id_for_testing as the switchId.
 */
const HwAsic* checkSameAndGetAsicForTesting(
    const std::vector<const HwAsic*>& asics);
folly::MacAddress getMacForFirstInterfaceWithPortsForTesting(
    const std::shared_ptr<SwitchState>& state);
InterfaceID firstInterfaceIDWithPortsForTesting(
    const std::shared_ptr<SwitchState>& state,
    std::optional<cfg::Scope> scope = std::nullopt);
std::shared_ptr<Interface> firstInterfaceWithPortsForTesting(
    const std::shared_ptr<SwitchState>& state);

std::unique_ptr<SwSwitch> createSwSwitchWithMultiSwitch(
    const AgentConfig* config,
    const AgentDirectoryUtil* dirUtil,
    HwSwitchHandlerInitFn initFunc = nullptr);

/**
 * A test implementation of HwSwitchThriftClientTable that allows controlling
 * the behavior of getProgrammedState and getHwSwitchRunState for testing.
 */
class HwSwitchThriftClientTableForTesting : public HwSwitchThriftClientTable {
 public:
  HwSwitchThriftClientTableForTesting(
      int16_t basePort,
      const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo)
      : HwSwitchThriftClientTable(basePort, switchIdToSwitchInfo) {}

  state::SwitchState getProgrammedState(
      const SwitchID& /* switchId */) override {
    if (shouldThrowOnGetProgrammedState_) {
      throw std::runtime_error("Failed to get programmed state");
    }
    return programmedState_;
  }

  SwitchRunState getHwSwitchRunState(const SwitchID& /* switchId */) override {
    if (shouldThrowOnGetRunState_) {
      throw std::runtime_error("Failed to get run state");
    }
    return runState_;
  }

  void setRunState(SwitchRunState state) {
    runState_ = state;
  }

  void setShouldThrowOnGetProgrammedState(bool shouldThrow) {
    shouldThrowOnGetProgrammedState_ = shouldThrow;
  }

  void setShouldThrowOnGetRunState(bool shouldThrow) {
    shouldThrowOnGetRunState_ = shouldThrow;
  }

  void setProgrammedState(const state::SwitchState& state) {
    programmedState_ = state;
  }

 private:
  state::SwitchState programmedState_;
  SwitchRunState runState_{SwitchRunState::INITIALIZED};
  bool shouldThrowOnGetProgrammedState_{false};
  bool shouldThrowOnGetRunState_{false};
};

} // namespace facebook::fboss
