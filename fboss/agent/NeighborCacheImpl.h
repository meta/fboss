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

#include "fboss/agent/NeighborCacheEntry.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/types.h"

#include <list>
#include <optional>
#include <string>

namespace facebook::fboss {

class Vlan;
template <typename NTable>
class NeighborCache;

/*
 * This class manages the sw state of the neighbor tables. It has
 * a map of NeighborCacheEntries that should each correspond to
 * a NeighborEntry SwitchState node. These NeighborCacheEntries store additional
 * information and manage the logic for NDP-like expiration and unreachable
 * neighbor detection.
 *
 * All calls into this should have acquired a cache level lock through
 * NeighborCache so only one thread should ever be operating on the
 * cache at a given time.
 */
template <typename NTable>
class NeighborCacheImpl {
  friend class NeighborCache<NTable>;

 public:
  using AddressType = typename NTable::Entry::AddressType;
  using Entry = NeighborCacheEntry<NTable>;
  using EntryFields = typename Entry::EntryFields;

  ~NeighborCacheImpl();

  bool flushEntryBlocking(AddressType ip);
  void repopulate(std::shared_ptr<NTable> table);

  NeighborCacheImpl(
      NeighborCache<NTable>* cache,
      SwSwitch* sw,
      VlanID vlanID,
      std::string vlanName,
      InterfaceID intfID)
      : cache_(cache),
        sw_(sw),
        vlanID_(vlanID),
        vlanName_(vlanName),
        intfID_(intfID),
        evb_(sw->getNeighborCacheEvb()),
        needL2EntryForNeighbor_(sw->needL2EntryForNeighbor()) {}

  // Methods useful for subclasses
  bool setPendingEntry(AddressType ip, PortDescriptor port, bool force = false);

  void setExistingEntry(
      AddressType ip,
      folly::MacAddress mac,
      PortDescriptor port,
      NeighborEntryState state);

  void setEntry(
      AddressType ip,
      folly::MacAddress mac,
      PortDescriptor port,
      NeighborEntryState state);

  void updateEntryState(AddressType ip, NeighborEntryState state);

  void updateEntryClassID(
      AddressType ip,
      std::optional<cfg::AclLookupClass> classID = std::nullopt);

  std::unique_ptr<EntryFields> cloneEntryFields(AddressType ip);

  void portDown(PortDescriptor port);

  void portFlushEntries(PortDescriptor port);

  SwSwitch* getSw() const {
    return sw_;
  }

  InterfaceID getIntfID() const {
    return intfID_;
  }

  void setIntfID(const InterfaceID intfID) {
    intfID_ = intfID;
  }

  void setVlanName(const std::string& vlanName) {
    vlanName_ = vlanName;
  }

  VlanID getVlanID() const {
    return vlanID_;
  }

  std::string getVlanName() const {
    return vlanName_;
  }

  // Has the entry corresponding to ip has been hit in hw
  bool isHit(AddressType ip);

  template <typename NeighborEntryThrift>
  std::list<NeighborEntryThrift> getCacheData() const;

  template <typename NeighborEntryThrift>
  std::optional<NeighborEntryThrift> getCacheData(AddressType ip) const;

 private:
  bool isHwUpdateProtected();
  // These are used to program entries into the SwitchState
  bool programEntry(Entry* entry);
  bool
  programPendingEntry(Entry* entry, PortDescriptor port, bool force = false);

  SwSwitch::StateUpdateFn getUpdateFnToProgramEntry(
      Entry* entry,
      cfg::SwitchType switchType);

  SwSwitch::StateUpdateFn getUpdateFnToProgramPendingEntry(
      Entry* entry,
      PortDescriptor port,
      bool force,
      cfg::SwitchType switchType);

  void processEntry(AddressType ip);

  // Pass in a non-null flushed if you care whether an entry
  // was actually flushed from the switch state
  void flushEntry(AddressType ip, bool* flushed = nullptr);

  template <typename VlanOrIntfT>
  bool flushEntryFromSwitchState(
      std::shared_ptr<SwitchState>* state,
      AddressType ip,
      VlanOrIntfT* vlanOrIntf);

  Entry* getCacheEntry(AddressType ip) const;
  void setCacheEntry(std::shared_ptr<Entry> entry);
  bool removeEntry(AddressType ip);

  Entry* addOrUpdateEntryInternal(
      const EntryFields& fields,
      NeighborEntryState state,
      state::NeighborEntryType type);

  // Forbidden copy constructor and assignment operator
  NeighborCacheImpl(NeighborCacheImpl const&) = delete;
  NeighborCacheImpl& operator=(NeighborCacheImpl const&) = delete;

  NeighborCache<NTable>* cache_;
  SwSwitch* sw_;
  VlanID vlanID_;
  std::string vlanName_;
  InterfaceID intfID_;
  FbossEventBase* evb_;
  bool needL2EntryForNeighbor_;

  // Map of all entries
  std::unordered_map<AddressType, std::shared_ptr<Entry>> entries_;
};

} // namespace facebook::fboss
