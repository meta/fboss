/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiArsProfileManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace facebook::fboss {

namespace {
std::pair<RouteNextHopEntry::NextHopSet, RouteNextHopEntry::NextHopSet>
checkAndGetPriAndBackupNhops(const RouteNextHopEntry::NextHopSet& swNextHops) {
  std::vector<NextHop> primaryNhops;
  std::vector<NextHop> backupNhops;
  primaryNhops.reserve(swNextHops.size());
  backupNhops.reserve(swNextHops.size());
  for (const auto& swNextHop : swNextHops) {
    switch (swNextHop.role()) {
      case NextHopRole::PRIMARY:
        primaryNhops.push_back(swNextHop);
        break;
      case NextHopRole::BACKUP:
        backupNhops.push_back(swNextHop);
        break;
    }
  }
  if (backupNhops.size() && primaryNhops.size() > 1) {
    throw FbossError(
        "Got : ",
        primaryNhops.size(),
        " primary nhops and ",
        backupNhops.size(),
        " backup nhops. While only 1:N protection model is supported");
  }
  return std::make_pair(
      RouteNextHopEntry::NextHopSet(
          boost::container::ordered_unique_range,
          std::make_move_iterator(primaryNhops.begin()),
          std::make_move_iterator(primaryNhops.end())),
      RouteNextHopEntry::NextHopSet(
          boost::container::ordered_unique_range,
          std::make_move_iterator(backupNhops.begin()),
          std::make_move_iterator(backupNhops.end())));
}
bool isEcmpModeARS(std::optional<cfg::SwitchingMode> switchingMode) {
  return (
      switchingMode.has_value() &&
      (switchingMode.value() == cfg::SwitchingMode::PER_PACKET_QUALITY ||
       switchingMode.value() == cfg::SwitchingMode::FLOWLET_QUALITY));
}

bool isHwProtectionNextHopGroupType(
    [[maybe_unused]] sai_next_hop_group_type_t nextHopGroupType) {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  return nextHopGroupType == SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION;
#else
  return false;
#endif
}

sai_next_hop_group_type_t getHwProtectionNextHopGroupType() {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  return SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION;
#else
  return SAI_NEXT_HOP_GROUP_TYPE_ECMP;
#endif
}

std::optional<cfg::SwitchingMode> getDesiredEcmpSwitchingMode(
    sai_next_hop_group_type_t nextHopGroupType,
    std::optional<cfg::SwitchingMode> overrideEcmpSwitchingMode,
    std::optional<cfg::SwitchingMode> primaryArsMode) {
  if (isProtectionNextHopGroupType(nextHopGroupType)) {
    return std::nullopt;
  }
  if (isHwProtectionNextHopGroupType(nextHopGroupType)) {
    return cfg::SwitchingMode::PER_PACKET_RANDOM;
  }
  return overrideEcmpSwitchingMode.has_value() ? overrideEcmpSwitchingMode
                                               : primaryArsMode;
}

// Which ecmpGroupSettings key a group falls under. Group type wins over
// switching mode: an FRR backup is programmed with random spray but is
// FRR_BACKUP, not ECMP_SPRAY. std::nullopt for a group with no category, which
// is left alone entirely.
std::optional<cfg::EcmpGroupType> classifyEcmpGroup(
    sai_next_hop_group_type_t nextHopGroupType,
    bool isArsGroup,
    std::optional<cfg::SwitchingMode> switchingMode) {
  if (isProtectionNextHopGroupType(nextHopGroupType)) {
    return cfg::EcmpGroupType::FRR_PRIMARY;
  }
  if (isHwProtectionNextHopGroupType(nextHopGroupType)) {
    return cfg::EcmpGroupType::FRR_BACKUP;
  }
  if (isArsGroup) {
    return cfg::EcmpGroupType::ARS;
  }
  if (switchingMode == cfg::SwitchingMode::PER_PACKET_RANDOM) {
    return cfg::EcmpGroupType::ECMP_SPRAY;
  }
  if (switchingMode == cfg::SwitchingMode::FIXED_ASSIGNMENT) {
    return cfg::EcmpGroupType::ECMP_FIXED_ASSIGNMENT;
  }
  return std::nullopt;
}

// Split horizon keeps a group from egressing a packet on the port it arrived
// on. On the protection parent it turns on source port based failover to the
// backup group, on the backup group it turns on tertiary member selection.
std::optional<SaiNextHopGroupTraits::Attributes::SplitHorizonEnable>
splitHorizonEnableFor(
    sai_next_hop_group_type_t nextHopGroupType,
    bool isArsGroup,
    std::optional<cfg::SwitchingMode> switchingMode,
    const EcmpGroupSettingsMap& ecmpGroupSettings) {
  // Classification and lookup are plain config reads, so they stay outside the
  // SDK gate. That keeps the unsupported-build error scoped to a group whose
  // own type was actually configured, instead of firing on every next hop group
  // create as soon as the map is non-empty.
  auto groupType =
      classifyEcmpGroup(nextHopGroupType, isArsGroup, switchingMode);
  if (!groupType) {
    return std::nullopt;
  }
  auto it = ecmpGroupSettings.find(*groupType);
  const bool configured = it != ecmpGroupSettings.end();
#if defined(BRCM_SAI_SDK_GTE_13_0) && !defined(BRCM_SAI_SDK_GTE_14_0) && \
    defined(BRCM_SAI_SDK_XGS)
  const bool enable = configured && *it->second.enableSplitHorizon();
  // FRR groups are programmed explicitly even when the key is absent, because
  // omitting the attribute makes the SDK default it to TRUE on an FLF primary.
  // Every other type -- ARS, spray, fixed assignment -- carries no such
  // default, so an absent key means leave the attribute alone rather than
  // program a value. A key that is present and false is a configured off and
  // is sent as false.
  const bool alwaysProgrammed = *groupType == cfg::EcmpGroupType::FRR_PRIMARY ||
      *groupType == cfg::EcmpGroupType::FRR_BACKUP;
  if (!alwaysProgrammed && !configured) {
    return std::nullopt;
  }
  return SaiNextHopGroupTraits::Attributes::SplitHorizonEnable{enable};
#else
  if (configured) {
    throw FbossError(
        "ecmpGroupSettings enables split horizon for ECMP group type ",
        apache::thrift::util::enumNameSafe(*groupType),
        ", but SAI_NEXT_HOP_GROUP_ATTR_SPLIT_HORIZON_ENABLE requires a "
        "brcm-sai 13.x XGS build.");
  }
  return std::nullopt;
#endif
}
} // namespace

bool isProtectionNextHopGroupType(
    [[maybe_unused]] sai_next_hop_group_type_t nextHopGroupType) {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  return nextHopGroupType == SAI_NEXT_HOP_GROUP_TYPE_PROTECTION;
#else
  return false;
#endif
}

sai_next_hop_group_type_t getNextHopGroupType(
    const RouteNextHopEntry::NextHopSet& nextHops) {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  if (std::any_of(nextHops.begin(), nextHops.end(), [](const auto& nextHop) {
        return nextHop.role() == NextHopRole::BACKUP;
      })) {
    return SAI_NEXT_HOP_GROUP_TYPE_PROTECTION;
  }
#endif
  return SAI_NEXT_HOP_GROUP_TYPE_ECMP;
}

SaiNextHopGroupManager::SaiNextHopGroupManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
std::optional<SaiNextHopGroupTraits::Attributes::ArsObjectId>
SaiNextHopGroupManager::getArsObjectId(
    std::optional<cfg::SwitchingMode> switchingMode,
    size_t nextHopGroupSize) const {
  if (!isEcmpModeARS(switchingMode)) {
    return std::nullopt;
  }

  auto arsHandle = managerTable_->arsManager().getArsHandle();
  if (minWidthForArsVirtualGroup_.has_value() &&
      nextHopGroupSize >= minWidthForArsVirtualGroup_.value()) {
    arsHandle = managerTable_->arsManager().getVirtualArsGroupHandle();
  }
  if (!arsHandle->ars) {
    return std::nullopt;
  }
  return SaiNextHopGroupTraits::Attributes::ArsObjectId{
      arsHandle->ars->adapterKey()};
}
#endif

std::shared_ptr<SaiNextHopGroupHandle>
SaiNextHopGroupManager::incRefOrAddNextHopGroup(const SaiNextHopGroupKey& key) {
  auto ins = handles_.refOrEmplace(key);
  std::shared_ptr<SaiNextHopGroupHandle> nextHopGroupHandle = ins.first;
  if (!ins.second) {
    return nextHopGroupHandle;
  }
  const auto& swNextHops = key.nextHops;
  auto [primaryNhops, backupNhops] = checkAndGetPriAndBackupNhops(swNextHops);
  const auto nextHopGroupType = key.groupType;
  auto childNextHopGroup = isProtectionNextHopGroupType(nextHopGroupType)
      ? incRefOrAddNextHopGroup(SaiNextHopGroupKey(
            backupNhops, key.switchingMode, getHwProtectionNextHopGroupType()))
      : nullptr;
  const auto& memberNhops = isHwProtectionNextHopGroupType(nextHopGroupType)
      ? backupNhops
      : primaryNhops;
  SaiNextHopGroupTraits::AdapterHostKey nextHopGroupAdapterHostKey;
  // Populate the set of rifId, IP pairs for the NextHopGroup's
  // AdapterHostKey, and a set of next hop ids to create members for
  // N.B.: creating a next hop group member relies on the next hop group
  // already existing, so we cannot create them inline in the loop (since
  // creating the next hop group requires going through all the next hops
  // to figure out the AdapterHostKey)
  std::vector<ResolvedNextHop> resolvedNextHops;
  resolvedNextHops.reserve(memberNhops.size());
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  std::unordered_map<
      const ResolvedNextHop*,
      std::shared_ptr<SaiSrv6SidListHandle>>
      srv6SidListMap;
#endif
  for (const auto& swNextHop : memberNhops) {
    // Compute the sai id of the next hop's router interface
    const InterfaceID interfaceId = swNextHop.intf();
    auto routerInterfaceHandle =
        managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
            interfaceId);
    if (!routerInterfaceHandle) {
      // For multi-NPU switches, skip next hops whose interface is not on this
      // ASIC. The route will still be programmed with the remaining next hops
      // that are local to this ASIC.
      if (platform_->hasMultipleSwitches()) {
        XLOG(DBG3)
            << "Skipping next hop without sai_router_interface for InterfaceID: "
            << interfaceId
            << " on switchId: " << platform_->getAsic()->getSwitchId().value();
        continue;
      }
      // For single switch cases, this is an error
      throw FbossError("Missing SAI router interface for ", interfaceId);
    }
    resolvedNextHops.emplace_back(folly::poly_cast<ResolvedNextHop>(swNextHop));
    auto& resolvedNextHop = resolvedNextHops.back();
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    std::optional<sai_object_id_t> sidListId;
    if (!resolvedNextHop.srv6SegmentList().empty()) {
      auto [sidListKey, sidListAttrs] = makeSrv6SidListKeyAndAttributes(
          routerInterfaceHandle->adapterKey(), resolvedNextHop);
      auto sidListHandle =
          managerTable_->srv6SidListManager().addOrReuseSrv6SidList(
              sidListKey, sidListAttrs);
      sidListId = sidListHandle->managedSidList->getSidList()->adapterKey();
      srv6SidListMap.emplace(&resolvedNextHop, std::move(sidListHandle));
    }
    auto nhk = managerTable_->nextHopManager().getAdapterHostKey(
        resolvedNextHop, sidListId);
#else
    auto nhk =
        managerTable_->nextHopManager().getAdapterHostKey(resolvedNextHop);
#endif
    nextHopGroupAdapterHostKey.nhopMemberSet.insert(
        std::make_pair(nhk, swNextHop.weight()));
  }

  // For multi-NPU switches, if all next hops were filtered out (none have
  // router interfaces on this ASIC), we should not create an empty group.
  // Return the existing handle which will be empty/null.
  if (nextHopGroupAdapterHostKey.nhopMemberSet.empty() &&
      platform_->hasMultipleSwitches()) {
    XLOG(DBG3) << "All next hops filtered out on switchId: "
               << platform_->getAsic()->getSwitchId().value()
               << ", not creating empty next hop group";
    return nextHopGroupHandle;
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  std::optional<SaiNextHopGroupTraits::Attributes::ArsObjectId> arsObjectId{
      std::nullopt};
#endif
  bool isArsGroup = false;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<SaiNextHopGroupTraits::Attributes::HashAlgorithm> hashAlgorithm{
      std::nullopt};
  std::optional<SaiNextHopGroupTraits::Attributes::HierarchicalNextHop>
      hierarchicalNextHop{std::nullopt};
#endif

  if (FLAGS_flowletSwitchingEnable &&
      platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
    nextHopGroupHandle->desiredEcmpSwitchingMode_ = getDesiredEcmpSwitchingMode(
        nextHopGroupType, key.switchingMode, primaryArsMode_);

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    arsObjectId = getArsObjectId(
        nextHopGroupHandle->desiredEcmpSwitchingMode_, swNextHops.size());
    isArsGroup = arsObjectId.has_value();
#endif
    if (!isEcmpModeARS(nextHopGroupHandle->desiredEcmpSwitchingMode_)) {
      if (nextHopGroupHandle->desiredEcmpSwitchingMode_.has_value() &&
          (nextHopGroupHandle->desiredEcmpSwitchingMode_.value() ==
           cfg::SwitchingMode::PER_PACKET_RANDOM)) {
        // setting hash algo to RANDOM is specific to TH* asics
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
        if (platform_->getAsic()->isSupported(
                HwAsic::Feature::SET_NEXT_HOP_GROUP_HASH_ALGORITHM)) {
          hashAlgorithm = SaiNextHopGroupTraits::Attributes::HashAlgorithm{
              SAI_HASH_ALGORITHM_RANDOM};
        }
        if (platform_->getAsic()->isSupported(
                HwAsic::Feature::ECMP_RANDOM_SPRAY_HIERARCHICAL_LEVEL)) {
          hierarchicalNextHop =
              SaiNextHopGroupTraits::Attributes::HierarchicalNextHop{false};
        }
#endif
      }
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    if (nextHopGroupHandle->desiredEcmpSwitchingMode_.has_value()) {
      nextHopGroupAdapterHostKey.mode =
          managerTable_->arsManager().cfgSwitchingModeToSai(
              nextHopGroupHandle->desiredEcmpSwitchingMode_.value());
    }
#endif
  }

  nextHopGroupAdapterHostKey.groupType = nextHopGroupType;
  if (childNextHopGroup) {
    CHECK(childNextHopGroup->nextHopGroup);
    nextHopGroupAdapterHostKey.childNextHopGroups.insert(
        childNextHopGroup->nextHopGroup->adapterHostKey());
  }

  const auto splitHorizonEnable = splitHorizonEnableFor(
      nextHopGroupType,
      isArsGroup,
      nextHopGroupHandle->desiredEcmpSwitchingMode_,
      ecmpGroupSettings_);

  // Create the NextHopGroup and NextHopGroupMembers
  auto& store = saiStore_->get<SaiNextHopGroupTraits>();
  SaiNextHopGroupTraits::CreateAttributes nextHopGroupAttributes{
      nextHopGroupType
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      ,
      arsObjectId
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      hashAlgorithm,
      hierarchicalNextHop
#endif
      ,
      splitHorizonEnable};
  nextHopGroupHandle->nextHopGroup =
      store.setObject(nextHopGroupAdapterHostKey, nextHopGroupAttributes);
  NextHopGroupSaiId nextHopGroupId =
      nextHopGroupHandle->nextHopGroup->adapterKey();
  nextHopGroupHandle->fixedWidthMode =
      nextHopGroupType == SAI_NEXT_HOP_GROUP_TYPE_ECMP &&
      isFixedWidthNextHopGroup(swNextHops);
  nextHopGroupHandle->saiStore_ = saiStore_;
  nextHopGroupHandle->maxVariableWidthEcmpSize =
      platform_->getAsic()->getMaxVariableWidthEcmpSize();
  nextHopGroupHandle->platform_ = platform_;

  XLOG(DBG2) << "Created NexthopGroup OID: " << nextHopGroupId;

  if (childNextHopGroup) {
    nextHopGroupHandle->childGroupMember_ =
        std::make_shared<SaiNextHopGroupChildGroupMember>(
            this, std::move(childNextHopGroup), nextHopGroupId);
  }

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0) || \
    defined(BRCM_SAI_SDK_XGS_GTE_13_0) || defined(CHENAB_SAI_SDK)
  bool canBulkCreateMembers = !isProtectionNextHopGroupType(nextHopGroupType);
  if (FLAGS_enable_bulk_create_ecmp_members && canBulkCreateMembers &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::BULK_CREATE_ECMP_MEMBER)) {
    // TODO(zecheng): Use bulk create for warmboot handle reclaiming as well.
    // There is a sequencing issue where the delayed bulk create will cause
    // object removal during warmboot handle reclaiming. To workaround this,
    // disable bulk create during this process (as it is no-op in SDK).
    if (platform_->getHwSwitch()->getRunState() >= SwitchRunState::CONFIGURED &&
        !dynamic_cast<SaiSwitch*>(platform_->getHwSwitch())
             ->getRollbackInProgress_()) {
      nextHopGroupHandle->bulkCreate = true;
    }
  }

#endif

  for (auto& resolvedNextHop : resolvedNextHops) {
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    std::shared_ptr<SaiSrv6SidListHandle> sidListHandle;
    auto it = srv6SidListMap.find(&resolvedNextHop);
    if (it != srv6SidListMap.end()) {
      sidListHandle = std::move(it->second);
    }
    auto managedNextHop = managerTable_->nextHopManager().addManagedSaiNextHop(
        resolvedNextHop, std::move(sidListHandle));
#else
    auto managedNextHop =
        managerTable_->nextHopManager().addManagedSaiNextHop(resolvedNextHop);
#endif
    auto memberKey = std::make_pair(nextHopGroupId, resolvedNextHop);
    NextHopGroupMember::NextHopWeight weight;
    if (nextHopGroupType == SAI_NEXT_HOP_GROUP_TYPE_ECMP) {
      weight = SaiNextHopGroupMemberTraits::Attributes::Weight{
          static_cast<sai_uint32_t>(
              resolvedNextHop.weight() == ECMP_WEIGHT
                  ? 1
                  : resolvedNextHop.weight())};
    }
    auto result = nextHopGroupMembers_.refOrEmplace(
        memberKey,
        this,
        nextHopGroupHandle.get(),
        nextHopGroupId,
        nextHopGroupType,
        managedNextHop,
        weight,
        nextHopGroupHandle->fixedWidthMode);
    nextHopGroupHandle->members_.push_back(result.first);
  }

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0) || \
    defined(BRCM_SAI_SDK_XGS_GTE_13_0) || defined(CHENAB_SAI_SDK)
  if (FLAGS_enable_bulk_create_ecmp_members &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::BULK_CREATE_ECMP_MEMBER)) {
    nextHopGroupHandle->bulkCreate = false;

    std::vector<SaiNextHopGroupMemberTraits::AdapterHostKey> adapterHostKeys;
    std::vector<SaiNextHopGroupMemberTraits::CreateAttributes> createAttributes;

    // If next hop is not yet resolved, it will not have adapterHostKey and
    // create attributes.
    for (const auto& member : nextHopGroupHandle->members_) {
      const auto& [adapterHostKey, createAttribute] =
          member->getAdapterHostKeyAndCreateAttributes();
      CHECK_EQ(adapterHostKey.has_value(), createAttribute.has_value());
      if (adapterHostKey.has_value() && createAttribute.has_value()) {
        adapterHostKeys.push_back(adapterHostKey.value());
        createAttributes.push_back(createAttribute.value());
      }
    }
    if (!adapterHostKeys.empty()) {
      auto& store = saiStore_->get<SaiNextHopGroupMemberTraits>();
      auto objects = store.bulkCreateObjects(adapterHostKeys, createAttributes);
      CHECK_EQ(objects.size(), adapterHostKeys.size());

      auto iter = nextHopGroupHandle->members_.begin();
      for (int i = 0; i < adapterHostKeys.size(); i++) {
        while ((*iter)->getAdapterHostKeyAndCreateAttributes().first !=
               adapterHostKeys[i]) {
          iter++;
        }
        (*iter)->setObject(objects[i]);
        iter++;
      }
    }
  }
#endif

  return nextHopGroupHandle;
}

const SaiNextHopGroupHandle* SaiNextHopGroupManager::getNextHopGroup(
    const SaiNextHopGroupKey& key) const {
  return handles_.get(key);
}

bool SaiNextHopGroupManager::isFixedWidthNextHopGroup(
    const RouteNextHopEntry::NextHopSet& swNextHops) const {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::WIDE_ECMP)) {
    return false;
  }
  auto totalWeight = 0;
  for (const auto& swNextHop : swNextHops) {
    auto weight = swNextHop.weight() == ECMP_WEIGHT ? 1 : swNextHop.weight();
    totalWeight += weight;
  }
  if (totalWeight > platform_->getAsic()->getMaxVariableWidthEcmpSize()) {
    return true;
  }
  return false;
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
std::optional<SaiNextHopGroupMemberTraits::Attributes::MonitoredObject>
SaiNextHopGroupManager::getMonitoredObjectIf(
    const SaiNeighborTraits::NeighborEntry& neighborEntry) const {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::NEXT_HOP_GROUP_MEMBER_MONITORED_OBJECT)) {
    // Broadcom infers the monitored object from the member's next hop.
    return std::nullopt;
  }
  auto portSaiId =
      managerTable_->neighborManager().getNeighborPortSaiId(neighborEntry);
  if (!portSaiId) {
    // On an ASIC that cannot infer the monitored object, an unset attribute is
    // a member the ASIC will never fail over -- silently no FRR. Fail the
    // member create instead: a rejected update is recoverable, a protection
    // group that looks programmed but cannot switch over is not.
    throw FbossError(
        "No egress port for protection primary ",
        neighborEntry.ip().str(),
        "; cannot derive MONITORED_OBJECT");
  }
  return SaiNextHopGroupMemberTraits::Attributes::MonitoredObject{*portSaiId};
}
#endif

std::shared_ptr<SaiNextHopGroupMember> SaiNextHopGroupManager::createSaiObject(
    const typename SaiNextHopGroupMemberTraits::AdapterHostKey& key,
    const typename SaiNextHopGroupMemberTraits::CreateAttributes& attributes) {
  auto& store = saiStore_->get<SaiNextHopGroupMemberTraits>();
  return store.setObject(key, attributes);
}

std::shared_ptr<SaiNextHopGroupMember> SaiNextHopGroupManager::getSaiObject(
    const typename SaiNextHopGroupMemberTraits::AdapterHostKey& key) {
  auto& store = saiStore_->get<SaiNextHopGroupMemberTraits>();
  return store.get(key);
}

std::shared_ptr<SaiNextHopGroupMember>
SaiNextHopGroupManager::getSaiObjectFromWBCache(
    const typename SaiNextHopGroupMemberTraits::AdapterHostKey& key) {
  auto& store = saiStore_->get<SaiNextHopGroupMemberTraits>();
  return store.getWarmbootHandle(key);
}

// Convert all nexthops to ARS mode
// Conversion depends on availability in flowset table which is of size 32k
// If a nexthop is already in ARS mode, set operation is NOP
void SaiNextHopGroupManager::updateArsModeAll(
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletConfig) {
  if (!FLAGS_flowletSwitchingEnable ||
      !platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
    return;
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  for (auto entry : handles_) {
    auto handle = entry.second;
    auto handlePtr = handle.lock();
    if (!handlePtr) {
      continue;
    }

    // do not convert backup modes to dynamic
    if (!isEcmpModeARS(handlePtr->desiredEcmpSwitchingMode_)) {
      continue;
    }

    auto arsHandlePtr = managerTable_->arsManager().getArsHandle();
    if (minWidthForArsVirtualGroup_.has_value() &&
        handlePtr->members_.size() >= minWidthForArsVirtualGroup_.value()) {
      arsHandlePtr = managerTable_->arsManager().getVirtualArsGroupHandle();
    }
    CHECK(arsHandlePtr->ars);
    auto arsSaiId = arsHandlePtr->ars->adapterKey();
    if (newFlowletConfig) {
      handlePtr->nextHopGroup->setOptionalAttribute(
          SaiNextHopGroupTraits::Attributes::ArsObjectId{arsSaiId});
    } else {
      // flowlet config removal scenario
      handlePtr->nextHopGroup->setOptionalAttribute(
          SaiNextHopGroupTraits::Attributes::ArsObjectId{SAI_NULL_OBJECT_ID});
    }
  }
#endif
}

void SaiNextHopGroupManager::setPrimaryArsSwitchingMode(
    std::optional<cfg::SwitchingMode> switchingMode) {
  primaryArsMode_ = switchingMode;
}

void SaiNextHopGroupManager::setMinWidthForArsVirtualGroup(
    std::optional<int32_t> minWidthForArsVirtualGroup) {
  minWidthForArsVirtualGroup_ = minWidthForArsVirtualGroup;
}

void SaiNextHopGroupManager::setEcmpGroupSettings(
    const EcmpGroupSettingsMap& ecmpGroupSettings) {
  ecmpGroupSettings_ = ecmpGroupSettings;
}

bool SaiNextHopGroupManager::isSplitHorizonEnabled(
    cfg::EcmpGroupType groupType) const {
  auto it = ecmpGroupSettings_.find(groupType);
  return it != ecmpGroupSettings_.end() && *it->second.enableSplitHorizon();
}

std::string SaiNextHopGroupManager::listManagedObjects() const {
  std::set<std::string> outputs{};
  for (auto entry : handles_) {
    auto handle = entry.second;
    auto handlePtr = handle.lock();
    if (!handlePtr) {
      continue;
    }
    std::string output{};
    for (auto member : handlePtr->members_) {
      output += member->toString();
    }
    outputs.insert(output);
  }

  std::string finalOutput{};
  for (auto output : outputs) {
    finalOutput += output;
    finalOutput += "\n";
  }
  return finalOutput;
}

cfg::SwitchingMode SaiNextHopGroupManager::getNextHopGroupSwitchingMode(
    const RouteNextHopEntry::NextHopSet& swNextHops) {
  auto nextHopGroupHandle =
      handles_.get(SaiNextHopGroupKey(swNextHops, std::nullopt));
  if (!nextHopGroupHandle) {
    // if not in dynamic mode, search for backup modes
    std::vector<cfg::SwitchingMode> modes = {
        cfg::SwitchingMode::FIXED_ASSIGNMENT,
        cfg::SwitchingMode::PER_PACKET_RANDOM};
    for (const auto& mode : modes) {
      nextHopGroupHandle = handles_.get(SaiNextHopGroupKey(swNextHops, mode));
      if (nextHopGroupHandle) {
        break;
      }
    }
  }

  if (nextHopGroupHandle &&
      nextHopGroupHandle->desiredEcmpSwitchingMode_.has_value()) {
    return nextHopGroupHandle->desiredEcmpSwitchingMode_.value();
  }
  return cfg::SwitchingMode::FIXED_ASSIGNMENT;
}

// Reads the ARS specific counters on every next hop group with an ARS object
// attached and adds them to the switch wide totals.
//
// The hardware counters are free running and per group, so each sweep adds a
// group's delta against its previous reading into an accumulator, and the
// accumulator is published. Summing the groups live instead would let the
// total move backwards when a group is deleted. Because the previous reading
// is kept on the handle:
//  1. A deleted group leaves what it already contributed in the total, and
//     its reading goes with it. What it counted since the last sweep is lost,
//     at most one interval.
//  2. A new group, or the first sweep after a warm boot, has nothing to
//     compare against, so its whole reading is taken.
void SaiNextHopGroupManager::updateStats() {
  uint64_t failPackets = 0;
  uint64_t portReassignments = 0;
  for (const auto& entry : handles_) {
    auto handle = entry.second.lock();
    if (!handle) {
      continue;
    }
    auto sinceLastSweep = handle->updateStats();
    failPackets += sinceLastSweep.failPackets;
    portReassignments += sinceLastSweep.portReassignments;
  }
  arsStats_.l3EcmpDlbFailPackets() =
      *arsStats_.l3EcmpDlbFailPackets() + failPackets;
  arsStats_.l3EcmpDlbPortReassignmentCount() =
      *arsStats_.l3EcmpDlbPortReassignmentCount() + portReassignments;
}

HwFlowletStats SaiNextHopGroupManager::getHwFlowletStats() const {
  return arsStats_;
}

std::vector<EcmpDetails> SaiNextHopGroupManager::getAllEcmpDetails() const {
  std::vector<EcmpDetails> ecmpDetails;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  // Collect ARS interval and table size from the ARS handle once
  int16_t flowletInterval = 0;
  int32_t flowletTableSize = 0;
  const auto* arsHandle = managerTable_->arsManager().getArsHandle();
  if (arsHandle && arsHandle->ars) {
    auto arsSaiId = arsHandle->ars->adapterKey();
    try {
      flowletInterval = static_cast<int16_t>(
          SaiApiTable::getInstance()->arsApi().getAttribute(
              arsSaiId, SaiArsTraits::Attributes::IdleTime{}));
      flowletTableSize = static_cast<int32_t>(
          SaiApiTable::getInstance()->arsApi().getAttribute(
              arsSaiId, SaiArsTraits::Attributes::MaxFlows{}));
    } catch (const std::exception& e) {
      XLOG(ERR) << "Failed to get ARS attributes: " << e.what();
    }
  }
#endif

  for (const auto& entry : handles_) {
    auto handle = entry.second.lock();
    if (!handle || !handle->nextHopGroup) {
      continue;
    }
    EcmpDetails ecmp;
    ecmp.ecmpId() = static_cast<int32_t>(handle->nextHopGroup->adapterKey());
    const bool flowletEnabled =
        isEcmpModeARS(handle->desiredEcmpSwitchingMode_);
    ecmp.flowletEnabled() = flowletEnabled;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    if (flowletEnabled) {
      ecmp.flowletInterval() = flowletInterval;
      ecmp.flowletTableSize() = flowletTableSize;
    }
#endif
    ecmpDetails.emplace_back(std::move(ecmp));
  }
  return ecmpDetails;
}

NextHopGroupMember::NextHopGroupMember(
    SaiNextHopGroupManager* manager,
    SaiNextHopGroupHandle* nhgroup,
    SaiNextHopGroupTraits::AdapterKey nexthopGroupId,
    sai_next_hop_group_type_t nextHopGroupType,
    ManagedSaiNextHop managedSaiNextHop,
    NextHopWeight nextHopWeight,
    bool fixedWidthMode) {
  std::visit(
      [=, this](auto managedNextHop) {
        using ObjectTraits = typename std::decay_t<
            decltype(managedNextHop)>::element_type::ObjectTraits;
        auto key = managedNextHop->adapterHostKey();
        std::ignore = key;
        using ManagedMemberType =
            ManagedSaiNextHopGroupNextHopMember<ObjectTraits>;
        auto managedMember = std::make_shared<ManagedMemberType>(
            manager,
            nhgroup,
            managedNextHop,
            nexthopGroupId,
            nextHopGroupType,
            nextHopWeight,
            fixedWidthMode);
        SaiObjectEventPublisher::getInstance()->get<ObjectTraits>().subscribe(
            managedMember);
        managedNextHopGroupMember_ = managedMember;
      },
      managedSaiNextHop);
}

SaiNextHopGroupChildGroupMember::SaiNextHopGroupChildGroupMember(
    SaiNextHopGroupManager* manager,
    std::shared_ptr<SaiNextHopGroupHandle> childNextHopGroup,
    const SaiNextHopGroupTraits::AdapterKey& parentNextHopGroupId)
    : childNextHopGroup_(std::move(childNextHopGroup)),
      parentNextHopGroupId_(parentNextHopGroupId) {
  CHECK(childNextHopGroup_);
  CHECK(childNextHopGroup_->nextHopGroup);
  auto childNextHopGroupId = childNextHopGroup_->adapterKey();
  adapterHostKey_.emplace(parentNextHopGroupId_, childNextHopGroupId);
  createAttributes_.emplace(
      parentNextHopGroupId_,
      childNextHopGroupId,
      std::nullopt
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole{
          SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_STANDBY},
      std::nullopt
#endif
  );
  nextHopGroupMember_ =
      manager->createSaiObject(*adapterHostKey_, *createAttributes_);
}

std::pair<
    std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey>,
    std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>>
SaiNextHopGroupChildGroupMember::getAdapterHostKeyAndCreateAttributes() {
  return std::make_pair(adapterHostKey_, createAttributes_);
}

template <typename NextHopTraits>
std::pair<
    std::optional<SaiNextHopGroupMemberTraits::AdapterHostKey>,
    std::optional<SaiNextHopGroupMemberTraits::CreateAttributes>>
ManagedSaiNextHopGroupNextHopMember<
    NextHopTraits>::getAdapterHostKeyAndCreateAttributes() {
  return std::make_pair(adapterHostKey_, createAttributes_);
}

template <typename NextHopTraits>
void ManagedSaiNextHopGroupNextHopMember<NextHopTraits>::createObject(
    typename ManagedSaiNextHopGroupNextHopMember<
        NextHopTraits>::PublisherObjects added) {
  CHECK(this->allPublishedObjectsAlive()) << "next hops are not ready";

  auto nexthopId = std::get<NextHopWeakPtr>(added).lock()->adapterKey();

  SaiNextHopGroupMemberTraits::AdapterHostKey adapterHostKey{
      nexthopGroupId_, nexthopId};
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole>
      configuredRole;
  std::optional<SaiNextHopGroupMemberTraits::Attributes::MonitoredObject>
      monitoredObject;
  if (isProtectionNextHopGroupType(nextHopGroupType_)) {
    configuredRole = SaiNextHopGroupMemberTraits::Attributes::ConfiguredRole{
        SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_PRIMARY};
    // A PRIMARY member monitors its own egress port/LAG: that going down is
    // what drives the ASIC's autonomous switchover to the standby group. Only
    // ASICs that cannot infer it from the member's next hop need it spelled
    // out (see NEXT_HOP_GROUP_MEMBER_MONITORED_OBJECT).
    monitoredObject =
        manager_->getMonitoredObjectIf(managedNextHop_->getNeighborEntry());
  }
#endif
  // In fixed width case, the member is added with weight 0
  // and proper weight is set through bulk set api. check comments
  // associated with SaiNextHopGroupHandle::bulkProgramMembers for details
  SaiNextHopGroupMemberTraits::CreateAttributes createAttributes{
      nexthopGroupId_,
      nexthopId,
      fixedWidthMode_
          ? NextHopWeight{SaiNextHopGroupMemberTraits::Attributes::Weight{0}}
          : weight_
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      configuredRole,
      monitoredObject
#endif
  };

  bool bulkUpdate{true};
  if (fixedWidthMode_) {
    // In fixed width case, do not recreate member with 0 weight
    // if it already exists.
    auto existingObj = manager_->getSaiObject(adapterHostKey);
    if (existingObj) {
      createAttributes = existingObj->attributes();
      // For warmboot, avoid bulk set as all members may not
      // be active yet.
      if (manager_->getSaiObjectFromWBCache(adapterHostKey)) {
        bulkUpdate = false;
      }
    }
  }

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0) || defined(BRCM_SAI_SDK_XGS_GTE_13_0)
  if (FLAGS_enable_bulk_create_ecmp_members && nhgroup_ &&
      nhgroup_->bulkCreate) {
    adapterHostKey_ = adapterHostKey;
    createAttributes_ = createAttributes;
  } else {
    auto object = manager_->createSaiObject(adapterHostKey, createAttributes);
    this->setObject(object);
  }
#else
  auto object = manager_->createSaiObject(adapterHostKey, createAttributes);
  this->setObject(object);
#endif

  if (fixedWidthMode_) {
    // notify nhgroup to bulk program correct weight
    nhgroup_->memberAdded({adapterHostKey, weight_.value()}, bulkUpdate);
  }
  XLOG(DBG2) << "ManagedSaiNextHopGroupNextHopMember::createObject: "
             << toString() << " weight "
             << (weight_.has_value() ? std::to_string(weight_->value())
                                     : "none");
}

template <typename NextHopTraits>
void ManagedSaiNextHopGroupNextHopMember<NextHopTraits>::removeObject(
    size_t /* index */,
    typename ManagedSaiNextHopGroupNextHopMember<
        NextHopTraits>::PublisherObjects
    /* removed */) {
  XLOG(DBG2) << "ManagedSaiNextHopGroupNextHopMember::removeObject: "
             << toString();
  if (fixedWidthMode_) {
    // notify nhgroup to bulk program with 0 weight. In fixed width mode
    // member cannot be removed directly. check comments associated with
    // SaiNextHopGroupHandle::bulkProgramMembers for details
    nhgroup_->memberRemoved(
        {this->getObject()->adapterHostKey(), weight_.value()});
  }
  this->createAttributes_ = std::nullopt;
  this->adapterHostKey_ = std::nullopt;
  /* remove nexthop group member if next hop is removed */
  this->resetObject();
}

size_t SaiNextHopGroupHandle::nextHopGroupSize() const {
  return std::count_if(
      std::begin(members_), std::end(members_), [](auto member) {
        return member->isProgrammed();
      });
}

ArsCounterDelta SaiNextHopGroupHandle::updateStats() {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  // Computed once rather than on every poll.
  static const bool haveFailPktCount =
      SaiNextHopGroupTraits::Attributes::ArsFailPktCount::
          optionalExtensionAttributeId()
              .has_value();
  static const bool havePortReassignCount =
      SaiNextHopGroupTraits::Attributes::ArsPortReassignCount::
          optionalExtensionAttributeId()
              .has_value();
  if (!nextHopGroup || (!haveFailPktCount && !havePortReassignCount)) {
    return ArsCounterDelta{};
  }
  // The counters are ARS attributes, so only read them when an ARS object is
  // attached. Leaving arsHwCounter_ alone while detached is what lets a
  // reattach carry on rather than count the whole history a second time.
  auto arsObjectId =
      std::get<std::optional<SaiNextHopGroupTraits::Attributes::ArsObjectId>>(
          nextHopGroup->attributes());
  if (!arsObjectId.has_value() || arsObjectId->value() == SAI_NULL_OBJECT_ID) {
    return ArsCounterDelta{};
  }
  auto delta = [](uint64_t now, uint64_t before) {
    return now >= before ? now - before : now;
  };
  auto& nextHopGroupApi = SaiApiTable::getInstance()->nextHopGroupApi();
  auto adapterKey = nextHopGroup->adapterKey();
  ArsHwCounter current;
  if (haveFailPktCount) {
    current.failPackets = nextHopGroupApi.getAttribute(
        adapterKey, SaiNextHopGroupTraits::Attributes::ArsFailPktCount{});
  }
  if (havePortReassignCount) {
    current.portReassignments = nextHopGroupApi.getAttribute(
        adapterKey, SaiNextHopGroupTraits::Attributes::ArsPortReassignCount{});
  }
  ArsCounterDelta sinceLastSweep{
      delta(current.failPackets, arsHwCounter_.failPackets),
      delta(current.portReassignments, arsHwCounter_.portReassignments)};
  arsHwCounter_ = current;
  return sinceLastSweep;
#else
  return ArsCounterDelta{};
#endif
}

void SaiNextHopGroupHandle::memberAdded(
    SaiNextHopGroupMemberInfo memberInfo,
    bool updateHardware) {
  bulkProgramMembers(memberInfo, true /* added */, updateHardware);
}

void SaiNextHopGroupHandle::memberRemoved(
    SaiNextHopGroupMemberInfo memberInfo,
    bool updateHardware) {
  bulkProgramMembers(memberInfo, false /* added */, updateHardware);
}

/*
 * Perform a bulk update of ecmp member weights.
 * Some ASICs support a fixed width ecmp mode in which the total weight
 * of ECMP members can be significantly higher than regular mode.
 * However the total member weight needs to fixed (eg 512)
 * for the life of the object. Hence members cannot be added or
 * removed individually from the nexthop group as it will result
 * in having less or more total weight than the original count.
 * In fixed width mode, member addition and deletion are performed
 * through the following sequence.
 * Member add:
 *     - Add new member to nexthop group with weight 0
 *     - Bulk set the weight of all members in group such that the
 *       total weight remains the same.
 * Member removal:
 *     - Bulk set the weight of member to be removed to 0 and the weights
 *       of remaining members such that total weight remains the same
 *     - Remove the member with weight 0 from nexthop group
 */
void SaiNextHopGroupHandle::bulkProgramMembers(
    SaiNextHopGroupMemberInfo modifiedMemberInfo,
    bool added,
    bool updateHardware) {
  if (added) {
    fixedWidthNextHopGroupMembers_.insert(modifiedMemberInfo);
  } else {
    if (fixedWidthNextHopGroupMembers_.find(modifiedMemberInfo) ==
        fixedWidthNextHopGroupMembers_.end()) {
      XLOG(DBG2) << "Cannot find member to delete for fixed width ecmp";
      return;
    }
    fixedWidthNextHopGroupMembers_.erase(modifiedMemberInfo);
  }
  if (!updateHardware) {
    return;
  }
  std::vector<uint64_t> memberWeights;
  uint64_t totalWeight{0};
  for (const auto& member : fixedWidthNextHopGroupMembers_) {
    const auto& weight = member.second.value();
    totalWeight += weight;
    memberWeights.emplace_back(weight);
  }
  // normalize the weights to ecmp width if needed
  if (totalWeight > maxVariableWidthEcmpSize) {
    RouteNextHopEntry::normalizeNextHopWeightsToMaxPaths(
        memberWeights, FLAGS_ecmp_width);
  }
  std::vector<SaiNextHopGroupMemberTraits::AdapterHostKey> adapterHostKeys;
  std::vector<SaiNextHopGroupMemberTraits::Attributes::Weight> weights;
  int idx = 0;
  for (const auto& member : fixedWidthNextHopGroupMembers_) {
    auto [adapterHostKey, weight] = member;
    adapterHostKeys.emplace_back(adapterHostKey);
    weights.emplace_back(memberWeights.at(idx++));
  }
  // For removed entry, set the weight to 0 before deleting
  // the member from hardware
  if (!added) {
    adapterHostKeys.emplace_back(modifiedMemberInfo.first);
    weights.emplace_back(0);
  }
  if (adapterHostKeys.size()) {
    auto& store = saiStore_->get<SaiNextHopGroupMemberTraits>();
    store.setObjects(adapterHostKeys, weights);
  }
}

SaiNextHopGroupHandle::~SaiNextHopGroupHandle() {
  if (fixedWidthMode) {
    std::vector<SaiNextHopGroupMemberTraits::AdapterHostKey> adapterHostKeys;
    for (const auto& member : fixedWidthNextHopGroupMembers_) {
      adapterHostKeys.emplace_back(member.first);
    }
    if (adapterHostKeys.size()) {
      // Bulk set member weights to 0 so that members can be removed from
      // group member destructor without violating fixed width restriction
      std::vector<SaiNextHopGroupMemberTraits::Attributes::Weight> weights(
          fixedWidthNextHopGroupMembers_.size(), 0);
      auto& store = saiStore_->get<SaiNextHopGroupMemberTraits>();
      store.setObjects(adapterHostKeys, weights);
    }
  }

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0) || \
    defined(BRCM_SAI_SDK_XGS_GTE_13_0) || defined(CHENAB_SAI_SDK)
  if (FLAGS_enable_bulk_create_ecmp_members && platform_ &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::BULK_CREATE_ECMP_MEMBER)) {
    std::vector<SaiNextHopGroupMemberTraits::AdapterKey> adapterKeys;
    for (const auto& member : members_) {
      auto obj = member->getObject();
      if (obj) {
        obj->setSkipRemove(true);
        adapterKeys.emplace_back(obj->adapterKey());
      }
    }

    if (adapterKeys.size()) {
      SaiApiTable::getInstance()->getApi<NextHopGroupApi>().bulkRemove(
          adapterKeys);
    }
  }
#endif

  // Clean up ECMP members in reverse order. Due to the feature of SHEL and ECMP
  // member placement in the hardware, removing ECMP members from head become
  // expensive.
  // In order to maintain a continuous block of ECMP members, SDK will need to
  // move the last member to the removed spot. Rather, remove the memebers in
  // FILO order to optimize the remove performance.
  while (!members_.empty()) {
    members_.pop_back();
  }
}

template <typename NextHopTraits>
std::string ManagedSaiNextHopGroupNextHopMember<NextHopTraits>::toString()
    const {
  auto nextHopGroupMemberIdStr = this->getObject()
      ? std::to_string(this->getObject()->adapterKey())
      : "none";
  return folly::to<std::string>(
      this->getObject() ? "active " : "inactive ",
      "managed nhg member: ",
      "NextHopGroupId: ",
      nexthopGroupId_,
      "NextHopGroupMemberId:",
      nextHopGroupMemberIdStr);
}

std::string SaiNextHopGroupChildGroupMember::toString() const {
  auto nextHopGroupMemberIdStr = nextHopGroupMember_
      ? std::to_string(nextHopGroupMember_->adapterKey())
      : "none";
  return folly::to<std::string>(
      nextHopGroupMember_ ? "active " : "inactive ",
      "next hop group child group member: ",
      "ParentNextHopGroupId: ",
      parentNextHopGroupId_,
      ", NextHopGroupMemberId: ",
      nextHopGroupMemberIdStr);
}

} // namespace facebook::fboss
