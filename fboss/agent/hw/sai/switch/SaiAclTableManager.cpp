/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/CounterUtils.h"
#include "fboss/agent/hw/sai/api/AclApi.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiArsManager.h"
#include "fboss/agent/hw/sai/switch/SaiArsProfileManager.h"
#include "fboss/agent/hw/sai/switch/SaiHostifManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiMirrorManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiTunnelManager.h"
#include "fboss/agent/hw/sai/switch/SaiUdfManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/NodeMapDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/MacAddress.h>
#include <chrono>
#include <cstdint>
#include <memory>

extern "C" {
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
#include <experimental/saiaclextensions.h>
#endif
}

using namespace std::chrono;

namespace facebook::fboss {

namespace {

// Match all 32 bits in the selected IPv6 word.
constexpr uint32_t kIpV6WordExactMatchMask = 0xFFFFFFFF;
constexpr int kMigratedAclTablePriority = 23;

int normalizeAclTablePriority(int priority) {
  // TODO: Add dedicated ACL Agent HW tests for configured priority changes,
  // then remove this after all ACL table configs use priority 23.
  return priority < kMigratedAclTablePriority ? kMigratedAclTablePriority
                                              : priority;
}

#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
template <typename UdfAttribute>
void normalizeNullUdfAttribute(std::optional<UdfAttribute>& attribute) {
  if (attribute.has_value() && attribute->value() == SAI_NULL_OBJECT_ID) {
    attribute.reset();
  }
}

void normalizeAclTableUdfAttributes(
    SaiAclTableTraits::CreateAttributes& attributes) {
  using Attributes = SaiAclTableTraits::Attributes;
  normalizeNullUdfAttribute(
      std::get<std::optional<Attributes::UserDefinedFieldGroupMin0>>(
          attributes));
  normalizeNullUdfAttribute(
      std::get<std::optional<Attributes::UserDefinedFieldGroupMin1>>(
          attributes));
  normalizeNullUdfAttribute(
      std::get<std::optional<Attributes::UserDefinedFieldGroupMin2>>(
          attributes));
  normalizeNullUdfAttribute(
      std::get<std::optional<Attributes::UserDefinedFieldGroupMin3>>(
          attributes));
  normalizeNullUdfAttribute(
      std::get<std::optional<Attributes::UserDefinedFieldGroupMin4>>(
          attributes));
}
#else
void normalizeAclTableUdfAttributes(SaiAclTableTraits::CreateAttributes&) {}
#endif

folly::IPAddressV6 ipV6WordToAddress(uint32_t word, int wordIndex) {
  CHECK(wordIndex == 2 || wordIndex == 3)
      << "Only DST IPv6 word2 and word3 are supported";

  folly::ByteArray16 bytes{{0}};
  // SAI word3 is the most-significant IPv6 word; word0 is least-significant.
  auto byteOffset = (3 - wordIndex) * sizeof(uint32_t);
  bytes[byteOffset] = static_cast<uint8_t>(word >> 24);
  bytes[byteOffset + 1] = static_cast<uint8_t>(word >> 16);
  bytes[byteOffset + 2] = static_cast<uint8_t>(word >> 8);
  bytes[byteOffset + 3] = static_cast<uint8_t>(word);
  return folly::IPAddressV6(bytes);
}

AclEntryFieldIpV6 ipV6WordField(uint32_t word, int wordIndex) {
  return AclEntryFieldIpV6(
      std::make_pair(
          ipV6WordToAddress(word, wordIndex),
          ipV6WordToAddress(kIpV6WordExactMatchMask, wordIndex)));
}

#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
std::vector<sai_int8_t> aclEntryLabel(const std::string& aclEntryName) {
  std::vector<sai_int8_t> label(aclEntryName.begin(), aclEntryName.end());
#if defined(BRCM_SAI_SDK_XGS)
  constexpr size_t kMaxLabelLen = 256;
  if (aclEntryName.size() >= kMaxLabelLen) {
    throw FbossError(
        "ACL entry name: ",
        aclEntryName,
        " size ",
        aclEntryName.size(),
        " exceeds max(",
        kMaxLabelLen - 1,
        ")");
  }
  // Broadcom overwrites the last label byte with NUL and returns it on GET.
  label.push_back('\0');
#endif
  return label;
}
#endif

} // namespace

sai_u32_range_t SaiAclTableManager::getFdbDstUserMetaDataRange() const {
  std::optional<SaiSwitchTraits::Attributes::FdbDstUserMetaDataRange> range =
      SaiSwitchTraits::Attributes::FdbDstUserMetaDataRange();
  return *(SaiApiTable::getInstance()->switchApi().getAttribute(
      managerTable_->switchManager().getSwitchSaiId(), range));
}

sai_u32_range_t SaiAclTableManager::getRouteDstUserMetaDataRange() const {
  std::optional<SaiSwitchTraits::Attributes::RouteDstUserMetaDataRange> range =
      SaiSwitchTraits::Attributes::RouteDstUserMetaDataRange();
  return *(SaiApiTable::getInstance()->switchApi().getAttribute(
      managerTable_->switchManager().getSwitchSaiId(), range));
}

sai_u32_range_t SaiAclTableManager::getNeighborDstUserMetaDataRange() const {
  std::optional<SaiSwitchTraits::Attributes::NeighborDstUserMetaDataRange>
      range = SaiSwitchTraits::Attributes::NeighborDstUserMetaDataRange();
  return *(SaiApiTable::getInstance()->switchApi().getAttribute(
      managerTable_->switchManager().getSwitchSaiId(), range));
}

sai_u32_range_t SaiAclTableManager::getPortUserMetaDataRange() const {
  std::optional<SaiSwitchTraits::Attributes::PortUserMetaDataRange> range =
      SaiSwitchTraits::Attributes::PortUserMetaDataRange();
  return *(SaiApiTable::getInstance()->switchApi().getAttribute(
      managerTable_->switchManager().getSwitchSaiId(), range));
}

sai_uint32_t SaiAclTableManager::getMetaDataMask(
    sai_uint32_t metaDataMax) const {
  /*
   * Round up to the next highest power of 2
   *
   * The idea is to set all the bits on the right hand side of the most
   * significant set bit to 1 and then increment the value by 1 so it 'rolls
   * over' to the nearest power of 2.
   *
   * Note, right shifting to power of 2 and OR'ing is enough - we don't need to
   * shift and OR by 3, 5, 6 etc. as shift + OR by (1, 2), (1, 4), (2, 4) etc.
   * already achieves that effect.
   *
   * Reference:
   * https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
   */

  // to handle the case when metaDataMax is already power of 2.
  metaDataMax--;
  std::array<int, 5> kNumBitsToShift = {1, 2, 4, 8, 16};
  for (auto numBitsToShift : kNumBitsToShift) {
    metaDataMax |= metaDataMax >> numBitsToShift;
  }
  metaDataMax++;

  // to handle the case when the passed metaDataMax is 0
  metaDataMax += (((metaDataMax == 0)) ? 1 : 0);

  return metaDataMax - 1;
}

std::vector<std::string> SaiAclTableManager::getAllHandleNames() const {
  std::vector<std::string> handleNames;
  for (const auto& [name, handle] : handles_) {
    handleNames.push_back(name);
  }
  return handleNames;
}

AclTableSaiId SaiAclTableManager::addAclTable(
    const std::shared_ptr<AclTable>& addedAclTable,
    cfg::AclStage aclStage,
    const std::shared_ptr<SwitchState>& /*state*/,
    cfg::AclTableGroupBindPoint bindPoint) {
  auto saiAclStage =
      SaiAclTableGroupManager::cfgAclStageToSaiAclStage(aclStage);

  /*
   * TODO(skhare)
   * Add single ACL Table for now (called during SaiSwitch::init()).
   * Later, extend SwitchState to carry AclTable, and then process it to
   * addAclTable.
   *
   * After ACL table is added, add it to appropriate ACL group:
   * managerTable_->switchManager().addTableGroupMember(aclStage,
   * aclTableSaiId);
   */

  // If we already store a handle for this this Acl Table, fail to add a new one
  auto aclTableName = addedAclTable->getID();
  auto handle = getAclTableHandle(aclTableName);
  if (handle) {
    throw FbossError("attempted to add a duplicate aclTable: ", aclTableName);
  }

  SaiAclTableTraits::AdapterHostKey adapterHostKey;
  SaiAclTableTraits::CreateAttributes attributes;

  std::tie(adapterHostKey, attributes) =
      aclTableCreateAttributes(saiAclStage, addedAclTable, bindPoint);

  auto& aclTableStore = saiStore_->get<SaiAclTableTraits>();
  std::shared_ptr<SaiAclTable> saiAclTable{};
  if (platform_->getHwSwitch()->getBootType() == BootType::WARM_BOOT) {
    if (auto existingAclTable = aclTableStore.get(adapterHostKey)) {
      auto expectedAttributes = attributes;
      auto existingAttributes = existingAclTable->attributes();
      normalizeAclTableUdfAttributes(expectedAttributes);
      normalizeAclTableUdfAttributes(existingAttributes);
      if (expectedAttributes != existingAttributes ||
          FLAGS_force_recreate_acl_tables) {
        auto key = existingAclTable->adapterHostKey();
        auto attrs = existingAclTable->attributes();
        existingAclTable = aclTableStore.setObject(key, attrs);
        recreateAclTable(existingAclTable, attributes);
      }
      saiAclTable = std::move(existingAclTable);
    }
  }
  saiAclTable = aclTableStore.setObject(adapterHostKey, attributes);
  auto aclTableHandle = std::make_unique<SaiAclTableHandle>();
  aclTableHandle->aclTable = saiAclTable;
  auto [it, inserted] =
      handles_.emplace(aclTableName, std::move(aclTableHandle));
  CHECK(inserted);

  auto aclTableSaiId = it->second->aclTable->adapterKey();

  // Add ACL Table to group based on the stage
  if (hasTableGroups_) {
    managerTable_->aclTableGroupManager().addAclTableGroupMember(
        saiAclStage,
        bindPoint,
        aclTableSaiId,
        aclTableName,
        normalizeAclTablePriority(addedAclTable->getPriority()));
  }

  return aclTableSaiId;
}

void SaiAclTableManager::removeAclTable(
    const std::shared_ptr<AclTable>& removedAclTable,
    cfg::AclStage aclStage,
    const std::shared_ptr<SwitchState>& /*state*/,
    cfg::AclTableGroupBindPoint bindPoint) {
  auto saiAclStage =
      SaiAclTableGroupManager::cfgAclStageToSaiAclStage(aclStage);
  auto aclTableName = removedAclTable->getID();

  // remove from acl table group
  if (hasTableGroups_) {
    managerTable_->aclTableGroupManager().removeAclTableGroupMember(
        saiAclStage, bindPoint, aclTableName);
  }

  // remove from handles
  handles_.erase(aclTableName);
}

void SaiAclTableManager::removeObsoletePortBoundAclTables(
    const std::shared_ptr<SwitchState>& oldState,
    const std::shared_ptr<SwitchState>& newState) {
  const auto& oldPortAclTableGroups = oldState->getPortAclTableGroups();
  const auto& newPortAclTableGroups = newState->getPortAclTableGroups();
  for (const auto& [_, tableGroupMap] : std::as_const(*oldPortAclTableGroups)) {
    for (const auto& [_, tableGroup] : std::as_const(*tableGroupMap)) {
      const auto aclStage = tableGroup->getID();
      const auto oldAclTables = tableGroup->getAclTableMap();
      std::shared_ptr<const AclTableMap> newAclTables;
      if (const auto newTableGroup =
              newPortAclTableGroups->getNodeIf(aclStage)) {
        newAclTables = newTableGroup->getAclTableMap();
      }
      DeltaFunctions::forEachRemoved(
          ThriftMapDelta<AclTableMap>(oldAclTables.get(), newAclTables.get()),
          [this, aclStage, &newState](const auto& removedAclTable) {
            removeAclTable(
                removedAclTable,
                aclStage,
                newState,
                cfg::AclTableGroupBindPoint::PORT);
          });
    }
  }
}

bool SaiAclTableManager::needsAclTableRecreate(
    const std::shared_ptr<AclTable>& oldAclTable,
    const std::shared_ptr<AclTable>& newAclTable,
    cfg::AclStage aclStage) {
  // TODO(zecheng): actionTypes has the same empty-means-ASIC-default semantics
  // as qualifiers (see getActionTypeList) and should be compared the same way.
  if (oldAclTable->getActionTypes() != newAclTable->getActionTypes() ||
      normalizeAclTablePriority(oldAclTable->getPriority()) !=
          normalizeAclTablePriority(newAclTable->getPriority()) ||
      oldAclTable->getUdfGroups()->toThrift() !=
          newAclTable->getUdfGroups()->toThrift()) {
    XLOG(DBG2) << "Recreating ACL table: table properties changed";
    return true;
  }
  if (oldAclTable->getQualifiers() == newAclTable->getQualifiers()) {
    return false;
  }
  // An empty list means "whatever the ASIC supports", so lists that differ can
  // still resolve to the same set. Reaching getQualifierSet only from here also
  // keeps it off the unchanged path, where it could throw for an egress table
  // on an ASIC without post lookup ACL support.
  auto saiAclStage =
      SaiAclTableGroupManager::cfgAclStageToSaiAclStage(aclStage);
  if (getQualifierSet(saiAclStage, oldAclTable) !=
      getQualifierSet(saiAclStage, newAclTable)) {
    XLOG(DBG2) << "Recreating ACL table: qualifiers changed";
    return true;
  }
  return false;
}

void SaiAclTableManager::removeAclEntriesFromTable(
    const std::shared_ptr<AclTable>& aclTable) {
  auto aclMap = aclTable->getAclMap().unwrap();
  for (auto const& iter : std::as_const(*aclMap)) {
    const auto& entry = iter.second;
    auto aclEntry = aclMap->getEntry(entry->getID());
    removeAclEntry(aclEntry, aclTable->getID());
  }
}

void SaiAclTableManager::addAclEntriesToTable(
    const std::shared_ptr<AclTable>& aclTable,
    std::shared_ptr<AclMap>& aclMap,
    const std::shared_ptr<SwitchState>& state) {
  auto newAclMap = aclTable->getAclMap().unwrap();
  for (auto const& iter : std::as_const(*aclMap)) {
    const auto& entry = iter.second;
    // only re-add the ACL entry if present in new table
    if (!newAclMap->getEntryIf(entry->getID())) {
      continue;
    }
    auto aclEntry = aclMap->getEntry(entry->getID());
    // Pass the new state through so PBR entries can resolve their NHGs on the
    // table-recreate path (it would otherwise throw on a null state).
    addAclEntry(aclEntry, aclTable->getID(), state);
  }
}

void SaiAclTableManager::changedAclTable(
    const std::shared_ptr<AclTable>& oldAclTable,
    const std::shared_ptr<AclTable>& newAclTable,
    cfg::AclStage aclStage,
    const std::shared_ptr<SwitchState>& state,
    cfg::AclTableGroupBindPoint bindPoint) {
  /*
   * If the only change in acl table is in acl entries, then the acl entry delta
   * processing will take care of changing those.
   * Changes to ACL table properties will need a remove and readd
   * Ensure that the newly added table also adds the old acls*/
  if (needsAclTableRecreate(oldAclTable, newAclTable, aclStage)) {
    if (bindPoint == cfg::AclTableGroupBindPoint::PORT) {
      // Stage the replacement under a temporary name so both tables coexist.
      const auto aclTableName = oldAclTable->getID();
      const auto stagedAclTableName =
          folly::to<std::string>(aclTableName, "-recreate");
      if (getAclTableHandle(stagedAclTableName)) {
        throw FbossError(
            "staged ACL table already exists: ", stagedAclTableName);
      }

      auto stagedAclTableFields = newAclTable->toThrift();
      stagedAclTableFields.id() = stagedAclTableName;
      auto stagedAclTable =
          std::make_shared<AclTable>(std::move(stagedAclTableFields));
      const auto oldAclTableId =
          getAclTableHandle(aclTableName)->aclTable->adapterKey();
      const auto newAclTableId =
          addAclTable(stagedAclTable, aclStage, state, bindPoint);

      try {
        // Program the replacement, then move ports to its OID.
        auto oldAclMap = oldAclTable->getAclMap().unwrap();
        addAclEntriesToTable(stagedAclTable, oldAclMap, state);
        managerTable_->portManager().replaceIngressAcl(
            oldAclTableId, newAclTableId);
      } catch (...) {
        managerTable_->portManager().replaceIngressAcl(
            newAclTableId, oldAclTableId);
        removeAclEntriesFromTable(stagedAclTable);
        removeAclTable(stagedAclTable, aclStage, state, bindPoint);
        throw;
      }

      // The old table is no longer bound and can now be removed.
      removeAclEntriesFromTable(oldAclTable);
      removeAclTable(oldAclTable, aclStage, state, bindPoint);

      // Restore the configured name in bookkeeping without changing its OID.
      managerTable_->aclTableGroupManager().removeAclTableGroupMember(
          SaiAclTableGroupManager::cfgAclStageToSaiAclStage(aclStage),
          bindPoint,
          stagedAclTableName);
      managerTable_->aclTableGroupManager().addAclTableGroupMember(
          SaiAclTableGroupManager::cfgAclStageToSaiAclStage(aclStage),
          bindPoint,
          newAclTableId,
          aclTableName,
          normalizeAclTablePriority(newAclTable->getPriority()));

      auto stagedHandle = std::move(handles_.at(stagedAclTableName));
      handles_.erase(stagedAclTableName);
      auto [_, inserted] =
          handles_.emplace(aclTableName, std::move(stagedHandle));
      CHECK(inserted);
      return;
    }
    // Remove acl entries from old acl table before removing the table
    removeAclEntriesFromTable(oldAclTable);
    removeAclTable(oldAclTable, aclStage, state, bindPoint);
    addAclTable(newAclTable, aclStage, state, bindPoint);

    // Add the old acl Entries back to new acl table
    auto oldAclMap = oldAclTable->getAclMap().unwrap();
    addAclEntriesToTable(newAclTable, oldAclMap, state);
  }
}

const SaiAclTableHandle* FOLLY_NULLABLE
SaiAclTableManager::getAclTableHandle(const std::string& aclTableName) const {
  return getAclTableHandleImpl(aclTableName);
}

SaiAclTableHandle* FOLLY_NULLABLE
SaiAclTableManager::getAclTableHandle(const std::string& aclTableName) {
  return getAclTableHandleImpl(aclTableName);
}

SaiAclTableHandle* FOLLY_NULLABLE SaiAclTableManager::getAclTableHandleImpl(
    const std::string& aclTableName) const {
  auto itr = handles_.find(aclTableName);
  if (itr == handles_.end()) {
    return nullptr;
  }
  if (!itr->second || !itr->second->aclTable) {
    XLOG(FATAL) << "invalid null Acl table for: " << aclTableName;
  }
  return itr->second.get();
}

sai_acl_ip_frag_t SaiAclTableManager::cfgIpFragToSaiIpFrag(
    cfg::IpFragMatch cfgType) const {
  switch (cfgType) {
    case cfg::IpFragMatch::MATCH_NOT_FRAGMENTED:
      return SAI_ACL_IP_FRAG_NON_FRAG;
    case cfg::IpFragMatch::MATCH_FIRST_FRAGMENT:
      return SAI_ACL_IP_FRAG_HEAD;
    case cfg::IpFragMatch::MATCH_NOT_FRAGMENTED_OR_FIRST_FRAGMENT:
      return SAI_ACL_IP_FRAG_NON_FRAG_OR_HEAD;
    case cfg::IpFragMatch::MATCH_NOT_FIRST_FRAGMENT:
      return SAI_ACL_IP_FRAG_NON_HEAD;
    case cfg::IpFragMatch::MATCH_ANY_FRAGMENT:
      return SAI_ACL_IP_FRAG_ANY;
  }
  // should return in one of the cases
  throw FbossError("Unsupported IP fragment option");
}

sai_acl_ip_type_t SaiAclTableManager::cfgIpTypeToSaiIpType(
    cfg::IpType cfgIpType) const {
  switch (cfgIpType) {
    case cfg::IpType::ANY:
      return SAI_ACL_IP_TYPE_ANY;
    case cfg::IpType::IP:
      return SAI_ACL_IP_TYPE_IP;
    case cfg::IpType::IP4:
      return SAI_ACL_IP_TYPE_IPV4ANY;
    case cfg::IpType::IP6:
      return SAI_ACL_IP_TYPE_IPV6ANY;
    case cfg::IpType::ARP_REQUEST:
      return SAI_ACL_IP_TYPE_ARP_REQUEST;
    case cfg::IpType::ARP_REPLY:
      return SAI_ACL_IP_TYPE_ARP_REPLY;
    case cfg::IpType::NON_IP:
      return SAI_ACL_IP_TYPE_NON_IP;
  }
  // should return in one of the cases
  throw FbossError("Unsupported IP Type option");
}

uint16_t SaiAclTableManager::cfgEtherTypeToSaiEtherType(
    cfg::EtherType cfgEtherType) const {
  switch (cfgEtherType) {
    case cfg::EtherType::ANY:
    case cfg::EtherType::IPv4:
    case cfg::EtherType::IPv6:
    case cfg::EtherType::EAPOL:
    case cfg::EtherType::MACSEC:
    case cfg::EtherType::LLDP:
    case cfg::EtherType::ARP:
    case cfg::EtherType::LACP:
    case cfg::EtherType::AIFM:
    case cfg::EtherType::MPLS:
      return static_cast<uint16_t>(cfgEtherType);
  }
  // should return in one of the cases
  throw FbossError("Unsupported EtherType option");
}

sai_uint32_t SaiAclTableManager::cfgLookupClassToSaiMetaDataAndMaskHelper(
    cfg::AclLookupClass lookupClass,
    sai_uint32_t dstUserMetaDataRangeMin,
    sai_uint32_t dstUserMetaDataRangeMax) const {
  auto dstUserMetaData = static_cast<int>(lookupClass);
  if (dstUserMetaData < dstUserMetaDataRangeMin ||
      dstUserMetaData > dstUserMetaDataRangeMax) {
    throw FbossError(
        "attempted to configure dstUserMeta larger than supported by this ASIC",
        dstUserMetaData,
        " supported min: ",
        dstUserMetaDataRangeMin,
        " max: ",
        dstUserMetaDataRangeMax);
  }

  return dstUserMetaData;
}

std::pair<sai_uint32_t, sai_uint32_t>
SaiAclTableManager::cfgLookupClassToSaiFdbMetaDataAndMask(
    cfg::AclLookupClass lookupClass) const {
  return std::make_pair(
      cfgLookupClassToSaiMetaDataAndMaskHelper(
          lookupClass,
          fdbDstUserMetaDataRangeMin_,
          fdbDstUserMetaDataRangeMax_),
      fdbDstUserMetaDataMask_);
}

std::pair<sai_uint32_t, sai_uint32_t>
SaiAclTableManager::cfgLookupClassToSaiRouteMetaDataAndMask(
    cfg::AclLookupClass lookupClass) const {
  return std::make_pair(
      cfgLookupClassToSaiMetaDataAndMaskHelper(
          lookupClass,
          routeDstUserMetaDataRangeMin_,
          routeDstUserMetaDataRangeMax_),
      routeDstUserMetaDataMask_);
}

std::pair<sai_uint32_t, sai_uint32_t>
SaiAclTableManager::cfgLookupClassToSaiNeighborMetaDataAndMask(
    cfg::AclLookupClass lookupClass) const {
  return std::make_pair(
      cfgLookupClassToSaiMetaDataAndMaskHelper(
          lookupClass,
          neighborDstUserMetaDataRangeMin_,
          neighborDstUserMetaDataRangeMax_),
      neighborDstUserMetaDataMask_);
}

std::pair<sai_uint32_t, sai_uint32_t>
SaiAclTableManager::cfgLookupClassToSaiPortMetaDataAndMask(
    cfg::AclLookupClassPort lookupClass) const {
  const auto range = getPortUserMetaDataRange();
  const auto metadata = static_cast<sai_uint32_t>(lookupClass);
  if (metadata < range.min || metadata > range.max) {
    throw FbossError(
        "attempted to configure port user metadata outside the range "
        "supported by this ASIC",
        metadata,
        " supported min: ",
        range.min,
        " max: ",
        range.max);
  }
  return std::make_pair(metadata, getMetaDataMask(range.max));
}

std::vector<sai_int32_t>
SaiAclTableManager::cfgActionTypeListToSaiActionTypeList(
    const std::vector<cfg::AclTableActionType>& actionTypes) const {
  std::vector<sai_int32_t> saiActionTypeList;

  for (const auto& actionType : actionTypes) {
    sai_int32_t saiActionType;
    switch (actionType) {
      case cfg::AclTableActionType::PACKET_ACTION:
        saiActionType = SAI_ACL_ACTION_TYPE_PACKET_ACTION;
        break;
      case cfg::AclTableActionType::COUNTER:
        saiActionType = SAI_ACL_ACTION_TYPE_COUNTER;
        break;
      case cfg::AclTableActionType::SET_TC:
        saiActionType = SAI_ACL_ACTION_TYPE_SET_TC;
        break;
      case cfg::AclTableActionType::SET_DSCP:
        saiActionType = SAI_ACL_ACTION_TYPE_SET_DSCP;
        break;
      case cfg::AclTableActionType::MIRROR_INGRESS:
        saiActionType = SAI_ACL_ACTION_TYPE_MIRROR_INGRESS;
        break;
      case cfg::AclTableActionType::MIRROR_EGRESS:
        saiActionType = SAI_ACL_ACTION_TYPE_MIRROR_EGRESS;
        break;
      case cfg::AclTableActionType::SET_USER_DEFINED_TRAP:
        saiActionType = SAI_ACL_ACTION_TYPE_SET_USER_TRAP_ID;
        break;
      case cfg::AclTableActionType::REDIRECT:
        saiActionType = SAI_ACL_ACTION_TYPE_REDIRECT;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      case cfg::AclTableActionType::SET_ARS_OBJECT:
        saiActionType = SAI_ACL_ACTION_TYPE_SET_ARS_OBJECT;
        break;
      case cfg::AclTableActionType::DISABLE_ARS_FORWARDING:
        saiActionType = SAI_ACL_ACTION_TYPE_DISABLE_ARS_FORWARDING;
        break;
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
      case cfg::AclTableActionType::L3_SWITCH_CANCEL:
        saiActionType = SAI_ACL_ACTION_TYPE_L3_SWITCH_CANCEL;
        break;
#endif
      default:
        // should return in one of the cases
        throw FbossError("Unsupported Acl Table action type");
    }
    saiActionTypeList.push_back(saiActionType);
  }

  return saiActionTypeList;
}

bool SaiAclTableManager::isSameAclCounterAttributes(
    const SaiAclCounterTraits::CreateAttributes& fromStore,
    const SaiAclCounterTraits::CreateAttributes& fromSw) {
  bool result = GET_ATTR(AclCounter, TableId, fromStore) ==
          GET_ATTR(AclCounter, TableId, fromSw) &&
      GET_OPT_ATTR(AclCounter, EnablePacketCount, fromStore) ==
          GET_OPT_ATTR(AclCounter, EnablePacketCount, fromSw) &&
      GET_OPT_ATTR(AclCounter, EnableByteCount, fromStore) ==
          GET_OPT_ATTR(AclCounter, EnableByteCount, fromSw);

  if (platform_->getAsic()->isSupported(HwAsic::Feature::ACL_COUNTER_LABEL)) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    result = result &&
        GET_OPT_ATTR(AclCounter, Label, fromStore) ==
            GET_OPT_ATTR(AclCounter, Label, fromSw);
#endif
  }
  return result;
}

std::pair<
    std::shared_ptr<SaiAclCounter>,
    std::vector<std::pair<cfg::CounterType, std::string>>>
SaiAclTableManager::addAclCounter(
    const SaiAclTableHandle* aclTableHandle,
    const cfg::TrafficCounter& trafficCount,
    const SaiAclEntryTraits::AdapterHostKey& aclEntryAdapterHostKey) {
  std::vector<std::pair<cfg::CounterType, std::string>> aclCounterTypeAndName;

  SaiAclCounterTraits::Attributes::TableId aclTableId{
      aclTableHandle->aclTable->adapterKey()};

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
  SaiCharArray32 counterLabel{};
  if ((*trafficCount.name()).size() > 31) {
    throw FbossError(
        "ACL Counter Label:",
        *trafficCount.name(),
        " size ",
        (*trafficCount.name()).size(),
        " exceeds max(31)");
  }

  std::copy(
      (*trafficCount.name()).begin(),
      (*trafficCount.name()).end(),
      counterLabel.begin());

  std::optional<SaiAclCounterTraits::Attributes::Label> aclCounterLabel{
      std::nullopt};
  if (platform_->getAsic()->isSupported(HwAsic::Feature::ACL_COUNTER_LABEL)) {
    aclCounterLabel = counterLabel;
  }
#endif

  std::optional<SaiAclCounterTraits::Attributes::EnablePacketCount>
      enablePacketCount{false};
  std::optional<SaiAclCounterTraits::Attributes::EnableByteCount>
      enableByteCount{false};

  for (const auto& counterType : *trafficCount.types()) {
    std::string statSuffix;
    switch (counterType) {
      case cfg::CounterType::PACKETS:
        enablePacketCount =
            SaiAclCounterTraits::Attributes::EnablePacketCount{true};
        statSuffix = "packets";
        break;
      case cfg::CounterType::BYTES:
        // Gate on ASIC type rather than HwAsic::Feature::ACL_BYTE_COUNTER;
        // gating with feature causes a failure with Cisco SDK.
        if (platform_->getAsic()->getAsicType() !=
            cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
          enableByteCount =
              SaiAclCounterTraits::Attributes::EnableByteCount{true};
        }
        statSuffix = "bytes";
        break;
      default:
        throw FbossError("Unsupported CounterType for ACL");
    }

    auto statName =
        folly::to<std::string>(*trafficCount.name(), ".", statSuffix);
    aclCounterTypeAndName.emplace_back(counterType, statName);
    if (aclCounterRefMap_.find(statName) == aclCounterRefMap_.end()) {
      // Create fb303 counter since stat is being added/readded again
      aclStats_.reinitStat(statName, std::nullopt);
      aclCounterRefMap_[statName] = 1;
    } else {
      aclCounterRefMap_[statName]++;
    }
  }

  SaiAclCounterTraits::AdapterHostKey adapterHostKey{
      aclTableId,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      aclCounterLabel,
#endif
      enablePacketCount,
      enableByteCount,
  };

  SaiAclCounterTraits::CreateAttributes attributes{
      aclTableId,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      aclCounterLabel,
#endif
      enablePacketCount,
      enableByteCount,
      std::nullopt, // counterPackets
      std::nullopt, // counterBytes
  };

  // The following logic is added temporarily for 5.1 -> 7.2 warmboot
  // transition. Sai spec 1.10.2 introduces label attributes for acl
  // counters, which requires agent detaching the old acl counter without
  // label attributes from the acl entry and then reattach the new acl
  // counter. In order to attach the new counter, agent needs to set counter
  // attribute to SAI_NULL_OBJECT_ID, and then attach the newly created
  // counter. Following is the logic to check if the attach/detach logic is
  // needed.
  // TODO(zecheng): remove the following logic when 7.2 upgrade completes.
  auto& aclCounterStore = saiStore_->get<SaiAclCounterTraits>();
  auto& aclEntryStore = saiStore_->get<SaiAclEntryTraits>();
  // If acl entry exists and already has different acl counter attached to
  // it, detach the old acl counter and then attach the new one.
  if (auto existingAclEntry = aclEntryStore.get(aclEntryAdapterHostKey)) {
    auto oldActionCounter = AclCounterSaiId{
        GET_OPT_ATTR(AclEntry, ActionCounter, existingAclEntry->attributes())
            .getData()};
    if (auto existingAclCounter = aclCounterStore.find(oldActionCounter)) {
      if (!isSameAclCounterAttributes(
              existingAclCounter->attributes(), attributes)) {
        // Detach the old one
        SaiAclEntryTraits::Attributes::ActionCounter actionCounterAttribute{
            SAI_NULL_OBJECT_ID};
        SaiApiTable::getInstance()->aclApi().setAttribute(
            existingAclEntry->adapterKey(), actionCounterAttribute);
      }
    }
  }
  auto saiAclCounter = aclCounterStore.setObject(adapterHostKey, attributes);

  return std::make_pair(saiAclCounter, aclCounterTypeAndName);
}

#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
void SaiAclTableManager::updateUdfGroupAttributes(
    const std::shared_ptr<AclEntry>& addedAclEntry,
    const std::string& aclTableName,
    std::optional<AclEntryUdfGroup0>& udfGroup0,
    std::optional<AclEntryUdfGroup1>& udfGroup1,
    std::optional<AclEntryUdfGroup2>& udfGroup2,
    std::optional<AclEntryUdfGroup3>& udfGroup3,
    std::optional<AclEntryUdfGroup4>& udfGroup4) {
  auto convertSignedCharsToUnsignedChars =
      [](const std::vector<signed char>& vec) {
        std::vector<unsigned char> unsignedVec;

        std::transform(
            vec.begin(),
            vec.end(),
            std::back_inserter(unsignedVec),
            [](signed char c) { return static_cast<unsigned char>(c); });

        return unsignedVec;
      };

  /*
   * UDF fields in ACL entries have to match the attribute ID offset where
   * group is defined in the ACL table
   * If udfGroup1 is at (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1),
   * the corresponding ACL entry fields should also use
   * (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1)
   * https://github.com/opencomputeproject/SAI/blob/master/doc/ACL/UDF-based-ACL.md#example-1
   *
   * We have capability to program 5 UDF fields. The below code will try
   * to match the UDF group SAI ID currently processed with the attribute used
   * in the ACL table
   */
  if (addedAclEntry->getUdfTable()) {
    auto& aclApi = SaiApiTable::getInstance()->aclApi();
    auto aclTableHandle = getAclTableHandle(aclTableName);
    auto aclTableSaiId = aclTableHandle->aclTable->adapterKey();
    // Get the udfGroup IDs, if programmed, from each of the 5 attributes
    auto udfGroupId0 = aclApi.getAttribute(aclTableSaiId, AclTableUdfGroup0());
    auto udfGroupId1 = aclApi.getAttribute(aclTableSaiId, AclTableUdfGroup1());
    auto udfGroupId2 = aclApi.getAttribute(aclTableSaiId, AclTableUdfGroup2());
    auto udfGroupId3 = aclApi.getAttribute(aclTableSaiId, AclTableUdfGroup3());
    auto udfGroupId4 = aclApi.getAttribute(aclTableSaiId, AclTableUdfGroup4());

    const auto udfTable = addedAclEntry->getUdfTable().value();
    for (const auto& udfEntry : udfTable) {
      auto data = convertSignedCharsToUnsignedChars(*udfEntry.roceBytes());
      auto mask = convertSignedCharsToUnsignedChars(*udfEntry.roceMask());
      auto udfData = std::make_pair(std::move(data), std::move(mask));

      std::vector<std::string> udfGroupNames = {*udfEntry.udfGroup()};
      auto udfGroupSaiIds =
          managerTable_->udfManager().getUdfGroupIds(udfGroupNames);
      if (udfGroupSaiIds.size() == 0) {
        throw FbossError(
            "Invalid UdfGroup {} in ACL entry", *udfEntry.udfGroup());
      }
      auto udfGroupSaiId = udfGroupSaiIds[0];

      // for each udfGroup used in this ACL entry, check which attribute it
      // uses in the ACL table and use the same ACL entry attribute
      if (udfGroupSaiId == udfGroupId0) {
        udfGroup0 = AclEntryUdfGroup0{AclEntryFieldU8List{udfData}};
        continue;
      }
      if (udfGroupSaiId == udfGroupId1) {
        udfGroup1 = AclEntryUdfGroup1{AclEntryFieldU8List{udfData}};
        continue;
      }
      if (udfGroupSaiId == udfGroupId2) {
        udfGroup2 = AclEntryUdfGroup2{AclEntryFieldU8List{udfData}};
        continue;
      }
      if (udfGroupSaiId == udfGroupId3) {
        udfGroup3 = AclEntryUdfGroup3{AclEntryFieldU8List{udfData}};
        continue;
      }
      if (udfGroupSaiId == udfGroupId4) {
        udfGroup4 = AclEntryUdfGroup4{AclEntryFieldU8List{udfData}};
        continue;
      }
    }
  }
}
#endif

std::shared_ptr<SaiAclRange> SaiAclTableManager::getOrCreateAclRange(
    sai_int32_t rangeType,
    uint32_t min,
    uint32_t max) {
  sai_u32_range_t limit{.min = min, .max = max};
  SaiAclRangeTraits::Attributes::Type typeAttr{rangeType};
  SaiAclRangeTraits::Attributes::Limit limitAttr{limit};
  SaiAclRangeTraits::AdapterHostKey key{typeAttr, limitAttr};
  SaiAclRangeTraits::CreateAttributes attrs{typeAttr, limitAttr};
  auto& store = saiStore_->get<SaiAclRangeTraits>();
  return store.setObject(key, attrs);
}

std::shared_ptr<SaiNextHopGroupHandle>
SaiAclTableManager::resolvePbrNextHopGroup(
    const std::shared_ptr<SwitchState>& state,
    int64_t nextHopGroupId,
    const std::string& aclEntryId) {
  if (!state) {
    throw FbossError(
        "PBR ACL entry ",
        aclEntryId,
        " references NextHopSetID ",
        nextHopGroupId,
        " but no SwitchState is available to resolve it");
  }
  // getNextHops throws on an unknown id, so an empty result means the id is
  // live but its named NHG currently resolves to no next hops - every member's
  // covering route withdrawn, say. Report that as "no group" and let the caller
  // drop rather than forward; throwing here would fail the whole state update.
  auto nexthops = getNextHops(state, nextHopGroupId);
  if (nexthops.empty()) {
    XLOG(DBG2) << "PBR ACL entry " << aclEntryId << " NextHopSetID "
               << nextHopGroupId << " resolves to an empty next hop group";
    return nullptr;
  }
  RouteNextHopSet nhops(nexthops.begin(), nexthops.end());
  auto handle = managerTable_->nextHopGroupManager().incRefOrAddNextHopGroup(
      SaiNextHopGroupKey(nhops, std::nullopt /*switchingMode*/));
  if (!handle->nextHopGroup) {
    // on multi-NPU switches a NHG can
    // legitimately have no members on the local ASIC (all next hops filtered
    // out), in which case the route side sets the route to DROP rather than
    // failing.
    if (platform_->hasMultipleSwitches()) {
      return nullptr;
    }
    throw FbossError(
        "PBR ACL entry ",
        aclEntryId,
        " NextHopSetID ",
        nextHopGroupId,
        " has no SAI next hop group (no members on this ASIC)");
  }
  return handle;
}

AclEntrySaiId SaiAclTableManager::addAclEntry(
    const std::shared_ptr<AclEntry>& addedAclEntry,
    const std::string& aclTableName,
    const std::shared_ptr<SwitchState>& state) {
  // If we attempt to add entry to a table that does not exist, fail.
  auto aclTableHandle = getAclTableHandle(aclTableName);
  if (!aclTableHandle) {
    throw FbossError(
        "attempted to add AclEntry to a AclTable that does not exist: ",
        aclTableName);
  }

  // If we already store a handle for this this Acl Entry, fail to add new one.
  auto aclEntryHandle = getAclEntryHandle(
      aclTableHandle, addedAclEntry->getPriority(), addedAclEntry->getID());
  if (aclEntryHandle) {
    throw FbossError(
        "attempted to add a duplicate aclEntry: ", addedAclEntry->getID());
  }

  auto& aclEntryStore = saiStore_->get<SaiAclEntryTraits>();

  SaiAclEntryTraits::Attributes::TableId aclTableId{
      aclTableHandle->aclTable->adapterKey()};
  SaiAclEntryTraits::Attributes::Priority priority{
      swPriorityToSaiPriority(addedAclEntry->getPriority())};
#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
  // Two entries in one table may share a priority (PBR does), so the entry name
  // is carried in the ACL entry label to keep their AdapterHostKeys distinct.
  std::optional<SaiAclEntryTraits::Attributes::Label> label{
      aclEntryLabel(addedAclEntry->getID())};
  SaiAclEntryTraits::AdapterHostKey adapterHostKey{aclTableId, priority, label};
#else
  SaiAclEntryTraits::AdapterHostKey adapterHostKey{aclTableId, priority};
#endif

  std::optional<SaiAclEntryTraits::Attributes::FieldSrcIpV6> fieldSrcIpV6{
      std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldSrcIpV4> fieldSrcIpV4{
      std::nullopt};
  if (addedAclEntry->getSrcIp().first) {
    if (addedAclEntry->getSrcIp().first.isV6()) {
      auto srcIpV6Mask = folly::IPAddressV6(
          folly::IPAddressV6::fetchMask(addedAclEntry->getSrcIp().second));
      fieldSrcIpV6 =
          SaiAclEntryTraits::Attributes::FieldSrcIpV6{AclEntryFieldIpV6(
              std::make_pair(
                  addedAclEntry->getSrcIp().first.asV6(), srcIpV6Mask))};
    } else if (addedAclEntry->getSrcIp().first.isV4()) {
      auto srcIpV4Mask = folly::IPAddressV4(
          folly::IPAddressV4::fetchMask(addedAclEntry->getSrcIp().second));
      fieldSrcIpV4 =
          SaiAclEntryTraits::Attributes::FieldSrcIpV4{AclEntryFieldIpV4(
              std::make_pair(
                  addedAclEntry->getSrcIp().first.asV4(), srcIpV4Mask))};
    }
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldDstIpV6> fieldDstIpV6{
      std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldDstIpV6Word3>
      fieldDstIpV6Word3{std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldDstIpV6Word2>
      fieldDstIpV6Word2{std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldDstIpV4> fieldDstIpV4{
      std::nullopt};
  if (addedAclEntry->getDstIp().first) {
    if (addedAclEntry->getDstIp().first.isV6()) {
      auto dstIpV6Mask = folly::IPAddressV6(
          folly::IPAddressV6::fetchMask(addedAclEntry->getDstIp().second));
      fieldDstIpV6 =
          SaiAclEntryTraits::Attributes::FieldDstIpV6{AclEntryFieldIpV6(
              std::make_pair(
                  addedAclEntry->getDstIp().first.asV6(), dstIpV6Mask))};
    } else if (addedAclEntry->getDstIp().first.isV4()) {
      auto dstIpV4Mask = folly::IPAddressV4(
          folly::IPAddressV4::fetchMask(addedAclEntry->getDstIp().second));
      fieldDstIpV4 =
          SaiAclEntryTraits::Attributes::FieldDstIpV4{AclEntryFieldIpV4(
              std::make_pair(
                  addedAclEntry->getDstIp().first.asV4(), dstIpV4Mask))};
    }
  }
  const auto dstIpV6Word3 = addedAclEntry->getDstIpV6Word3();
  const auto dstIpV6Word2 = addedAclEntry->getDstIpV6Word2();
  const auto dstIpV6WordQualifiersSupported = platform_->getAsic()->isSupported(
      HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS);
  if (dstIpV6Word3 && dstIpV6WordQualifiersSupported) {
    fieldDstIpV6Word3 = SaiAclEntryTraits::Attributes::FieldDstIpV6Word3{
        ipV6WordField(*dstIpV6Word3, 3 /* wordIndex */)};
  }
  if (dstIpV6Word2 && dstIpV6WordQualifiersSupported) {
    fieldDstIpV6Word2 = SaiAclEntryTraits::Attributes::FieldDstIpV6Word2{
        ipV6WordField(*dstIpV6Word2, 2 /* wordIndex */)};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldSrcPort> fieldSrcPort{
      std::nullopt};
  if (addedAclEntry->getSrcPort()) {
    if (addedAclEntry->getSrcPort().value() !=
        cfg::switch_config_constants::CPU_PORT_LOGICALID()) {
      auto portHandle = managerTable_->portManager().getPortHandle(
          PortID(addedAclEntry->getSrcPort().value()));
      if (!portHandle) {
        throw FbossError(
            "attempted to configure srcPort: ",
            addedAclEntry->getSrcPort().value(),
            " ACL:",
            addedAclEntry->getID());
      }
      fieldSrcPort =
          SaiAclEntryTraits::Attributes::FieldSrcPort{AclEntryFieldSaiObjectIdT(
              std::make_pair(portHandle->port->adapterKey(), kMaskDontCare))};
    } else {
      PortSaiId portSaiId;

      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::CPU_TX_VIA_RECYCLE_PORT)) {
        /*
         * For ASICs that implement CPU tx using recycle port, on tx:
         *  - FBOSS injects pkt from CPU.
         *  - SAI impl sends pkt to recycle port with pipeline bypass.
         *  - Packet is injected into pipeline with srcPort = recycle port.
         *
         *  Thus, if we want to program an ACL to trap such packets to CPU, we
         *  need to use recycle port as the src port qualifier as the packet is
         *  now "originating" from recycle port.
         */
        CHECK(managerTable_->switchManager().getCpuRecyclePort().has_value());
        portSaiId = managerTable_->switchManager().getCpuRecyclePort().value();
      } else {
        portSaiId = managerTable_->switchManager().getCpuPort();
      }

      fieldSrcPort = SaiAclEntryTraits::Attributes::FieldSrcPort{
          AclEntryFieldSaiObjectIdT(std::make_pair(portSaiId, kMaskDontCare))};
    }
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldOutPort> fieldOutPort{
      std::nullopt};
  if (addedAclEntry->getDstPort()) {
    auto portHandle = managerTable_->portManager().getPortHandle(
        PortID(addedAclEntry->getDstPort().value()));
    if (!portHandle) {
      throw FbossError(
          "attempted to configure dstPort: ",
          addedAclEntry->getDstPort().value(),
          " ACL:",
          addedAclEntry->getID());
    }
    fieldOutPort =
        SaiAclEntryTraits::Attributes::FieldOutPort{AclEntryFieldSaiObjectIdT(
            std::make_pair(portHandle->port->adapterKey(), kMaskDontCare))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldL4SrcPort> fieldL4SrcPort{
      std::nullopt};
  if (addedAclEntry->getL4SrcPort()) {
    fieldL4SrcPort =
        SaiAclEntryTraits::Attributes::FieldL4SrcPort{AclEntryFieldU16(
            std::make_pair(
                addedAclEntry->getL4SrcPort().value(), kL4PortMask))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldL4DstPort> fieldL4DstPort{
      std::nullopt};
  if (addedAclEntry->getL4DstPort()) {
    fieldL4DstPort =
        SaiAclEntryTraits::Attributes::FieldL4DstPort{AclEntryFieldU16(
            std::make_pair(
                addedAclEntry->getL4DstPort().value(), kL4PortMask))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldAclRangeType>
      fieldAclRangeType{std::nullopt};
  std::shared_ptr<SaiAclRange> dstPortRangeObj;
  if (addedAclEntry->getL4DstPortRange()) {
    auto range = addedAclEntry->getL4DstPortRange().value();
    dstPortRangeObj = getOrCreateAclRange(
        SAI_ACL_RANGE_TYPE_L4_DST_PORT_RANGE,
        *range.minimum(),
        *range.maximum());
    std::vector<sai_object_id_t> rangeOids{dstPortRangeObj->adapterKey()};
    fieldAclRangeType = SaiAclEntryTraits::Attributes::FieldAclRangeType{
        AclEntryFieldSaiObjectIdList(
            std::make_pair(rangeOids, std::vector<sai_object_id_t>{}))};
  }

  bool matchV4 = !addedAclEntry->getEtherType().has_value() ||
      addedAclEntry->getEtherType().value() == cfg::EtherType::IPv4;
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
  bool matchV6 = !addedAclEntry->getEtherType().has_value() ||
      addedAclEntry->getEtherType().value() == cfg::EtherType::IPv6;
#endif
  std::optional<SaiAclEntryTraits::Attributes::FieldIpProtocol> fieldIpProtocol{
      std::nullopt};

  auto stage = static_cast<sai_acl_stage_t>(
      GET_ATTR(AclTable, Stage, aclTableHandle->aclTable->attributes()));
  auto qualifierSet = getSupportedQualifierSet(stage);
  if (qualifierSet.find(cfg::AclTableQualifier::IP_PROTOCOL_NUMBER) !=
          qualifierSet.end() &&
      matchV4 && addedAclEntry->getProto()) {
    fieldIpProtocol =
        SaiAclEntryTraits::Attributes::FieldIpProtocol{AclEntryFieldU8(
            std::make_pair(
                addedAclEntry->getProto().value(), kIpProtocolMask))};
  }

#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
  std::optional<SaiAclEntryTraits::Attributes::FieldIpv6NextHeader>
      fieldIpv6NextHeader{std::nullopt};
  if (qualifierSet.find(cfg::AclTableQualifier::IPV6_NEXT_HEADER) !=
          qualifierSet.end() &&
      matchV6 && addedAclEntry->getProto()) {
    fieldIpv6NextHeader =
        SaiAclEntryTraits::Attributes::FieldIpv6NextHeader{AclEntryFieldU8(
            std::make_pair(
                addedAclEntry->getProto().value(), kIpv6NextHeaderMask))};
  }
#endif

  std::optional<SaiAclEntryTraits::Attributes::FieldTcpFlags> fieldTcpFlags{
      std::nullopt};
  if (addedAclEntry->getTcpFlagsBitMap()) {
    fieldTcpFlags =
        SaiAclEntryTraits::Attributes::FieldTcpFlags{AclEntryFieldU8(
            std::make_pair(
                addedAclEntry->getTcpFlagsBitMap().value(),
                addedAclEntry->getTcpFlagsMask().value_or(kTcpFlagsMask)))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldIpFrag> fieldIpFrag{
      std::nullopt};
  if (addedAclEntry->getIpFrag()) {
    auto ipFragData = cfgIpFragToSaiIpFrag(addedAclEntry->getIpFrag().value());
    fieldIpFrag = SaiAclEntryTraits::Attributes::FieldIpFrag{
        AclEntryFieldU32(std::make_pair(ipFragData, kMaskDontCare))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldIcmpV4Type> fieldIcmpV4Type{
      std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldIcmpV4Code> fieldIcmpV4Code{
      std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldIcmpV6Type> fieldIcmpV6Type{
      std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldIcmpV6Code> fieldIcmpV6Code{
      std::nullopt};
  if (addedAclEntry->getIcmpType()) {
    if ((addedAclEntry->getProto() &&
         addedAclEntry->getProto().value() == AclEntry::kProtoIcmp) ||
        (addedAclEntry->getEtherType() &&
         addedAclEntry->getEtherType().value() == cfg::EtherType::IPv4)) {
      fieldIcmpV4Type =
          SaiAclEntryTraits::Attributes::FieldIcmpV4Type{AclEntryFieldU8(
              std::make_pair(
                  addedAclEntry->getIcmpType().value(), kIcmpTypeMask))};
      if (addedAclEntry->getIcmpCode()) {
        fieldIcmpV4Code =
            SaiAclEntryTraits::Attributes::FieldIcmpV4Code{AclEntryFieldU8(
                std::make_pair(
                    addedAclEntry->getIcmpCode().value(), kIcmpCodeMask))};
      }
    } else if (
        (addedAclEntry->getProto() &&
         addedAclEntry->getProto().value() == AclEntry::kProtoIcmpv6) ||
        (addedAclEntry->getEtherType() &&
         addedAclEntry->getEtherType().value() == cfg::EtherType::IPv6)) {
      fieldIcmpV6Type =
          SaiAclEntryTraits::Attributes::FieldIcmpV6Type{AclEntryFieldU8(
              std::make_pair(
                  addedAclEntry->getIcmpType().value(), kIcmpTypeMask))};
      if (addedAclEntry->getIcmpCode()) {
        fieldIcmpV6Code =
            SaiAclEntryTraits::Attributes::FieldIcmpV6Code{AclEntryFieldU8(
                std::make_pair(
                    addedAclEntry->getIcmpCode().value(), kIcmpCodeMask))};
      }
    } else {
      throw FbossError(
          "proto or etherType not sepcified in ACL when matching icmp type/code");
    }
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldDscp> fieldDscp{
      std::nullopt};
  if (addedAclEntry->getDscp()) {
    fieldDscp = SaiAclEntryTraits::Attributes::FieldDscp{AclEntryFieldU8(
        std::make_pair(addedAclEntry->getDscp().value(), kDscpMask))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldTc> fieldTc{std::nullopt};
  if (addedAclEntry->getTrafficClass()) {
    fieldTc = SaiAclEntryTraits::Attributes::FieldTc{AclEntryFieldU8(
        std::make_pair(addedAclEntry->getTrafficClass().value(), kTcMask))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldDstMac> fieldDstMac{
      std::nullopt};
  if (addedAclEntry->getDstMac()) {
    fieldDstMac = SaiAclEntryTraits::Attributes::FieldDstMac{AclEntryFieldMac(
        std::make_pair(addedAclEntry->getDstMac().value(), kMacMask()))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldIpType> fieldIpType{
      std::nullopt};
  if (addedAclEntry->getIpType()) {
    auto ipTypeData = cfgIpTypeToSaiIpType(addedAclEntry->getIpType().value());
    fieldIpType = SaiAclEntryTraits::Attributes::FieldIpType{
        AclEntryFieldU32(std::make_pair(ipTypeData, kMaskDontCare))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldTtl> fieldTtl{std::nullopt};
  if (addedAclEntry->getTtl()) {
    fieldTtl = SaiAclEntryTraits::Attributes::FieldTtl{AclEntryFieldU8(
        std::make_pair(
            addedAclEntry->getTtl().value().getValue(),
            addedAclEntry->getTtl().value().getMask()))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldMplsLabel0Ttl>
      fieldMplsLabel0Ttl{std::nullopt};
  if (addedAclEntry->getMplsLabel0Ttl()) {
    if (!platform_->getAsic()->isSupported(
            HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL)) {
      throw FbossError(
          "MPLS label0 TTL ACL qualifier is not supported on this SDK, acl: ",
          addedAclEntry->getID());
    }
    fieldMplsLabel0Ttl =
        SaiAclEntryTraits::Attributes::FieldMplsLabel0Ttl{AclEntryFieldU8(
            std::make_pair(
                addedAclEntry->getMplsLabel0Ttl().value().getValue(),
                addedAclEntry->getMplsLabel0Ttl().value().getMask()))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldRouteDstUserMeta>
      fieldRouteDstUserMeta{std::nullopt};
  if (addedAclEntry->getLookupClassRoute()) {
    fieldRouteDstUserMeta =
        SaiAclEntryTraits::Attributes::FieldRouteDstUserMeta{
            AclEntryFieldU32(cfgLookupClassToSaiRouteMetaDataAndMask(
                addedAclEntry->getLookupClassRoute().value()))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldNeighborDstUserMeta>
      fieldNeighborDstUserMeta{std::nullopt};
  if (addedAclEntry->getLookupClassNeighbor()) {
    fieldNeighborDstUserMeta =
        SaiAclEntryTraits::Attributes::FieldNeighborDstUserMeta{
            AclEntryFieldU32(cfgLookupClassToSaiNeighborMetaDataAndMask(
                addedAclEntry->getLookupClassNeighbor().value()))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldPortUserMeta>
      fieldPortUserMeta{std::nullopt};
  if (auto lookupClassPort = addedAclEntry->getLookupClassPort()) {
    fieldPortUserMeta =
        SaiAclEntryTraits::Attributes::FieldPortUserMeta{AclEntryFieldU32(
            cfgLookupClassToSaiPortMetaDataAndMask(*lookupClassPort))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldEthertype> fieldEtherType{
      std::nullopt};
  if (addedAclEntry->getEtherType()) {
    auto etherTypeData =
        cfgEtherTypeToSaiEtherType(addedAclEntry->getEtherType().value());
    fieldEtherType = SaiAclEntryTraits::Attributes::FieldEthertype{
        AclEntryFieldU16(std::make_pair(etherTypeData, kEtherTypeMask))};
  }

  std::optional<SaiAclEntryTraits::Attributes::FieldOuterVlanId>
      fieldOuterVlanId{std::nullopt};
  if (addedAclEntry->getVlanID()) {
    fieldOuterVlanId =
        SaiAclEntryTraits::Attributes::FieldOuterVlanId{AclEntryFieldU16(
            std::make_pair(
                addedAclEntry->getVlanID().value(), kOuterVlanIdMask))};
  }

#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
  std::optional<SaiAclEntryTraits::Attributes::FieldBthOpcode> fieldBthOpcode{
      std::nullopt};
  if (addedAclEntry->getRoceOpcode()) {
    fieldBthOpcode =
        SaiAclEntryTraits::Attributes::FieldBthOpcode{AclEntryFieldU8(
            std::make_pair(
                addedAclEntry->getRoceOpcode().value(), kBthOpcodeMask))};
  }
#endif

  std::optional<SaiAclEntryTraits::Attributes::FieldFdbDstUserMeta>
      fieldFdbDstUserMeta{std::nullopt};
  if (addedAclEntry->getLookupClassL2()) {
    fieldFdbDstUserMeta = SaiAclEntryTraits::Attributes::FieldFdbDstUserMeta{
        AclEntryFieldU32(cfgLookupClassToSaiFdbMetaDataAndMask(
            addedAclEntry->getLookupClassL2().value()))};
  }

#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
  std::optional<AclEntryUdfGroup0> userDefinedGroup0{std::nullopt};
  std::optional<AclEntryUdfGroup1> userDefinedGroup1{std::nullopt};
  std::optional<AclEntryUdfGroup2> userDefinedGroup2{std::nullopt};
  std::optional<AclEntryUdfGroup3> userDefinedGroup3{std::nullopt};
  std::optional<AclEntryUdfGroup4> userDefinedGroup4{std::nullopt};

  updateUdfGroupAttributes(
      addedAclEntry,
      aclTableName,
      userDefinedGroup0,
      userDefinedGroup1,
      userDefinedGroup2,
      userDefinedGroup3,
      userDefinedGroup4);
#endif

  // TODO(skhare) Support all other ACL actions
  std::optional<SaiAclEntryTraits::Attributes::ActionPacketAction>
      aclActionPacketAction{std::nullopt};
  /*
   * FBOSS ACL action -> SAI packet action:
   *  - PERMIT: leave the pipeline's forwarding decision alone (FORWARD),
   *    except on a PBR entry whose redirect next-hop group resolved, where
   *    FORWARD conflicts with the redirect action.
   *  - DENY: drop the packet (DROP). A copy to CPU that a lower priority ACL
   *    or a host interface trap asked for still happens.
   *  - DENY_DATA_AND_CONTROL_PLANE: drop the packet and cancel that copy to CPU
   * (DENY, which the SAI spec defines as COPY_CANCEL plus DROP).
   */
  switch (addedAclEntry->getActionType()) {
    case cfg::AclActionType::DENY:
      aclActionPacketAction = SaiAclEntryTraits::Attributes::ActionPacketAction{
          SAI_PACKET_ACTION_DROP};
      break;
    case cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE:
      aclActionPacketAction = SaiAclEntryTraits::Attributes::ActionPacketAction{
          SAI_PACKET_ACTION_DENY};
      break;
    case cfg::AclActionType::PERMIT:
      aclActionPacketAction = SaiAclEntryTraits::Attributes::ActionPacketAction{
          SAI_PACKET_ACTION_FORWARD};
      break;
  }

  std::optional<SaiAclEntryTraits::Attributes::ActionRedirect>
      aclActionRedirect{std::nullopt};
  std::shared_ptr<SaiObject<SaiTunnelEncapNextHopTraits>> tunnelEncapNextHop{
      nullptr};
  std::shared_ptr<SaiNextHopGroupHandle> matchNhgHandle{nullptr};
  std::shared_ptr<SaiNextHopGroupHandle> redirectNhgHandle{nullptr};

  std::shared_ptr<SaiAclCounter> saiAclCounter{nullptr};
  std::vector<std::pair<cfg::CounterType, std::string>> aclCounterTypeAndName;
  std::optional<SaiAclEntryTraits::Attributes::ActionCounter> aclActionCounter{
      std::nullopt};

  std::optional<SaiAclEntryTraits::Attributes::ActionSetTC> aclActionSetTC{
      std::nullopt};

  std::optional<SaiAclEntryTraits::Attributes::ActionSetDSCP> aclActionSetDSCP{
      std::nullopt};

  std::optional<SaiAclEntryTraits::Attributes::ActionMirrorIngress>
      aclActionMirrorIngress{};

  std::optional<SaiAclEntryTraits::Attributes::ActionMirrorEgress>
      aclActionMirrorEgress{};

  std::optional<std::string> ingressMirror{std::nullopt};
  std::optional<std::string> egressMirror{std::nullopt};
  std::shared_ptr<SaiHostifUserDefinedTrapHandle> userDefinedTrap{nullptr};

  std::optional<SaiAclEntryTraits::Attributes::ActionMacsecFlow>
      aclActionMacsecFlow{std::nullopt};

#if !defined(TAJO_SDK)
  std::optional<SaiAclEntryTraits::Attributes::ActionSetUserTrap>
      aclActionSetUserTrap{std::nullopt};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<SaiAclEntryTraits::Attributes::ActionSetArsObject>
      aclActionSetArsObject{std::nullopt};
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  std::optional<SaiAclEntryTraits::Attributes::ActionDisableArsForwarding>
      aclActionDisableArsForwarding{std::nullopt};
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<SaiAclEntryTraits::Attributes::ActionSetEcmpHashAlgorithm>
      aclActionSetEcmpHashAlgorithm{std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::ActionL3SwitchCancel>
      aclActionL3SwitchCancel{std::nullopt};
  std::optional<SaiAclEntryTraits::Attributes::FieldRouteDestination>
      aclFieldRouteDestination{std::nullopt};
  if (addedAclEntry->getNextHopGroupId()) {
    if (!SaiAclEntryTraits::Attributes::FieldRouteDestination::
             optionalExtensionAttributeId()
                 .has_value()) {
      throw FbossError(
          "PBR ACL entry ",
          addedAclEntry->getID(),
          " matches on a next-hop-group but this SDK does not implement the "
          "route-dst ACL extension");
    }
    matchNhgHandle = resolvePbrNextHopGroup(
        state, *addedAclEntry->getNextHopGroupId(), addedAclEntry->getID());
    if (!matchNhgHandle) {
      // No SAI next-hop-group to match on, either because the NHG resolved to
      // no next hops or because none of them are on this ASIC. Routes resolving
      // to it are already DROP, so skip programming this PBR entry here.
      XLOG(DBG2) << "skip PBR acl entry " << addedAclEntry->getID()
                 << ": match next-hop-group is unresolved on this ASIC";
      return AclEntrySaiId{0};
    }
    aclFieldRouteDestination =
        SaiAclEntryTraits::Attributes::FieldRouteDestination{
            AclEntryFieldSaiObjectIdT(
                std::make_pair(
                    matchNhgHandle->nextHopGroup->adapterKey(),
                    kMaskDontCare))};
  }
#else
  if (addedAclEntry->getNextHopGroupId()) {
    throw FbossError(
        "PBR ACL entry ",
        addedAclEntry->getID(),
        " matches on a next-hop-group but SAI FieldRouteDestination requires "
        "SAI_API_VERSION >= 1.16.0");
  }
#endif

  auto action = addedAclEntry->getAclAction();
  if (action) {
    // THRIFT_COPY
    auto matchAction = MatchAction::fromThrift(action->toThrift());
    if (matchAction.getTrafficCounter()) {
      std::tie(saiAclCounter, aclCounterTypeAndName) = addAclCounter(
          aclTableHandle,
          matchAction.getTrafficCounter().value(),
          adapterHostKey);
      aclActionCounter = SaiAclEntryTraits::Attributes::ActionCounter{
          AclEntryActionSaiObjectIdT(
              AclCounterSaiId{saiAclCounter->adapterKey()})};
    }
    // SaiAclTableManager assumes queueId is always equal to tcVal when
    // programming aclActionSetTC. We will remove this assumption by directly
    // using the tc value from setTcAction instead. Before changing prod
    // config to use setTc, we still need to keep sendToQueueAction logics
    // temporaily.
    std::optional<sai_uint8_t> tc = std::nullopt;
    std::optional<bool> sendToCpu = std::nullopt;
    if (matchAction.getSendToQueue()) {
      auto sendToQueue = matchAction.getSendToQueue().value();
      tc = static_cast<sai_uint8_t>(*sendToQueue.first.queueId());
      sendToCpu = sendToQueue.second;
    }
    if (matchAction.getSetTc()) {
      auto setTc = matchAction.getSetTc().value();
      tc = static_cast<sai_uint8_t>(*setTc.first.tcValue());
      if (sendToCpu != std::nullopt && sendToCpu != setTc.second) {
        throw FbossError(
            "Inconsistent actions between sendToQueue with sendToCpu ",
            (sendToCpu ? "true" : "false"),
            " and setTc with sendToCpu ",
            (setTc.second ? "true" : "false"));
      }
      sendToCpu = setTc.second;
    }

    auto setCopyOrTrap =
        [&aclActionPacketAction](const MatchAction& matchAction) {
          if (matchAction.getToCpuAction()) {
            switch (matchAction.getToCpuAction().value()) {
              case cfg::ToCpuAction::COPY:
                aclActionPacketAction =
                    SaiAclEntryTraits::Attributes::ActionPacketAction{
                        SAI_PACKET_ACTION_COPY};
                break;
              case cfg::ToCpuAction::TRAP:
                aclActionPacketAction =
                    SaiAclEntryTraits::Attributes::ActionPacketAction{
                        SAI_PACKET_ACTION_TRAP};
                break;
            }
          }
        };

    if (tc != std::nullopt) {
      if (!sendToCpu) {
        aclActionSetTC = SaiAclEntryTraits::Attributes::ActionSetTC{
            AclEntryActionU8(tc.value())};
      } else {
        /*
         * When sendToCpu is set, a copy of the packet will be sent
         * to CPU.
         * By default, these packets are sent to queue 0.
         * Set TC to set the right traffic class which
         * will be mapped to queue id.
         *
         * TODO(skhare)
         * By default, BCM maps TC i to Queue i for i in [0, 9].
         * Tajo claims to map TC i to Queue i by default as well.
         * However, explicitly set the QoS Map and associate with the CPU port.
         */
        setCopyOrTrap(matchAction);
        aclActionSetTC = SaiAclEntryTraits::Attributes::ActionSetTC{
            AclEntryActionU8(tc.value())};
      }
    }

    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::SAI_USER_DEFINED_TRAP) &&
        FLAGS_sai_user_defined_trap) {
      // Some platform requires implementing ACL copy/trap to cpu action
      // along with user defined trap action mapping to the desrited cpu
      // queue, so as to make ACL rule take precedence over hostif trap
      // rule.
#if !defined(TAJO_SDK)
      if (matchAction.getUserDefinedTrap()) {
        auto queueId = *matchAction.getUserDefinedTrap().value().queueId();
        if (tc != std::nullopt && tc != queueId) {
          throw FbossError(
              "Inconsistent ACL action between set tc value",
              tc.value(),
              " and set user defined trap with queue id ",
              queueId);
        }
        userDefinedTrap =
            managerTable_->hostifManager().ensureHostifUserDefinedTrap(queueId);
        aclActionSetUserTrap = SaiAclEntryTraits::Attributes::ActionSetUserTrap{
            AclEntryActionSaiObjectIdT(userDefinedTrap->trap->adapterKey())};
        setCopyOrTrap(matchAction);
      }
#endif
    }

    if (matchAction.getIngressMirror().has_value()) {
      ingressMirror = matchAction.getIngressMirror().value();
      std::vector<sai_object_id_t> aclEntryMirrorIngressOidList;
      auto mirrorHandle = managerTable_->mirrorManager().getMirrorHandle(
          matchAction.getIngressMirror().value());
      if (mirrorHandle) {
        aclEntryMirrorIngressOidList.push_back(mirrorHandle->adapterKey());
        aclActionMirrorIngress =
            SaiAclEntryTraits::Attributes::ActionMirrorIngress{
                AclEntryActionSaiObjectIdList(aclEntryMirrorIngressOidList)};
      }
    }

    if (matchAction.getEgressMirror().has_value()) {
      egressMirror = matchAction.getEgressMirror().value();
      std::vector<sai_object_id_t> aclEntryMirrorEgressOidList;
      auto mirrorHandle = managerTable_->mirrorManager().getMirrorHandle(
          matchAction.getEgressMirror().value());
      if (mirrorHandle) {
        aclEntryMirrorEgressOidList.push_back(mirrorHandle->adapterKey());
        aclActionMirrorEgress =
            SaiAclEntryTraits::Attributes::ActionMirrorEgress{
                AclEntryActionSaiObjectIdList(aclEntryMirrorEgressOidList)};
      }
    }

    if (matchAction.getSetDscp()) {
      const int dscpValue = *matchAction.getSetDscp().value().dscpValue();

      aclActionSetDSCP = SaiAclEntryTraits::Attributes::ActionSetDSCP{
          AclEntryActionU8(dscpValue)};
    }

    if (matchAction.getMacsecFlow()) {
      auto macsecFlowAction = matchAction.getMacsecFlow().value();
      if (*macsecFlowAction.action() ==
          cfg::MacsecFlowPacketAction::MACSEC_FLOW) {
        sai_object_id_t flowId =
            static_cast<sai_object_id_t>(*macsecFlowAction.flowId());
        aclActionMacsecFlow = SaiAclEntryTraits::Attributes::ActionMacsecFlow{
            AclEntryActionSaiObjectIdT(flowId)};
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
        // MACSec flow action should not be set along with regulation packet
        // action otherwise packet action gets priority
        aclActionPacketAction = std::nullopt;
#endif
      } else if (
          *macsecFlowAction.action() == cfg::MacsecFlowPacketAction::FORWARD) {
        aclActionPacketAction =
            SaiAclEntryTraits::Attributes::ActionPacketAction{
                SAI_PACKET_ACTION_FORWARD};
      } else if (
          *macsecFlowAction.action() == cfg::MacsecFlowPacketAction::DROP) {
        aclActionPacketAction =
            SaiAclEntryTraits::Attributes::ActionPacketAction{
                SAI_PACKET_ACTION_DROP};
      } else {
        throw FbossError(
            "Unsupported Macsec Flow action for ACL entry: ",
            addedAclEntry->getID(),
            " Macsec Flow action ",
            apache::thrift::util::enumNameSafe(*macsecFlowAction.action()));
      }
    }

    if (matchAction.getEcmpHashAction()) {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      auto ecmpHashAction = matchAction.getEcmpHashAction().value();
      switch (*ecmpHashAction.switchingMode()) {
        case cfg::SwitchingMode::FIXED_ASSIGNMENT:
          aclActionSetEcmpHashAlgorithm =
              SaiAclEntryTraits::Attributes::ActionSetEcmpHashAlgorithm{
                  SAI_HASH_ALGORITHM_NONE};
          break;
        default:
          throw FbossError(
              "Unsupported ecmp hash action", *ecmpHashAction.switchingMode());
      }
#endif
    }

    /*
     * Chenab supports
     *  - Set ARS object
     *  - Disable ARS forwarding - only true
     * BCM supports
     *  - Disable ARS forwarding - only false
     */
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    if (FLAGS_flowletSwitchingEnable &&
        platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
      if (matchAction.getFlowletAction().has_value()) {
        auto flowletAction = matchAction.getFlowletAction().value();
        switch (flowletAction) {
          case cfg::FlowletAction::FORWARD: {
#if defined(CHENAB_SAI_SDK)
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
            auto arsHandlePtr = managerTable_->arsManager().getArsHandle();
            if (arsHandlePtr->ars) {
              aclActionSetArsObject =
                  SaiAclEntryTraits::Attributes::ActionSetArsObject{
                      AclEntryActionSaiObjectIdT(
                          arsHandlePtr->ars->adapterKey())};
            }
#endif
#else
            aclActionDisableArsForwarding =
                SaiAclEntryTraits::Attributes::ActionDisableArsForwarding{
                    false};
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
            auto arsProfileHandle =
                managerTable_->arsProfileManager().getArsProfileHandle();
            // A DLB eligible packet hits entries in both IFP groups, and
            // DynamicEcmpEnable and L3Switch are not mutually exclusive, so
            // both actions get applied. Without the cancel the packet then
            // egresses through the static ECMP group rather than the DLB one,
            // and split horizon on the ARS group never comes into play.
            const bool arsSplitHorizonEnabled =
                managerTable_->nextHopGroupManager().isSplitHorizonEnabled(
                    cfg::EcmpGroupType::ARS);
            if ((arsProfileHandle &&
                 arsProfileHandle->arsVirtualGroupsEnabled) ||
                arsSplitHorizonEnabled) {
              aclActionL3SwitchCancel =
                  SaiAclEntryTraits::Attributes::ActionL3SwitchCancel{true};
            }
#endif
#endif
          } break;
          case cfg::FlowletAction::DISABLE:
            aclActionDisableArsForwarding =
                SaiAclEntryTraits::Attributes::ActionDisableArsForwarding{true};
            break;
        }
      }
    }
#endif

    if (auto redirectNhgId = matchAction.getRedirectNextHopGroupId()) {
      redirectNhgHandle =
          resolvePbrNextHopGroup(state, *redirectNhgId, addedAclEntry->getID());
      if (redirectNhgHandle) {
        aclActionRedirect = SaiAclEntryTraits::Attributes::ActionRedirect{
            AclEntryActionSaiObjectIdT(
                NextHopGroupSaiId{
                    redirectNhgHandle->nextHopGroup->adapterKey()})};
        // The redirect already forwards the packet, and the SDK rejects
        // FORWARD alongside it as a conflicting action.
        if (aclActionPacketAction.has_value() &&
            aclActionPacketAction->value().getData() ==
                SAI_PACKET_ACTION_FORWARD) {
          aclActionPacketAction.reset();
        }
      } else {
        // Redirect NHG has no resolved member. Setting action to drop as the
        // packets cannot be forwarded.
        // In the case of the redirect group losing the last resolved member
        // (e.g. link down), the fast-ECMP shrink path will remove the last
        // member, therefore the ACL is still redirecting to an empty ECMP
        // group.
        // When the next update comes (e.g. via BGP/openR updates),
        // redirectNhgHandle will become nullptr and hence the action will be
        // updated to drop.
        // TODO(zecheng): Fail hard if there is PBR rule for multiple hw agent
        XLOG(DBG2) << "PBR acl entry " << addedAclEntry->getID()
                   << ": redirect next-hop-group " << *redirectNhgId
                   << " has no resolved member, setting action to drop";
        aclActionPacketAction =
            SaiAclEntryTraits::Attributes::ActionPacketAction{
                SAI_PACKET_ACTION_DROP};
      }
    } else if (matchAction.getRedirectToNextHop()) {
      auto& [redirectAction, resolvedNexthops] =
          matchAction.getRedirectToNextHop().value();
      for (const auto& nhStruct : *redirectAction.redirectNextHops()) {
        if (nhStruct.tunnelType().has_value() &&
            nhStruct.tunnelType().value() == TunnelType::IP_IN_IP_ENCAP &&
            nhStruct.tunnelId().has_value()) {
          auto tunnelHandle = managerTable_->tunnelManager().getTunnelHandle(
              nhStruct.tunnelId().value());
          if (!tunnelHandle) {
            throw FbossError(
                "Missing tunnel for redirect: ", nhStruct.tunnelId().value());
          }
          auto tunnelSaiId = tunnelHandle->tunnel->adapterKey();
          auto nhIp = folly::IPAddress(*nhStruct.ip());
          SaiTunnelEncapNextHopTraits::AdapterHostKey nhKey{tunnelSaiId, nhIp};
          SaiTunnelEncapNextHopTraits::CreateAttributes nhAttrs{
              SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP,
              tunnelSaiId,
              nhIp,
              std::nullopt,
              std::nullopt};
          auto& nhStore = saiStore_->get<SaiTunnelEncapNextHopTraits>();
          tunnelEncapNextHop = nhStore.setObject(nhKey, nhAttrs);
          aclActionRedirect = SaiAclEntryTraits::Attributes::ActionRedirect{
              AclEntryActionSaiObjectIdT(
                  NextHopSaiId{tunnelEncapNextHop->adapterKey()})};
          break;
        }
      }
    }
  }

  // TODO(skhare) At least one field and one action must be specified.
  // Once we add support for all fields and actions, throw error if that is
  // not honored.
  auto matcherIsValid =
      (fieldSrcIpV6.has_value() || fieldDstIpV6.has_value() ||
       fieldDstIpV6Word3.has_value() || fieldDstIpV6Word2.has_value() ||
       fieldSrcIpV4.has_value() || fieldDstIpV4.has_value() ||
       fieldSrcPort.has_value() || fieldOutPort.has_value() ||
       fieldL4SrcPort.has_value() || fieldL4DstPort.has_value() ||
       fieldAclRangeType.has_value() || fieldIpProtocol.has_value() ||
       fieldTcpFlags.has_value() || fieldIpFrag.has_value() ||
       fieldIcmpV4Type.has_value() || fieldIcmpV4Code.has_value() ||
       fieldIcmpV6Type.has_value() || fieldIcmpV6Code.has_value() ||
       fieldDscp.has_value() || fieldTc.has_value() ||
       fieldDstMac.has_value() || fieldIpType.has_value() ||
       fieldTtl.has_value() || fieldMplsLabel0Ttl.has_value() ||
       fieldFdbDstUserMeta.has_value() || fieldRouteDstUserMeta.has_value() ||
       fieldEtherType.has_value() || fieldNeighborDstUserMeta.has_value() ||
       fieldPortUserMeta.has_value() || fieldOuterVlanId.has_value() ||
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
       fieldBthOpcode.has_value() ||
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
       fieldIpv6NextHeader.has_value() ||
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
       userDefinedGroup0.has_value() || userDefinedGroup1.has_value() ||
       userDefinedGroup2.has_value() || userDefinedGroup3.has_value() ||
       userDefinedGroup4.has_value() ||
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
       aclFieldRouteDestination.has_value() ||
#endif
       // With EMPTY_ACL_MATCHER, SaiSwitch allows programming an ACL entry
       // with no matchers. Such entries implement match-all ACLs. However,
       // a qualifier SaiSwitch does not support (e.g. PacketLookupResultType)
       // would also yield an entry with no matchers, i.e. a match-all entry,
       // which shadows every entry below it in the table. Avoid that by
       // explicitly disallowing entries where SaiSwitch found no matcher, but
       // a matcher does exist in the SwitchState.
       //
       // TODO(skhare): temporary fix that retains the current SaiSwitch
       // behavior of not programming the mpls-dest-nomatch ACL, which matches
       // on PacketLookupResultType. A subsequent config change will remove
       // this ACL entry altogether. At that time, enhance this into a
       // stricter check that throws an error if SwSwitch attempts to program
       // an ACL with matcher(s) not supported by SaiSwitch.
       (platform_->getAsic()->isSupported(HwAsic::Feature::EMPTY_ACL_MATCHER) &&
        !addedAclEntry->hasMatcher()));
  if ((dstIpV6Word3 || dstIpV6Word2) && !dstIpV6WordQualifiersSupported) {
    throw FbossError(
        "ACL entry ",
        addedAclEntry->getID(),
        " uses DST IPv6 word qualifiers, but ASIC ",
        platform_->getAsic()->getAsicTypeStr(),
        " does not support ACL_DST_IPV6_WORD_QUALIFIERS");
  }
  if (fieldSrcPort.has_value()) {
    auto srcPortQualifierSupported = platform_->getAsic()->isSupported(
        HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER);
#if defined(TAJO_SDK_VERSION_1_42_8)
    srcPortQualifierSupported = false;
#endif
    matcherIsValid &= srcPortQualifierSupported;
  }
  auto actionIsValid =
      (aclActionPacketAction.has_value() || aclActionCounter.has_value() ||
       aclActionSetTC.has_value() || aclActionSetDSCP.has_value() ||
       aclActionMirrorIngress.has_value() ||
       aclActionMirrorEgress.has_value() || aclActionMacsecFlow.has_value() ||
       aclActionRedirect.has_value()
#if !defined(TAJO_SDK)
       || aclActionSetUserTrap.has_value()
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
       || aclActionDisableArsForwarding.has_value()
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
       || aclActionSetArsObject.has_value() ||
       aclActionSetEcmpHashAlgorithm.has_value() ||
       aclActionL3SwitchCancel.has_value()
#endif
      );

  if (!(matcherIsValid && actionIsValid)) {
    XLOG(WARNING) << "Unsupported field/action for aclEntry: "
                  << addedAclEntry->getID() << " MactherValid "
                  << ((matcherIsValid) ? "true" : "false") << " ActionValid "
                  << ((actionIsValid) ? "true" : "false");
    return AclEntrySaiId{0};
  }

  SaiAclEntryTraits::CreateAttributes attributes{
      aclTableId,
      priority,
      true,
      fieldSrcIpV6,
      fieldDstIpV6,
      fieldDstIpV6Word3,
      fieldDstIpV6Word2,
      fieldSrcIpV4,
      fieldDstIpV4,
      fieldSrcPort,
      fieldOutPort,
      fieldL4SrcPort,
      fieldL4DstPort,
      fieldIpProtocol,
      fieldTcpFlags,
      fieldIpFrag,
      fieldIcmpV4Type,
      fieldIcmpV4Code,
      fieldIcmpV6Type,
      fieldIcmpV6Code,
      fieldDscp,
      fieldTc,
      fieldDstMac,
      fieldIpType,
      fieldTtl,
      fieldMplsLabel0Ttl,
      fieldFdbDstUserMeta,
      fieldRouteDstUserMeta,
      fieldNeighborDstUserMeta,
      fieldEtherType,
      fieldOuterVlanId,
      fieldAclRangeType,
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
      fieldBthOpcode,
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
      fieldIpv6NextHeader,
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
      userDefinedGroup0,
      userDefinedGroup1,
      userDefinedGroup2,
      userDefinedGroup3,
      userDefinedGroup4,
#endif
      aclActionPacketAction,
      aclActionRedirect,
      aclActionCounter,
      aclActionSetTC,
      aclActionSetDSCP,
      aclActionMirrorIngress,
      aclActionMirrorEgress,
      aclActionMacsecFlow,
// action not supported by tajo. Besides, user defined trap
// is used to make ACL take precedence over Hostif trap.
// Tajo already supports this behavior
#if !defined(TAJO_SDK)
      aclActionSetUserTrap,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      aclActionSetArsObject,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      aclActionDisableArsForwarding,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      aclActionSetEcmpHashAlgorithm,
      aclActionL3SwitchCancel,
      aclFieldRouteDestination,
#endif
#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
      label,
#endif
      fieldPortUserMeta,
  };

  auto saiAclEntry = aclEntryStore.setObject(adapterHostKey, attributes);
  auto entryHandle = std::make_unique<SaiAclEntryHandle>();
  entryHandle->dstPortRange = dstPortRangeObj;
  entryHandle->aclEntry = saiAclEntry;
  entryHandle->aclEntryName = addedAclEntry->getID();
  entryHandle->aclCounter = saiAclCounter;
  entryHandle->tunnelEncapNextHop = tunnelEncapNextHop;
  entryHandle->matchNhgHandle = matchNhgHandle;
  entryHandle->redirectNhgHandle = redirectNhgHandle;
  entryHandle->aclCounterTypeAndName = aclCounterTypeAndName;
  entryHandle->ingressMirror = ingressMirror;
  entryHandle->egressMirror = egressMirror;
  entryHandle->userDefinedTrap = userDefinedTrap;
  auto [it, inserted] = aclTableHandle->aclTableMembers.emplace(
      std::make_pair(addedAclEntry->getPriority(), addedAclEntry->getID()),
      std::move(entryHandle));
  CHECK(inserted);

  XLOG(DBG2) << "added acl entry " << addedAclEntry->getID() << " priority "
             << addedAclEntry->getPriority();

  auto enabled = SaiApiTable::getInstance()->aclApi().getAttribute(
      it->second->aclEntry->adapterKey(),
      SaiAclEntryTraits::Attributes::Enabled{});
  CHECK(enabled) << "Acl entry: " << addedAclEntry->getID() << " not enabled";
  return it->second->aclEntry->adapterKey();
}

void SaiAclTableManager::removeAclEntry(
    const std::shared_ptr<AclEntry>& removedAclEntry,
    const std::string& aclTableName,
    const std::shared_ptr<SwitchState>& /*state*/) {
  // If we attempt to remove entry for a table that does not exist, fail.
  auto aclTableHandle = getAclTableHandle(aclTableName);
  if (!aclTableHandle) {
    throw FbossError(
        "attempted to remove AclEntry to a AclTable that does not exist: ",
        aclTableName);
  }

  // If we attempt to remove entry that does not exist, fail.
  auto itr = aclTableHandle->aclTableMembers.find(
      {removedAclEntry->getPriority(), removedAclEntry->getID()});
  if (itr == aclTableHandle->aclTableMembers.end()) {
    // an acl entry that uses cpu port as qualifier may not have been created
    // even if it exists in switch state.
    XLOG(ERR) << "attempted to remove aclEntry which does not exist: "
              << removedAclEntry->getID();
    return;
  }

  aclTableHandle->aclTableMembers.erase(itr);

  auto action = removedAclEntry->getAclAction();
  if (action && action->cref<switch_state_tags::trafficCounter>()) {
    // THRIFT_COPY
    removeAclCounter(
        action->cref<switch_state_tags::trafficCounter>()->toThrift());
  }
  XLOG(DBG2) << "removed acl  entry " << removedAclEntry->getID()
             << " priority " << removedAclEntry->getPriority();
}

void SaiAclTableManager::removeAclCounter(
    const cfg::TrafficCounter& trafficCount) {
  for (const auto& counterType : *trafficCount.types()) {
    auto statName =
        utility::statNameFromCounterType(*trafficCount.name(), counterType);
    auto entry = aclCounterRefMap_.find(statName);
    if (entry != aclCounterRefMap_.end()) {
      entry->second--;
      if (entry->second == 0) {
        // Counter no longer used. Remove from fb303 counters
        aclStats_.removeStat(statName);
        aclCounterRefMap_.erase(entry);
      }
    } else {
      throw FbossError("Acl counter ", statName, " not found om counter map");
    }
  }
}

void SaiAclTableManager::changedAclEntry(
    const std::shared_ptr<AclEntry>& oldAclEntry,
    const std::shared_ptr<AclEntry>& newAclEntry,
    const std::string& aclTableName,
    const std::shared_ptr<SwitchState>& state) {
  /*
   * ASIC/SAI implementation typically does not allow modifying an ACL entry.
   * Thus, remove and re-add.
   */
  XLOG(DBG2) << "changing acl entry " << oldAclEntry->getID();
  removeAclEntry(oldAclEntry, aclTableName, state);
  addAclEntry(newAclEntry, aclTableName, state);
}

const SaiAclEntryHandle* FOLLY_NULLABLE SaiAclTableManager::getAclEntryHandle(
    const SaiAclTableHandle* aclTableHandle,
    int priority,
    const std::string& aclEntryName) const {
  auto itr = aclTableHandle->aclTableMembers.find({priority, aclEntryName});
  if (itr == aclTableHandle->aclTableMembers.end()) {
    return nullptr;
  }
  if (!itr->second || !itr->second->aclEntry) {
    XLOG(FATAL) << "invalid null Acl entry for: " << aclEntryName;
  }
  return itr->second.get();
}

void SaiAclTableManager::programMirror(
    const SaiAclEntryHandle* aclEntryHandle,
    MirrorDirection direction,
    MirrorAction action,
    const std::optional<std::string>& mirrorId) {
  if (!mirrorId.has_value()) {
    XLOG(DBG) << "mirror session not configured: ";
    return;
  }

  std::vector<sai_object_id_t> mirrorOidList{};
  if (action == MirrorAction::START) {
    auto mirrorHandle =
        managerTable_->mirrorManager().getMirrorHandle(mirrorId.value());
    if (mirrorHandle) {
      mirrorOidList.push_back(mirrorHandle->adapterKey());
    }
  }

  if (direction == MirrorDirection::INGRESS) {
    aclEntryHandle->aclEntry->setOptionalAttribute(
        SaiAclEntryTraits::Attributes::ActionMirrorIngress{mirrorOidList});
  } else {
    aclEntryHandle->aclEntry->setOptionalAttribute(
        SaiAclEntryTraits::Attributes::ActionMirrorEgress{mirrorOidList});
  }
}

void SaiAclTableManager::programMirrorOnAllAcls(
    const std::optional<std::string>& mirrorId,
    MirrorAction action) {
  for (const auto& handle : handles_) {
    for (const auto& aclMember : handle.second->aclTableMembers) {
      if (aclMember.second->getIngressMirror() == mirrorId) {
        programMirror(
            aclMember.second.get(), MirrorDirection::INGRESS, action, mirrorId);
      }
      if (aclMember.second->getEgressMirror() == mirrorId) {
        programMirror(
            aclMember.second.get(), MirrorDirection::EGRESS, action, mirrorId);
      }
    }
  }
}

void SaiAclTableManager::removeUnclaimedAclEntries() {
  auto& aclEntryStore = saiStore_->get<SaiAclEntryTraits>();
  aclEntryStore.removeUnexpectedUnclaimedWarmbootHandles();
}

void SaiAclTableManager::updateStats() {
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  const auto* asic = platform_->getAsic();
  const bool aclBytes = asic->isSupported(HwAsic::Feature::ACL_BYTE_COUNTER);
  const bool pbrBytes =
      asic->isSupported(HwAsic::Feature::PBR_ACL_BYTE_COUNTER);

  for (const auto& handle : handles_) {
    const bool readBytes = aclBytes ||
        (pbrBytes &&
         handle.first == cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE());
    for (const auto& aclMember : handle.second->aclTableMembers) {
      for (const auto& [counterType, counterName] :
           aclMember.second->aclCounterTypeAndName) {
        switch (counterType) {
          case cfg::CounterType::PACKETS: {
            auto counterPackets =
                SaiApiTable::getInstance()->aclApi().getAttribute(
                    aclMember.second->aclCounter->adapterKey(),
                    SaiAclCounterTraits::Attributes::CounterPackets());
            aclStats_.updateStat(now, counterName, counterPackets);
          } break;
          case cfg::CounterType::BYTES: {
            if (!readBytes) {
              break;
            }
            auto counterBytes =
                SaiApiTable::getInstance()->aclApi().getAttribute(
                    aclMember.second->aclCounter->adapterKey(),
                    SaiAclCounterTraits::Attributes::CounterBytes());
            aclStats_.updateStat(now, counterName, counterBytes);
          } break;
          default:
            throw FbossError("Unsupported CounterType for ACL");
        }
      }
    }
  }
}

// Loop through all the ACL tables and compute the free entries and counters
std::pair<int32_t, int32_t> SaiAclTableManager::getAclResourceUsage() {
  int32_t aclEntriesFree = 0, aclCountersFree = 0;
  for (const auto& handle : handles_) {
    auto aclTableHandle = handle.second.get();
    auto aclTableId = aclTableHandle->aclTable->adapterKey();
    auto& aclApi = SaiApiTable::getInstance()->aclApi();

    aclEntriesFree += aclApi.getAttribute(
        aclTableId, SaiAclTableTraits::Attributes::AvailableEntry{});
    aclCountersFree += aclApi.getAttribute(
        aclTableId, SaiAclTableTraits::Attributes::AvailableCounter{});
  }
  return std::make_pair(aclEntriesFree, aclCountersFree);
}

std::set<cfg::AclTableQualifier> SaiAclTableManager::getSupportedQualifierSet(
    cfg::AclStage stage) const {
  return getSupportedQualifierSet(
      SaiAclTableGroupManager::cfgAclStageToSaiAclStage(stage));
}

std::set<cfg::AclTableQualifier> SaiAclTableManager::getSupportedQualifierSet(
    sai_acl_stage_t aclStage) const {
  if (aclStage == SAI_ACL_STAGE_EGRESS &&
      !platform_->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_POST_LOOKUP_ACL_TABLE)) {
    throw FbossError("egress acl table is not supported on switch asic");
  }
  /*
   * Not all the qualifiers are supported by every ASIC.
   * Moreover, different ASICs have different max key widths.
   * Thus, enabling all the supported qualifiers in the same ACL Table could
   * overflow the max key width.
   *
   * Thus, only enable a susbet of supported qualifiers based on the ASIC
   * capability.
   */
  bool isTajo = platform_->getAsic()->getAsicVendor() ==
      HwAsic::AsicVendor::ASIC_VENDOR_TAJO;
  bool isJericho2 =
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO2;
  bool isJericho3 =
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3;
  bool isJericho4 =
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO4;
  bool isQumran4d =
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_QUMRAN4D;
  bool isFake =
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_FAKE;
  bool isTomahawk5 =
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK5;
  bool isTomahawk6 =
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK6;
  bool isChenab = platform_->getAsic()->getAsicVendor() ==
      HwAsic::AsicVendor::ASIC_VENDOR_CHENAB;

  if (isTajo) {
    std::set<cfg::AclTableQualifier> tajoQualifiers = {
        cfg::AclTableQualifier::SRC_IPV6,
        cfg::AclTableQualifier::DST_IPV6,
        cfg::AclTableQualifier::SRC_IPV4,
        cfg::AclTableQualifier::DST_IPV4,
        cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
        cfg::AclTableQualifier::DSCP,
        cfg::AclTableQualifier::IP_TYPE,
        cfg::AclTableQualifier::TTL,
        cfg::AclTableQualifier::LOOKUP_CLASS_L2,
        cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR,
        cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE};

#if defined(TAJO_SDK_GTE_24_8_3001)
    std::vector<cfg::AclTableQualifier> tajoExtraQualifierList = {
        cfg::AclTableQualifier::ETHER_TYPE,
        cfg::AclTableQualifier::BTH_OPCODE,
        cfg::AclTableQualifier::SRC_PORT,
        cfg::AclTableQualifier::L4_SRC_PORT,
        cfg::AclTableQualifier::L4_DST_PORT,
        cfg::AclTableQualifier::TCP_FLAGS,
        cfg::AclTableQualifier::IP_FRAG,
        cfg::AclTableQualifier::ICMPV4_TYPE,
        cfg::AclTableQualifier::ICMPV4_CODE,
        cfg::AclTableQualifier::ICMPV6_TYPE,
        cfg::AclTableQualifier::ICMPV6_CODE,
        cfg::AclTableQualifier::DST_MAC,
    };
    for (const auto& qualifier : tajoExtraQualifierList) {
      tajoQualifiers.insert(qualifier);
    }
#endif
    return tajoQualifiers;
  } else if (isJericho2) {
    // TODO(skhare)
    // Extend this list once the SAI implementation supports more qualifiers
    std::set<cfg::AclTableQualifier> jericho2Qualifiers = {
        cfg::AclTableQualifier::SRC_IPV6,
        cfg::AclTableQualifier::DST_IPV6,
        cfg::AclTableQualifier::SRC_PORT,
        cfg::AclTableQualifier::DSCP,
        cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
        cfg::AclTableQualifier::IP_TYPE,
        cfg::AclTableQualifier::TTL,
    };
    return jericho2Qualifiers;
  } else if (isJericho3 || isJericho4 || isQumran4d) {
    std::set<cfg::AclTableQualifier> jericho3Qualifiers = {
        cfg::AclTableQualifier::ETHER_TYPE,
        cfg::AclTableQualifier::SRC_IPV6,
        cfg::AclTableQualifier::DST_IPV6,
        cfg::AclTableQualifier::SRC_IPV4,
        cfg::AclTableQualifier::DST_IPV4,
        cfg::AclTableQualifier::SRC_PORT,
        cfg::AclTableQualifier::DSCP,
        cfg::AclTableQualifier::TTL,
        cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
        cfg::AclTableQualifier::IPV6_NEXT_HEADER,
        cfg::AclTableQualifier::IP_TYPE,
        cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR,
        cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE,
        cfg::AclTableQualifier::L4_SRC_PORT,
        cfg::AclTableQualifier::L4_DST_PORT,
        cfg::AclTableQualifier::ICMPV4_TYPE,
        cfg::AclTableQualifier::ICMPV4_CODE,
        cfg::AclTableQualifier::ICMPV6_TYPE,
        cfg::AclTableQualifier::ICMPV6_CODE,
        cfg::AclTableQualifier::DST_MAC,
        cfg::AclTableQualifier::BTH_OPCODE};
    return jericho3Qualifiers;
  } else if (isChenab) {
    /* TODO(pshaikh): review the qualifiers */
    if (aclStage == SAI_ACL_STAGE_INGRESS) {
      // full set of qualifiers supported but cant fit in single acl table
      return {
          cfg::AclTableQualifier::DST_IPV6,
          cfg::AclTableQualifier::DST_IPV4,
          cfg::AclTableQualifier::SRC_IPV6,
          cfg::AclTableQualifier::SRC_IPV4,
          cfg::AclTableQualifier::L4_SRC_PORT,
          cfg::AclTableQualifier::L4_DST_PORT,
          cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
          cfg::AclTableQualifier::IPV6_NEXT_HEADER,
          cfg::AclTableQualifier::SRC_PORT,
          cfg::AclTableQualifier::DSCP,
          cfg::AclTableQualifier::TTL,
          cfg::AclTableQualifier::IP_TYPE,
          cfg::AclTableQualifier::ETHER_TYPE,
          cfg::AclTableQualifier::OUTER_VLAN,
          cfg::AclTableQualifier::ICMPV4_TYPE,
          cfg::AclTableQualifier::ICMPV4_CODE,
          cfg::AclTableQualifier::ICMPV6_TYPE,
          cfg::AclTableQualifier::ICMPV6_CODE,
          cfg::AclTableQualifier::UDF,
      };
    } else {
      return {
          cfg::AclTableQualifier::DSCP,
          cfg::AclTableQualifier::OUT_PORT,
          cfg::AclTableQualifier::LOOKUP_CLASS_L2,
          cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE,
      };
    }
  } else {
    std::set<cfg::AclTableQualifier> bcmQualifiers = {
        cfg::AclTableQualifier::SRC_IPV6,
        cfg::AclTableQualifier::DST_IPV6,
        cfg::AclTableQualifier::SRC_IPV4,
        cfg::AclTableQualifier::DST_IPV4,
        cfg::AclTableQualifier::L4_SRC_PORT,
        cfg::AclTableQualifier::L4_DST_PORT,
        cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
        cfg::AclTableQualifier::TCP_FLAGS,
        cfg::AclTableQualifier::SRC_PORT,
        cfg::AclTableQualifier::OUT_PORT,
        cfg::AclTableQualifier::IP_FRAG,
        cfg::AclTableQualifier::ICMPV4_TYPE,
        cfg::AclTableQualifier::ICMPV4_CODE,
        cfg::AclTableQualifier::ICMPV6_TYPE,
        cfg::AclTableQualifier::ICMPV6_CODE,
        cfg::AclTableQualifier::DSCP,
        cfg::AclTableQualifier::DST_MAC,
        cfg::AclTableQualifier::IP_TYPE,
        cfg::AclTableQualifier::TTL,
        cfg::AclTableQualifier::LOOKUP_CLASS_L2,
        cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR,
        cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE};

    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::ACL_DST_IPV6_WORD_QUALIFIERS)) {
      bcmQualifiers.insert(cfg::AclTableQualifier::DST_IPV6_WORD3);
      bcmQualifiers.insert(cfg::AclTableQualifier::DST_IPV6_WORD2);
    }

    // TH5 fails creating ACL table after adding vlan as qualifier with 10.0
    // CS00012342272
    if (isTomahawk5) {
      bcmQualifiers.erase(cfg::AclTableQualifier::OUTER_VLAN);
    }
    // ETHER_TYPE required for Aifm controller packets using 0x88B6
    if (isTomahawk6) {
      bcmQualifiers.insert(cfg::AclTableQualifier::ETHER_TYPE);
      bcmQualifiers.erase(cfg::AclTableQualifier::OUT_PORT);
    }
    if (isFake) {
      bcmQualifiers.insert(cfg::AclTableQualifier::L4_DST_PORT_RANGE);
    }

    return bcmQualifiers;
  }
}

void SaiAclTableManager::addDefaultAclTable(
    cfg::AclStage stage,
    const std::string& name) {
  if (handles_.find(name) != handles_.end()) {
    throw FbossError("default acl table ", name, " already exists.");
  }
  // TODO(saranicholas): set appropriate table priority
  state::AclTableFields aclTableFields{};
  aclTableFields.priority() = 0;
  aclTableFields.id() = name;
  auto table1 = std::make_shared<AclTable>(std::move(aclTableFields));
  addAclTable(table1, stage, nullptr /*state*/);
}

void SaiAclTableManager::removeDefaultAclTable(
    cfg::AclStage stage,
    const std::string& name) {
  if (handles_.find(name) == handles_.end()) {
    return;
  }
  // remove from acl table group
  sai_acl_stage_t saiAclStage =
      SaiAclTableGroupManager::cfgAclStageToSaiAclStage(stage);
  if (platform_->getAsic()->isSupported(HwAsic::Feature::ACL_TABLE_GROUP)) {
    managerTable_->aclTableGroupManager().removeAclTableGroupMember(
        saiAclStage, cfg::AclTableGroupBindPoint::SWITCH, name);
  }
  handles_.erase(cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
}

bool SaiAclTableManager::isQualifierSupported(
    const std::string& aclTableName,
    cfg::AclTableQualifier qualifier) const {
  auto handle = getAclTableHandle(aclTableName);
  if (!handle) {
    throw FbossError("ACL table ", aclTableName, " not found.");
  }
  auto attributes = handle->aclTable->attributes();

  auto hasField = [attributes](auto field) {
    if (!field) {
      return false;
    }
    return field->value();
  };

  using cfg::AclTableQualifier;
  switch (qualifier) {
    case cfg::AclTableQualifier::SRC_IPV6:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldSrcIpV6>>(
              attributes));
    case cfg::AclTableQualifier::DST_IPV6:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldDstIpV6>>(
              attributes));
    case cfg::AclTableQualifier::DST_IPV6_WORD3:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldDstIpV6Word3>>(
              attributes));
    case cfg::AclTableQualifier::DST_IPV6_WORD2:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldDstIpV6Word2>>(
              attributes));
    case cfg::AclTableQualifier::SRC_IPV4:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldSrcIpV4>>(
              attributes));
    case cfg::AclTableQualifier::DST_IPV4:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldDstIpV4>>(
              attributes));
    case cfg::AclTableQualifier::L4_SRC_PORT:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldL4SrcPort>>(
              attributes));
    case cfg::AclTableQualifier::L4_DST_PORT:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldL4DstPort>>(
              attributes));
    case cfg::AclTableQualifier::IP_PROTOCOL_NUMBER:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldIpProtocol>>(
              attributes));
    case cfg::AclTableQualifier::TCP_FLAGS:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldTcpFlags>>(
              attributes));
    case cfg::AclTableQualifier::SRC_PORT:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldSrcPort>>(
              attributes));
    case cfg::AclTableQualifier::OUT_PORT:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldOutPort>>(
              attributes));
    case cfg::AclTableQualifier::IP_FRAG:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldIpFrag>>(
              attributes));
    case cfg::AclTableQualifier::ICMPV4_TYPE:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldIcmpV4Type>>(
              attributes));
    case cfg::AclTableQualifier::ICMPV4_CODE:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldIcmpV4Code>>(
              attributes));
    case cfg::AclTableQualifier::ICMPV6_TYPE:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldIcmpV6Type>>(
              attributes));
    case cfg::AclTableQualifier::ICMPV6_CODE:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldIcmpV6Code>>(
              attributes));
    case cfg::AclTableQualifier::DSCP:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldDscp>>(
              attributes));
    case cfg::AclTableQualifier::DST_MAC:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldDstMac>>(
              attributes));
    case cfg::AclTableQualifier::IP_TYPE:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldIpType>>(
              attributes));
    case cfg::AclTableQualifier::TTL:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldTtl>>(
              attributes));
    case cfg::AclTableQualifier::MPLS_LABEL0_TTL:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldMplsLabel0Ttl>>(
              attributes));
    case cfg::AclTableQualifier::LOOKUP_CLASS_L2:
      return hasField(
          std::get<std::optional<
              SaiAclTableTraits::Attributes::FieldFdbDstUserMeta>>(attributes));
    case cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR:
      return hasField(
          std::get<std::optional<
              SaiAclTableTraits::Attributes::FieldNeighborDstUserMeta>>(
              attributes));
    case cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE:
      return hasField(
          std::get<std::optional<
              SaiAclTableTraits::Attributes::FieldRouteDstUserMeta>>(
              attributes));
    case cfg::AclTableQualifier::LOOKUP_CLASS_PORT:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldPortUserMeta>>(
              attributes));
    case cfg::AclTableQualifier::ETHER_TYPE:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldEthertype>>(
              attributes));
    case cfg::AclTableQualifier::OUTER_VLAN:
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldOuterVlanId>>(
              attributes));

    case cfg::AclTableQualifier::BTH_OPCODE:
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
      return hasField(
          std::get<
              std::optional<SaiAclTableTraits::Attributes::FieldBthOpcode>>(
              attributes));
#else
      return false;
#endif
    case cfg::AclTableQualifier::IPV6_NEXT_HEADER:
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
      return hasField(
          std::get<std::optional<
              SaiAclTableTraits::Attributes::FieldIpv6NextHeader>>(attributes));
#else
      return false;
#endif
    case cfg::AclTableQualifier::L4_DST_PORT_RANGE: {
      auto field = std::get<
          std::optional<SaiAclTableTraits::Attributes::FieldAclRangeType>>(
          attributes);
      return field.has_value();
    }
    case cfg::AclTableQualifier::TC:
      return hasField(
          std::get<std::optional<SaiAclTableTraits::Attributes::FieldTc>>(
              attributes));
    case cfg::AclTableQualifier::NEXT_HOP_GROUP_ID:
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      return hasField(
          std::get<std::optional<
              SaiAclTableTraits::Attributes::FieldRouteDestination>>(
              attributes));
#else
      return false;
#endif
    case cfg::AclTableQualifier::UDF:
      /* not supported */
      return false;
  }
  return false;
}

bool SaiAclTableManager::areQualifiersSupported(
    const std::string& aclTableName,
    const std::set<cfg::AclTableQualifier>& qualifiers) const {
  return std::all_of(
      std::begin(qualifiers),
      std::end(qualifiers),
      [this, aclTableName](auto qualifier) {
        return isQualifierSupported(aclTableName, qualifier);
      });
}

bool SaiAclTableManager::areQualifiersSupportedInDefaultAclTable(
    const std::set<cfg::AclTableQualifier>& qualifiers) const {
  return areQualifiersSupported(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE(), qualifiers);
}

void SaiAclTableManager::recreateAclTable(
    std::shared_ptr<SaiAclTable>& aclTable,
    const SaiAclTableTraits::CreateAttributes& newAttributes) {
  bool aclTableUpdateSupport =
      platform_->getAsic()->isSupported(HwAsic::Feature::SAI_ACL_TABLE_UPDATE);
#if defined(TAJO_SDK_VERSION_1_42_8)
  aclTableUpdateSupport = false;
#endif
  if (!aclTableUpdateSupport) {
    XLOG(WARNING) << "feature to update acl table is not supported";
    return;
  }
  if (!aclTable) {
    XLOG(ERR) << "cannot recreate a null ACL table";
    return;
  }
  XLOG(DBG2) << "refreshing acl table schema";
  auto adapterHostKey = aclTable->adapterHostKey();
  auto aclTableId = static_cast<sai_object_id_t>(aclTable->adapterKey());
  auto& aclEntryStore = saiStore_->get<SaiAclEntryTraits>();

  std::map<
      SaiAclEntryTraits::AdapterHostKey,
      SaiAclEntryTraits::CreateAttributes>
      entries{};
  // remove acl entries from acl table, retain their attributes
  for (const auto& entry : aclEntryStore) {
    auto aclEntry = entry.second.lock();
    if (!aclEntry) {
      XLOG(WARNING) << "expired ACL entry during ACL table recreation";
      continue;
    }
    auto key = aclEntry->adapterHostKey();
    if (std::get<SaiAclEntryTraits::Attributes::TableId>(key) != aclTableId) {
      continue;
    }
    auto value = aclEntry->attributes();
    auto aclEntryObject = aclEntryStore.setObject(key, value);
    entries.emplace(key, value);
    aclEntryObject.reset();
  }
  // remove group member and acl table, since store holds only weak ptr after
  // setObject is invoked, clearing returned shared ptr is enough to destroy
  // SAI object and call SAI remove API.
  std::shared_ptr<SaiAclTableGroupMember> groupMember{};
  SaiAclTableGroupMemberTraits::AdapterHostKey memberAdapterHostKey{};
  SaiAclTableGroupMemberTraits::CreateAttributes memberAttrs{};
  auto& aclGroupMemberStore = saiStore_->get<SaiAclTableGroupMemberTraits>();
  sai_object_id_t aclTableGroupId{};
  if (platform_->getAsic()->isSupported(HwAsic::Feature::ACL_TABLE_GROUP)) {
    for (auto entry : aclGroupMemberStore) {
      auto member = entry.second.lock();
      if (!member) {
        XLOG(WARNING) << "expired ACL table-group member during ACL table "
                         "recreation";
        continue;
      }
      auto key = member->adapterHostKey();
      auto attrs = member->attributes();
      if (std::get<SaiAclTableGroupMemberTraits::Attributes::TableId>(attrs) !=
          aclTableId) {
        continue;
      }
      groupMember = aclGroupMemberStore.setObject(key, attrs);
      if (!groupMember) {
        XLOG(ERR) << "failed to materialize ACL table-group member for ACL "
                  << aclTableId;
        return;
      }
      memberAdapterHostKey = groupMember->adapterHostKey();
      memberAttrs = groupMember->attributes();
      break;
    }
    if (!groupMember) {
      XLOG(ERR) << "ACL table-group member missing for ACL table "
                << aclTableId;
      return;
    }
    aclTableGroupId =
        std::get<SaiAclTableGroupMemberTraits::Attributes::TableGroupId>(
            memberAttrs)
            .value();
    managerTable_->switchManager().resetIngressAcl();
    // reset group member
    groupMember.reset();
  }
  // remove acl table
  aclTable.reset();

  // update acl table
  auto& aclTableStore = saiStore_->get<SaiAclTableTraits>();
  aclTable = aclTableStore.setObject(adapterHostKey, newAttributes);
  // restore acl table group member

  sai_object_id_t tableId = aclTable->adapterKey();
  if (platform_->getAsic()->isSupported(HwAsic::Feature::ACL_TABLE_GROUP)) {
    std::get<SaiAclTableGroupMemberTraits::Attributes::TableId>(
        memberAdapterHostKey) = tableId;
    std::get<SaiAclTableGroupMemberTraits::Attributes::TableId>(memberAttrs) =
        tableId;
    aclGroupMemberStore.addWarmbootHandle(memberAdapterHostKey, memberAttrs);
    managerTable_->switchManager().setIngressAcl(aclTableGroupId);
  }
  // skip recreating acl entries as acl entry information is lost.
  // this happens because SAI API layer returns default values for unset ACL
  // entry attributes. some of the attributes may be unsupported in sdk or
  // could stretch the key width beyind what's supported.
}

void SaiAclTableManager::removeUnclaimedAclCounter() {
  saiStore_->get<SaiAclCounterTraits>().removeUnclaimedWarmbootHandlesIf(
      [](const auto& aclCounter) {
        aclCounter->release();
        return true;
      });
}

AclStats SaiAclTableManager::getAclStats() const {
  AclStats aclStats;
  for (const auto& handle : handles_) {
    for (const auto& aclMember : handle.second->aclTableMembers) {
      for (const auto& [counterType, counterName] :
           aclMember.second->aclCounterTypeAndName) {
        aclStats.statNameToCounterMap()->insert(
            {counterName, aclStats_.getCumulativeValueIf(counterName)});
      }
    }
  }
  return aclStats;
}

std::shared_ptr<AclTable> SaiAclTableManager::reconstructAclTable(
    int /*priority*/,
    const std::string& /*name*/) const {
  throw FbossError("reconstructAclTable not implemented in SaiAclTableManager");
}

std::shared_ptr<AclEntry> SaiAclTableManager::reconstructAclEntry(
    const std::string& /*tableName*/,
    const std::string& /*aclEntryName*/,
    int /*priority*/) const {
  throw FbossError("reconstructAclEntry not implemented in SaiAclTableManager");
}

void SaiAclTableManager::addDefaultIngressAclTable() {
  addDefaultAclTable(
      cfg::AclStage::INGRESS,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
}
void SaiAclTableManager::removeDefaultIngressAclTable() {
  removeDefaultAclTable(
      cfg::AclStage::INGRESS,
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
}
} // namespace facebook::fboss
