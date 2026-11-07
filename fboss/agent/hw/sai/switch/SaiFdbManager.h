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

#include "fboss/agent/L2Entry.h"
#include "fboss/agent/hw/sai/api/FdbApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber-defs.h"
#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber.h"

#include "folly/MacAddress.h"
#include "folly/container/F14Map.h"
#include "folly/container/F14Set.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace facebook::fboss {

struct ConcurrentIndices;
class SaiManagerTable;
class SaiPlatform;
class MacEntry;
class SaiFdbManager;
class SaiStore;

using SaiFdbEntry = SaiObject<SaiFdbTraits>;

class ManagedFdbEntry : public SaiObjectEventAggregateSubscriber<
                            ManagedFdbEntry,
                            SaiFdbTraits,
                            SaiBridgePortTraits> {
 public:
  using Base = SaiObjectEventAggregateSubscriber<
      ManagedFdbEntry,
      SaiFdbTraits,
      SaiBridgePortTraits>;

  using BridgePortWeakPtr = std::weak_ptr<const SaiObject<SaiBridgePortTraits>>;
  using PublisherObjects = std::tuple<BridgePortWeakPtr>;

  ManagedFdbEntry(
      SaiFdbManager* manager,
      SwitchSaiId switchId,
      std::tuple<SaiPortDescriptor, RouterInterfaceSaiId> saiPortAndIntf,
      std::tuple<InterfaceID, folly::MacAddress> intfIDAndMac,
      sai_fdb_entry_type_t type,
      std::optional<sai_uint32_t> metadata,
      bool supportsPending)
      : Base(std::get<SaiPortDescriptor>(saiPortAndIntf)),
        manager_(manager),
        saiSwitchId_(switchId),
        saiPortAndIntf_(saiPortAndIntf),
        intfIDAndMac_(intfIDAndMac),
        type_(type),
        metadata_(metadata),
        supportsPending_(supportsPending) {}

  void createObject(PublisherObjects);
  void removeObject(size_t, PublisherObjects);
  SaiFdbTraits::FdbEntry makeFdbEntry(
      const SaiManagerTable* managerTable) const;
  void handleLinkDown();

  SaiPortDescriptor getPortId() const;
  L2Entry toL2Entry() const;
  InterfaceID getInterfaceID() const {
    return std::get<InterfaceID>(intfIDAndMac_);
  }
  folly::MacAddress getMac() const {
    return std::get<folly::MacAddress>(intfIDAndMac_);
  }
  sai_fdb_entry_type_t getType() const {
    return type_;
  }
  sai_uint32_t getMetaData() const {
    return metadata_.has_value() ? metadata_.value() : 0;
  }

  void update(const std::shared_ptr<MacEntry>& updated);

  std::string toString() const;

 private:
  SaiFdbManager* manager_;
  SwitchSaiId saiSwitchId_;
  std::tuple<SaiPortDescriptor, RouterInterfaceSaiId> saiPortAndIntf_;
  std::tuple<InterfaceID, folly::MacAddress> intfIDAndMac_;
  sai_fdb_entry_type_t type_;
  std::optional<sai_uint32_t> metadata_;
  bool supportsPending_;
};

class SaiFdbManager {
 public:
  SaiFdbManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform,
      const ConcurrentIndices* concurrentIndices);

  void addFdbEntry(
      SaiPortDescriptor,
      InterfaceID,
      folly::MacAddress,
      sai_fdb_entry_type_t type,
      std::optional<sai_uint32_t> metadata);
  void removeFdbEntry(InterfaceID, folly::MacAddress);
  // Process mac Table changes. vlanId is the vlan of the MacTable holding the
  // entry, which can differ from the port's current vlan.
  void addMac(const std::shared_ptr<MacEntry>& addedEntry, VlanID vlanId);
  void removeMac(const std::shared_ptr<MacEntry>& removedEntry, VlanID vlanId);
  void changeMac(
      const std::shared_ptr<MacEntry>& oldEntry,
      const std::shared_ptr<MacEntry>& newEntry,
      VlanID vlanId);
  std::vector<L2EntryThrift> getL2Entries(bool sdk = false) const;
  void handleLinkDown(SaiPortDescriptor portId);
  std::shared_ptr<SaiFdbEntry> createSaiObject(
      const typename SaiFdbTraits::AdapterHostKey& key,
      const typename SaiFdbTraits::CreateAttributes& attributes,
      const PublisherKey<SaiFdbTraits>::custom_type& publisherKey);

  void removeUnclaimedDynanicEntries();

  std::string listManagedObjects() const;

 private:
  L2EntryThrift fdbToL2Entry(const SaiFdbTraits::FdbEntry& fdbEntry) const;

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  const ConcurrentIndices* concurrentIndices_;
  folly::F14FastMap<
      PublisherKey<SaiFdbTraits>::custom_type,
      std::shared_ptr<ManagedFdbEntry>>
      managedFdbEntries_;
  folly::F14FastMap<
      SaiPortDescriptor,
      folly::F14FastSet<PublisherKey<SaiFdbTraits>::custom_type>>
      portToKeys_;
};

} // namespace facebook::fboss
