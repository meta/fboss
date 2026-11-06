/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmAclEntry.h"

#include "fboss/agent/hw/bcm/BcmAclStat.h"
#include "fboss/agent/hw/bcm/BcmAclTable.h"
#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmClassIDUtil.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorUtils.h"
#include "fboss/agent/hw/bcm/BcmMirrorTable.h"
#include "fboss/agent/hw/bcm/BcmMultiPathNextHop.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmUdfManager.h"
#include "fboss/agent/hw/bcm/BcmWarmBootCache.h"

#include <folly/MacAddress.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

extern "C" {
#include <bcm/field.h>
}

using folly::MacAddress;

namespace {

using facebook::fboss::bcmCheckError;
namespace switch_state_tags = facebook::fboss::switch_state_tags;
using folly::IPAddressV4;
using folly::IPAddressV6;

// IPType is either IPAddressV4 or IPAddressV6
template <typename IPType>
void qualifyIpAddresses(
    const std::shared_ptr<facebook::fboss::AclEntry>& acl,
    const int unit,
    const bcm_field_entry_t& aclEntry) {
  int rv;
  if (acl->getSrcIp().first) {
    bcm_ip6_t addr;
    bcm_ip6_t mask;
    facebook::fboss::networkToBcmIp6(acl->getSrcIp(), &addr, &mask);
    rv = bcm_field_qualify_SrcIp6(unit, aclEntry, addr, mask);
    bcmCheckError(rv, "failed to add Src IP field");
  }
  if (acl->getDstIp().first) {
    bcm_ip6_t addr;
    bcm_ip6_t mask;
    facebook::fboss::networkToBcmIp6(acl->getDstIp(), &addr, &mask);
    rv = bcm_field_qualify_DstIp6(unit, aclEntry, addr, mask);
    bcmCheckError(rv, "failed to add Dst IP field");
  }
}

void applyAclMirrorAction(
    facebook::fboss::BcmAclEntry* acl,
    const std::optional<facebook::fboss::MatchAction>& action) {
  if (!action) {
    return;
  }
  if (action->getIngressMirror()) {
    acl->applyMirrorAction(
        action.value().getIngressMirror().value(),
        facebook::fboss::MirrorAction::START,
        facebook::fboss::MirrorDirection::INGRESS);
  }
  if (action->getEgressMirror()) {
    acl->applyMirrorAction(
        action.value().getEgressMirror().value(),
        facebook::fboss::MirrorAction::START,
        facebook::fboss::MirrorDirection::EGRESS);
  }
}

/* BcmMirrorTable is reconstructed and mirrors are already programmed using
warmboot cache */
std::optional<facebook::fboss::BcmMirrorHandle> getAclMirrorHandle(
    const facebook::fboss::BcmSwitch* hw,
    const std::optional<std::string>& mirrorName) {
  auto* mirror = mirrorName
      ? hw->getBcmMirrorTable()->getNodeIf(mirrorName.value())
      : nullptr;
  if (!mirror) {
    return std::nullopt;
  }
  return mirror->getHandle();
}

facebook::fboss::utility::BcmAclActionParameters getAclActionParameters(
    const facebook::fboss::BcmSwitch* hw,
    const std::shared_ptr<facebook::fboss::AclEntry>& acl) {
  facebook::fboss::utility::BcmAclActionParameters parameters;
  if (!acl->getAclAction()) {
    return parameters;
  }
  auto& inMirror =
      acl->getAclAction()->cref<switch_state_tags::ingressMirror>();
  if (inMirror) {
    parameters.mirrors.ingressMirrorHandle =
        getAclMirrorHandle(hw, inMirror->cref());
  }
  auto& egMirror = acl->getAclAction()->cref<switch_state_tags::egressMirror>();
  if (egMirror) {
    parameters.mirrors.egressMirrorHandle =
        getAclMirrorHandle(hw, egMirror->cref());
  }
  return parameters;
}
} // unnamed namespace

namespace facebook::fboss {

using namespace facebook::fboss::utility;

void BcmAclEntry::createAclQualifiers() {
  int rv;
  // One or both IP addresses can be empty
  auto isV4 = (acl_->getSrcIp().first && acl_->getSrcIp().first.isV4()) ||
      (acl_->getDstIp().first && acl_->getDstIp().first.isV4());
  auto isV6 = (acl_->getSrcIp().first && acl_->getSrcIp().first.isV6()) ||
      (acl_->getDstIp().first && acl_->getDstIp().first.isV6());
  // Bcm ContentAware processing engine use the same field for both V4 and V6.
  // For a V4 packet, the lower 32 bit is the V4 address, with all other
  // (128 - 32) bits set to 0.
  if (isV4) {
    qualifyIpAddresses<IPAddressV4>(acl_, hw_->getUnit(), handle_);
  } else if (isV6) {
    qualifyIpAddresses<IPAddressV6>(acl_, hw_->getUnit(), handle_);
  }

  if (acl_->getProto()) {
    rv = bcm_field_qualify_IpProtocol(
        hw_->getUnit(), handle_, acl_->getProto().value(), 0xFF);
    bcmCheckError(rv, "failed to add IP protocol field");
  }
  if (acl_->getL4SrcPort()) {
    rv = bcm_field_qualify_L4SrcPort(
        hw_->getUnit(), handle_, acl_->getL4SrcPort().value(), 0xFFFF);
    bcmCheckError(rv, "failed to add L4 Src Port field");
  }
  if (acl_->getL4DstPort()) {
    rv = bcm_field_qualify_L4DstPort(
        hw_->getUnit(), handle_, acl_->getL4DstPort().value(), 0xFFFF);
    bcmCheckError(rv, "failed to add L4 Dst Port field");
  }
  if (acl_->getTcpFlagsMask()) {
    throw FbossError("tcpFlagsMask is not supported on BcmSwitch");
  }
  if (acl_->getTcpFlagsBitMap()) {
    rv = bcm_field_qualify_TcpControl(
        hw_->getUnit(), handle_, acl_->getTcpFlagsBitMap().value(), 0xFF);
    bcmCheckError(rv, "failed to add TCP flags field");
  }
  if (acl_->getSrcPort()) {
    rv = bcm_field_qualify_SrcPort(
        hw_->getUnit(),
        handle_,
        0x0,
        0xFF, // checked with bcm, both port and module mask value should not
              // be bigger than 0xFF
        acl_->getSrcPort().value(),
        0xFF);
    bcmCheckError(rv, "failed to add physical Src Port field");
  }
  if (acl_->getDstPort()) {
    rv = bcm_field_qualify_DstPort(
        hw_->getUnit(),
        handle_,
        0x0,
        0xFF, // checked with bcm, both port and module mask value should not
              // be bigger than 0xFF
        acl_->getDstPort().value(),
        0xFF);
    bcmCheckError(rv, "failed to add physical Dst Port field");
  }
  bcm_mac_t macMask;
  macToBcm(MacAddress::BROADCAST, &macMask);
  if (acl_->getDstMac()) {
    bcm_mac_t dstMac;
    macToBcm(acl_->getDstMac().value(), &dstMac);
    rv = bcm_field_qualify_DstMac(hw_->getUnit(), handle_, dstMac, macMask);
    bcmCheckError(rv, "failed to add Dst Mac field");
  }

  if (acl_->getIpFrag()) {
    bcm_field_IpFrag_t ipFragOpt =
        cfgIpFragToBcmIpFrag(acl_->getIpFrag().value());
    rv = bcm_field_qualify_IpFrag(hw_->getUnit(), handle_, ipFragOpt);
    bcmCheckError(rv, "failed to qualify a ip frag option");
  }

  if (acl_->getIcmpType()) {
    uint16_t typeCode, typeCodeMask;
    cfgIcmpTypeCodeToBcmIcmpCodeMask(
        acl_->getIcmpType(), acl_->getIcmpCode(), &typeCode, &typeCodeMask);
    // BRCM is reusing L4SrcPort for icmp
    rv = bcm_field_qualify_L4SrcPort(
        hw_->getUnit(), handle_, typeCode, typeCodeMask);
    bcmCheckError(rv, "failed to qualify icmp type code");
  }

  if (acl_->getDscp()) {
    uint8 bcmData, bcmMask;
    cfgDscpToBcmDscp(acl_->getDscp().value(), &bcmData, &bcmMask);
    rv = bcm_field_qualify_DSCP(hw_->getUnit(), handle_, bcmData, bcmMask);
    bcmCheckError(rv, "failed to qualify on DSCP");
  }
  if (acl_->getIpType()) {
    bcm_field_IpType_t ipType = cfgIpTypeToBcmIpType(acl_->getIpType().value());
    rv = bcm_field_qualify_IpType(hw_->getUnit(), handle_, ipType);
    bcmCheckError(rv, "failed to qualify IP type");
  }

  if (acl_->getEtherType()) {
    uint16_t bcmData, bcmMask;
    bcmData = static_cast<uint16>(acl_->getEtherType().value());
    bcmMask = 0xFFFF;
    rv = bcm_field_qualify_EtherType(hw_->getUnit(), handle_, bcmData, bcmMask);
    bcmCheckError(rv, "failed to qualify Ether type");
  }

  if (acl_->getTtl()) {
    rv = bcm_field_qualify_Ttl(
        hw_->getUnit(),
        handle_,
        acl_->getTtl().value().getValue(),
        acl_->getTtl().value().getMask());
    bcmCheckError(rv, "failed to qualify TTL");
  }

  /*
   * lookupClassNeighbor and lookupClassRoute are both represented by
   * DstClassL3.
   */
  if (acl_->getLookupClassNeighbor()) {
    auto lookupClassNeighbor = acl_->getLookupClassNeighbor().value();

    if (BcmClassIDUtil::isValidLookupClass(lookupClassNeighbor)) {
      auto classId = static_cast<int>(lookupClassNeighbor);
      rv = bcm_field_qualify_DstClassL3(
          hw_->getUnit(), handle_, classId, 0xFFFFFFFF);
      bcmCheckError(rv, "failed to qualify DstClassL3:", classId);
    } else {
      throw FbossError(
          "Unrecognized acl lookupClassNeighbor ",
          apache::thrift::util::enumNameSafe(lookupClassNeighbor));
    }
  }

  if (acl_->getLookupClassRoute()) {
    auto lookupClassRoute = acl_->getLookupClassRoute().value();

    if (BcmClassIDUtil::isValidLookupClass(lookupClassRoute)) {
      auto classId = static_cast<int>(lookupClassRoute);
      rv = bcm_field_qualify_DstClassL3(
          hw_->getUnit(), handle_, classId, 0xFFFFFFFF);
      bcmCheckError(rv, "failed to qualify DstClassL3:", classId);
    } else {
      throw FbossError(
          "Unrecognized acl lookupClassRoute ",
          apache::thrift::util::enumNameSafe(lookupClassRoute));
    }
  }

  if (acl_->getLookupClassL2()) {
    auto lookupClassL2 = acl_->getLookupClassL2().value();

    auto classId = static_cast<int>(lookupClassL2);
    rv = bcm_field_qualify_DstClassL2(
        hw_->getUnit(), handle_, classId, 0xFFFFFFFF);
    bcmCheckError(rv, "failed to qualify DstClassL2:", classId);
  }

  if (acl_->getPacketLookupResult()) {
    auto packetLookupResultValue = acl_->getPacketLookupResult().value();
    auto packetLookupResult =
        cfgPacketLookupResultToBcmPktResult(packetLookupResultValue);
    rv = bcm_field_qualify_PacketRes(
        hw_->getUnit(), handle_, packetLookupResult, 0xFFFFFFFF);
    bcmCheckError(
        rv, "failed to qualify PacketLookupResult:", packetLookupResult);
  }

  if (acl_->getVlanID()) {
    rv = bcm_field_qualify_OuterVlanId(
        hw_->getUnit(), handle_, acl_->getVlanID().value(), 0xFFF);
    bcmCheckError(
        rv, "failed to qualify OuterVlanId:", acl_->getVlanID().value());
  }

  if (acl_->getUdfGroups() && acl_->getRoceOpcode()) {
    const auto groups = acl_->getUdfGroups().value();
    for (const auto& group : groups) {
      const int bcmUdfGroupId = hw_->getUdfMgr()->getBcmUdfGroupId(group);
      uint8 data[] = {acl_->getRoceOpcode().value()};
      uint8 mask[] = {0xff};

      rv = bcm_field_qualify_udf(
          hw_->getUnit(),
          handle_,
          bcmUdfGroupId,
          1 /*length*/,
          &data[0],
          &mask[0]);
      bcmCheckError(
          rv,
          "failed to qualify udf opcode ",
          group,
          " bcmUdfGroupId:",
          bcmUdfGroupId);
    }
  }

  if (acl_->getUdfGroups() && acl_->getRoceBytes() && acl_->getRoceMask()) {
    const auto groups = acl_->getUdfGroups().value();
    for (const auto& group : groups) {
      const int bcmUdfGroupId = hw_->getUdfMgr()->getBcmUdfGroupId(group);
      const auto roceBytes = acl_->getRoceBytes().value();
      const auto roceMask = acl_->getRoceMask().value();
      size_t size = roceBytes.size();
      uint8 data[8], mask[8];
      std::copy(roceBytes.begin(), roceBytes.end(), data);
      std::copy(roceMask.begin(), roceMask.end(), mask);

      rv = bcm_field_qualify_udf(
          hw_->getUnit(),
          handle_,
          bcmUdfGroupId,
          size /*length*/,
          &data[0],
          &mask[0]);
      bcmCheckError(
          rv,
          "failed to qualify udf bytes/mask ",
          group,
          " bcmUdfGroupId:",
          bcmUdfGroupId);
    }
  }

  if (acl_->getUdfTable()) {
    const auto udfTable = acl_->getUdfTable().value();
    for (const auto& udfEntry : udfTable) {
      const int bcmUdfGroupId =
          hw_->getUdfMgr()->getBcmUdfGroupId(*udfEntry.udfGroup());
      const auto roceBytes = *udfEntry.roceBytes();
      const auto roceMask = *udfEntry.roceMask();
      size_t size = roceBytes.size();
      uint8 data[8], mask[8];
      std::copy(roceBytes.begin(), roceBytes.end(), data);
      std::copy(roceMask.begin(), roceMask.end(), mask);

      rv = bcm_field_qualify_udf(
          hw_->getUnit(), handle_, bcmUdfGroupId, size /*length*/, data, mask);
      bcmCheckError(
          rv,
          "failed to qualify udf table ",
          *udfEntry.udfGroup(),
          " bcmUdfGroupId:",
          bcmUdfGroupId);
    }
  }
}

void BcmAclEntry::createAclActions() {
  int rv;
  const auto& act = acl_->getActionType();
  // add action to the entry
  switch (act) {
    case cfg::AclActionType::PERMIT:
      // Do nothing
      break;
    case cfg::AclActionType::DENY:
      rv = bcm_field_action_add(
          hw_->getUnit(), handle_, bcmFieldActionDrop, 0, 0);
      bcmCheckError(rv, "failed to add field action");
      break;
    case cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE:
      throw FbossError(
          "AclActionType DENY_DATA_AND_CONTROL_PLANE is not supported by the native BCM "
          "implementation");
    default:
      throw FbossError("Unrecognized action ", act);
  }

  auto action = acl_->getAclAction();
  if (action) {
    if (auto& sendToQueue = action->cref<switch_state_tags::sendToQueue>()) {
      auto queueMatchAction =
          sendToQueue->cref<switch_state_tags::action>()->toThrift();
      auto sendToCPU =
          sendToQueue->cref<switch_state_tags::sendToCPU>()->cref();
      bcm_field_action_t actionToTake = bcmFieldActionCosQNew;
      auto errorMsg =
          folly::to<std::string>("cos q ", *queueMatchAction.queueId());
      if (sendToCPU) {
        rv = bcm_field_action_add(
            hw_->getUnit(), handle_, bcmFieldActionCopyToCpu, 0, 0);
        bcmCheckError(rv, "failed to add send to CPU action");
        actionToTake = bcmFieldActionCosQCpuNew;
        errorMsg = "CPU Q";
      }
      rv = bcm_field_action_add(
          hw_->getUnit(),
          handle_,
          actionToTake,
          *queueMatchAction.queueId(),
          0);
      bcmCheckError(rv, "failed to add set ", errorMsg, " field action");
    }
    if (auto& setDscp = action->cref<switch_state_tags::setDscp>()) {
      const int dscpValue =
          setDscp->cref<switch_config_tags::dscpValue>()->cref();
      rv = bcm_field_action_add(
          hw_->getUnit(), handle_, bcmFieldActionDscpNew, dscpValue, 0);
      bcmCheckError(rv, "failed to add set dscp field action");
    }
    if (action->cref<switch_state_tags::trafficCounter>()) {
      createAclStat();
    }
    // THRIFT_COPY
    std::optional<MatchAction> matchAction =
        MatchAction::fromThrift(action->toThrift());
    applyAclMirrorAction(this, matchAction);
    if (auto& redirectToNextHop =
            action->cref<switch_state_tags::redirectToNextHop>()) {
      // THRIFT_COPY
      applyRedirectToNextHopAction(
          util::toRouteNextHopSet(
              redirectToNextHop->cref<switch_state_tags::resolvedNexthops>()
                  ->toThrift(),
              true),
          false /* isWarmBoot */);
    }

    if (hw_->getPlatform()->getAsic()->isSupported(HwAsic::Feature::ARS)) {
      if (matchAction->getFlowletAction()) {
        XLOG(DBG2) << "Adding flowlet action for handle :" << handle_;
        applyFlowletAction(matchAction);
      }
    }

    if (matchAction->getEcmpHashAction()) {
      XLOG(DBG2) << "Adding ecmp hash action for handle :" << handle_;
      applyEcmpHashAction(matchAction);
    }
  }
}

void BcmAclEntry::applyEcmpHashAction(
    const std::optional<facebook::fboss::MatchAction>& action) {
  int rv;
  auto ecmpHashAction = action->getEcmpHashAction().value();
  switch (*ecmpHashAction.switchingMode()) {
    case cfg::SwitchingMode::FIXED_ASSIGNMENT:
      rv = bcm_field_action_add(
          hw_->getUnit(),
          handle_,
          bcmFieldActionEcmpRandomRoundRobinHashCancel,
          0,
          0);
      bcmCheckError(rv, "failed to add ecmp hash action");
      break;
    default:
      throw FbossError(
          "Unsupported ecmp hash action", *ecmpHashAction.switchingMode());
  }
}

void BcmAclEntry::applyFlowletAction(
    const std::optional<facebook::fboss::MatchAction>& action) {
  int rv;
  auto flowletAction = action->getFlowletAction().value();
  switch (flowletAction) {
    case cfg::FlowletAction::FORWARD:
      rv = bcm_field_action_add(
          hw_->getUnit(), handle_, bcmFieldActionDynamicEcmpEnable, 0, 0);
      bcmCheckError(rv, "failed to add dynamic ecmp enable action");
      break;
    case cfg::FlowletAction::DISABLE:
      throw FbossError("Unsupported flowlet action ", flowletAction);
    default:
      throw FbossError("Unrecognized flowlet action ", flowletAction);
  }
}

void BcmAclEntry::applyRedirectToNextHopAction(
    const RouteNextHopSet& nexthops,
    bool isWarmBoot,
    bcm_vrf_t vrf) {
  bcm_if_t egressId;
  if (nexthops.size() > 0) {
    redirectNexthop_ =
        hw_->writableMultiPathNextHopTable()->referenceOrEmplaceNextHop(
            BcmMultiPathNextHopKey(vrf, nexthops, std::nullopt));
    egressId = redirectNexthop_->getEgressId();
  } else {
    egressId = hw_->getDropEgressId();
  }
  if (!isWarmBoot) {
    int rv = bcm_field_action_add(
        hw_->getUnit(), handle_, bcmFieldActionL3Switch, egressId, 0);
    bcmCheckError(rv, "Failed to add L3 redirect action");
  }
}

void BcmAclEntry::createAclStat() {
  auto action = acl_->getAclAction();
  if (!action || !action->cref<switch_state_tags::trafficCounter>()) {
    return;
  }
  auto aclTable = hw_->writableAclTable();
  const auto warmBootCache = hw_->getWarmBootCache();
  auto counterName = action->cref<switch_state_tags::trafficCounter>()
                         ->cref<switch_config_tags::name>()
                         ->cref();
  // THRIFT_COPY
  auto counterTypes = action->cref<switch_state_tags::trafficCounter>()
                          ->cref<switch_config_tags::types>()
                          ->toThrift();
  auto warmBootItr = warmBootCache->findAclStat(handle_);
  if (warmBootItr != warmBootCache->AclEntry2AclStat_end()) {
    // If we are re-using an existing stat, call programmed() to indicate
    // that it has been claimed and must not be detached when cleaning the
    // warmboot cache
    aclTable->incRefOrCreateBcmAclStat(
        counterName, counterTypes, warmBootItr->second.stat);
    warmBootCache->programmed(warmBootItr);
  } else {
    XLOG(DBG3) << "Reattaching counter " << counterName;
    auto bcmAclStat =
        aclTable->incRefOrCreateBcmAclStat(counterName, counterTypes, gid_);
    bcmAclStat->attach(handle_);
  }
}

void BcmAclEntry::createNewAclEntry() {
  auto rv = bcm_field_entry_create(hw_->getUnit(), gid_, &handle_);
  bcmCheckError(rv, "failed to create field entry");

  createAclQualifiers();
  createAclActions();

  /*
   * On BCM side, larger priority value means higher priority. On SW side,
   * smaller ACL ID means higher priority.
   */
  rv = bcm_field_entry_prio_set(
      hw_->getUnit(), handle_, swPriorityToHwPriority(acl_->getPriority()));
  bcmCheckError(rv, "failed to set priority");

  rv = bcm_field_entry_install(hw_->getUnit(), handle_);
  bcmCheckError(rv, "failed to install field group");

  int enabled = 1;
  if (acl_->isEnabled().has_value()) {
    enabled = acl_->isEnabled().value() ? 1 : 0;
  }
  rv = bcm_field_entry_enable_set(hw_->getUnit(), handle_, enabled);
  bcmCheckError(rv, "Failed to set enable/disable for acl. enable:", enabled);
}

BcmAclEntry::BcmAclEntry(
    BcmSwitch* hw,
    int gid,
    const std::shared_ptr<AclEntry>& acl)
    : hw_(hw), gid_(gid), acl_(acl) {
  const auto warmBootCache = hw_->getWarmBootCache();
  auto warmbootItr = warmBootCache->findAcl(acl_->getPriority());
  if (warmbootItr != warmBootCache->priority2BcmAclEntryHandle_end()) {
    handle_ = warmbootItr->second;
    const auto& action = acl_->getAclAction();
    if (action && action->cref<switch_state_tags::redirectToNextHop>()) {
      applyRedirectToNextHopAction(
          util::toRouteNextHopSet(
              action->cref<switch_state_tags::redirectToNextHop>()
                  ->cref<switch_state_tags::resolvedNexthops>()
                  ->toThrift(),
              true),
          true /* isWarmBoot */);
    }
    // check whether acl in S/W and H/W in sync
    CHECK(BcmAclEntry::isStateSame(hw_, gid_, handle_, acl_))
        << "Warmboot ACL doesn't match the one from H/W";
    createAclStat();
    /* if acl entry has mirror actions, then mirror actions are claimed from
     * warmboot cache,  this is because mirror action parameters are mirror
     * destination descriptors which are also claimed from warmboot cache.  this
     * is unlike other acl entry actions.
     */
    // THRIFT_COPY
    std::optional<MatchAction> matchAction{};
    if (action) {
      matchAction = MatchAction::fromThrift(action->toThrift());
    }
    applyAclMirrorAction(this, matchAction);
    warmBootCache->programmed(warmbootItr);
  } else {
    createNewAclEntry();
  }
}

void BcmAclEntry::deleteAclEntry() {
  int rv;
  auto aclTable = hw_->writableAclTable();

  auto action = acl_->getAclAction();
  // Remove any mirroring action. This must be done before destroying the ACL.
  if (action && action->cref<switch_state_tags::egressMirror>()) {
    applyMirrorAction(
        action->cref<switch_state_tags::egressMirror>()->cref(),
        MirrorAction::STOP,
        MirrorDirection::EGRESS);
  }
  if (action && action->cref<switch_state_tags::ingressMirror>()) {
    applyMirrorAction(
        action->cref<switch_state_tags::ingressMirror>()->cref(),
        MirrorAction::STOP,
        MirrorDirection::INGRESS);
  }
  // Detach and remove the stat. This must be done before destroying the ACL.
  if (action && action->cref<switch_state_tags::trafficCounter>()) {
    auto counterName = action->cref<switch_state_tags::trafficCounter>()
                           ->cref<switch_config_tags::name>()
                           ->cref();
    auto aclStat = aclTable->getAclStat(counterName);
    aclStat->detach(handle_);
    aclTable->derefBcmAclStat(counterName);
  }

  // Destroy the ACL entry
  rv = bcm_field_entry_destroy(hw_->getUnit(), handle_);
  bcmLogFatal(rv, hw_, "failed to destroy the acl entry");
}

BcmAclEntry::~BcmAclEntry() {
  deleteAclEntry();
}

bool BcmAclEntry::isStateSame(
    const BcmSwitch* hw,
    int gid,
    BcmAclEntryHandle handle,
    const std::shared_ptr<AclEntry>& acl) {
  auto aclMsg = folly::to<std::string>(
      "Group=", gid, ", acl=", acl->getID(), ", handle=", handle);
  bool isSame = true;
  // There is aliasing among qualifiers in hardware in the tcam and it will set
  // the same field depending on the packet type. Thus, bcm will use the same
  // field for icmp type of icmp package and L4 src port of UDP/TCP package.
  // So for these re-use fields, we only check them if S/W also have the value.
  // And then at the very end of this function, we will check the H/W value
  // should be empty if no one is using it.
  bool isSrcL4PortFieldUsed = false;

  // check enable status
  int enableExpected = 1;
  if (acl->isEnabled().has_value()) {
    enableExpected = acl->isEnabled().value() ? 1 : 0;
  }
  int enableFlag = 0;
  auto rv = bcm_field_entry_enable_get(hw->getUnit(), handle, &enableFlag);
  bcmCheckError(rv, aclMsg, " failed to get acl enable status");
  if (enableFlag != enableExpected) {
    XLOG(ERR) << aclMsg << " has incorrect enable setting. expected "
              << enableExpected << " actual " << enableFlag;
    isSame = false;
  }

  // check priority
  int prio = 0;
  rv = bcm_field_entry_prio_get(hw->getUnit(), handle, &prio);
  bcmCheckError(rv, aclMsg, " failed to get acl priority");
  auto expectedPrio = swPriorityToHwPriority(acl->getPriority());
  if (prio != expectedPrio) {
    XLOG(ERR) << aclMsg << " priority doesn't match. Expected=" << expectedPrio
              << ", actual=" << prio;
    isSame = false;
  }

  // check action type
  isSame &= isActionStateSame(
      hw, hw->getUnit(), handle, acl, aclMsg, getAclActionParameters(hw, acl));

  // check ip addresses
  isSame &= isBcmIp6QualFieldStateSame(
      hw->getUnit(),
      handle,
      aclMsg,
      "Src IP",
      bcm_field_qualify_SrcIp6_get,
      acl->getSrcIp());
  isSame &= isBcmIp6QualFieldStateSame(
      hw->getUnit(),
      handle,
      aclMsg,
      "Dst IP",
      bcm_field_qualify_DstIp6_get,
      acl->getDstIp());

  // check protocol
  std::optional<uint8> protoD{std::nullopt};
  if (acl->getProto()) {
    protoD = acl->getProto().value();
  }
  isSame &= isBcmQualFieldStateSame(
      bcm_field_qualify_IpProtocol_get,
      hw->getUnit(),
      handle,
      protoD,
      aclMsg,
      "IpProtocol");

  // check l4 src/dst port
  if (acl->getL4SrcPort()) {
    isSrcL4PortFieldUsed = true;
    std::optional<bcm_l4_port_t> portData{acl->getL4SrcPort().value()};
    isSame &= isBcmQualFieldStateSame(
        bcm_field_qualify_L4SrcPort_get,
        hw->getUnit(),
        handle,
        portData,
        aclMsg,
        "L4SrcPort");
  }
  std::optional<bcm_l4_port_t> portData{std::nullopt};
  if (acl->getL4DstPort()) {
    portData = acl->getL4DstPort().value();
  }
  isSame &= isBcmQualFieldStateSame(
      bcm_field_qualify_L4DstPort_get,
      hw->getUnit(),
      handle,
      portData,
      aclMsg,
      "L4DstPort");

  // check tcp flags mask
  std::optional<uint8> tcpFlagsD{std::nullopt};
  if (acl->getTcpFlagsBitMap()) {
    tcpFlagsD = acl->getTcpFlagsBitMap().value();
  }
  isSame &= isBcmQualFieldStateSame(
      bcm_field_qualify_TcpControl_get,
      hw->getUnit(),
      handle,
      tcpFlagsD,
      aclMsg,
      "TcpFlags");

  // check src/dst port
  isSame &= isBcmPortQualFieldStateSame(
      hw->getUnit(),
      handle,
      aclMsg,
      "SrcPort",
      bcm_field_qualify_SrcPort_get,
      acl->getSrcPort());
  isSame &= isBcmPortQualFieldStateSame(
      hw->getUnit(),
      handle,
      aclMsg,
      "DstPort",
      bcm_field_qualify_DstPort_get,
      acl->getDstPort());

  // check ip frag
  std::optional<bcm_field_IpFrag_t> ipFragD{std::nullopt};
  if (acl->getIpFrag()) {
    ipFragD = cfgIpFragToBcmIpFrag(acl->getIpFrag().value());
  }
  isSame &= isBcmEnumQualFieldStateSame(
      bcm_field_qualify_IpFrag_get,
      hw->getUnit(),
      handle,
      ipFragD,
      aclMsg,
      "IpFrag");

  // check icmp type. Since the latest sdk is 6.4+, no need to check sdk version
  if (acl->getIcmpType()) {
    isSrcL4PortFieldUsed = true;
    uint16_t typeCode, typeCodeMask;
    cfgIcmpTypeCodeToBcmIcmpCodeMask(
        acl->getIcmpType(), acl->getIcmpCode(), &typeCode, &typeCodeMask);
    std::optional<bcm_l4_port_t> icmpD = typeCode;
    std::optional<bcm_l4_port_t> icmpM = typeCodeMask;
    // BRCM is reusing L4SrcPort for icmp
    isSame &= isBcmQualFieldWithMaskStateSame(
        bcm_field_qualify_L4SrcPort_get,
        hw->getUnit(),
        handle,
        icmpD,
        icmpM,
        aclMsg,
        "IcmpTypeCode");
  }

  // check dscp
  std::optional<uint8> dscpD{std::nullopt};
  if (acl->getDscp()) {
    uint8 data, mask;
    cfgDscpToBcmDscp(acl->getDscp().value(), &data, &mask);
    dscpD = data;
  }
  isSame &= isBcmQualFieldStateSame(
      bcm_field_qualify_DSCP_get, hw->getUnit(), handle, dscpD, aclMsg, "Dscp");

  // check ipType
  std::optional<bcm_field_IpType_t> ipTypeD{std::nullopt};
  if (acl->getIpType()) {
    ipTypeD = cfgIpTypeToBcmIpType(acl->getIpType().value());
  }
  isSame &= isBcmEnumQualFieldStateSame(
      bcm_field_qualify_IpType_get,
      hw->getUnit(),
      handle,
      ipTypeD,
      aclMsg,
      "IpType");

  // check ttl
  std::optional<uint8_t> ttlD{std::nullopt};
  if (acl->getTtl()) {
    auto ttl = acl->getTtl().value();
    ttlD = ttl.getValue() & ttl.getMask();
  }
  isSame &= isBcmQualFieldStateSame(
      bcm_field_qualify_Ttl_get, hw->getUnit(), handle, ttlD, aclMsg, "Ttl");

  std::optional<MacAddress> dstMacMask{std::nullopt};
  if (acl->getDstMac()) {
    dstMacMask = MacAddress::BROADCAST;
  }
  // DstMac
  isSame &= isBcmMacQualFieldStateSame(
      hw->getUnit(),
      handle,
      aclMsg,
      "DstMac",
      bcm_field_qualify_DstMac_get,
      acl->getDstMac());
  // if isSrcL4PortFieldUsed we need to check H/W value shoule be empty too
  if (!isSrcL4PortFieldUsed) {
    std::optional<bcm_l4_port_t> nullDate{std::nullopt};
    isSame &= isBcmQualFieldStateSame(
        bcm_field_qualify_L4SrcPort_get,
        hw->getUnit(),
        handle,
        nullDate,
        aclMsg,
        "L4SrcPort");
  }

  /*
   * lookupClassNeighbor and lookupClassRoute are both represented by
   * DstClassL3. So, if either is set, compare it with DstClassL3.
   */
  std::optional<uint32> lookupClassNeighborOrRoute;
  if (acl->getLookupClassNeighbor()) {
    lookupClassNeighborOrRoute =
        static_cast<int>(acl->getLookupClassNeighbor().value());
  } else if (acl->getLookupClassRoute()) {
    lookupClassNeighborOrRoute =
        static_cast<int>(acl->getLookupClassRoute().value());
  }
  isSame &= isBcmQualFieldStateSame(
      bcm_field_qualify_DstClassL3_get,
      hw->getUnit(),
      handle,
      lookupClassNeighborOrRoute,
      aclMsg,
      "LookupClassNeighborOrRoute");

  if (BCM_FIELD_QSET_TEST(getAclQset(), bcmFieldQualifyDstClassL2)) {
    std::optional<uint32> lookupClassL2;
    if (acl->getLookupClassL2()) {
      lookupClassL2 = static_cast<int>(acl->getLookupClassL2().value());
    }
    isSame &= isBcmQualFieldStateSame(
        bcm_field_qualify_DstClassL2_get,
        hw->getUnit(),
        handle,
        lookupClassL2,
        aclMsg,
        "LookupClassL2");
  }

  if (BCM_FIELD_QSET_TEST(getAclQset(), bcmFieldQualifyPacketRes)) {
    std::optional<uint32> packetLookupResult;
    if (acl->getPacketLookupResult()) {
      packetLookupResult = cfgPacketLookupResultToBcmPktResult(
          acl->getPacketLookupResult().value());
    }
    isSame &= isBcmQualFieldStateSame(
        bcm_field_qualify_PacketRes_get,
        hw->getUnit(),
        handle,
        packetLookupResult,
        aclMsg,
        "PacketRes");
  }

  // check EtherType
  if (BCM_FIELD_QSET_TEST(getAclQset(), bcmFieldQualifyEtherType)) {
    std::optional<uint16> etherType{std::nullopt};
    if (acl->getEtherType()) {
      uint16 data, mask;
      cfgEtherTypeToBcmEtherType(acl->getEtherType().value(), &data, &mask);
      etherType = data;
    }
    isSame &= isBcmQualFieldStateSame(
        bcm_field_qualify_EtherType_get,
        hw->getUnit(),
        handle,
        etherType,
        aclMsg,
        "EtherType");
  }

  if (BCM_FIELD_QSET_TEST(getAclQset(), bcmFieldQualifyOuterVlanId)) {
    std::optional<bcm_vlan_t> outerVlanId{std::nullopt};
    if (acl->getVlanID()) {
      outerVlanId = acl->getVlanID().value();
    }
    isSame &= isBcmQualFieldStateSame(
        bcm_field_qualify_OuterVlanId_get,
        hw->getUnit(),
        handle,
        outerVlanId,
        aclMsg,
        "OuterVlanId");
  }

  if (acl->getUdfGroups() && acl->getRoceOpcode()) {
    const auto groups = acl->getUdfGroups().value();
    for (const auto& group : groups) {
      const int bcmUdfGroupId = hw->getUdfMgr()->getBcmUdfGroupId(group);
      uint8 swData[] = {acl->getRoceOpcode().value()};
      uint8 swMask[] = {0xff};

      isSame &= isBcmQualFieldStateSame(
          bcm_field_qualify_udf_get,
          hw->getUnit(),
          handle,
          bcmUdfGroupId,
          1 /* max_length */,
          true,
          swData,
          swMask,
          1 /* size */,
          aclMsg,
          "BcmUdfGroupId");
    }
  }

  if (acl->getUdfGroups() && acl->getRoceBytes() && acl->getRoceMask()) {
    const auto groups = acl->getUdfGroups().value();
    for (const auto& group : groups) {
      const int bcmUdfGroupId = hw->getUdfMgr()->getBcmUdfGroupId(group);
      const auto roceBytes = acl->getRoceBytes().value();
      const auto roceMask = acl->getRoceMask().value();
      size_t size = roceBytes.size();
      uint8 swData[8], swMask[8];
      std::copy(roceBytes.begin(), roceBytes.end(), swData);
      std::copy(roceMask.begin(), roceMask.end(), swMask);

      isSame &= isBcmQualFieldStateSame(
          bcm_field_qualify_udf_get,
          hw->getUnit(),
          handle,
          bcmUdfGroupId,
          size /* max_length */,
          true,
          swData,
          swMask,
          size,
          aclMsg,
          "BcmUdfGroupId");
    }
  }

  if (acl->getUdfTable()) {
    const auto udfTable = acl->getUdfTable().value();
    for (const auto& udfEntry : udfTable) {
      const int bcmUdfGroupId =
          hw->getUdfMgr()->getBcmUdfGroupId(*udfEntry.udfGroup());
      const auto roceBytes = *udfEntry.roceBytes();
      const auto roceMask = *udfEntry.roceMask();
      size_t size = roceBytes.size();
      uint8 swData[8], swMask[8];
      std::copy(roceBytes.begin(), roceBytes.end(), swData);
      std::copy(roceMask.begin(), roceMask.end(), swMask);

      isSame &= isBcmQualFieldStateSame(
          bcm_field_qualify_udf_get,
          hw->getUnit(),
          handle,
          bcmUdfGroupId,
          size /* max_length */,
          true,
          swData,
          swMask,
          size,
          aclMsg,
          "BcmUdfGroupId");
    }
  }

  // check acl stat
  auto aclStatHandle =
      BcmAclStat::getAclStatHandleFromAttachedAcl(hw, gid, handle);
  if (aclStatHandle && acl->getAclAction() &&
      acl->getAclAction()->cref<switch_state_tags::trafficCounter>()) {
    // THRIFT_COPY
    auto counter = acl->getAclAction()
                       ->cref<switch_state_tags::trafficCounter>()
                       ->toThrift();
    isSame &=
        ((BcmAclStat::isStateSame(hw, (*aclStatHandle).first, counter)) ? 1
                                                                        : 0);
  } else {
    isSame &= ((!aclStatHandle.has_value()) ? 1 : 0);
  }

  return isSame;
}

std::optional<std::string> BcmAclEntry::getIngressAclMirror() {
  if (acl_->getAclAction() &&
      acl_->getAclAction()->cref<switch_state_tags::ingressMirror>()) {
    return acl_->getAclAction()
        ->cref<switch_state_tags::ingressMirror>()
        ->cref();
  }
  return std::nullopt;
}

std::optional<std::string> BcmAclEntry::getEgressAclMirror() {
  if (acl_->getAclAction() &&
      acl_->getAclAction()->cref<switch_state_tags::egressMirror>()) {
    return acl_->getAclAction()
        ->cref<switch_state_tags::egressMirror>()
        ->cref();
  }
  return std::nullopt;
}

const std::string& BcmAclEntry::getID() const {
  return acl_->getID();
}

void BcmAclEntry::applyMirrorAction(
    const std::string& mirrorName,
    MirrorAction action,
    MirrorDirection direction) {
  auto* bcmMirror = hw_->getBcmMirrorTable()->getNodeIf(mirrorName);
  CHECK(bcmMirror != nullptr);
  bcmMirror->applyAclMirrorAction(handle_, action, direction);
}

} // namespace facebook::fboss
