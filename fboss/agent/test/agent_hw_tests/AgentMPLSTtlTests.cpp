/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/container/flat_set.hpp>
#include <fb303/ServiceData.h>
#include <folly/Conv.h>
#include <folly/IPAddressV6.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/packet/MPLSHdr.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/agent_hw_tests/AgentMPLSDataplaneTest.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"

namespace {
constexpr auto kMplsTtlAclTableName = "mpls-ttl-acl-table";
constexpr auto kMplsTtlAclName = "mpls-ttl-acl";
constexpr auto kMplsTtlAclCounterName = "mpls-ttl-acl-stats";
const std::string kMplsTtlExceededCounter =
    facebook::fboss::SwitchStats::kCounterPrefix + "mpls.ttl_exceeded.sum";

constexpr uint8_t kExpiredTtl = 1;
constexpr uint8_t kExpiredTtlMask = 0xFF;

// One above the match, so a mask wider than 0xFF would wrongly catch it.
constexpr uint8_t kNearExpiredTtl = 2;
constexpr uint8_t kForwardingTtl = 64;
// An ingress ACL traps before the MPLS TTL decrement, so the CPU receives the
// frame with the label TTL it arrived with rather than a decremented one.
constexpr uint8_t kTrappedTtl = kExpiredTtl;

const facebook::fboss::Label kTopLabel{1101};
const facebook::fboss::LabelForwardingAction::Label kSwapLabel{1201};
} // namespace

namespace facebook::fboss {

class AgentMplsTtlAclTest : public AgentMPLSDataplaneTest<PortID> {
 protected:
  using BaseT = AgentMPLSDataplaneTest<PortID>;
  using EcmpSetupHelper =
      utility::MplsEcmpSetupTargetedPorts<folly::IPAddressV6>;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::MPLS_TTL_ACL, ProductionFeature::MULTI_ACL_TABLE};
  }

  void setCmdLineFlagOverrides() const override {
    BaseT::setCmdLineFlagOverrides();
    FLAGS_enable_acl_table_group = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = BaseT::initialConfig(ensemble);
    utility::addAclTable(
        &cfg,
        kMplsTtlAclTableName,
        1 /* priority */,
        {
            cfg::AclTableActionType::PACKET_ACTION,
            cfg::AclTableActionType::COUNTER,
            // Punting to a CPU queue is expressed as a traffic class, so the
            // table has to advertise SET_TC or entry create is rejected.
            cfg::AclTableActionType::SET_TC,
        },
        {cfg::AclTableQualifier::ETHER_TYPE,
         cfg::AclTableQualifier::MPLS_LABEL0_TTL});
    return cfg;
  }

  // The label TTL qualifier reads the forwarding label, so the packet only
  // carries a non-zero value once an InSeg entry exists for the top label.
  // Swap models a transit LSR, which is where label TTL expiry is observed.
  void configureStaticMplsSwapRoute(cfg::SwitchConfig& config) const {
    LabelForwardingAction action(
        LabelForwardingAction::LabelForwardingType::SWAP, kSwapLabel);

    config.staticMplsRoutesWithNhops()->emplace_back();
    auto& route = config.staticMplsRoutesWithNhops()->back();
    route.ingressLabel() = kTopLabel.value();

    EcmpSetupHelper helper(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        kTopLabel,
        action.type());
    auto nhop = helper.nhop(egressPortDescriptor());

    NextHopThrift nextHopThrift;
    CHECK(nhop.linkLocalNhopIp.has_value());
    nextHopThrift.address() =
        network::toBinaryAddress(folly::IPAddress(*nhop.linkLocalNhopIp));
    nextHopThrift.address()->ifName() =
        folly::to<std::string>("fboss", nhop.intf);
    nextHopThrift.mplsAction() = action.toThrift();
    route.nexthops()->push_back(nextHopThrift);
  }

  void resolveNextHopForPortWithMac(
      const PortDescriptor& nextHop,
      folly::MacAddress nextHopMac) {
    applyNewState(
        [this, nextHop, nextHopMac](const std::shared_ptr<SwitchState>& state) {
          utility::EcmpSetupTargetedPorts6 helper(
              state, getSw()->needL2EntryForNeighbor(), nextHopMac);
          return helper.resolveNextHops(
              state,
              boost::container::flat_set<PortDescriptor>{nextHop},
              true /* useLinkLocal */);
        },
        "resolve MPLS TTL ACL nexthop with explicit MAC");
  }

  cfg::AclEntry makeMplsTtlAcl() const {
    cfg::AclEntry acl;
    acl.name() = kMplsTtlAclName;
    acl.actionType() = cfg::AclActionType::PERMIT;
    acl.etherType() = cfg::EtherType::MPLS;
    cfg::Ttl ttl;
    ttl.value() = kExpiredTtl;
    ttl.mask() = kExpiredTtlMask;
    acl.mplsLabel0Ttl() = ttl;
    return acl;
  }

  void addMplsTtlAclAndStat(cfg::SwitchConfig* cfg) {
    auto acl = makeMplsTtlAcl();
    utility::addAclEntry(
        cfg, acl, kMplsTtlAclTableName, cfg::AclStage::INGRESS);
    // Trap to CPU rather than permit: this ACL replaces the MPLS_TTL_1 trap
    // mechanism, not its behaviour, so expired frames must still reach
    // MPLSHandler. Punt to the same queue that rx reason uses.
    utility::addTrafficCounter(
        cfg,
        kMplsTtlAclCounterName,
        std::vector<cfg::CounterType>{
            cfg::CounterType::PACKETS, cfg::CounterType::BYTES});
    auto* ensemble = getAgentEnsemble();
    auto asic = checkSameAndGetAsicForTesting(ensemble->getL3Asics());
    auto action = utility::getToQueueAction(
        asic,
        utility::kCoppLowPriQueueId,
        ensemble->isSai(),
        cfg::ToCpuAction::TRAP);
    action.counter() = kMplsTtlAclCounterName;
    utility::addMatcher(cfg, kMplsTtlAclName, action);
  }

  void setupMplsRouteAndAcl() {
    auto config = initialConfig(*getAgentEnsemble());
    applyConfigAndEnableTrunks(config);
    // The nexthop object must exist before the InSeg entry is created.
    resolveNextHopForPortWithMac(egressPortDescriptor(), routerMac());

    configureStaticMplsSwapRoute(config);
    addMplsTtlAclAndStat(&config);
    applyConfigAndEnableTrunks(config);
  }

  uint64_t getAclPacketCounter() const {
    return utility::getAclInOutPackets(getSw(), kMplsTtlAclCounterName);
  }

  uint64_t getAclByteCounter() const {
    return utility::getAclInOutPackets(
        getSw(), kMplsTtlAclCounterName, true /* bytes */);
  }

  // Inject an MPLS frame on a front panel port so it takes the regular ingress
  // pipeline. Addressed to the router MAC so the top label is forwarded.
  size_t sendMplsPacket(uint8_t ttl) {
    // nullopt on port based router interface platforms; frame goes out untagged
    auto vlan = getVlanIDForTx();

    MPLSHdr::Label mplsLabel{
        static_cast<uint32_t>(kTopLabel.value()),
        0 /* trafficClass */,
        true /* bottomOfStack */,
        ttl};
    auto frame = utility::getEthFrame(
        utility::kLocalCpuMac(),
        folly::MacAddress{"02:00:00:00:00:02"},
        {mplsLabel},
        folly::IPAddressV6{"1001::1"},
        folly::IPAddressV6{"2001::1"},
        10000 /* srcPort */,
        20000 /* dstPort */,
        vlan);
    auto pkt = frame.getTxPacket(
        [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); });
    auto size = pkt->buf()->computeChainDataLength();
    EXPECT_TRUE(
        getAgentEnsemble()->ensureSendPacketOutOfPort(
            std::move(pkt), ingressPort()));
    return size;
  }
};

// An MPLS frame whose label TTL has expired matches the ACL and bumps both
// counters. A frame with a forwarding TTL must not match.
TEST_F(AgentMplsTtlAclTest, VerifyMplsTtlAclCounter) {
  auto setup = [this]() { setupMplsRouteAndAcl(); };

  auto verify = [this]() {
    // HW stats reach SwSwitch asynchronously, so wait for a fresh push before
    // snapshotting. Otherwise a warm boot baseline can miss the counter
    // preserved across the boot and inflate every later delta.
    getNextUpdatedPortStats(ingressPort());
    auto pktsBefore = getAclPacketCounter();
    auto bytesBefore = getAclByteCounter();
    auto ttlExceededBefore =
        fb303::fbData->getCounterIfExists(kMplsTtlExceededCounter).value_or(0);
    auto switchId = switchIdForPort(ingressPort());
    auto cpuBefore = utility::getQueueOutPacketsWithRetry(
        getSw(), switchId, utility::kCoppLowPriQueueId, 0 /* retryTimes */, 0);

    utility::SwSwitchPacketSnooper snooper(getSw(), "mpls-ttl-acl-trap");
    snooper.ignoreUnclaimedRxPkts();

    auto packetSize = sendMplsPacket(kExpiredTtl);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(1, getAclPacketCounter() - pktsBefore);
      EXPECT_EVENTUALLY_GE(getAclByteCounter() - bytesBefore, packetSize);
    });

    // The ACL stands in for the MPLS_TTL_1 rx reason, so an expired frame must
    // still reach MPLSHandler.
    WITH_RETRIES({
      auto ttlExceededNow =
          fb303::fbData->getCounterIfExists(kMplsTtlExceededCounter)
              .value_or(0);
      EXPECT_EVENTUALLY_EQ(1, ttlExceededNow - ttlExceededBefore);
    });

    // Punted on the queue the MPLS_TTL_1 rx reason would have used.
    WITH_RETRIES({
      auto cpuAfter = utility::getQueueOutPacketsWithRetry(
          getSw(),
          switchId,
          utility::kCoppLowPriQueueId,
          0 /* retryTimes */,
          cpuBefore + 1);
      EXPECT_EVENTUALLY_EQ(1, cpuAfter - cpuBefore);
    });

    // The frame delivered to the CPU is the one that was injected, label stack
    // intact. An ingress ACL traps ahead of the MPLS TTL decrement, so the TTL
    // is unchanged rather than decremented.
    auto pktBuf = snooper.waitForPacket(10 /* timeout_s */);
    ASSERT_TRUE(pktBuf.has_value());
    ASSERT_TRUE(*pktBuf);
    folly::io::Cursor cursor((*pktBuf).get());
    utility::EthFrame frame(cursor);
    // EthFrame::mplsPayLoad() returns the payload by value, so hold it in a
    // local. Binding a reference straight through the temporary leaves the
    // label stack dangling and reads freed memory.
    auto mplsPayLoad = frame.mplsPayLoad();
    ASSERT_TRUE(mplsPayLoad.has_value());
    const auto& stack = mplsPayLoad->header().stack();
    ASSERT_FALSE(stack.empty());
    EXPECT_EQ(
        static_cast<uint32_t>(kTopLabel.value()), stack[0].getLabelValue());
    EXPECT_EQ(
        static_cast<int>(kTrappedTtl), static_cast<int>(stack[0].timeToLive));

    // Label TTLs that have not expired must not match.
    for (auto ttl : {kNearExpiredTtl, kForwardingTtl}) {
      SCOPED_TRACE(folly::to<std::string>("label ttl ", ttl));
      auto pktsBeforeMiss = getAclPacketCounter();
      sendMplsPacket(ttl);
      WITH_RETRIES(
          { EXPECT_EVENTUALLY_EQ(0, getAclPacketCounter() - pktsBeforeMiss); });
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
