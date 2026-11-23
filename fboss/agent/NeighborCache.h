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
#include "fboss/agent/NeighborCacheImpl-defs.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/PortDescriptor.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/Memory.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>
#include <chrono>
#include <list>
#include <string>

namespace facebook::fboss {

/*
 * This class manages the sw state of the neighbor tables. All of the
 * actual work is done by NeighborCacheImpl. This class is used to
 * provide the locking for those calls. All calls into the cache should
 * go through this class and SHOULD NOT call into the NeighborCacheImpl
 * directly. NeighborCacheImpl assumes that it does not need to worry
 * about synchronization because of this.
 *
 * This class wraps the common logic for a NeighborCache. It is meant to be
 * extended for ARP/NDP specific caches.
 */
template <typename NTable>
class NeighborCache {
  friend class NeighborCacheEntry<NTable>;

 public:
  using AddressType = typename NTable::Entry::AddressType;

  virtual ~NeighborCache() {}

  bool flushEntryBlocking(AddressType ip) {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->flushEntryBlocking(ip);
  }

  void repopulate(std::shared_ptr<NTable> table) {
    std::lock_guard<std::mutex> g(cacheLock_);
    impl_->repopulate(table);
  }

  void setIntfID(const InterfaceID intfID) {
    impl_->setIntfID(intfID);
  }

  void setVlanName(const std::string& vlanName) {
    impl_->setVlanName(vlanName);
  }

  void portDown(PortDescriptor port) {
    std::lock_guard<std::mutex> g(cacheLock_);
    impl_->portDown(port);
  }

  void portFlushEntries(PortDescriptor port) {
    std::lock_guard<std::mutex> g(cacheLock_);
    impl_->portFlushEntries(port);
  }

  template <typename NeighborEntryThrift>
  std::list<NeighborEntryThrift> getCacheData() {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->template getCacheData<NeighborEntryThrift>();
  }

  template <typename NeighborEntryThrift>
  std::optional<NeighborEntryThrift> getCacheData(AddressType ip) {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->template getCacheData<NeighborEntryThrift>(ip);
  }

  template <typename NeighborEntryThrift>
  std::list<NeighborEntryThrift> getCacheData() const {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->template getCacheData<NeighborEntryThrift>();
  }

  template <typename NeighborEntryThrift>
  std::optional<NeighborEntryThrift> getCacheData(AddressType ip) const {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->template getCacheData<NeighborEntryThrift>(ip);
  }
  void setTimeout(std::chrono::seconds timeout) {
    timeout_ = timeout;
  }

  void setMaxNeighborProbes(uint32_t maxNeighborProbes) {
    maxNeighborProbes_ = maxNeighborProbes;
  }

  void setStaleEntryInterval(std::chrono::seconds staleEntryInterval) {
    staleEntryInterval_ = staleEntryInterval;
  }

  void updateEntryClassID(
      AddressType ip,
      std::optional<cfg::AclLookupClass> classID = std::nullopt) {
    impl_->updateEntryClassID(ip, classID);
  }

  uint32_t getMaxNeighborProbes() const {
    return maxNeighborProbes_;
  }

 protected:
  // protected constructor since this is only meant to be inherited from
  NeighborCache(
      SwSwitch* sw,
      VlanID vlanID,
      std::string vlanName,
      InterfaceID intfID,
      std::chrono::seconds timeout,
      uint32_t maxNeighborProbes,
      std::chrono::seconds staleEntryInterval)
      : sw_(sw),
        timeout_(timeout),
        maxNeighborProbes_(maxNeighborProbes),
        staleEntryInterval_(staleEntryInterval),
        impl_(
            std::make_unique<NeighborCacheImpl<NTable>>(
                this,
                sw,
                vlanID,
                vlanName,
                intfID)) {}

  // Methods useful for subclasses
  bool setPendingEntry(AddressType ip, PortDescriptor port) {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->setPendingEntry(ip, port);
  }

  void setExistingEntry(
      AddressType ip,
      folly::MacAddress mac,
      PortDescriptor port,
      NeighborEntryState state) {
    std::lock_guard<std::mutex> g(cacheLock_);
    impl_->setExistingEntry(ip, mac, port, state);
  }

  void setEntry(
      AddressType ip,
      folly::MacAddress mac,
      PortDescriptor port,
      NeighborEntryState state) {
    std::lock_guard<std::mutex> g(cacheLock_);
    impl_->setEntry(ip, mac, port, state);
  }

  void updateEntryState(AddressType ip, NeighborEntryState state) {
    std::lock_guard<std::mutex> g(cacheLock_);
    impl_->updateEntryState(ip, state);
  }

  std::unique_ptr<typename NeighborCacheImpl<NTable>::EntryFields>
  cloneEntryFields(AddressType ip) {
    std::lock_guard<std::mutex> g(cacheLock_);
    // this intentionally makes a copy so that callers do not have a
    // reference to memory that could be deleted.
    return impl_->cloneEntryFields(ip);
  }

  SwSwitch* getSw() const {
    return sw_;
  }

  InterfaceID getIntfID() const {
    return impl_->getIntfID();
  }

  VlanID getVlanID() const {
    return impl_->getVlanID();
  }

  std::string getVlanName() const {
    return impl_->getVlanName();
  }

  std::chrono::seconds getBaseTimeout() const {
    return timeout_;
  }

  std::chrono::seconds getStaleEntryInterval() const {
    return staleEntryInterval_;
  }

 private:
  // This should only be called by a NeighborCacheEntry
  virtual void checkReachability(
      AddressType /*targetIP*/,
      folly::MacAddress /*targetMac*/,
      PortDescriptor /* portID */) const {
    // send unicast probe to see if neighbor is still reachable on known
    // L2 address
    XLOG(DFATAL) << " Only derived class probeFor should ever be called";
  }

  virtual void probeFor(AddressType /*ip*/) const {
    // send multicast probes to find L2 address for given L3 address
    XLOG(DFATAL) << " Only derived class probeFor should ever be called";
  }

  void flushEntry(AddressType ip) {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->flushEntry(ip);
  }

  void processEntry(AddressType ip) {
    std::lock_guard<std::mutex> g(cacheLock_);
    return impl_->processEntry(ip);
  }

  // Forbidden copy constructor and assignment operator
  NeighborCache(NeighborCache const&) = delete;
  NeighborCache& operator=(NeighborCache const&) = delete;
  NeighborCache(NeighborCache&&) = delete;
  NeighborCache& operator=(NeighborCache&&) = delete;

  SwSwitch* sw_;
  std::chrono::seconds timeout_;
  uint32_t maxNeighborProbes_{0};
  std::chrono::seconds staleEntryInterval_;
  std::unique_ptr<NeighborCacheImpl<NTable>> impl_;
  // mutuable to allow lock guard to be used
  mutable std::mutex cacheLock_;
};

} // namespace facebook::fboss
