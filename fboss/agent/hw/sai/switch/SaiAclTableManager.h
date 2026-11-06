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

#include "fboss/agent/hw/sai/api/AclApi.h"
#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/types.h"

#include "fboss/agent/hw/HwFb303Stats.h"

#include <folly/MacAddress.h>

#include <memory>
#include <string>

DECLARE_bool(sai_user_defined_trap);

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;
struct SaiHostifUserDefinedTrapHandle;
struct SaiNextHopGroupHandle;
class SwitchState;

using SaiAclTable = SaiObject<SaiAclTableTraits>;
using SaiAclEntry = SaiObject<SaiAclEntryTraits>;
using SaiAclCounter = SaiObject<SaiAclCounterTraits>;
using SaiAclRange = SaiObject<SaiAclRangeTraits>;

#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
using AclTableUdfGroup0 =
    SaiAclTableTraits::Attributes::UserDefinedFieldGroupMin0;
using AclTableUdfGroup1 =
    SaiAclTableTraits::Attributes::UserDefinedFieldGroupMin1;
using AclTableUdfGroup2 =
    SaiAclTableTraits::Attributes::UserDefinedFieldGroupMin2;
using AclTableUdfGroup3 =
    SaiAclTableTraits::Attributes::UserDefinedFieldGroupMin3;
using AclTableUdfGroup4 =
    SaiAclTableTraits::Attributes::UserDefinedFieldGroupMin4;

using AclEntryUdfGroup0 =
    SaiAclEntryTraits::Attributes::UserDefinedFieldGroupMin0;
using AclEntryUdfGroup1 =
    SaiAclEntryTraits::Attributes::UserDefinedFieldGroupMin1;
using AclEntryUdfGroup2 =
    SaiAclEntryTraits::Attributes::UserDefinedFieldGroupMin2;
using AclEntryUdfGroup3 =
    SaiAclEntryTraits::Attributes::UserDefinedFieldGroupMin3;
using AclEntryUdfGroup4 =
    SaiAclEntryTraits::Attributes::UserDefinedFieldGroupMin4;
#endif

struct SaiAclEntryHandle {
  /*
   * In FBOSS implementation, an ACL counter is always associated with single
   * ACL Entry. Thus, it is part of SaiAclEntryHandle.
   * ACL Counter is created first and then associated with an ACL Entry.
   * Thus, an ACL counter must be disassociated from ACL Entry before deleting
   * else the deletion fails with SAI_STATUS_OBJECT_IN_USE.
   *
   * Declare SaiAclCounter before SaiAclEntry as class members are destructed
   * in the reverse order of declaration.
   */
  std::shared_ptr<SaiAclRange> dstPortRange;
  std::shared_ptr<SaiHostifUserDefinedTrapHandle> userDefinedTrap;
  std::shared_ptr<SaiAclCounter> aclCounter;
  std::shared_ptr<SaiObject<SaiTunnelEncapNextHopTraits>> tunnelEncapNextHop;
  std::shared_ptr<SaiNextHopGroupHandle> matchNhgHandle;
  std::shared_ptr<SaiNextHopGroupHandle> redirectNhgHandle;
  std::shared_ptr<SaiAclEntry> aclEntry;
  std::string aclEntryName;
  std::vector<std::pair<cfg::CounterType, std::string>> aclCounterTypeAndName;
  std::optional<std::string> ingressMirror;
  std::optional<std::string> egressMirror;
  std::optional<std::string> getIngressMirror() {
    return ingressMirror;
  }
  std::optional<std::string> getEgressMirror() {
    return egressMirror;
  }
};

struct SaiAclTableHandle {
  std::shared_ptr<SaiAclTable> aclTable;
  // (ACL priority, ACL entry name) to corresponding handle. The name is part
  // of the key because PBR programs every rule at one priority and tells them
  // apart by the SAI ACL entry label, which carries this name.
  folly::F14FastMap<
      std::pair<int, std::string>,
      std::unique_ptr<SaiAclEntryHandle>>
      aclTableMembers;

  // Name of an entry already programmed at this priority, else nullptr.
  const std::string* FOLLY_NULLABLE entryNameAtPriority(int priority) const {
    for (const auto& [key, _] : aclTableMembers) {
      if (key.first == priority) {
        return &key.second;
      }
    }
    return nullptr;
  }
};

class SaiAclTableManager {
 public:
  SaiAclTableManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);

  /*
   * TOS is 8-bits: 6-bit DSCP followed by 2-bit ECN.
   * DSCP Mask is for 6-bit DSCP.
   * Thus, match all mask is  0b111111 i.e. 0x3F.
   */
  static auto constexpr kDscpMask = 0x3F;

  // Traffic class is a u8; PBR matches on the full value.
  static auto constexpr kTcMask = 0xFF;

  static auto constexpr kMaxUdfGroups = 5;

  /*
   * L4 Src/Dst Port Mask.
   * L4 Src/Dst Port is 16-bit.
   */
  static auto constexpr kL4PortMask = 0xFFFF;

  static auto constexpr kIpProtocolMask = 0xFF;
  // Used when the ACL entry does not set tcpFlagsMask.
  static auto constexpr kTcpFlagsMask = 0x3F;
  // Mask is not applicable for given field
  static auto constexpr kMaskDontCare = 0;
  static auto constexpr kIcmpTypeMask = 0xFF;
  static auto constexpr kIcmpCodeMask = 0xFF;
  static auto constexpr kEtherTypeMask = 0xFFFF;
  static auto constexpr kOuterVlanIdMask = 0xFFF;
  static auto constexpr kBthOpcodeMask = 0xFF;
  static auto constexpr kIpv6NextHeaderMask = 0xFF;

  static const folly::MacAddress& kMacMask() {
    static const folly::MacAddress macMask{"FF:FF:FF:FF:FF:FF"};

    return macMask;
  }

  std::vector<std::string> getAllHandleNames() const;

  AclTableSaiId addAclTable(
      const std::shared_ptr<AclTable>& addedAclTable,
      cfg::AclStage aclStage,
      const std::shared_ptr<SwitchState>& state,
      cfg::AclTableGroupBindPoint bindPoint =
          cfg::AclTableGroupBindPoint::SWITCH);
  void removeAclTable(
      const std::shared_ptr<AclTable>& removedAclTable,
      cfg::AclStage aclStage,
      const std::shared_ptr<SwitchState>& state,
      cfg::AclTableGroupBindPoint bindPoint =
          cfg::AclTableGroupBindPoint::SWITCH);
  void removeObsoletePortBoundAclTables(
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState);
  void changedAclTable(
      const std::shared_ptr<AclTable>& oldAclTable,
      const std::shared_ptr<AclTable>& newAclTable,
      cfg::AclStage aclStage,
      const std::shared_ptr<SwitchState>& state,
      cfg::AclTableGroupBindPoint bindPoint =
          cfg::AclTableGroupBindPoint::SWITCH);
  std::shared_ptr<AclTable> reconstructAclTable(
      int priority,
      const std::string& name) const;
  std::shared_ptr<AclEntry> reconstructAclEntry(
      const std::string& tableName,
      const std::string& aclEntryName,
      int priority) const;
  bool needsAclTableRecreate(
      const std::shared_ptr<AclTable>& oldAclTable,
      const std::shared_ptr<AclTable>& newAclTable,
      cfg::AclStage aclStage);
  void removeAclEntriesFromTable(const std::shared_ptr<AclTable>& aclTable);
  void addAclEntriesToTable(
      const std::shared_ptr<AclTable>& aclTable,
      std::shared_ptr<AclMap>& aclMap,
      const std::shared_ptr<SwitchState>& state);

  const SaiAclTableHandle* FOLLY_NULLABLE
  getAclTableHandle(const std::string& aclTableName) const;
  SaiAclTableHandle* FOLLY_NULLABLE
  getAclTableHandle(const std::string& aclTableName);

#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
  void updateUdfGroupAttributes(
      const std::shared_ptr<AclEntry>& addedAclEntry,
      const std::string& aclTableName,
      std::optional<AclEntryUdfGroup0>& udfGroup0,
      std::optional<AclEntryUdfGroup1>& udfGroup1,
      std::optional<AclEntryUdfGroup2>& udfGroup2,
      std::optional<AclEntryUdfGroup3>& udfGroup3,
      std::optional<AclEntryUdfGroup4>& udfGroup4);
#endif
  AclEntrySaiId addAclEntry(
      const std::shared_ptr<AclEntry>& addedAclEntry,
      const std::string& aclTableName,
      const std::shared_ptr<SwitchState>& state);
  void removeAclEntry(
      const std::shared_ptr<AclEntry>& removedAclEntry,
      const std::string& aclTableName,
      const std::shared_ptr<SwitchState>& state = nullptr);
  void changedAclEntry(
      const std::shared_ptr<AclEntry>& oldAclEntry,
      const std::shared_ptr<AclEntry>& newAclEntry,
      const std::string& aclTableName,
      const std::shared_ptr<SwitchState>& state);

  const SaiAclEntryHandle* FOLLY_NULLABLE getAclEntryHandle(
      const SaiAclTableHandle* aclTableHandle,
      int priority,
      const std::string& aclEntryName) const;

  std::pair<
      std::shared_ptr<SaiAclCounter>,
      std::vector<std::pair<cfg::CounterType, std::string>>>
  addAclCounter(
      const SaiAclTableHandle* aclTableHandle,
      const cfg::TrafficCounter& trafficCount,
      const SaiAclEntryTraits::AdapterHostKey& aclEntryAdapterHostKey);
  void removeAclCounter(const cfg::TrafficCounter& trafficCount);

  sai_uint32_t swPriorityToSaiPriority(int priority) const;

  sai_acl_ip_frag_t cfgIpFragToSaiIpFrag(cfg::IpFragMatch cfgType) const;
  sai_acl_ip_type_t cfgIpTypeToSaiIpType(cfg::IpType cfgIpType) const;
  uint16_t cfgEtherTypeToSaiEtherType(cfg::EtherType cfgEtherType) const;

  std::pair<sai_uint32_t, sai_uint32_t> cfgLookupClassToSaiFdbMetaDataAndMask(
      cfg::AclLookupClass lookupClass) const;
  std::pair<sai_uint32_t, sai_uint32_t> cfgLookupClassToSaiRouteMetaDataAndMask(
      cfg::AclLookupClass lookupClass) const;
  std::pair<sai_uint32_t, sai_uint32_t>
  cfgLookupClassToSaiNeighborMetaDataAndMask(
      cfg::AclLookupClass lookupClass) const;
  std::pair<sai_uint32_t, sai_uint32_t> cfgLookupClassToSaiPortMetaDataAndMask(
      cfg::AclLookupClassPort lookupClass) const;
  std::vector<sai_int32_t> cfgActionTypeListToSaiActionTypeList(
      const std::vector<cfg::AclTableActionType>& actionTypes) const;

  void programMirrorOnAllAcls(
      const std::optional<std::string>& mirrorId,
      MirrorAction action);

  void removeUnclaimedAclEntries();

  void updateStats();

  std::pair<int32_t, int32_t> getAclResourceUsage();

  std::set<cfg::AclTableQualifier> getSupportedQualifierSet(
      cfg::AclStage stage) const;

  void addDefaultIngressAclTable();
  void removeDefaultIngressAclTable();

  bool isQualifierSupported(
      const std::string& aclTableName,
      cfg::AclTableQualifier qualifier) const;

  bool areQualifiersSupported(
      const std::string& aclTableName,
      const std::set<cfg::AclTableQualifier>& qualifiers) const;

  bool areQualifiersSupportedInDefaultAclTable(
      const std::set<cfg::AclTableQualifier>& qualifiers) const;

  size_t aclEntryCount(const std::string& aclTableName) const {
    auto* table = getAclTableHandle(aclTableName);
    if (!table) {
      return 0;
    }
    return table->aclTableMembers.size();
  }

  bool hasAclEntryWithPriority(
      const std::string& aclTableName,
      const int priority) const {
    auto* table = getAclTableHandle(aclTableName);
    if (!table) {
      throw FbossError("ACL table ", aclTableName, " not found.");
    }
    return table->entryNameAtPriority(priority) != nullptr;
  }

  void removeUnclaimedAclCounter();

  AclStats getAclStats() const;

 private:
  SaiAclTableHandle* FOLLY_NULLABLE
  getAclTableHandleImpl(const std::string& aclTableName) const;

  std::pair<
      SaiAclTableTraits::AdapterHostKey,
      SaiAclTableTraits::CreateAttributes>
  createAclTableV4AndV6Helper(bool isV4);

  std::vector<sai_int32_t> getActionTypeList(
      const std::shared_ptr<AclTable>& addedAclTable);
  std::set<cfg::AclTableQualifier> getQualifierSet(
      sai_acl_stage_t aclStage,
      const std::shared_ptr<AclTable>& addedAclTable);

  std::pair<
      SaiAclTableTraits::AdapterHostKey,
      SaiAclTableTraits::CreateAttributes>
  aclTableCreateAttributes(
      sai_acl_stage_t aclStage,
      const std::shared_ptr<AclTable>& addedAclTable,
      cfg::AclTableGroupBindPoint bindPoint);

  sai_u32_range_t getFdbDstUserMetaDataRange() const;
  sai_u32_range_t getRouteDstUserMetaDataRange() const;
  sai_u32_range_t getNeighborDstUserMetaDataRange() const;
  sai_u32_range_t getPortUserMetaDataRange() const;

  sai_uint32_t getMetaDataMask(sai_uint32_t metaDataMax) const;

  sai_uint32_t cfgLookupClassToSaiMetaDataAndMaskHelper(
      cfg::AclLookupClass lookupClass,
      sai_uint32_t dstUserMetaDataRangeMin,
      sai_uint32_t dstUserMetaDataRangeMax) const;

  void programMirror(
      const SaiAclEntryHandle* aclEntryHandle,
      MirrorDirection direction,
      MirrorAction action,
      const std::optional<std::string>& mirrorId);

  std::shared_ptr<SaiAclRange>
  getOrCreateAclRange(sai_int32_t rangeType, uint32_t min, uint32_t max);

  std::shared_ptr<SaiNextHopGroupHandle> resolvePbrNextHopGroup(
      const std::shared_ptr<SwitchState>& state,
      int64_t nextHopGroupId,
      const std::string& aclEntryId);

  void recreateAclTable(
      std::shared_ptr<SaiAclTable>& exisitingTable,
      const SaiAclTableTraits::CreateAttributes& attributes);

  bool isSameAclCounterAttributes(
      const SaiAclCounterTraits::CreateAttributes& fromStore,
      const SaiAclCounterTraits::CreateAttributes& fromSw);

  std::set<cfg::AclTableQualifier> getSupportedQualifierSet(
      sai_acl_stage_t aclStage) const;

  void addDefaultAclTable(cfg::AclStage stage, const std::string& name);
  void removeDefaultAclTable(cfg::AclStage stage, const std::string& name);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;

  // SAI ACL Table name to corresponding Handle
  /*
   * TODO(skhare)
   * Extend SwitchState to carry AclTable, then use name from AclTable as key.
   */
  using SaiAclTableHandles =
      folly::F14FastMap<std::string, std::unique_ptr<SaiAclTableHandle>>;
  SaiAclTableHandles handles_;

  HwFb303Stats aclStats_;
  /*
   * 1. During an acl addition, we add an acl counter and the corresponding
   * fb303 counter.
   * 2. During acl deletion, we remove acl counter and the corresponding fb303
   * counter.
   * If the old acls list is A->B->C and the new one is A->C, then as
   * a part of the delta processing, we process the second node as a changed
   * AclEntry (where B is removed and C is added) and the third node as a
   * removed AclEntry (Where C gets removed). This causes the counters for
   * AclEntry C to be added during processing of the second node and
   * subsequently removed during the third node processing. To avoid this, keep
   * a ref count for acl counters. During delta processing, add and remove only
   * the entries which have count as 1 and 0 respectively.
   * NOTE: https://fburl.com/gdoc/96rz0n7q contains details of the
   * issue
   */
  folly::F14FastMap<std::string, int> aclCounterRefMap_;

  const sai_uint32_t aclEntryMinimumPriority_;
  const sai_uint32_t aclEntryMaximumPriority_;

  const sai_uint32_t fdbDstUserMetaDataRangeMin_;
  const sai_uint32_t fdbDstUserMetaDataRangeMax_;
  const sai_uint32_t fdbDstUserMetaDataMask_;

  const sai_uint32_t routeDstUserMetaDataRangeMin_;
  const sai_uint32_t routeDstUserMetaDataRangeMax_;
  const sai_uint32_t routeDstUserMetaDataMask_;

  const sai_uint32_t neighborDstUserMetaDataRangeMin_;
  const sai_uint32_t neighborDstUserMetaDataRangeMax_;
  const sai_uint32_t neighborDstUserMetaDataMask_;

  // Stored for later use during destruction
  //
  // We're a part of SaiPlatform (SaiSwitch->SaiManagerTable). Each platform,
  // such as SaiElbert8DDPhyPlatform, derives from SaiPlatform and implement
  // getAsic().
  //
  // During destruction, the derived platform gets destroyed first and the
  // vtable now points at SaiPlatform. Then we gets destroyed as a part of
  // SaiPlatform. If we or our peers such as SaiMacsecManager tries to call
  // removeAclTable() and we call
  // platform_->getAsic()->isSupported(HwAsic::Feature::ACL_TABLE_GROUP) to
  // decide how to clean up the table, getAsic() points at pure virtual
  // implementation at SaiPlatform and we crash.
  //
  // See
  // https://stackoverflow.com/questions/10707286/how-to-resolve-pure-virtual-method-called
  // https://www.artima.com/articles/pure-virtual-function-called-an-explanation
  bool hasTableGroups_;
};

} // namespace facebook::fboss
