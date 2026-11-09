/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/agent_hw_tests/AgentArsBase.h"

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/MirrorTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/ScaleTestUtils.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"
#include "fboss/lib/CommonUtils.h"

const std::string kSflowMirrorName = "sflow_mirror";

namespace facebook::fboss {

void AgentArsBase::SetUp() {
  AgentHwTest::SetUp();
  if (IsSkipped()) {
    return;
  }
  helper_ = std::make_unique<utility::EcmpSetupTargetedPorts6>(
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), RouterID(0));
}

void AgentArsBase::TearDown() {
  helper_.reset();
  AgentHwTest::TearDown();
}

cfg::SwitchConfig AgentArsBase::initialConfig(
    const AgentEnsemble& ensemble) const {
  auto cfg = utility::onePortPerInterfaceConfig(
      ensemble.getSw(),
      ensemble.masterLogicalPortIds(),
      true /*interfaceHasSubnet*/);
  return cfg;
}

bool AgentArsBase::isChenab(const AgentEnsemble& ensemble) const {
  auto hwAsic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
  return (hwAsic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB);
}

bool AgentArsBase::isTH3(const AgentEnsemble& ensemble) const {
  auto hwAsic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
  return (hwAsic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK3);
}

std::string AgentArsBase::getAclName(AclType aclType) const {
  std::string aclName{};
  switch (aclType) {
    case AclType::UDF_ACK:
      aclName = "test-udf-acl";
      break;
    case AclType::UDF_NAK:
    case AclType::UDF_ACK_WITH_NAK:
      aclName = "test-udf-nak-acl";
      break;
    case AclType::UDF_WR_IMM_ZERO:
      aclName = "test-wr-imm-zero-acl";
      break;
    case AclType::FLOWLET:
    case AclType::FLOWLET_WITH_UDF_ACK:
    case AclType::FLOWLET_WITH_UDF_NAK:
      aclName = "test-flowlet-acl";
      break;
    case AclType::UDF_FLOWLET:
    case AclType::UDF_FLOWLET_WITH_UDF_ACK:
    case AclType::UDF_FLOWLET_WITH_UDF_NAK:
      aclName = utility::kFlowletAclName;
      break;
    case AclType::ECMP_HASH_CANCEL:
      aclName = "test-ecmp-hash-cancel";
      break;
    case AclType::ROCE_SPRAY_MISS:
      aclName = "test-roce-spray-miss";
      break;
    default:
      break;
  }
  return aclName;
}

std::string AgentArsBase::getCounterName(AclType aclType) const {
  return getAclName(aclType) + "-stats";
}

std::vector<PortID> AgentArsBase::getTestPorts() const {
  return masterLogicalInterfacePortIds();
}

void AgentArsBase::setup(int ecmpWidth) {
  std::vector<PortID> portIds = getTestPorts();
  boost::container::flat_set<PortDescriptor> portDescs;
  std::vector<PortDescriptor> tempPortDescs;
  for (size_t w = 0; w < ecmpWidth; ++w) {
    tempPortDescs.emplace_back(portIds[w]);
  }
  portDescs.insert(
      std::make_move_iterator(tempPortDescs.begin()),
      std::make_move_iterator(tempPortDescs.end()));

  applyNewState([&portDescs, this](const std::shared_ptr<SwitchState>& in) {
    return helper_->resolveNextHops(in, portDescs);
  });
  auto wrapper = getSw()->getRouteUpdater();
  helper_->programRoutes(&wrapper, portDescs);

  // make front panel port for test routable
  portDescs.emplace(portIds[kFrontPanelPortForTest]);
  applyNewState([&portDescs, this](const std::shared_ptr<SwitchState>& in) {
    return helper_->resolveNextHops(in, portDescs);
  });

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
}

RoutePrefixV6 AgentArsBase::getMirrorDestRoutePrefix(
    const folly::IPAddress dip) const {
  return RoutePrefixV6{
      folly::IPAddressV6{dip.str()}, static_cast<uint8_t>(dip.bitCount())};
}

void AgentArsBase::addSamplingConfig(cfg::SwitchConfig& config) {
  auto trafficPort = getAgentEnsemble()->masterLogicalPortIds(
      {cfg::PortType::INTERFACE_PORT})[kFrontPanelPortForTest];
  std::vector<PortID> samplePorts = {trafficPort};
  utility::configureSflowSampling(config, kSflowMirrorName, samplePorts, 1);
}

void AgentArsBase::addAclTableConfig(
    cfg::SwitchConfig& config,
    std::vector<std::string>& udfGroups) const {
  if (FLAGS_enable_acl_table_group) {
    std::vector<cfg::AclTableQualifier> qualifiers = {};
    std::vector<cfg::AclTableActionType> actions = {};
    utility::addAclTableGroup(
        &config, cfg::AclStage::INGRESS, utility::kDefaultAclTableGroupName());
    utility::addDefaultAclTable(config, udfGroups);
  }
}

void AgentArsBase::resolveMirror(
    const std::string& mirrorName,
    uint8_t dstPort) {
  auto destinationPort = getAgentEnsemble()->masterLogicalPortIds(
      {cfg::PortType::INTERFACE_PORT})[dstPort];
  applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    boost::container::flat_set<PortDescriptor> nhopPorts{
        PortDescriptor(destinationPort)};
    return helper_->resolveNextHops(in, nhopPorts);
  });
  getSw()->getUpdateEvb()->runInFbossEventBaseThreadAndWait([] {});
  auto mirror = getSw()->getState()->getMirrors()->getNodeIf(mirrorName);
  auto dip = mirror->getDestinationIp();
  if (dip.has_value()) {
    auto prefix = getMirrorDestRoutePrefix(dip.value());
    boost::container::flat_set<PortDescriptor> nhopPorts{
        PortDescriptor(destinationPort)};
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, nhopPorts, {prefix});
  }
}

void AgentArsBase::generateApplyConfig(AclType aclType) {
  const auto& ensemble = *getAgentEnsemble();
  auto newCfg{initialConfig(ensemble)};
  auto hwAsic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
  auto streamType =
      *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
  utility::addNetworkAIQueueConfig(
      &newCfg, streamType, cfg::QueueScheduling::STRICT_PRIORITY, hwAsic);
  utility::addNetworkAIQosMaps(newCfg, ensemble.getL3Asics());
  if (hwAsic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    utility::addCpuQueueConfig(newCfg, ensemble.getL3Asics(), ensemble.isSai());
  }

  // add ACL entry after above config since addNetworkAIQosMaps overwrites
  // dataPlaneTrafficPolicy
  std::vector<std::string> udfGroups = getUdfGroupsForAcl(aclType);
  addAclTableConfig(newCfg, udfGroups);
  addAclAndStat(&newCfg, aclType, ensemble.isSai());
  applyNewConfig(newCfg);
}

void AgentArsBase::flowletSwitchingAclHitHelper(
    AclType aclTypePre,
    AclType aclTypePost) {
  auto setup = [this, aclTypePre]() {
    this->setup();
    generateApplyConfig(aclTypePre);
  };

  auto verify = [this, aclTypePre]() { verifyAcl(aclTypePre); };

  auto setupPostWarmboot = [this, aclTypePost]() {
    generateApplyConfig(aclTypePost);
  };

  auto verifyPostWarmboot = [this, aclTypePost]() { verifyAcl(aclTypePost); };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

void AgentArsBase::verifyUdfAddDelete(AclType aclTypePre, AclType aclTypePost) {
  auto setup = [this, aclTypePre]() {
    this->setup();
    generateApplyConfig(aclTypePre);
    verifyAcl(aclTypePre);
  };

  auto verify = [this, aclTypePost]() {
    generateApplyConfig(aclTypePost);
    verifyAcl(aclTypePost);
  };

  verifyAcrossWarmBoots(setup, verify);
}

size_t AgentArsBase::sendRoceTraffic(
    const PortID& frontPanelEgrPort,
    int roceOpcode,
    const std::optional<std::vector<uint8_t>>& nxtHdr,
    int packetCount,
    int destPort,
    uint8_t reserved,
    const std::optional<folly::IPAddressV6>& srcIp) {
  auto vlanId = getVlanIDForTx();
  auto intfMac =
      getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  return utility::pumpRoCETraffic(
      true,
      utility::getAllocatePktFn(getAgentEnsemble()),
      utility::getSendPktFunc(getAgentEnsemble()),
      intfMac,
      vlanId,
      frontPanelEgrPort,
      srcIp.value_or(folly::IPAddressV6(kRoceSrcIp)),
      folly::IPAddressV6(kRoceDstIp),
      destPort,
      255,
      std::nullopt,
      packetCount,
      roceOpcode,
      reserved,
      nxtHdr);
}

auto AgentArsBase::verifyAclType(bool bumpOnHit, AclType aclType) {
  auto egressPort =
      helper_->ecmpPortDescriptorAt(kFrontPanelPortForTest).phyPortID();
  auto pktsBefore = *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
  auto aclPktCountBefore =
      utility::getAclInOutPackets(getSw(), getCounterName(aclType));
  auto aclBytesCountBefore = utility::getAclInOutPackets(
      getSw(), getCounterName(aclType), true /* bytes */);
  size_t sizeOfPacketSent = 0;

  std::vector<uint8_t> aethHdr = {0x0a, 0x11, 0x22, 0x33};
  // RDMAeth header with DMA length 0
  std::vector<uint8_t> rethHdr(16);

  switch (aclType) {
    case AclType::UDF_ACK:
      sizeOfPacketSent =
          sendRoceTraffic(egressPort, utility::kUdfRoceOpcodeAck, aethHdr);
      break;
    case AclType::UDF_NAK:
      aethHdr[0] = 0x6a; // MSB bits 2 and 3 indicate NAK
      sizeOfPacketSent =
          sendRoceTraffic(egressPort, utility::kUdfRoceOpcodeAck, aethHdr);
      break;
    case AclType::FLOWLET:
    case AclType::UDF_FLOWLET:
      rethHdr[15] = 0xFF; // non-zero sized packet
      sizeOfPacketSent = sendRoceTraffic(
          egressPort, utility::kUdfRoceOpcodeWriteImmediate, rethHdr);
      break;
    case AclType::UDF_WR_IMM_ZERO:
      sizeOfPacketSent = sendRoceTraffic(
          egressPort, utility::kUdfRoceOpcodeWriteImmediate, rethHdr);
      break;
    default:
      break;
  }

  WITH_RETRIES({
    auto aclPktCountAfter =
        utility::getAclInOutPackets(getSw(), getCounterName(aclType));

    auto aclBytesCountAfter = utility::getAclInOutPackets(
        getSw(), getCounterName(aclType), true /* bytes */);

    auto pktsAfter = *getNextUpdatedPortStats(egressPort).outUnicastPkts_();
    XLOG(DBG2) << "\n"
               << "PacketCounter: " << pktsBefore << " -> " << pktsAfter << "\n"
               << "aclPacketCounter(" << getCounterName(aclType)
               << "): " << aclPktCountBefore << " -> " << (aclPktCountAfter)
               << "\n"
               << "aclBytesCounter(" << getCounterName(aclType)
               << "): " << aclBytesCountBefore << " -> " << aclBytesCountAfter;

    if (bumpOnHit) {
      EXPECT_EVENTUALLY_GT(pktsAfter, pktsBefore);
      // On some ASICs looped back pkt hits the ACL before being
      // dropped in the ingress pipeline, hence GE
      EXPECT_EVENTUALLY_GE(aclPktCountAfter, aclPktCountBefore + 1);
      // At most we should get a pkt bump of 2
      EXPECT_EVENTUALLY_LE(aclPktCountAfter, aclPktCountBefore + 2);
      // TODO ruinanhu: Remove this once we have a fix for TH6 counter problem
      auto hwAsic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      auto extraBytes =
          (hwAsic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK6) ? 4 : 0;

      EXPECT_EVENTUALLY_GE(
          aclBytesCountAfter + extraBytes,
          aclBytesCountBefore + sizeOfPacketSent);
      // On native BCM we see 4 extra bytes in the acl counter. This is
      // likely due to ingress vlan getting imposed and getting counted
      // when packet hits acl in ingress pipeline
      if (!getAgentEnsemble()->isSai()) {
        EXPECT_EVENTUALLY_LE(
            aclBytesCountAfter,
            aclBytesCountBefore + (2 * sizeOfPacketSent) + 4);
      }
    } else {
      EXPECT_EVENTUALLY_EQ(aclPktCountBefore, aclPktCountAfter);
      EXPECT_EVENTUALLY_EQ(aclBytesCountBefore, aclBytesCountAfter);
    }
  });
}

void AgentArsBase::verifyAcl(AclType aclType) {
  switch (aclType) {
    case AclType::UDF_ACK:
      verifyAclType(true, AclType::UDF_ACK);
      break;
    case AclType::UDF_NAK:
      verifyAclType(true, AclType::UDF_NAK);
      break;
    case AclType::UDF_ACK_WITH_NAK:
      verifyAclType(true, AclType::UDF_ACK);
      verifyAclType(true, AclType::UDF_NAK);
      break;
    case AclType::UDF_WR_IMM_ZERO:
      verifyAclType(true, AclType::UDF_WR_IMM_ZERO);
      break;
    case AclType::FLOWLET:
      verifyAclType(true, AclType::FLOWLET);
      break;
    case AclType::FLOWLET_WITH_UDF_ACK:
      verifyAclType(true, AclType::FLOWLET);
      verifyAclType(true, AclType::UDF_ACK);
      break;
    case AclType::FLOWLET_WITH_UDF_NAK:
      verifyAclType(true, AclType::FLOWLET);
      verifyAclType(true, AclType::UDF_NAK);
      break;
    case AclType::UDF_FLOWLET:
      verifyAclType(true, AclType::UDF_FLOWLET);
      break;
    case AclType::UDF_FLOWLET_WITH_UDF_ACK:
      verifyAclType(true, AclType::UDF_FLOWLET);
      verifyAclType(true, AclType::UDF_ACK);
      break;
    case AclType::UDF_FLOWLET_WITH_UDF_NAK:
      verifyAclType(true, AclType::UDF_FLOWLET);
      verifyAclType(true, AclType::UDF_NAK);
      break;
    default:
      break;
  }
}

std::vector<cfg::AclUdfEntry> AgentArsBase::addUdfTable(
    const std::vector<std::string>& udfGroups,
    const std::vector<std::vector<int8_t>>& roceBytes,
    const std::vector<std::vector<int8_t>>& roceMask) const {
  std::vector<cfg::AclUdfEntry> udfTable;
  for (int i = 0; i < udfGroups.size(); i++) {
    cfg::AclUdfEntry aclUdfEntry;
    aclUdfEntry.udfGroup() = udfGroups[i];
    aclUdfEntry.roceBytes() = roceBytes[i];
    aclUdfEntry.roceMask() = roceMask[i];
    udfTable.push_back(aclUdfEntry);
  }
  return udfTable;
}

void AgentArsBase::addRoceAcl(
    cfg::SwitchConfig* config,
    const std::string& aclName,
    const std::string& counterName,
    bool isSai,
    const std::optional<std::string>& udfGroups,
    const std::optional<int>& roceOpcode,
    const std::optional<int>& roceBytes,
    const std::optional<int>& roceMask,
    const std::optional<std::vector<cfg::AclUdfEntry>>& udfTable,
    bool addMirror) const {
  cfg::AclEntry aclEntry;
  aclEntry.name() = aclName;
  aclEntry.actionType() = aclActionType_;
  auto acl = utility::addAcl(config, aclEntry, cfg::AclStage::INGRESS);
  std::vector<cfg::CounterType> setCounterTypes{
      cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  acl->srcPort() =
      PortDescriptor(getTestPorts()[kFrontPanelPortForTest]).phyPortID();
  if (udfTable.has_value()) {
    acl->udfTable() = udfTable.value();
  }
  if (isSai) {
    std::vector<std::string> groups;
    std::vector<std::vector<int8_t>> data;
    std::vector<std::vector<int8_t>> mask;
    if (udfGroups.has_value()) {
      groups = {udfGroups.value()};
    }
    if (roceOpcode.has_value()) {
      data = {{static_cast<signed char>(utility::kUdfRoceOpcodeAck)}};
      mask = {{static_cast<signed char>(utility::kUdfRoceOpcodeMask)}};
    }
    if (roceBytes.has_value()) {
      data = {{static_cast<signed char>(roceBytes.value())}};
    }
    if (roceMask.has_value()) {
      mask = {{static_cast<signed char>(roceMask.value())}};
    }
    if (!udfTable.has_value()) {
      acl->udfTable() = addUdfTable(groups, data, mask);
    }
  } else {
    if (udfGroups.has_value()) {
      acl->udfGroups() = {udfGroups.value()};
    }
    if (roceOpcode.has_value()) {
      acl->roceOpcode() = roceOpcode.value();
    }
    if (roceBytes.has_value()) {
      acl->roceBytes() = {static_cast<signed char>(roceBytes.value())};
    }
    if (roceMask.has_value()) {
      acl->roceMask() = {static_cast<signed char>(roceMask.value())};
    }
  }
  if (aclName == getAclName(AclType::UDF_NAK)) {
    cfg::Ttl ttl;
    ttl.value() = 255;
    ttl.mask() = 0xFF;
    acl->ttl() = ttl;
  }
  if (aclName == getAclName(AclType::UDF_FLOWLET) ||
      aclName == getAclName(AclType::ROCE_SPRAY_MISS)) {
    acl->proto() = 17;
    acl->l4DstPort() = 4791;
    auto asic =
        checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
    utility::addEtherTypeToAcl(asic, acl, cfg::EtherType::IPv6);
  }
  if (aclName == getAclName(AclType::ECMP_HASH_CANCEL)) {
    // this is a catchall entry, so nothing to match on
    cfg::Ttl ttl;
    ttl.value() = 0;
    ttl.mask() = 0x0;
    acl->ttl() = ttl;
  }
  if (aclName == getAclName(AclType::UDF_ACK)) {
    // set dscp value to 30 and send to queue 6
    utility::addAclDscpQueueAction(
        config, aclName, counterName, kDscp, kOutQueue);
  } else if (
      aclName == getAclName(AclType::ECMP_HASH_CANCEL) ||
      aclName == getAclName(AclType::ROCE_SPRAY_MISS)) {
    utility::addAclEcmpHashCancelAction(config, aclName, counterName);
  } else if (aclName == getAclName(AclType::UDF_NAK) && addMirror) {
    // mirror session only present for mirror related tests
    utility::addAclMirrorAction(config, aclName, counterName, kAclMirror);
  } else {
    utility::addAclStat(
        config, aclName, counterName, std::move(setCounterTypes));
  }
}

std::vector<std::string> AgentArsBase::getUdfGroupsForAcl(
    AclType aclType) const {
  std::vector<std::string> retUdfGroups = {};
  switch (aclType) {
    case AclType::UDF_ACK:
      retUdfGroups.push_back(utility::kUdfAclRoceOpcodeGroupName);
      break;
    case AclType::UDF_NAK: {
      retUdfGroups.push_back(utility::kUdfAclRoceOpcodeGroupName);
      retUdfGroups.push_back(utility::kUdfAclAethNakGroupName);
    } break;
    case AclType::UDF_WR_IMM_ZERO: {
      retUdfGroups.push_back(utility::kUdfAclRoceOpcodeGroupName);
      retUdfGroups.push_back(utility::kUdfAclRethWrImmZeroGroupName);
    } break;
    case AclType::UDF_ACK_WITH_NAK: {
      retUdfGroups.push_back(utility::kUdfAclRoceOpcodeGroupName);
      retUdfGroups.push_back(utility::kUdfAclAethNakGroupName);
    } break;
    case AclType::FLOWLET:
      break;
    case AclType::FLOWLET_WITH_UDF_ACK:
      retUdfGroups.push_back(utility::kUdfAclRoceOpcodeGroupName);
      break;
    case AclType::UDF_FLOWLET:
      retUdfGroups.push_back(utility::kRoceUdfFlowletGroupName);
      break;
    case AclType::UDF_FLOWLET_WITH_UDF_ACK:
      retUdfGroups.push_back(utility::kUdfAclRoceOpcodeGroupName);
      retUdfGroups.push_back(utility::kRoceUdfFlowletGroupName);
      break;
    case AclType::UDF_FLOWLET_WITH_UDF_NAK: {
      retUdfGroups.push_back(utility::kUdfAclRoceOpcodeGroupName);
      retUdfGroups.push_back(utility::kUdfAclAethNakGroupName);
      retUdfGroups.push_back(utility::kRoceUdfFlowletGroupName);
    } break;
    default:
      break;
  }
  return retUdfGroups;
}

void AgentArsBase::addAclAndStat(
    cfg::SwitchConfig* config,
    AclType aclType,
    bool isSai,
    bool addMirror) const {
  auto aclName = getAclName(aclType);
  auto counterName = getCounterName(aclType);
  const signed char bm = 0xFF;
  std::vector<signed char> dmaLengthZeros = {0x0, 0x0};
  std::vector<signed char> dmaLengthMask = {bm, bm};
  switch (aclType) {
    case AclType::UDF_ACK:
      config->udfConfig() =
          utility::addUdfAclConfig(utility::kUdfOffsetBthOpcode);
      addRoceAcl(
          config,
          aclName,
          counterName,
          isSai,
          utility::kUdfAclRoceOpcodeGroupName,
          utility::kUdfRoceOpcodeAck,
          std::nullopt,
          std::nullopt,
          std::nullopt);
      break;
    case AclType::UDF_NAK: {
      config->udfConfig() = utility::addUdfAclConfig(
          utility::kUdfOffsetBthOpcode | utility::kUdfOffsetAethSyndrome);
      auto udfTable = addUdfTable(
          {utility::kUdfAclRoceOpcodeGroupName,
           utility::kUdfAclAethNakGroupName},
          {{utility::kUdfRoceOpcodeAck}, {utility::kAethSyndromeWithNak}},
          {{utility::kUdfRoceOpcodeMask}, {utility::kAethSyndromeWithNak}});
      addRoceAcl(
          config,
          aclName,
          counterName,
          isSai,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::move(udfTable),
          addMirror);
    } break;
    case AclType::UDF_WR_IMM_ZERO: {
      config->udfConfig() = utility::addUdfAclConfig(
          utility::kUdfOffsetBthOpcode | utility::kUdfOffsetRethDmaLength);
      auto udfTable = addUdfTable(
          {utility::kUdfAclRoceOpcodeGroupName,
           utility::kUdfAclRethWrImmZeroGroupName},
          {{utility::kUdfRoceOpcodeWriteImmediate}, std::move(dmaLengthZeros)},
          {{utility::kUdfRoceOpcodeMask}, std::move(dmaLengthMask)});
      addRoceAcl(
          config,
          aclName,
          counterName,
          isSai,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::move(udfTable));
    } break;
    case AclType::UDF_ACK_WITH_NAK: {
      config->udfConfig() = utility::addUdfAclConfig(
          utility::kUdfOffsetBthOpcode | utility::kUdfOffsetAethSyndrome);
      auto udfTable = addUdfTable(
          {utility::kUdfAclRoceOpcodeGroupName,
           utility::kUdfAclAethNakGroupName},
          {{utility::kUdfRoceOpcodeAck}, {utility::kAethSyndromeWithNak}},
          {{utility::kUdfRoceOpcodeMask}, {utility::kAethSyndromeWithNak}});
      addRoceAcl(
          config,
          aclName,
          counterName,
          isSai,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::move(udfTable));
      addRoceAcl(
          config,
          getAclName(AclType::UDF_ACK),
          getCounterName(AclType::UDF_ACK),
          isSai,
          utility::kUdfAclRoceOpcodeGroupName,
          utility::kUdfRoceOpcodeAck,
          std::nullopt,
          std::nullopt,
          std::nullopt);
    } break;
    case AclType::FLOWLET: {
      utility::addFlowletAcl(*config, isSai, aclName, counterName, false);
    } break;
    case AclType::FLOWLET_WITH_UDF_ACK:
      config->udfConfig() =
          utility::addUdfAclConfig(utility::kUdfOffsetBthOpcode);
      addRoceAcl(
          config,
          getAclName(AclType::UDF_ACK),
          getCounterName(AclType::UDF_ACK),
          isSai,
          utility::kUdfAclRoceOpcodeGroupName,
          utility::kUdfRoceOpcodeAck,
          std::nullopt,
          std::nullopt,
          std::nullopt);
      utility::addFlowletAcl(*config, isSai, aclName, counterName, false);
      break;
    case AclType::UDF_FLOWLET:
      config->udfConfig() =
          utility::addUdfAclConfig(utility::kUdfOffsetBthReserved);
      addRoceAcl(
          config,
          getAclName(AclType::UDF_FLOWLET),
          getCounterName(AclType::UDF_FLOWLET),
          isSai,
          utility::kRoceUdfFlowletGroupName,
          std::nullopt,
          utility::kRoceReserved,
          utility::kRoceReserved,
          std::nullopt);
      break;
    case AclType::UDF_FLOWLET_WITH_UDF_ACK:
      config->udfConfig() = utility::addUdfAclConfig(
          utility::kUdfOffsetBthOpcode | utility::kUdfOffsetBthReserved);
      addRoceAcl(
          config,
          getAclName(AclType::UDF_ACK),
          getCounterName(AclType::UDF_ACK),
          isSai,
          utility::kUdfAclRoceOpcodeGroupName,
          utility::kUdfRoceOpcodeAck,
          std::nullopt,
          std::nullopt,
          std::nullopt);
      addRoceAcl(
          config,
          getAclName(AclType::UDF_FLOWLET),
          getCounterName(AclType::UDF_FLOWLET),
          isSai,
          utility::kRoceUdfFlowletGroupName,
          std::nullopt,
          utility::kRoceReserved,
          utility::kRoceReserved,
          std::nullopt);
      break;
    case AclType::UDF_FLOWLET_WITH_UDF_NAK: {
      config->udfConfig() = utility::addUdfAclConfig(
          utility::kUdfOffsetBthOpcode | utility::kUdfOffsetBthReserved |
          utility::kUdfOffsetAethSyndrome);
      auto udfTable = addUdfTable(
          {utility::kUdfAclRoceOpcodeGroupName,
           utility::kUdfAclAethNakGroupName},
          {{utility::kUdfRoceOpcodeAck}, {utility::kAethSyndromeWithNak}},
          {{utility::kUdfRoceOpcodeMask}, {utility::kAethSyndromeWithNak}});
      addRoceAcl(
          config,
          getAclName(AclType::UDF_NAK),
          getCounterName(AclType::UDF_NAK),
          isSai,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::move(udfTable));
      addRoceAcl(
          config,
          getAclName(AclType::UDF_FLOWLET),
          getCounterName(AclType::UDF_FLOWLET),
          isSai,
          utility::kRoceUdfFlowletGroupName,
          std::nullopt,
          utility::kRoceReserved,
          utility::kRoceReserved,
          std::nullopt);
    } break;
    case AclType::ECMP_HASH_CANCEL:
      addRoceAcl(
          config,
          getAclName(AclType::ECMP_HASH_CANCEL),
          getCounterName(AclType::ECMP_HASH_CANCEL),
          isSai,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt);
      break;
    case AclType::ROCE_SPRAY_MISS:
      addRoceAcl(
          config,
          getAclName(AclType::ROCE_SPRAY_MISS),
          getCounterName(AclType::ROCE_SPRAY_MISS),
          isSai,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt);
      break;
    default:
      break;
  }
}

void AgentArsBase::generatePrefixes() {
  std::vector<PortID> portIds = getTestPorts();
  std::vector<PortDescriptor> portDescriptorIds;
  std::transform(
      portIds.begin(),
      portIds.end(),
      std::back_inserter(portDescriptorIds),
      [](const PortID& portId) { return PortDescriptor(portId); });

  // Request all size>=2 ECMP groups the ports allow
  // (2^kMaxEcmpWidthForTest - kMaxEcmpWidthForTest - 1); generateEcmpGroupScale
  // does not build the empty set or single-port subsets.
  std::vector<std::vector<PortDescriptor>> allCombinations =
      utility::generateEcmpGroupScale(
          portDescriptorIds,
          (1 << kMaxEcmpWidthForTest) - kMaxEcmpWidthForTest - 1,
          kMaxEcmpWidthForTest);
  for (const auto& combination : allCombinations) {
    nhopSets.emplace_back(combination.begin(), combination.end());
  }
  applyNewState(
      [&portDescriptorIds, this](const std::shared_ptr<SwitchState>& in) {
        return helper_->resolveNextHops(
            in,
            boost::container::flat_set<PortDescriptor>(
                std::make_move_iterator(portDescriptorIds.begin()),
                std::make_move_iterator(portDescriptorIds.end())));
      });

  std::generate_n(std::back_inserter(prefixes), 4096, [i = 0]() mutable {
    return RoutePrefixV6{
        folly::IPAddressV6(folly::to<std::string>(2401, "::", i++)), 128};
  });
}

cfg::SwitchingMode AgentArsBase::getFwdSwitchingMode(
    const RoutePrefixV6& prefix) const {
  return getAgentEnsemble()->getFwdSwitchingMode(prefix);
}

void AgentArsBase::verifyFwdSwitchingMode(
    const RoutePrefixV6& prefix,
    cfg::SwitchingMode switchingMode) const {
  WITH_RETRIES(
      { EXPECT_EVENTUALLY_EQ(getFwdSwitchingMode(prefix), switchingMode); });
}

uint32_t AgentArsBase::getMaxArsGroups() const {
  auto asic =
      checkSameAndGetAsicForTesting(this->getAgentEnsemble()->getL3Asics());
  auto maxArsGroups = asic->getMaxArsGroups();
  CHECK(maxArsGroups.has_value());
  return maxArsGroups.value();
}

cfg::PortFlowletConfig AgentArsBase::getPortFlowletConfig(
    int scalingFactor,
    int loadWeight,
    int queueWeight) const {
  cfg::PortFlowletConfig portFlowletConfig;
  portFlowletConfig.scalingFactor() = scalingFactor;
  portFlowletConfig.loadWeight() = loadWeight;
  portFlowletConfig.queueWeight() = queueWeight;
  return portFlowletConfig;
}

void AgentArsBase::updatePortFlowletConfigs(
    cfg::SwitchConfig& cfg,
    int scalingFactor,
    int loadWeight,
    int queueWeight) const {
  cfg.portFlowletConfigs() = {
      {"default",
       getPortFlowletConfig(scalingFactor, loadWeight, queueWeight)}};
}

void AgentArsBase::updatePortFlowletConfigName(cfg::SwitchConfig& cfg) const {
  for (const auto& portId : masterLogicalInterfacePortIds()) {
    auto portCfg = utility::findCfgPort(cfg, portId);
    portCfg->flowletConfigName() = "default";
  }
}

void AgentArsBase::updateFlowletConfigs(
    cfg::SwitchConfig& cfg,
    const cfg::SwitchingMode switchingMode,
    int flowletTableSize,
    int scalingFactor,
    int loadWeight,
    int queueWeight,
    const std::optional<cfg::SwitchingMode> backupSwitchingMode) const {
  cfg.flowletSwitchingConfig()->switchingMode() = switchingMode;
  cfg.flowletSwitchingConfig()->flowletTableSize() = flowletTableSize;
  if (backupSwitchingMode.has_value()) {
    cfg.flowletSwitchingConfig()->backupSwitchingMode() =
        backupSwitchingMode.value();
  }
  updatePortFlowletConfigs(cfg, scalingFactor, loadWeight, queueWeight);
}

bool AgentArsBase::verifyPortFlowletConfig(
    const folly::CIDRNetwork& ip,
    cfg::PortFlowletConfig& portFlowletConfig,
    const PortID& port) {
  AgentEnsemble* ensemble = getAgentEnsemble();
  auto switchId = ensemble->scopeResolver().scope(port).switchId();
  auto client = ensemble->getHwAgentTestClient(switchId);

  facebook::fboss::utility::CIDRNetwork cidr;
  cidr.IPAddress() = ip.first.str();
  cidr.mask() = ip.second;

  return client->sync_verifyPortFlowletConfig(cidr, portFlowletConfig, true);
}

bool AgentArsBase::verifyEcmpForFlowletSwitching(
    const folly::CIDRNetwork& ip,
    const cfg::FlowletSwitchingConfig& flowletCfg,
    bool flowletEnable,
    const PortID& port) {
  AgentEnsemble* ensemble = getAgentEnsemble();
  auto switchId = ensemble->scopeResolver().scope(port).switchId();
  auto client = ensemble->getHwAgentTestClient(switchId);
  facebook::fboss::utility::CIDRNetwork cidr;
  cidr.IPAddress() = ip.first.str();
  cidr.mask() = ip.second;
  state::SwitchSettingsFields settings;
  settings.flowletSwitchingConfig() = flowletCfg;
  return client->sync_verifyEcmpForFlowletSwitchingHandler(
      cidr, settings, flowletEnable);
}

bool AgentArsBase::verifyEcmpForNonFlowlet(
    const folly::CIDRNetwork& ip,
    const cfg::FlowletSwitchingConfig& flowletCfg,
    const bool expectFlowsetFree,
    const PortID& port) {
  AgentEnsemble* ensemble = getAgentEnsemble();
  auto switchId = ensemble->scopeResolver().scope(port).switchId();
  auto client = ensemble->getHwAgentTestClient(switchId);
  facebook::fboss::utility::CIDRNetwork cidr;
  cidr.IPAddress() = ip.first.str();
  cidr.mask() = ip.second;
  state::SwitchSettingsFields settings;
  settings.flowletSwitchingConfig() = flowletCfg;
  return client->sync_verifyEcmpForNonFlowlet(
      cidr, settings, expectFlowsetFree);
}

void AgentArsBase::setupEcmpGroups(int numEcmp) {
  generatePrefixes();
  std::vector<RoutePrefixV6> testPrefixes = {
      prefixes.begin(), prefixes.begin() + numEcmp};
  std::vector<boost::container::flat_set<PortDescriptor>> testNhopSets = {
      nhopSets.begin(), nhopSets.begin() + numEcmp};
  auto wrapper = getSw()->getRouteUpdater();
  helper_->programRoutes(&wrapper, testNhopSets, testPrefixes);
}

} // namespace facebook::fboss
