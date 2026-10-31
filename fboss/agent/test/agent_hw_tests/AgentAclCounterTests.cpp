/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/packet/ArpHdr.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/StateUtils.h"

#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"
#include "fboss/lib/CommonUtils.h"

DECLARE_bool(flowletSwitchingEnable);

namespace {
enum AclType {
  TCP_TTLD,
  UDP_TTLD,
  SRC_PORT,
  SRC_PORT_DENY,
  L4_DST_PORT,
  UDF_OPCODE_ACK,
  UDF_OPCODE_WRITE_IMMEDIATE,
  BTH_OPCODE,
  FLOWLET,
  // UDF flowlet also matches on udf in addtion to FLOWLET fields
  UDF_FLOWLET,
};

constexpr auto kL4DstPortRangeAclTableName = "l4-dst-port-range-acl-table";
// Two disjoint, non-adjacent ranges in a single table, so the table carries two
// distinct SAI ACL range objects and each range can be checked to match only
// its own ports. Kept narrow: both ranges are swept exhaustively.
constexpr auto kL4DstPortRangeAclNameA = "l4-dst-port-range-acl-a";
constexpr auto kL4DstPortRangeAclCounterNameA = "l4-dst-port-range-acl-a-stats";
constexpr auto kL4DstPortRangeMinA = 100;
constexpr auto kL4DstPortRangeMaxA = 119;
constexpr auto kL4DstPortRangeAclNameB = "l4-dst-port-range-acl-b";
constexpr auto kL4DstPortRangeAclCounterNameB = "l4-dst-port-range-acl-b-stats";
constexpr auto kL4DstPortRangeMinB = 300;
constexpr auto kL4DstPortRangeMaxB = 319;

// Which range is expected to match. Both matching is not a valid outcome, since
// the ranges are disjoint.
enum class ExpectedRangeHit { RANGE_A, RANGE_B, NEITHER };
constexpr auto kDstIpV6WordAclTableName = "dst-ipv6-word-acl-table";
constexpr auto kDstIpV6WordAclName = "dst-ipv6-word-acl";
constexpr auto kDstIpV6WordAclCounterName = "dst-ipv6-word-acl-stats";
constexpr auto kDstIpV6WordEcmpWidth = 1;
// Program the route to helper port 0, but inject from the next helper port so
// the packet does not ingress on the same port selected for egress.
constexpr auto kDstIpV6WordInjectionPortIndex = kDstIpV6WordEcmpWidth;
constexpr uint32_t kDstIpV6Word3 = 0x12345678;
constexpr uint32_t kDstIpV6Word2 = 0x9abcdef0;
} // namespace

namespace facebook::fboss {

class AgentAclCounterTest : public AgentHwTest {
 public:
  cfg::AclActionType aclActionType_ = cfg::AclActionType::PERMIT;
  uint8_t roceReservedByte_ = utility::kRoceReserved;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if (!FLAGS_enable_acl_table_group) {
      return {
          ProductionFeature::ACL_COUNTER, ProductionFeature::SINGLE_ACL_TABLE};
    } else {
      return {
          ProductionFeature::ACL_COUNTER, ProductionFeature::MULTI_ACL_TABLE};
    }
  }

 protected:
  void SetUp() override {
    AgentHwTest::SetUp();
    if (IsSkipped()) {
      return;
    }
    helper_ = std::make_unique<utility::EcmpSetupAnyNPorts6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), RouterID(0));
  }
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
  }

  std::string getAclName(AclType aclType) const {
    std::string aclName{};
    switch (aclType) {
      case AclType::SRC_PORT:
        aclName = "test-acl";
        break;
      case AclType::SRC_PORT_DENY:
        aclName = "test-deny-acl";
        break;
      case AclType::TCP_TTLD:
        aclName = "test-tcp-acl";
        break;
      case AclType::UDP_TTLD:
        aclName = "test-udp-acl";
        break;
      case AclType::L4_DST_PORT:
        aclName = "test-l4-port-acl";
        break;
      case AclType::UDF_OPCODE_ACK:
        aclName = "test-udf-opc-ack-acl";
        break;
      case AclType::UDF_OPCODE_WRITE_IMMEDIATE:
        aclName = "test-udf-opc-wrt-immdt-acl";
        break;
      case AclType::BTH_OPCODE:
        aclName = "test-bth-opc-acl";
        break;
      case AclType::FLOWLET:
        aclName = "test-flowlet-acl";
        break;
      case AclType::UDF_FLOWLET:
        aclName = utility::kFlowletAclName;
        break;
    }
    return aclName;
  }

  std::string getCounterName(AclType aclType) const {
    std::string counterName{};
    switch (aclType) {
      case AclType::SRC_PORT:
        counterName = "test-acl-stats";
        break;
      case AclType::SRC_PORT_DENY:
        counterName = "test-deny-acl-stats";
        break;
      case AclType::TCP_TTLD:
        counterName = "test-tcp-acl-stats";
        break;
      case AclType::UDP_TTLD:
        counterName = "test-udp-acl-stats";
        break;
      case AclType::L4_DST_PORT:
        counterName = "test-l4-port-acl-stats";
        break;
      case AclType::UDF_OPCODE_ACK:
        counterName = "test-udf-opc-ack-acl-stats";
        break;
      case AclType::UDF_OPCODE_WRITE_IMMEDIATE:
        counterName = "test-udf-opc-wrt-immdt-acl-cnt";
        break;
      case AclType::BTH_OPCODE:
        counterName = "test-bth-opc-acl-stats";
        break;
      case AclType::FLOWLET:
        counterName = "test-flowlet-acl-stats";
        break;
      case AclType::UDF_FLOWLET:
        counterName = utility::kFlowletAclCounterName;
        break;
    }
    return counterName;
  }

  void counterBumpOnHitHelper(
      bool bumpOnHit,
      bool frontPanel,
      std::vector<AclType> aclTypes,
      bool alsoVerifyNoHit = false) {
    auto setup = [this, aclTypes]() {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return helper_->resolveNextHops(in, 2);
      });
      auto wrapper = getSw()->getRouteUpdater();
      helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
      auto newCfg{initialConfig(*getAgentEnsemble())};
      for (auto aclType : aclTypes) {
        addAclAndStat(&newCfg, aclType);
      }
      applyNewConfig(newCfg);
    };

    auto verify = [this, bumpOnHit, frontPanel, aclTypes, alsoVerifyNoHit]() {
      for (auto aclType : aclTypes) {
        verifyAclType(bumpOnHit, frontPanel, aclType);
      }
      if (alsoVerifyNoHit) {
        for (auto aclType : aclTypes) {
          verifyAclType(false /* no hit, no bump */, frontPanel, aclType);
        }
      }
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void counterBumpOnFlowletAclHitHelper(
      bool bumpOnHit,
      bool frontPanel,
      std::vector<AclType> aclTypes) {
    auto setup = [this, aclTypes]() {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return helper_->resolveNextHops(in, 2);
      });
      auto wrapper = getSw()->getRouteUpdater();
      helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
      auto newCfg{initialConfig(*getAgentEnsemble())};
      for (auto aclType : aclTypes) {
        switch (aclType) {
          case AclType::FLOWLET:
          case AclType::UDF_FLOWLET:
            utility::addFlowletAcl(
                newCfg,
                getAgentEnsemble()->isSai(),
                getAclName(aclType),
                getCounterName(aclType),
                aclType != AclType::FLOWLET);
            break;
          default:
            addAclAndStat(&newCfg, aclType);
            break;
        }
      }
      applyNewConfig(newCfg);

      XLOG(DBG3) << "setting ECMP Member Status: ";
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        auto out = in->clone();
        for (const auto& [_, switchSetting] :
             std::as_const(*out->getSwitchSettings())) {
          auto newSwitchSettings = switchSetting->modify(&out);
          newSwitchSettings->setForceEcmpDynamicMemberUp(true);
        }
        return out;
      });
    };

    auto verify = [this, bumpOnHit, frontPanel, aclTypes]() {
      // since FLOWLET Acl presents ahead of UDF Acl in TCAM
      // the packet always hit the FLOWLET Acl. Hence verify the FLOWLET Acl
      verifyAclType(bumpOnHit, frontPanel, aclTypes[0]);
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  size_t sendRoceTraffic(const PortID frontPanelEgrPort, AclType aclType) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    uint8_t opcode = (aclType == AclType::UDF_OPCODE_WRITE_IMMEDIATE)
        ? utility::kUdfRoceOpcodeWriteImmediate
        : utility::kUdfRoceOpcodeAck;
    return utility::pumpRoCETraffic(
        true,
        utility::getAllocatePktFn(getAgentEnsemble()),
        utility::getSendPktFunc(getAgentEnsemble()),
        intfMac,
        vlanId,
        frontPanelEgrPort,
        utility::kUdfL4DstPort,
        255,
        std::nullopt,
        1 /* one packet */,
        opcode,
        this->roceReservedByte_);
  }

  size_t sendPacket(bool frontPanel, bool bumpOnHit, AclType aclType) {
    // TTL is configured for value >= 128
    auto ttl = bumpOnHit &&
            (aclType == AclType::UDP_TTLD || aclType == AclType::TCP_TTLD)
        ? 200
        : 10;
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    int l4DstPort = kTestDstPort;
    if (aclType == AclType::L4_DST_PORT) {
      l4DstPort = kL4DstPort2();
    }

    auto txPacket = aclType == AclType::UDP_TTLD ? utility::makeUDPTxPacket(
                                                       getSw(),
                                                       vlanId,
                                                       srcMac, // src mac
                                                       intfMac, // dst mac
                                                       kSrcIP(),
                                                       kDstIP(),
                                                       kTestSrcPort,
                                                       l4DstPort,
                                                       0,
                                                       ttl)
                                                 : utility::makeTCPTxPacket(
                                                       getSw(),
                                                       vlanId,
                                                       srcMac, // src mac
                                                       intfMac, // dst mac
                                                       kSrcIP(),
                                                       kDstIP(),
                                                       kTestSrcPort,
                                                       l4DstPort,
                                                       0,
                                                       ttl);

    size_t txPacketSize = txPacket->buf()->length();
    // port is in LB mode, so it will egress and immediately loop back.
    // Since it is not re-written, it should hit the pipeline as if it
    // ingressed on the port, and be properly queued.
    if (frontPanel) {
      auto outPort =
          helper_->ecmpPortDescriptorAt(kDefaultEcmpWidth).phyPortID();
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), outPort);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }

    return txPacketSize;
  }

  folly::IPAddressV6 kSrcIP() {
    return folly::IPAddressV6("2620:0:1cfe:face:b00c::1");
  }

  folly::IPAddressV6 kDstIP() {
    return folly::IPAddressV6("2620:0:1cfe:face:b00c::10");
  }

  folly::IPAddressV4 kSrcIPv4() {
    return folly::IPAddressV4("10.0.0.1");
  }

  folly::IPAddressV4 kDstIPv4() {
    return folly::IPAddressV4("100.100.100.1");
  }

  int kL4DstPort2() const {
    return 8002;
  }

  // This test verifies if the ACL priorities are taking effect as expected.
  // ACLs are processed in the priority in which they are listed in the config
  // 1. Install PERMIT ACL matching on SRC_PORT
  // 2. Install DENY ACL matching on SRC_PORT
  //
  // The expectation here is both ACLs are hit and PERMIT ACL gets priority.
  void aclPriorityTestHelper() {
    auto setup = [this]() {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return helper_->resolveNextHops(in, 2);
      });
      auto wrapper = getSw()->getRouteUpdater();
      helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
      auto newCfg{initialConfig(*getAgentEnsemble())};
      this->aclActionType_ = cfg::AclActionType::PERMIT;
      addAclAndStat(&newCfg, AclType::SRC_PORT);
      this->aclActionType_ = cfg::AclActionType::DENY;
      addAclAndStat(&newCfg, AclType::SRC_PORT_DENY);
      applyNewConfig(newCfg);
    };

    auto verify = [this]() {
      // The first parameter in both invocations is bumpOnHit.
      // True means the verifier checks if counter increment for the PERMIT ACL
      // False means the DENY ACL counter did not change.
      //
      // Higher priority PERMIT ACL counter went up
      verifyAclType(true, true, AclType::SRC_PORT);
      // Lower priority DENY ACL counter remains same
      verifyAclType(false, true, AclType::SRC_PORT_DENY);
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void aclPriorityTestHelper2() {
    auto setup = [this]() {
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return helper_->resolveNextHops(in, 2);
      });
      auto wrapper = getSw()->getRouteUpdater();
      helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
      auto newCfg{initialConfig(*getAgentEnsemble())};
      // match on SRC_PORT=1 + L4_DST_PORT=8002
      this->aclActionType_ = cfg::AclActionType::PERMIT;
      addAclAndStat(&newCfg, AclType::L4_DST_PORT);
      // match on SRC_PORT=1
      this->aclActionType_ = cfg::AclActionType::DENY;
      addAclAndStat(&newCfg, AclType::SRC_PORT);
      applyNewConfig(newCfg);
    };

    auto verify = [this]() {
      // Sends a packet with dst port 8002
      verifyAclType(true, true, AclType::L4_DST_PORT);
      // Sends a packet with dst port 8001
      verifyAclType(true, true, AclType::SRC_PORT);
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void verifyAclType(bool bumpOnHit, bool frontPanel, AclType aclType) {
    auto egressPort = helper_->ecmpPortDescriptorAt(0).phyPortID();
    auto pktsBefore = *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
    auto aclPktCountBefore =
        utility::getAclInOutPackets(getSw(), getCounterName(aclType));
    auto aclBytesCountBefore = utility::getAclInOutPackets(
        getSw(), getCounterName(aclType), true /* bytes */);
    size_t sizeOfPacketSent = 0;

    // for udf or bth_opcode testing, send roce packets
    if (aclType == AclType::UDF_OPCODE_ACK ||
        aclType == AclType::UDF_OPCODE_WRITE_IMMEDIATE ||
        aclType == AclType::BTH_OPCODE || aclType == AclType::FLOWLET ||
        aclType == AclType::UDF_FLOWLET) {
      sizeOfPacketSent = sendRoceTraffic(egressPort, aclType);
    } else {
      sizeOfPacketSent = sendPacket(frontPanel, bumpOnHit, aclType);
    }
    WITH_RETRIES({
      auto aclPktCountAfter =
          utility::getAclInOutPackets(getSw(), getCounterName(aclType));

      auto aclBytesCountAfter = utility::getAclInOutPackets(
          getSw(), getCounterName(aclType), true /* bytes */);

      auto pktsAfter = *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
      XLOG(DBG2) << "\n"
                 << "PacketCounter: " << pktsBefore << " -> " << pktsAfter
                 << "\n"
                 << "aclPacketCounter(" << getCounterName(aclType)
                 << "): " << aclPktCountBefore << " -> " << (aclPktCountAfter)
                 << "\n"
                 << "aclBytesCounter(" << getCounterName(aclType)
                 << "): " << aclBytesCountBefore << " -> "
                 << aclBytesCountAfter;

      if (bumpOnHit) {
        EXPECT_EVENTUALLY_GT(pktsAfter, pktsBefore);
        // On some ASICs looped back pkt hits the ACL before being
        // dropped in the ingress pipeline, hence GE
        EXPECT_EVENTUALLY_GE(aclPktCountAfter, aclPktCountBefore + 1);
        // At most we should get a pkt bump of 2
        EXPECT_EVENTUALLY_LE(aclPktCountAfter, aclPktCountBefore + 2);
        if (isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER)) {
          // TODO ruinanhu: Remove this once we have a fix for TH6 counter
          // problem
          auto hwAsic =
              checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
          auto extraBytes =
              (hwAsic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK6) ? 4
                                                                            : 0;

          EXPECT_EVENTUALLY_GE(
              aclBytesCountAfter + extraBytes,
              aclBytesCountBefore + sizeOfPacketSent);
          //  The ACL byte counter can include the 4-byte FCS for each packet
          //  that hits the ACL (in addition to bytes from ingress vlan
          //  imposition seen on native BCM). The ACL is hit once on most ASICs
          //  but twice on some (the looped-back pkt re-hits the ACL before
          //  being dropped in the ingress pipeline), so scale the FCS byte
          //  tolerance by the number of ACL hits.
          auto numAclHits = aclPktCountAfter - aclPktCountBefore;
          EXPECT_EVENTUALLY_LE(
              aclBytesCountAfter,
              aclBytesCountBefore + (2 * sizeOfPacketSent) + (4 * numAclHits));
        }
      } else {
        EXPECT_EVENTUALLY_EQ(aclPktCountBefore, aclPktCountAfter);
        if (isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER)) {
          EXPECT_EVENTUALLY_EQ(aclBytesCountBefore, aclBytesCountAfter);
        }
      }
    });
  }

  void addAclAndStat(cfg::SwitchConfig* config, AclType aclType) const {
    auto aclName = getAclName(aclType);
    auto counterName = getCounterName(aclType);
    cfg::AclEntry aclEntry;
    aclEntry.name() = aclName;
    aclEntry.actionType() = aclActionType_;
    auto* acl = &aclEntry;
    auto l3Asics = getAgentEnsemble()->getL3Asics();
    auto asic = checkSameAndGetAsicForTesting(l3Asics);
    std::vector<cfg::CounterType> counterTypes{cfg::CounterType::PACKETS};
    if (asic->isSupported(HwAsic::Feature::ACL_BYTE_COUNTER)) {
      counterTypes.push_back(cfg::CounterType::BYTES);
    }
    bool isSai = getAgentEnsemble()->isSai();
    switch (aclType) {
      case AclType::TCP_TTLD:
      case AclType::UDP_TTLD:
        acl->srcIp() = "2620:0:1cfe:face:b00c::/64";
        acl->proto() = aclType == AclType::UDP_TTLD ? 17 : 6;
        acl->ipType() = cfg::IpType::IP6;
        acl->ttl() = cfg::Ttl();
        *acl->ttl()->value() = 128;
        *acl->ttl()->mask() = 128;
        if (asic->isSupported(HwAsic::Feature::ACL_ENTRY_ETHER_TYPE)) {
          acl->etherType() = cfg::EtherType::IPv6;
        }
        break;
      case AclType::SRC_PORT:
      case AclType::SRC_PORT_DENY:
        acl->srcPort() = helper_->ecmpPortDescriptorAt(0).phyPortID();
        if (asic->isSupported(HwAsic::Feature::ACL_ENTRY_ETHER_TYPE)) {
          if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
            // NON_IP traps packets which are neither v4 nor v6 on Chenab. Set
            // to trap any packet since intention of the test is to verify if
            // packet ingressing on a specific port is trapped
            acl->ipType() = cfg::IpType::ANY;
          } else if (
              asic->getAsicType() == cfg::AsicType::ASIC_TYPE_QUMRAN4D ||
              asic->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO4) {
            // Q4D/J4 (DNX) reject FIELD_SRC_PORT + FIELD_ACL_IP_TYPE=NON_IP.
            // Per Broadcom, install the source-port entry in both ACL tables:
            // an etherType=IPv4 copy in the default table and an etherType=IPv6
            // copy in the IPv6 table, so the source-port ACL matches both v4
            // and v6 traffic. The test sends IPv6 traffic, so the IPv6 copy
            // keeps the canonical name/counter (the one verifyAclType checks);
            // the IPv4 copy gets a "-v4" suffix.
            auto addSrcPortEntry = [&](const std::string& name,
                                       const std::string& counter,
                                       cfg::EtherType etherType,
                                       const std::string& tableName) {
              cfg::AclEntry entry;
              entry.name() = name;
              entry.actionType() = aclActionType_;
              entry.srcPort() = helper_->ecmpPortDescriptorAt(0).phyPortID();
              entry.etherType() = etherType;
              utility::addAclEntry(config, entry, tableName);
              utility::addAclStat(config, name, counter, counterTypes);
            };
            addSrcPortEntry(
                aclName + "-v4",
                counterName + "-v4",
                cfg::EtherType::IPv4,
                utility::kDefaultAclTable());
            addSrcPortEntry(
                aclName,
                counterName,
                cfg::EtherType::IPv6,
                utility::kIpv6AclTable());
            return;
          } else {
            // Set the IP type to NON_IP to match all ingress packets in ASIC
            // SRC port
            acl->ipType() = cfg::IpType::NON_IP;
          }
        }
        break;
      case AclType::L4_DST_PORT:
        acl->srcPort() = helper_->ecmpPortDescriptorAt(0).phyPortID();
        acl->l4DstPort() = kL4DstPort2();
        break;
      case AclType::UDF_OPCODE_ACK: {
        if (isSai) {
          utility::addUdfTableToAcl(
              acl,
              utility::kUdfAclRoceOpcodeGroupName,
              {utility::kUdfRoceOpcodeAck},
              {utility::kUdfRoceOpcodeMask});
        } else {
          acl->udfGroups() = {utility::kUdfAclRoceOpcodeGroupName};
          acl->roceBytes() = {utility::kUdfRoceOpcodeAck};
          acl->roceMask() = {utility::kUdfRoceOpcodeMask};
        }
      } break;
      case AclType::UDF_OPCODE_WRITE_IMMEDIATE: {
        if (isSai) {
          utility::addUdfTableToAcl(
              acl,
              utility::kUdfAclRoceOpcodeGroupName,
              {utility::kUdfRoceOpcodeWriteImmediate},
              {utility::kUdfRoceOpcodeMask});
        } else {
          acl->udfGroups() = {utility::kUdfAclRoceOpcodeGroupName};
          acl->roceBytes() = {utility::kUdfRoceOpcodeWriteImmediate};
          acl->roceMask() = {utility::kUdfRoceOpcodeMask};
        }
      } break;
      case AclType::BTH_OPCODE:
        acl->etherType() = cfg::EtherType::IPv6;
        acl->roceOpcode() = utility::kUdfRoceOpcodeAck;
        break;
      case AclType::FLOWLET:
      case AclType::UDF_FLOWLET:
        break;
    }
    utility::addAcl(config, aclEntry, cfg::AclStage::INGRESS);

    utility::addAclStat(config, aclName, counterName, counterTypes);
  }

  std::unique_ptr<utility::EcmpSetupAnyNPorts6> helper_;
};

class AgentL4DstPortAclCounterTest : public AgentAclCounterTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if (!FLAGS_enable_acl_table_group) {
      return {
          ProductionFeature::ACL_COUNTER,
          ProductionFeature::SINGLE_ACL_TABLE,
          ProductionFeature::L4_DST_PORT_ACL};
    } else {
      return {
          ProductionFeature::ACL_COUNTER,
          ProductionFeature::MULTI_ACL_TABLE,
          ProductionFeature::L4_DST_PORT_ACL};
    }
  }
};

class AgentAclCounterL4DstPortRangeTest : public AgentAclCounterTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::ACL_COUNTER,
        ProductionFeature::MULTI_ACL_TABLE,
        ProductionFeature::L4_DST_PORT_RANGE};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentAclCounterTest::initialConfig(ensemble);
    if (!FLAGS_enable_acl_table_group) {
      return cfg;
    }

    utility::addAclTable(
        &cfg,
        kL4DstPortRangeAclTableName,
        1 /* priority */,
        {
            cfg::AclTableActionType::PACKET_ACTION,
            cfg::AclTableActionType::COUNTER,
        },
        {cfg::AclTableQualifier::L4_DST_PORT_RANGE});

    addRangeAclAndStat(
        &cfg,
        kL4DstPortRangeAclNameA,
        kL4DstPortRangeAclCounterNameA,
        kL4DstPortRangeMinA,
        kL4DstPortRangeMaxA);
    addRangeAclAndStat(
        &cfg,
        kL4DstPortRangeAclNameB,
        kL4DstPortRangeAclCounterNameB,
        kL4DstPortRangeMinB,
        kL4DstPortRangeMaxB);
    return cfg;
  }

  void addRangeAclAndStat(
      cfg::SwitchConfig* cfg,
      const std::string& aclName,
      const std::string& counterName,
      int min,
      int max) const {
    cfg::AclEntry acl;
    acl.name() = aclName;
    acl.actionType() = cfg::AclActionType::PERMIT;
    cfg::Range range;
    range.minimum() = min;
    range.maximum() = max;
    acl.l4DstPortRange() = range;
    utility::addAclEntry(
        cfg, acl, kL4DstPortRangeAclTableName, cfg::AclStage::INGRESS);
    utility::addAclStat(
        cfg,
        aclName,
        counterName,
        {cfg::CounterType::PACKETS, cfg::CounterType::BYTES});
  }

  void setupRangeAclCounterTest() {
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper_->resolveNextHops(in, 2);
    });
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
    applyNewConfig(initialConfig(*getAgentEnsemble()));
  }

  uint64_t getRangeAclPacketCounter(const std::string& counterName) const {
    return utility::getAclInOutPackets(getSw(), counterName);
  }

  std::vector<int> portsInRange(int min, int max) const {
    std::vector<int> l4DstPorts;
    l4DstPorts.reserve(max - min + 1);
    for (auto l4DstPort = min; l4DstPort <= max; ++l4DstPort) {
      l4DstPorts.push_back(l4DstPort);
    }
    return l4DstPorts;
  }

  // Just outside each range on both sides, so both boundaries of both ranges
  // are pinned.
  std::vector<int> portsOutsideRanges() const {
    return {
        kL4DstPortRangeMinA - 1,
        kL4DstPortRangeMaxA + 1,
        kL4DstPortRangeMinB - 1,
        kL4DstPortRangeMaxB + 1};
  }

  void sendPacketWithDstPort(bool frontPanel, bool isV6, int l4DstPort) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);

    std::unique_ptr<TxPacket> txPacket;
    if (isV6) {
      txPacket = utility::makeTCPTxPacket(
          getSw(),
          vlanId,
          srcMac, // src mac
          intfMac, // dst mac
          kSrcIP(),
          kDstIP(),
          kTestSrcPort,
          l4DstPort,
          0,
          255);
    } else {
      txPacket = utility::makeTCPTxPacket(
          getSw(),
          vlanId,
          srcMac, // src mac
          intfMac, // dst mac
          kSrcIPv4(),
          kDstIPv4(),
          kTestSrcPort,
          l4DstPort,
          0,
          255);
    }

    if (frontPanel) {
      auto outPort =
          helper_->ecmpPortDescriptorAt(kDefaultEcmpWidth).phyPortID();
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), outPort);
    } else {
      sendPacketSwitchedAsync(std::move(txPacket));
    }
  }

  // Both counters are sampled on every send, so a range programmed too wide is
  // caught by the other range's counter moving.
  void verifyL4DstPortRangeAclCounters(
      const std::string& name,
      bool isFrontPanel,
      bool isV6,
      const std::vector<int>& l4DstPorts,
      ExpectedRangeHit expectedHit) {
    SCOPED_TRACE(name);
    auto egressPort = helper_->ecmpPortDescriptorAt(0).phyPortID();
    auto pktsBefore = *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
    auto countBeforeA =
        getRangeAclPacketCounter(kL4DstPortRangeAclCounterNameA);
    auto countBeforeB =
        getRangeAclPacketCounter(kL4DstPortRangeAclCounterNameB);
    for (auto l4DstPort : l4DstPorts) {
      sendPacketWithDstPort(isFrontPanel, isV6, l4DstPort);
    }

    WITH_RETRIES({
      auto pktsAfter = *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
      XLOG(DBG2) << "\n"
                 << "PacketCounter: " << pktsBefore << " -> " << pktsAfter;
      // EXPECT_EVENTUALLY_* reads state declared by WITH_RETRIES, so this must
      // stay a lambda inside the block rather than a member function.
      // The upper bound allows for the counter double-counting a packet.
      auto verifyRangeCounter = [&](const std::string& counterName,
                                    uint64_t countBefore,
                                    bool expectHit) {
        SCOPED_TRACE(counterName);
        auto countAfter = getRangeAclPacketCounter(counterName);
        XLOG(DBG2) << "aclPacketCounter(" << counterName << "): " << countBefore
                   << " -> " << countAfter;
        if (expectHit) {
          EXPECT_EVENTUALLY_GE(countAfter, countBefore + l4DstPorts.size());
          EXPECT_EVENTUALLY_LE(
              countAfter, countBefore + (2 * l4DstPorts.size()));
        } else {
          EXPECT_EVENTUALLY_EQ(countBefore, countAfter);
        }
      };
      verifyRangeCounter(
          kL4DstPortRangeAclCounterNameA,
          countBeforeA,
          expectedHit == ExpectedRangeHit::RANGE_A);
      verifyRangeCounter(
          kL4DstPortRangeAclCounterNameB,
          countBeforeB,
          expectedHit == ExpectedRangeHit::RANGE_B);
    });
  }
};

class AgentDstIpV6WordAclCounterTest : public AgentAclCounterTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentAclCounterTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::MODIFY_ACL_QUALIFIERS);
    features.push_back(ProductionFeature::DST_IPV6_WORD_ACL_QUALIFIERS);
    return features;
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentAclCounterTest::initialConfig(ensemble);
    if (!FLAGS_enable_acl_table_group) {
      return cfg;
    }

    utility::addAclTable(
        &cfg,
        kDstIpV6WordAclTableName,
        1 /* priority */,
        {
            cfg::AclTableActionType::PACKET_ACTION,
            cfg::AclTableActionType::COUNTER,
        },
        {cfg::AclTableQualifier::DST_IPV6_WORD3,
         cfg::AclTableQualifier::DST_IPV6_WORD2});
    return cfg;
  }

  cfg::AclEntry makeDstIpV6WordAcl() const {
    cfg::AclEntry acl;
    acl.name() = kDstIpV6WordAclName;
    acl.actionType() = cfg::AclActionType::PERMIT;
    acl.dstIpV6Word3() = kDstIpV6Word3;
    acl.dstIpV6Word2() = kDstIpV6Word2;
    return acl;
  }

  void addDstIpV6WordAclAndStat(cfg::SwitchConfig* cfg) const {
    auto acl = makeDstIpV6WordAcl();
    if (FLAGS_enable_acl_table_group) {
      utility::addAclEntry(
          cfg, acl, kDstIpV6WordAclTableName, cfg::AclStage::INGRESS);
    } else {
      utility::addAcl(cfg, acl, cfg::AclStage::INGRESS);
    }
    utility::addAclStat(
        cfg,
        kDstIpV6WordAclName,
        kDstIpV6WordAclCounterName,
        {cfg::CounterType::PACKETS, cfg::CounterType::BYTES});
  }

  void setupDstIpV6WordAclCounterTest() {
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper_->resolveNextHops(in, kDstIpV6WordEcmpWidth);
    });
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, kDstIpV6WordEcmpWidth);
    auto newCfg{initialConfig(*getAgentEnsemble())};
    addDstIpV6WordAclAndStat(&newCfg);
    applyNewConfig(newCfg);
  }

  uint64_t getDstIpV6WordAclPacketCounter() const {
    return utility::getAclInOutPackets(getSw(), kDstIpV6WordAclCounterName);
  }

  uint64_t getDstIpV6WordAclByteCounter() const {
    return utility::getAclInOutPackets(
        getSw(), kDstIpV6WordAclCounterName, true /* bytes */);
  }

  size_t sendPacketWithDstIpV6(const folly::IPAddressV6& dstIp) {
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto txPacket = utility::makeTCPTxPacket(
        getSw(),
        vlanId,
        srcMac,
        intfMac,
        kSrcIP(),
        dstIp,
        kTestSrcPort,
        kTestDstPort,
        0,
        255);
    auto txPacketSize = txPacket->buf()->length();
    auto outPort = helper_->ecmpPortDescriptorAt(kDstIpV6WordInjectionPortIndex)
                       .phyPortID();
    getSw()->sendPacketOutOfPortAsync(std::move(txPacket), outPort);
    return txPacketSize;
  }

  void verifyDstIpV6WordAclCounter(
      const std::string& name,
      const folly::IPAddressV6& dstIp,
      bool expectHit) {
    SCOPED_TRACE(name);
    auto egressPort =
        helper_->ecmpPortDescriptorAt(kDstIpV6WordEcmpWidth - 1).phyPortID();
    auto egressPktsBefore =
        *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
    auto aclPktCountBefore = getDstIpV6WordAclPacketCounter();
    auto aclByteCountBefore = getDstIpV6WordAclByteCounter();

    auto sizeOfPacketSent = sendPacketWithDstIpV6(dstIp);

    WITH_RETRIES({
      auto egressPktsAfter =
          *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
      auto aclPktCountAfter = getDstIpV6WordAclPacketCounter();
      auto aclByteCountAfter = getDstIpV6WordAclByteCounter();
      XLOG(DBG2) << "\n"
                 << "egressPacketCounter: " << egressPktsBefore << " -> "
                 << egressPktsAfter << "\n"
                 << "aclPacketCounter(" << kDstIpV6WordAclCounterName
                 << "): " << aclPktCountBefore << " -> " << aclPktCountAfter
                 << "\n"
                 << "aclByteCounter(" << kDstIpV6WordAclCounterName
                 << "): " << aclByteCountBefore << " -> " << aclByteCountAfter;
      EXPECT_EVENTUALLY_GE(egressPktsAfter, egressPktsBefore + 1);
      if (expectHit) {
        // Some ASICs can count the looped-back packet a second time before it
        // is dropped later in the ingress pipeline. For one sent packet,
        // require one or two ACL hits.
        EXPECT_EVENTUALLY_GE(aclPktCountAfter, aclPktCountBefore + 1);
        EXPECT_EVENTUALLY_LE(aclPktCountAfter, aclPktCountBefore + 2);
        if (isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER)) {
          auto numAclHits = aclPktCountAfter - aclPktCountBefore;
          auto expectedByteDelta = numAclHits * sizeOfPacketSent;
          EXPECT_EVENTUALLY_GE(
              aclByteCountAfter, aclByteCountBefore + expectedByteDelta);
          // ACL byte counters may include the 4-byte FCS for each packet that
          // hits the ACL, so scale the byte tolerance by the observed hit
          // count.
          EXPECT_EVENTUALLY_LE(
              aclByteCountAfter,
              aclByteCountBefore + expectedByteDelta + (4 * numAclHits));
        }
      } else {
        EXPECT_EVENTUALLY_EQ(aclPktCountBefore, aclPktCountAfter);
        if (isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER)) {
          EXPECT_EVENTUALLY_EQ(aclByteCountBefore, aclByteCountAfter);
        }
      }
    });
  }
};

namespace {
constexpr auto kPortUserMetaAclTableName = "port-user-meta-acl-table";
constexpr auto kPortUserMetaAclName = "port-user-meta-acl";
constexpr auto kPortUserMetaAclCounterName = "port-user-meta-acl-stats";
constexpr auto kPortUserMetaFallbackAclName = "port-user-meta-fallback-acl";
constexpr auto kPortUserMetaFallbackCounterName =
    "port-user-meta-fallback-stats";
constexpr auto kPortUserMetaWarmbootAclTableName = "port-user-meta-wb-table";
constexpr auto kPortUserMetaPermitAclName = "port-user-meta-permit-53";
constexpr auto kPortUserMetaPermitCounterName = "port-meta-permit-53-stats";
constexpr auto kPortUserMetaDefaultDenyAclName = "port-user-meta-default-deny";
constexpr auto kPortUserMetaDefaultDenyCounterName = "port-meta-deny-stats";
constexpr uint16_t kPortUserMetaPermittedL4DstPort = 53;
constexpr uint16_t kPortUserMetaDeniedL4DstPort = 54;
constexpr auto kPortLookupClass =
    cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED;
constexpr int kPortUserMetaCoppAclTablePriority = 30;
constexpr uint32_t kControlPlanePuntTimeoutS = 5;
} // namespace

class AgentPortUserMetaAclTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::ACL_COUNTER,
        ProductionFeature::PORT_USER_METADATA,
    };
  }

 protected:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_acl_table_group = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalInterfacePortIds(),
        true /* interfaceHasSubnet */);
  }

  std::unique_ptr<TxPacket> makePacket(uint16_t l4DstPort = kTestDstPort) {
    const auto vlanId = getVlanIDForTx();
    const auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    const auto srcMac =
        utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    return utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        srcMac,
        intfMac,
        folly::IPAddressV6("2620:0:1cfe:face:b00c::1"),
        folly::IPAddressV6("2620:0:1cfe:face:b00c::2"),
        kTestSrcPort,
        l4DstPort,
        0,
        255);
  }

  void addWarmbootAclTable(
      cfg::SwitchConfig* config,
      cfg::AclActionType denyAction = cfg::AclActionType::DENY,
      int priority = 1) const {
    utility::addAclTable(
        config,
        kPortUserMetaWarmbootAclTableName,
        priority,
        {cfg::AclTableActionType::PACKET_ACTION,
         cfg::AclTableActionType::COUNTER},
        {cfg::AclTableQualifier::LOOKUP_CLASS_PORT,
         cfg::AclTableQualifier::L4_DST_PORT});

    cfg::AclEntry permitAcl;
    permitAcl.name() = kPortUserMetaPermitAclName;
    permitAcl.actionType() = cfg::AclActionType::PERMIT;
    permitAcl.lookupClassPort() = kPortLookupClass;
    permitAcl.l4DstPort() = kPortUserMetaPermittedL4DstPort;
    utility::addAclEntry(
        config,
        permitAcl,
        kPortUserMetaWarmbootAclTableName,
        cfg::AclStage::INGRESS);
    utility::addAclStat(
        config,
        kPortUserMetaPermitAclName,
        kPortUserMetaPermitCounterName,
        {cfg::CounterType::PACKETS});

    cfg::AclEntry defaultDenyAcl;
    defaultDenyAcl.name() = kPortUserMetaDefaultDenyAclName;
    defaultDenyAcl.actionType() = denyAction;
    defaultDenyAcl.lookupClassPort() = kPortLookupClass;
    utility::addAclEntry(
        config,
        defaultDenyAcl,
        kPortUserMetaWarmbootAclTableName,
        cfg::AclStage::INGRESS);
    utility::addAclStat(
        config,
        kPortUserMetaDefaultDenyAclName,
        kPortUserMetaDefaultDenyCounterName,
        {cfg::CounterType::PACKETS});
  }

  void addAcls(cfg::SwitchConfig* config) const {
    cfg::AclEntry metadataAcl;
    metadataAcl.name() = kPortUserMetaAclName;
    metadataAcl.actionType() = cfg::AclActionType::DENY;
    metadataAcl.lookupClassPort() = kPortLookupClass;

    cfg::AclEntry fallbackAcl;
    fallbackAcl.name() = kPortUserMetaFallbackAclName;
    fallbackAcl.actionType() = cfg::AclActionType::DENY;
    fallbackAcl.ipType() = cfg::IpType::IP6;

    utility::addAclTable(
        config,
        kPortUserMetaAclTableName,
        1 /* priority */,
        {cfg::AclTableActionType::PACKET_ACTION,
         cfg::AclTableActionType::COUNTER},
        {cfg::AclTableQualifier::LOOKUP_CLASS_PORT,
         cfg::AclTableQualifier::IP_TYPE});
    utility::addAclEntry(
        config, metadataAcl, kPortUserMetaAclTableName, cfg::AclStage::INGRESS);
    utility::addAclEntry(
        config, fallbackAcl, kPortUserMetaAclTableName, cfg::AclStage::INGRESS);

    utility::addAclStat(
        config,
        kPortUserMetaAclName,
        kPortUserMetaAclCounterName,
        {cfg::CounterType::PACKETS});
    utility::addAclStat(
        config,
        kPortUserMetaFallbackAclName,
        kPortUserMetaFallbackCounterName,
        {cfg::CounterType::PACKETS});
  }

  uint64_t getCounter(const std::string& name) const {
    return utility::getAclInOutPackets(getSw(), name);
  }

  void sendPacketToPort(PortID port, uint16_t l4DstPort) {
    ASSERT_TRUE(
        getAgentEnsemble()->ensureSendPacketOutOfPort(
            makePacket(l4DstPort), port));
  }
};

class AgentPortUserMetaMultiAclTableTest : public AgentPortUserMetaAclTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::ACL_COUNTER,
        ProductionFeature::L3_FORWARDING,
        ProductionFeature::MULTI_ACL_TABLE,
        ProductionFeature::PORT_USER_METADATA,
    };
  }

 protected:
  void SetUp() override {
    AgentPortUserMetaAclTest::SetUp();
    if (IsSkipped()) {
      return;
    }
    helper_ = std::make_unique<utility::EcmpSetupAnyNPorts6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), RouterID(0));
  }

  void setupRoute() {
    XLOG(INFO) << "Programming the IPv6 route used to verify forwarded packets";
    applyNewState([this](const std::shared_ptr<SwitchState>& in) {
      return helper_->resolveNextHops(in, 1);
    });
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, 1);
  }

  PortID routedEgressPort() const {
    return helper_->ecmpPortDescriptorAt(0).phyPortID();
  }

  void verifyForwarded(PortID ingressPort, uint16_t l4DstPort) {
    const auto egressPort = routedEgressPort();
    const auto egressPacketsBefore =
        *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
    XLOG(INFO) << "Sending UDP packet: ingressPort=" << ingressPort
               << ", l4DstPort=" << l4DstPort
               << ", expected=PERMITTED, egressPort=" << egressPort
               << ", egressPacketsBefore=" << egressPacketsBefore;
    sendPacketToPort(ingressPort, l4DstPort);
    uint64_t egressPacketsAfter{0};
    WITH_RETRIES({
      egressPacketsAfter =
          *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
      EXPECT_EVENTUALLY_GT(egressPacketsAfter, egressPacketsBefore);
    });
    XLOG(INFO) << "Packet result=PERMITTED: ingressPort=" << ingressPort
               << ", l4DstPort=" << l4DstPort
               << ", egressPackets=" << egressPacketsBefore << " -> "
               << egressPacketsAfter;
  }

  void verifyForwardedWithoutPolicyMatch(
      PortID ingressPort,
      uint16_t l4DstPort) {
    const auto permitCounterBefore = getCounter(kPortUserMetaPermitCounterName);
    const auto denyCounterBefore =
        getCounter(kPortUserMetaDefaultDenyCounterName);
    const auto egressPort = routedEgressPort();
    const auto egressPacketsBefore =
        *getNextUpdatedPortStats(egressPort).outUnicastPkts_();

    XLOG(INFO) << "Sending UDP packet from untagged port: ingressPort="
               << ingressPort << ", l4DstPort=" << l4DstPort
               << ", expected=PERMITTED without policy match. Counters before: "
               << kPortUserMetaPermitCounterName << "=" << permitCounterBefore
               << ", " << kPortUserMetaDefaultDenyCounterName << "="
               << denyCounterBefore
               << ", egressPackets=" << egressPacketsBefore;
    sendPacketToPort(ingressPort, l4DstPort);
    uint64_t permitCounterAfter{0};
    uint64_t denyCounterAfter{0};
    uint64_t egressPacketsAfter{0};
    WITH_RETRIES({
      egressPacketsAfter =
          *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
      permitCounterAfter = getCounter(kPortUserMetaPermitCounterName);
      denyCounterAfter = getCounter(kPortUserMetaDefaultDenyCounterName);
      EXPECT_EVENTUALLY_GT(egressPacketsAfter, egressPacketsBefore);
      EXPECT_EVENTUALLY_EQ(permitCounterBefore, permitCounterAfter);
      EXPECT_EVENTUALLY_EQ(denyCounterBefore, denyCounterAfter);
    });
    XLOG(INFO) << "Packet result=PERMITTED without policy match. Counters "
                  "after: "
               << kPortUserMetaPermitCounterName << "=" << permitCounterAfter
               << ", " << kPortUserMetaDefaultDenyCounterName << "="
               << denyCounterAfter << ", egressPackets=" << egressPacketsAfter;
  }

  void verifyRestrictedPortPermit(PortID ingressPort) {
    const auto permitCounterBefore = getCounter(kPortUserMetaPermitCounterName);
    const auto denyCounterBefore =
        getCounter(kPortUserMetaDefaultDenyCounterName);
    const auto egressPort = routedEgressPort();
    const auto egressPacketsBefore =
        *getNextUpdatedPortStats(egressPort).outUnicastPkts_();

    XLOG(INFO) << "Sending UDP/" << kPortUserMetaPermittedL4DstPort
               << " packet from restricted port " << ingressPort
               << "; expected=PERMITTED. Counters before: "
               << kPortUserMetaPermitCounterName << "=" << permitCounterBefore
               << ", " << kPortUserMetaDefaultDenyCounterName << "="
               << denyCounterBefore
               << ", egressPackets=" << egressPacketsBefore;
    sendPacketToPort(ingressPort, kPortUserMetaPermittedL4DstPort);
    uint64_t permitCounterAfter{0};
    uint64_t denyCounterAfter{0};
    uint64_t egressPacketsAfter{0};
    WITH_RETRIES({
      permitCounterAfter = getCounter(kPortUserMetaPermitCounterName);
      denyCounterAfter = getCounter(kPortUserMetaDefaultDenyCounterName);
      egressPacketsAfter =
          *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
      EXPECT_EVENTUALLY_GT(egressPacketsAfter, egressPacketsBefore);
      EXPECT_EVENTUALLY_GE(permitCounterAfter, permitCounterBefore + 1);
      EXPECT_EVENTUALLY_LE(permitCounterAfter, permitCounterBefore + 2);
      EXPECT_EVENTUALLY_EQ(denyCounterBefore, denyCounterAfter);
    });
    XLOG(INFO) << "Packet result=PERMITTED by the UDP/53 ACL. Counters after: "
               << kPortUserMetaPermitCounterName << "=" << permitCounterAfter
               << ", " << kPortUserMetaDefaultDenyCounterName << "="
               << denyCounterAfter << ", egressPackets=" << egressPacketsAfter;
  }

  void verifyRestrictedPortDrop(PortID ingressPort) {
    const auto permitCounterBefore = getCounter(kPortUserMetaPermitCounterName);
    const auto denyCounterBefore =
        getCounter(kPortUserMetaDefaultDenyCounterName);
    const auto egressPort = routedEgressPort();
    const auto egressPacketsBefore =
        *getNextUpdatedPortStats(egressPort).outUnicastPkts_();

    XLOG(INFO) << "Sending UDP/" << kPortUserMetaDeniedL4DstPort
               << " packet from restricted port " << ingressPort
               << "; expected=DENIED. Counters before: "
               << kPortUserMetaPermitCounterName << "=" << permitCounterBefore
               << ", " << kPortUserMetaDefaultDenyCounterName << "="
               << denyCounterBefore
               << ", egressPackets=" << egressPacketsBefore;
    sendPacketToPort(ingressPort, kPortUserMetaDeniedL4DstPort);
    uint64_t permitCounterAfter{0};
    uint64_t denyCounterAfter{0};
    uint64_t egressPacketsAfter{0};
    WITH_RETRIES({
      permitCounterAfter = getCounter(kPortUserMetaPermitCounterName);
      denyCounterAfter = getCounter(kPortUserMetaDefaultDenyCounterName);
      egressPacketsAfter =
          *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
      EXPECT_EVENTUALLY_EQ(egressPacketsAfter, egressPacketsBefore);
      EXPECT_EVENTUALLY_EQ(permitCounterBefore, permitCounterAfter);
      EXPECT_EVENTUALLY_GE(denyCounterAfter, denyCounterBefore + 1);
      EXPECT_EVENTUALLY_LE(denyCounterAfter, denyCounterBefore + 2);
    });
    XLOG(INFO) << "Packet result=DENIED by the restricted-port fallback ACL. "
                  "Counters after: "
               << kPortUserMetaPermitCounterName << "=" << permitCounterAfter
               << ", " << kPortUserMetaDefaultDenyCounterName << "="
               << denyCounterAfter << ", egressPackets=" << egressPacketsAfter;
  }

 private:
  std::unique_ptr<utility::EcmpSetupAnyNPorts6> helper_;
};

class AgentPortUserMetaDenyControlPlaneTest
    : public AgentPortUserMetaMultiAclTableTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features =
        AgentPortUserMetaMultiAclTableTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::ACL_DENY_DATA_AND_CONTROL_PLANE);
    features.push_back(ProductionFeature::COPP);
    return features;
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return addCoppConfig(
        ensemble, AgentPortUserMetaMultiAclTableTest::initialConfig(ensemble));
  }

  std::map<std::string, std::unique_ptr<TxPacket>> makeControlPlanePackets(
      PortID ingressPort) {
    const auto state = getProgrammedState();
    const auto intfId = getInterfaceIDForPort(ingressPort, state);
    const auto intfMac = state->getInterfaces()->getNode(intfId)->getMac();
    const auto myIpsV4 = utility::getIntfAddrsV4(state, intfId);
    const auto myIpsV6 = utility::getIntfAddrsV6(state, intfId);
    CHECK(!myIpsV4.empty() && !myIpsV6.empty())
        << "interface " << intfId << " needs both a v4 and a v6 address";
    const auto vlanId = getSw()->getVlanIDForTx(intfId);
    const auto srcMac =
        utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    const folly::IPAddressV6 srcIpV6("2620:0:1cfe:face:b00c::1");

    std::map<std::string, std::unique_ptr<TxPacket>> packets;
    packets["arp-request"] = utility::makeARPTxPacket(
        getSw(),
        vlanId,
        srcMac,
        folly::MacAddress::BROADCAST,
        folly::IPAddressV4("10.0.0.2"),
        myIpsV4[0],
        ARP_OPER::ARP_OPER_REQUEST);
    packets["ndp-neighbor-solicit"] = utility::makeNeighborSolicitation(
        getSw(), vlanId, srcMac, srcIpV6, myIpsV6[0]);
    packets["ip2me"] = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        srcMac,
        intfMac,
        srcIpV6,
        myIpsV6[0],
        kTestSrcPort,
        kPortUserMetaDeniedL4DstPort,
        0,
        255);
    return packets;
  }

  std::set<std::string> puntedControlPlanePackets(PortID ingressPort) {
    auto packets = makeControlPlanePackets(ingressPort);
    std::map<std::string, std::unique_ptr<utility::SwSwitchPacketSnooper>>
        snoopers;
    for (const auto& [name, pkt] : packets) {
      snoopers[name] = std::make_unique<utility::SwSwitchPacketSnooper>(
          getSw(),
          name,
          std::nullopt /* port */,
          utility::makeEthFrame(*pkt, true /* skipTtlDecrement */));
      snoopers[name]->ignoreUnclaimedRxPkts();
    }
    for (auto& [name, pkt] : packets) {
      EXPECT_TRUE(
          getSw()->sendPacketOutOfPortAsync(std::move(pkt), ingressPort))
          << "failed to send " << name;
    }
    std::set<std::string> punted;
    for (const auto& [name, snooper] : snoopers) {
      if (snooper->waitForPacket(kControlPlanePuntTimeoutS).has_value()) {
        punted.insert(name);
      }
    }
    return punted;
  }
};

// Verify port metadata selects its ACL, while traffic from an untagged port
// falls through to the IPv6 fallback ACL.
TEST_F(AgentPortUserMetaAclTest, MatchIngressPortMetadata) {
  if (!getAgentEnsemble()->isSai()) {
    GTEST_SKIP() << "Port user metadata is a SAI-only test";
  }
  const auto ports = masterLogicalInterfacePortIds();
  if (ports.size() < 2) {
    GTEST_SKIP() << "Need two interface ports";
  }
  const PortID metadataPort{ports[0]};
  const PortID otherPort{ports[1]};

  auto setup = [=, this]() {
    XLOG(INFO) << "Configuring port " << metadataPort
               << " with restricted user metadata and installing metadata "
                  "and IPv6 fallback deny ACLs";
    auto config = initialConfig(*getAgentEnsemble());
    auto portCfg = utility::findCfgPort(config, metadataPort);
    portCfg->userMetaData() = kPortLookupClass;
    addAcls(&config);
    applyNewConfig(config);
  };

  auto verify = [=, this]() {
    const auto bootType =
        getSw()->getBootType() == BootType::WARM_BOOT ? "warmboot" : "coldboot";
    XLOG(INFO) << "Verifying port metadata ACL matching during " << bootType;
    auto metadataBefore = getCounter(kPortUserMetaAclCounterName);
    auto fallbackBefore = getCounter(kPortUserMetaFallbackCounterName);

    XLOG(INFO) << "Sending a packet from metadata port " << metadataPort
               << "; expect deny by the port metadata ACL. Counters before: "
               << kPortUserMetaAclCounterName << "=" << metadataBefore << ", "
               << kPortUserMetaFallbackCounterName << "=" << fallbackBefore;
    ASSERT_TRUE(
        getAgentEnsemble()->ensureSendPacketOutOfPort(
            makePacket(), metadataPort));
    WITH_RETRIES({
      const auto metadataAfter = getCounter(kPortUserMetaAclCounterName);
      const auto fallbackAfter = getCounter(kPortUserMetaFallbackCounterName);
      EXPECT_EVENTUALLY_EQ(metadataBefore + 1, metadataAfter);
      EXPECT_EVENTUALLY_EQ(fallbackBefore, fallbackAfter);
    });
    XLOG(INFO) << "Packet from metadata port was denied by the port metadata "
                  "ACL. Counters after: "
               << kPortUserMetaAclCounterName << "="
               << getCounter(kPortUserMetaAclCounterName) << ", "
               << kPortUserMetaFallbackCounterName << "="
               << getCounter(kPortUserMetaFallbackCounterName);

    metadataBefore = getCounter(kPortUserMetaAclCounterName);
    fallbackBefore = getCounter(kPortUserMetaFallbackCounterName);

    XLOG(INFO) << "Sending a packet from untagged port " << otherPort
               << "; expect deny by the IPv6 fallback ACL. Counters before: "
               << kPortUserMetaAclCounterName << "=" << metadataBefore << ", "
               << kPortUserMetaFallbackCounterName << "=" << fallbackBefore;
    ASSERT_TRUE(
        getAgentEnsemble()->ensureSendPacketOutOfPort(makePacket(), otherPort));
    WITH_RETRIES({
      const auto metadataAfter = getCounter(kPortUserMetaAclCounterName);
      const auto fallbackAfter = getCounter(kPortUserMetaFallbackCounterName);
      EXPECT_EVENTUALLY_EQ(metadataBefore, metadataAfter);
      EXPECT_EVENTUALLY_EQ(fallbackBefore + 1, fallbackAfter);
    });
    XLOG(INFO) << "Packet from untagged port was denied by the IPv6 fallback "
                  "ACL. Counters after: "
               << kPortUserMetaAclCounterName << "="
               << getCounter(kPortUserMetaAclCounterName) << ", "
               << kPortUserMetaFallbackCounterName << "="
               << getCounter(kPortUserMetaFallbackCounterName);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// After warm boot, verify a second ACL table permits UDP/53 on a restricted
// port, drops its other traffic, and leaves an untagged port unrestricted.
TEST_F(
    AgentPortUserMetaMultiAclTableTest,
    AddPortMetadataAclTableAfterWarmboot) {
  if (!getAgentEnsemble()->isSai()) {
    GTEST_SKIP() << "Port user metadata is a SAI-only test";
  }
  ASSERT_TRUE(isSupportedOnAllAsics(HwAsic::Feature::MULTIPLE_ACL_TABLES));

  const auto ports = masterLogicalInterfacePortIds();
  if (ports.size() < 3) {
    GTEST_SKIP() << "Need one routed egress port and two ingress ports";
  }
  const PortID restrictedPort{ports[1]};
  const PortID unchangedPort{ports[2]};

  auto setup = [this]() {
    XLOG(INFO) << "Coldboot setup: install only the default ACL table";
    setupRoute();
  };

  auto verify = [=, this]() {
    XLOG(INFO) << "Coldboot verification: all four packets must be permitted";
    verifyForwarded(restrictedPort, kPortUserMetaPermittedL4DstPort);
    verifyForwarded(restrictedPort, kPortUserMetaDeniedL4DstPort);
    verifyForwarded(unchangedPort, kPortUserMetaPermittedL4DstPort);
    verifyForwarded(unchangedPort, kPortUserMetaDeniedL4DstPort);
  };

  auto setupPostWarmboot = [=, this]() {
    XLOG(INFO) << "Warmboot setup: mark port " << restrictedPort
               << " restricted and add the port metadata ACL table";
    auto config = initialConfig(*getAgentEnsemble());
    auto portCfg = utility::findCfgPort(config, restrictedPort);
    portCfg->userMetaData() = kPortLookupClass;
    addWarmbootAclTable(&config);
    applyNewConfig(config);
  };

  auto verifyPostWarmboot = [=, this]() {
    XLOG(INFO) << "Warmboot verification: restricted UDP/53 is permitted, "
                  "restricted UDP/54 is denied, and untagged traffic is "
                  "permitted";
    verifyRestrictedPortDrop(restrictedPort);
    verifyForwardedWithoutPolicyMatch(
        unchangedPort, kPortUserMetaPermittedL4DstPort);
    verifyForwardedWithoutPolicyMatch(
        unchangedPort, kPortUserMetaDeniedL4DstPort);
    verifyRestrictedPortPermit(restrictedPort);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// Removing user metadata has to clear the tag in hardware, not just in switch
// state. The ACL table stays in place, so traffic that its restricted-class
// deny used to drop must be forwarded again, without matching either ACL.
TEST_F(AgentPortUserMetaMultiAclTableTest, RemovePortMetadataAfterWarmboot) {
  if (!getAgentEnsemble()->isSai()) {
    GTEST_SKIP() << "Port user metadata is a SAI-only test";
  }
  ASSERT_TRUE(isSupportedOnAllAsics(HwAsic::Feature::MULTIPLE_ACL_TABLES));

  const auto ports = masterLogicalInterfacePortIds();
  if (ports.size() < 3) {
    GTEST_SKIP() << "Need one routed egress port and two ingress ports";
  }
  const PortID restrictedPort{ports[1]};
  const PortID unchangedPort{ports[2]};

  auto setup = [=, this]() {
    XLOG(INFO) << "Coldboot setup: mark port " << restrictedPort
               << " restricted and install the port metadata ACL table";
    setupRoute();
    auto config = initialConfig(*getAgentEnsemble());
    auto portCfg = utility::findCfgPort(config, restrictedPort);
    portCfg->userMetaData() = kPortLookupClass;
    addWarmbootAclTable(&config);
    applyNewConfig(config);
  };

  auto verify = [=, this]() {
    XLOG(INFO) << "Coldboot verification: the restricted port is policed";
    verifyRestrictedPortDrop(restrictedPort);
    verifyRestrictedPortPermit(restrictedPort);
  };

  auto setupPostWarmboot = [=, this]() {
    XLOG(INFO) << "Warmboot setup: drop user metadata from port "
               << restrictedPort << ", keeping the ACL table in place";
    auto config = initialConfig(*getAgentEnsemble());
    addWarmbootAclTable(&config);
    applyNewConfig(config);
  };

  auto verifyPostWarmboot = [=, this]() {
    XLOG(INFO) << "Warmboot verification: port " << restrictedPort
               << " is no longer classified, so the deny that used to drop its "
                  "UDP/54 traffic no longer matches";
    EXPECT_FALSE(
        getProgrammedState()
            ->getPorts()
            ->getNode(restrictedPort)
            ->getUserMetaData()
            .has_value());
    verifyForwardedWithoutPolicyMatch(
        restrictedPort, kPortUserMetaDeniedL4DstPort);
    verifyForwardedWithoutPolicyMatch(
        unchangedPort, kPortUserMetaDeniedL4DstPort);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// DENY_DATA_AND_CONTROL_PLANE drops the restricted port's traffic and also
// cancels the copy to CPU a trap would make, so ARP, NDP and IP2Me packets
// that an untagged port punts never reach the CPU.
TEST_F(AgentPortUserMetaDenyControlPlaneTest, DenyCancelsControlPlanePunt) {
  if (!getAgentEnsemble()->isSai()) {
    GTEST_SKIP() << "Port user metadata is a SAI-only test";
  }
  ASSERT_TRUE(isSupportedOnAllAsics(HwAsic::Feature::MULTIPLE_ACL_TABLES));

  const auto ports = masterLogicalInterfacePortIds();
  if (ports.size() < 3) {
    GTEST_SKIP() << "Need one routed egress port and two ingress ports";
  }
  const PortID restrictedPort{ports[1]};
  const PortID unchangedPort{ports[2]};

  auto setup = [=, this]() {
    setupRoute();
    auto config = initialConfig(*getAgentEnsemble());
    auto portCfg = utility::findCfgPort(config, restrictedPort);
    portCfg->userMetaData() = kPortLookupClass;
    addWarmbootAclTable(
        &config,
        cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE,
        kPortUserMetaCoppAclTablePriority);
    applyNewConfig(config);
  };

  auto verify = [=, this]() {
    verifyRestrictedPortDrop(restrictedPort);
    verifyRestrictedPortPermit(restrictedPort);
    const std::set<std::string> allPackets{
        "arp-request", "ndp-neighbor-solicit", "ip2me"};
    EXPECT_EQ(puntedControlPlanePackets(unchangedPort), allPackets)
        << "control plane packets from untagged port " << unchangedPort;
    EXPECT_EQ(
        puntedControlPlanePackets(restrictedPort), std::set<std::string>{})
        << "control plane packets from restricted port " << restrictedPort;
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Verify that traffic arrive on a front panel port increments ACL counter.
TEST_F(AgentAclCounterTest, VerifyCounterBumpOnTtlHit) {
  this->counterBumpOnHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::TCP_TTLD, AclType::UDP_TTLD});
}

TEST_F(AgentAclCounterTest, VerifyCounterBumpOnSportHit) {
  this->counterBumpOnHitHelper(
      true /* bump on hit */, true /* front panel port */, {AclType::SRC_PORT});
}

TEST_F(
    AgentL4DstPortAclCounterTest,
    VerifyCounterBumpOnL4DstportHitFrontPanel) {
  this->counterBumpOnHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::L4_DST_PORT});
}

TEST_F(AgentAclCounterL4DstPortRangeTest, VerifyL4DstPortRangeAcl) {
  auto setup = [this]() { setupRangeAclCounterTest(); };
  auto verify = [this]() {
    const auto portsA = portsInRange(kL4DstPortRangeMinA, kL4DstPortRangeMaxA);
    const auto portsB = portsInRange(kL4DstPortRangeMinB, kL4DstPortRangeMaxB);
    const auto portsOutside = portsOutsideRanges();
    for (auto isFrontPanel : {true, false}) {
      for (auto isV6 : {false, true}) {
        const auto trafficSrc = isFrontPanel ? "front-panel" : "cpu";
        const auto ipFamily = isV6 ? "IPv6" : "IPv4";
        verifyL4DstPortRangeAclCounters(
            folly::to<std::string>(trafficSrc, ", ", ipFamily, ", range A hit"),
            isFrontPanel,
            isV6,
            portsA,
            ExpectedRangeHit::RANGE_A);
        verifyL4DstPortRangeAclCounters(
            folly::to<std::string>(trafficSrc, ", ", ipFamily, ", range B hit"),
            isFrontPanel,
            isV6,
            portsB,
            ExpectedRangeHit::RANGE_B);
        verifyL4DstPortRangeAclCounters(
            folly::to<std::string>(trafficSrc, ", ", ipFamily, ", miss"),
            isFrontPanel,
            isV6,
            portsOutside,
            ExpectedRangeHit::NEITHER);
      }
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentDstIpV6WordAclCounterTest, VerifyDstIpV6Word2AndWord3AclCounter) {
  auto setup = [this]() { setupDstIpV6WordAclCounterTest(); };
  auto verify = [this]() {
    verifyDstIpV6WordAclCounter(
        "word2 and word3 hit",
        folly::IPAddressV6("1234:5678:9abc:def0::1"),
        true);
    verifyDstIpV6WordAclCounter(
        "word2 miss while word3 still matches",
        folly::IPAddressV6("1234:5678:1111:2222::1"),
        false);
    verifyDstIpV6WordAclCounter(
        "word3 miss while word2 still matches",
        folly::IPAddressV6("1111:2222:9abc:def0::1"),
        false);
    verifyDstIpV6WordAclCounter(
        "same word2 and word3 with all lower 64 bits different still hits",
        folly::IPAddressV6("1234:5678:9abc:def0:ffff:ffff:ffff:ffff"),
        true);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentAclCounterTest, VerifyCounterBumpOnSportHitWithDrop) {
  this->aclActionType_ = cfg::AclActionType::DENY;
  this->counterBumpOnHitHelper(
      true /* bump on hit */, true /* front panel port */, {AclType::SRC_PORT});
}
// Verify that traffic arrive on a front panel port increments ACL counter.
TEST_F(AgentAclCounterTest, VerifyCounterNoTtlHitNoBump) {
  this->counterBumpOnHitHelper(
      false /* no hit, no bump */,
      true /* front panel port */,
      {AclType::TCP_TTLD, AclType::UDP_TTLD});
}

TEST_F(AgentAclCounterTest, VerifyAclPrioritySportHitFrontPanel) {
  this->aclPriorityTestHelper();
}

TEST_F(AgentL4DstPortAclCounterTest, VerifyAclPriorityL4DstportHitFrontPanel) {
  this->aclPriorityTestHelper2();
}

/*
 * UDF Acls are not supported on SAI and multi ACL. So we only test with
 * multi acl disabled for now.
 */
class AgentUdfAclCounterTest : public AgentAclCounterTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    cfg.udfConfig() = utility::addUdfAclConfig();
    if (FLAGS_enable_acl_table_group) {
      std::vector<std::string> udfGroups = {
          utility::kUdfAclRoceOpcodeGroupName};
      utility::addAclTableGroup(
          &cfg, cfg::AclStage::INGRESS, utility::kDefaultAclTableGroupName());
      utility::addDefaultAclTable(cfg, udfGroups);
    }
    return cfg;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentAclCounterTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::UDF_WR_IMMEDIATE_ACL);
    return features;
  }
};

TEST_F(AgentUdfAclCounterTest, VerifyUdfWithOtherAcls) {
  counterBumpOnHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::UDF_OPCODE_ACK,
       AclType::UDF_OPCODE_WRITE_IMMEDIATE,
       AclType::SRC_PORT});
}

TEST_F(AgentUdfAclCounterTest, VerifyAddRemoveUdfAcls) {
  auto setup = [this]() {
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper_->resolveNextHops(in, 2);
    });
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
    auto newCfg{initialConfig(*getAgentEnsemble())};
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    applyNewConfig(newCfg);
  };

  auto verify = [this]() {
    XLOG(DBG2) << "verify roce ack packets are matched";
    verifyAclType(true, true, AclType::UDF_OPCODE_ACK);
    XLOG(DBG2) << "verify roce write immediate packets are not matched";
    verifyAclType(false, true, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    auto newCfg{initialConfig(*getAgentEnsemble())};
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    applyNewConfig(newCfg);
    XLOG(DBG2) << "verify roce ack packets are not matched";
    verifyAclType(false, true, AclType::UDF_OPCODE_ACK);
    XLOG(DBG2) << "verify roce write immediate packets are matched";
    verifyAclType(true, true, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    newCfg = initialConfig(*getAgentEnsemble());
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    applyNewConfig(newCfg);
  };

  auto setupPostWarmboot = [this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    applyNewConfig(newCfg);
  };

  auto verifyPostWarmboot = [this]() {
    XLOG(DBG2) << "verify roce ack packets are matched";
    verifyAclType(true, true, AclType::UDF_OPCODE_ACK);
    XLOG(DBG2) << "verify roce write immediate packets are matched";
    verifyAclType(true, true, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    auto newCfg{initialConfig(*getAgentEnsemble())};
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    applyNewConfig(newCfg);
    XLOG(DBG2) << "verify roce ack packets are matched";
    verifyAclType(true, true, AclType::UDF_OPCODE_ACK);
    XLOG(DBG2) << "verify roce write immediate packets are not matched";
    verifyAclType(false, true, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// Add UDF for hash config after warmboot
TEST_F(AgentUdfAclCounterTest, VerifyUdfPlusUdfHash) {
  auto setup = [this]() {
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper_->resolveNextHops(in, 2);
    });
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
    auto newCfg{initialConfig(*getAgentEnsemble())};
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    applyNewConfig(newCfg);
  };

  auto verify = [this]() {
    verifyAclType(true, true, AclType::UDF_OPCODE_ACK);
    verifyAclType(true, true, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
  };

  auto setupPostWarmboot = [this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    auto asicType = checkSameAndGetAsicType(newCfg);

    newCfg.udfConfig() = utility::addUdfHashAclConfig(asicType);
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    applyNewConfig(newCfg);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verify);
}

// Remove UDF for hash config after warmboot
TEST_F(AgentUdfAclCounterTest, VerifyUdfMinusUdfHash) {
  auto setup = [this]() {
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper_->resolveNextHops(in, 2);
    });
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, kDefaultEcmpWidth);
    auto newCfg{initialConfig(*getAgentEnsemble())};
    auto asicType = checkSameAndGetAsicType(newCfg);
    newCfg.udfConfig() = utility::addUdfHashAclConfig(asicType);
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    applyNewConfig(newCfg);
  };

  auto verify = [this]() {
    verifyAclType(true, true, AclType::UDF_OPCODE_ACK);
    verifyAclType(true, true, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
  };

  auto setupPostWarmboot = [this]() {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_ACK);
    addAclAndStat(&newCfg, AclType::UDF_OPCODE_WRITE_IMMEDIATE);
    applyNewConfig(newCfg);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verify);
}

class AgentBthOpcodeAclCounterTest : public AgentAclCounterTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::BTH_OPCODE_ACL, ProductionFeature::SINGLE_ACL_TABLE};
  }
};

TEST_F(
    AgentBthOpcodeAclCounterTest,
    VerifyCounterBumpOnBthOpcodeHitFrontPanel) {
  this->counterBumpOnHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::BTH_OPCODE});
}

/*
 * Flowlet Acls are not supported on SAI and multi ACL. So we only test with
 * multi acl disabled for now.
 */
class AgentFlowletAclCounterTest : public AgentAclCounterTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::DLB,
        ProductionFeature::SINGLE_ACL_TABLE,
        ProductionFeature::UDF_WR_IMMEDIATE_ACL};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    cfg.udfConfig() = utility::addUdfAclConfig(
        utility::kUdfOffsetBthOpcode | utility::kUdfOffsetBthReserved);
    if (FLAGS_enable_acl_table_group) {
      std::vector<std::string> udfGroups = {
          utility::kUdfAclRoceOpcodeGroupName,
          utility::kRoceUdfFlowletGroupName};
      utility::addAclTableGroup(
          &cfg, cfg::AclStage::INGRESS, utility::kDefaultAclTableGroupName());
      utility::addDefaultAclTable(cfg, udfGroups);
    }
    return cfg;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_flowletSwitchingEnable = true;
  }
};

TEST_F(AgentFlowletAclCounterTest, VerifyFlowletNegative) {
  this->roceReservedByte_ = 0x0;
  counterBumpOnFlowletAclHitHelper(
      false /* bump on hit */,
      true /* front panel port */,
      {AclType::UDF_FLOWLET});
}

TEST_F(AgentFlowletAclCounterTest, VerifyFlowletWithOtherAcls) {
  counterBumpOnFlowletAclHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::FLOWLET, AclType::SRC_PORT});
}

TEST_F(AgentFlowletAclCounterTest, VerifyUdfFlowletWithOtherAcls) {
  counterBumpOnFlowletAclHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::UDF_FLOWLET, AclType::SRC_PORT});
}

TEST_F(AgentFlowletAclCounterTest, VerifyFlowletWithUdf) {
  counterBumpOnFlowletAclHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::FLOWLET,
       AclType::UDF_OPCODE_ACK,
       AclType::UDF_OPCODE_WRITE_IMMEDIATE});
}

// Verifying the FLOWLET Acl always hit ahead of UDF Acl
// when FLOWLET Acl present before UDF Acl
TEST_F(AgentFlowletAclCounterTest, VerifyUdfFlowletWithUdf) {
  counterBumpOnFlowletAclHitHelper(
      true /* bump on hit */,
      true /* front panel port */,
      {AclType::UDF_FLOWLET,
       AclType::UDF_OPCODE_ACK,
       AclType::UDF_OPCODE_WRITE_IMMEDIATE});
}
} // namespace facebook::fboss
