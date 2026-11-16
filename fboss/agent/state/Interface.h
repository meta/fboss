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

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/Range.h>
#include <folly/json/dynamic.h>
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/ArpResponseTable.h"
#include "fboss/agent/state/NdpResponseTable.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

#include <boost/container/flat_map.hpp>

#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/NdpTable.h"

namespace facebook::fboss {

class SwitchState;

using DhcpV4OverrideMap = std::map<folly::MacAddress, folly::IPAddressV4>;
using DhcpV6OverrideMap = std::map<folly::MacAddress, folly::IPAddressV6>;

// both arp table and ndp table have same thrift type representation as map of
// string to neighbor entry fields. define which of these two members of struct
// resolves to which class.
class Interface;
RESOLVE_STRUCT_MEMBER(Interface, switch_state_tags::arpTable, ArpTable)
RESOLVE_STRUCT_MEMBER(Interface, switch_state_tags::ndpTable, NdpTable)
RESOLVE_STRUCT_MEMBER(
    Interface,
    switch_state_tags::arpResponseTable,
    ArpResponseTable)
RESOLVE_STRUCT_MEMBER(
    Interface,
    switch_state_tags::ndpResponseTable,
    NdpResponseTable)

/*
 * Interface stores a routing domain on the switch
 */
class Interface : public ThriftStructNode<Interface, state::InterfaceFields> {
 public:
  using Base = ThriftStructNode<Interface, state::InterfaceFields>;
  using Base::modify;
  using AddressesType = Base::Fields::TypeFor<switch_state_tags::addresses>;
  using Addresses = std::map<folly::IPAddress, uint8_t>;
  Interface(
      InterfaceID id,
      std::optional<VlanID> vlan,
      folly::StringPiece name,
      folly::MacAddress mac,
      int mtu,
      bool isVirtual,
      bool isStateSyncDisabled,
      cfg::InterfaceType type = cfg::InterfaceType::VLAN,
      std::optional<RemoteInterfaceType> remoteIntfType = std::nullopt,
      std::optional<LivenessStatus> remoteIntfLivenessStatus = std::nullopt,
      cfg::Scope scope = cfg::Scope::LOCAL) {
    set<switch_state_tags::interfaceId>(id);
    if (vlan) {
      setVlanID(*vlan);
    }
    setName(name.str());
    setMac(mac);
    setMtu(mtu);
    setIsVirtual(isVirtual);
    setIsStateSyncDisabled(isStateSyncDisabled);
    setType(type);
    setRemoteInterfaceType(remoteIntfType);
    setScope(scope);
  }

  InterfaceID getID() const {
    return InterfaceID(get<switch_state_tags::interfaceId>()->cref());
  }
  std::optional<SystemPortID> getSystemPortID() const;

  cfg::InterfaceType getType() const {
    return get<switch_state_tags::type>()->cref();
  }
  void setType(cfg::InterfaceType type) {
    set<switch_state_tags::type>(type);
  }
  VlanID getVlanID() const {
    CHECK(getType() == cfg::InterfaceType::VLAN);
    return VlanID(get<switch_state_tags::vlanId>()->cref());
  }
  void setVlanID(VlanID id) {
    CHECK(getType() == cfg::InterfaceType::VLAN);
    set<switch_state_tags::vlanId>(id);
  }

  VlanID getVlanIDHelper() const {
    // TODO(skhare)
    // VOQ/Fabric switches require that the packets are not tagged with any
    // VLAN. We are gradually enhancing wedge_agent to handle tagged as well as
    // untagged packets. During this transition, we will use VlanID 0 to
    // populate SwitchState/Neighbor cache etc. data structures. Once the
    // wedge_agent changes are complete, we will no longer need this function.
    switch (getType()) {
      case cfg::InterfaceType::VLAN:
        return getVlanID();
      case cfg::InterfaceType::PORT:
      case cfg::InterfaceType::SYSTEM_PORT:
        return VlanID(0);
    }
    throw FbossError("interface type is unknown type");
  }

  Interface* modify(std::shared_ptr<SwitchState>* state);
  int getMtu() const {
    return get<switch_state_tags::mtu>()->cref();
  }
  void setMtu(int mtu) {
    set<switch_state_tags::mtu>(mtu);
  }

  std::string getName() const {
    return get<switch_state_tags::name>()->cref();
  }
  void setName(const std::string& name) {
    set<switch_state_tags::name>(name);
  }

  folly::MacAddress getMac() const {
    return folly::MacAddress::fromNBO(get<switch_state_tags::mac>()->cref());
  }
  void setMac(folly::MacAddress mac) {
    set<switch_state_tags::mac>(mac.u64NBO());
  }

  bool isVirtual() const {
    return get<switch_state_tags::isVirtual>()->cref();
  }
  void setIsVirtual(bool isVirtual) {
    set<switch_state_tags::isVirtual>(isVirtual);
  }

  bool isStateSyncDisabled() const {
    return get<switch_state_tags::isStateSyncDisabled>()->cref();
  }
  void setIsStateSyncDisabled(bool isStateSyncDisabled) {
    set<switch_state_tags::isStateSyncDisabled>(isStateSyncDisabled);
  }

  template <typename NTableT>
  auto getTable() const {
    if constexpr (std::is_same_v<NTableT, ArpTable>) {
      return getArpTable();
    } else if constexpr (std::is_same_v<NTableT, NdpTable>) {
      return getNdpTable();
    }
  }

  std::shared_ptr<ArpTable> getArpTable() const {
    return get<switch_state_tags::arpTable>();
  }
  std::shared_ptr<NdpTable> getNdpTable() const {
    return get<switch_state_tags::ndpTable>();
  }
  template <
      typename AddressType,
      std::enable_if_t<std::is_same_v<AddressType, folly::IPAddressV4>, bool> =
          true>
  auto getNeighborEntryTable() const {
    return getArpTable();
  }

  template <
      typename AddressType,
      std::enable_if_t<std::is_same_v<AddressType, folly::IPAddressV6>, bool> =
          true>
  auto getNeighborEntryTable() const {
    return getNdpTable();
  }

  template <typename NTable>
  inline const std::shared_ptr<NTable> getNeighborTable() const;

  void setArpTable(state::NeighborEntries arpTable) {
    set<switch_state_tags::arpTable>(std::move(arpTable));
  }
  void setNdpTable(state::NeighborEntries ndpTable) {
    set<switch_state_tags::ndpTable>(std::move(ndpTable));
  }

  void setArpTable(std::shared_ptr<ArpTable> table) {
    ref<switch_state_tags::arpTable>() = std::move(table);
  }
  void setNdpTable(std::shared_ptr<NdpTable> table) {
    ref<switch_state_tags::ndpTable>() = std::move(table);
  }

  template <typename AddressType>
  void setNeighborEntryTable(state::NeighborEntries nbrTable) {
    if constexpr (std::is_same_v<AddressType, folly::IPAddressV4>) {
      return setArpTable(std::move(nbrTable));
    }
    return setNdpTable(std::move(nbrTable));
  }

  const std::shared_ptr<ArpResponseTable> getArpResponseTable() const {
    return get<switch_state_tags::arpResponseTable>();
  }
  void setArpResponseTable(std::shared_ptr<ArpResponseTable> table) {
    ref<switch_state_tags::arpResponseTable>() = std::move(table);
  }

  const std::shared_ptr<NdpResponseTable> getNdpResponseTable() const {
    return get<switch_state_tags::ndpResponseTable>();
  }
  void setNdpResponseTable(std::shared_ptr<NdpResponseTable> table) {
    ref<switch_state_tags::ndpResponseTable>() = std::move(table);
  }

  std::optional<folly::IPAddressV4> getDhcpV4Relay() const {
    if (auto dhcpV4Relay = cref<switch_state_tags::dhcpV4Relay>()) {
      return folly::IPAddressV4(dhcpV4Relay->toThrift());
    }
    return std::nullopt;
  }

  void setDhcpV4Relay(std::optional<folly::IPAddressV4> dhcpV4Relay) {
    if (!dhcpV4Relay) {
      ref<switch_state_tags::dhcpV4Relay>().reset();
    } else {
      set<switch_state_tags::dhcpV4Relay>((*dhcpV4Relay).str());
    }
  }

  std::optional<folly::IPAddressV6> getDhcpV6Relay() const {
    if (auto dhcpV6Relay = cref<switch_state_tags::dhcpV6Relay>()) {
      return folly::IPAddressV6(dhcpV6Relay->toThrift());
    }
    return std::nullopt;
  }

  void setDhcpV6Relay(std::optional<folly::IPAddressV6> dhcpV6Relay) {
    if (!dhcpV6Relay) {
      ref<switch_state_tags::dhcpV6Relay>().reset();
    } else {
      set<switch_state_tags::dhcpV6Relay>((*dhcpV6Relay).str());
    }
  }

  DhcpV4OverrideMap getDhcpV4RelayOverrides() const {
    DhcpV4OverrideMap overrideMap{};
    for (auto iter :
         std::as_const(*get<switch_state_tags::dhcpRelayOverridesV4>())) {
      overrideMap.emplace(
          folly::MacAddress(iter.first),
          folly::IPAddressV4(iter.second->cref()));
    }
    return overrideMap;
  }

  void setDhcpV4RelayOverrides(DhcpV4OverrideMap map) {
    std::map<std::string, std::string> overrideMap{};
    for (auto iter : map) {
      overrideMap.emplace(iter.first.toString(), iter.second.str());
    }
    set<switch_state_tags::dhcpRelayOverridesV4>(std::move(overrideMap));
  }

  DhcpV6OverrideMap getDhcpV6RelayOverrides() const {
    DhcpV6OverrideMap overrideMap{};
    for (auto iter :
         std::as_const(*get<switch_state_tags::dhcpRelayOverridesV6>())) {
      overrideMap.emplace(
          folly::MacAddress(iter.first),
          folly::IPAddressV6(iter.second->cref()));
    }
    return overrideMap;
  }

  void setDhcpV6RelayOverrides(DhcpV6OverrideMap map) {
    std::map<std::string, std::string> overrideMap{};
    for (auto iter : map) {
      overrideMap.emplace(iter.first.toString(), iter.second.str());
    }
    set<switch_state_tags::dhcpRelayOverridesV6>(std::move(overrideMap));
  }

  auto getAddresses() const {
    return get<switch_state_tags::addresses>();
  }
  void setAddresses(Addresses addrs) {
    std::map<std::string, int16_t> addresses;
    for (const auto& [addr, mask] : addrs) {
      addresses.emplace(addr.str(), mask);
    }
    set<switch_state_tags::addresses>(std::move(addresses));
  }
  bool hasAddress(folly::IPAddress ip) const {
    auto& addresses = std::as_const(*getAddresses());
    return (addresses.find(ip.str()) != addresses.end());
  }

  template <typename NTable>
  void setNeighborTable(std::shared_ptr<NTable> table) {
    if constexpr (std::is_same_v<NTable, ArpTable>) {
      ref<switch_state_tags::arpTable>() = std::move(table);
    } else {
      ref<switch_state_tags::ndpTable>() = std::move(table);
    }
  }

  template <typename NTable>
  void setNeighborTable(state::NeighborEntries nbrTable) {
    if constexpr (std::is_same_v<NTable, ArpTable>) {
      return setArpTable(std::move(nbrTable));
    }
    return setNdpTable(std::move(nbrTable));
  }

  /**
   * Find the interface IP address to reach the given destination
   */
  std::optional<folly::CIDRNetwork> getAddressToReach(
      const folly::IPAddress& dest) const;

  /**
   * Returns true if the address can be reached through this interface
   */
  bool canReachAddress(const folly::IPAddress& dest) const;

  auto getNdpConfig() const {
    return get<switch_state_tags::ndpConfig>();
  }
  void setNdpConfig(cfg::NdpConfig ndp) {
    set<switch_state_tags::ndpConfig>(std::move(ndp));
  }

  /*
   * A utility function to check if an IP address is in a locally attached
   * subnet on the given interface.
   *
   * This a generic helper function.  The Interface class simply seemed like
   * the best place to put it for now.
   */
  static bool isIpAttached(
      folly::IPAddress ip,
      InterfaceID intfID,
      const std::shared_ptr<SwitchState>& state);

  bool operator!=(const Interface& other) const {
    return !(*this == other);
  }

  Addresses getAddressesCopy() const {
    // THRIFT_COPY: evaluate if this function can be retired..
    Addresses addrs{};
    for (auto iter : std::as_const(*getAddresses())) {
      addrs.emplace(folly::IPAddress(iter.first), iter.second->cref());
    }
    return addrs;
  }

  int32_t routerAdvertisementSeconds() const {
    return getNdpConfig()
        ->get<switch_config_tags::routerAdvertisementSeconds>()
        ->cref();
  }

  int32_t curHopLimit() const {
    return getNdpConfig()->get<switch_config_tags::curHopLimit>()->cref();
  }

  int32_t routerLifetime() const {
    return getNdpConfig()->get<switch_config_tags::routerLifetime>()->cref();
  }

  int32_t prefixValidLifetimeSeconds() const {
    return getNdpConfig()
        ->get<switch_config_tags::prefixValidLifetimeSeconds>()
        ->cref();
  }

  int32_t prefixPreferredLifetimeSeconds() const {
    return getNdpConfig()
        ->get<switch_config_tags::prefixPreferredLifetimeSeconds>()
        ->cref();
  }

  bool routerAdvertisementManagedBit() const {
    return getNdpConfig()
        ->get<switch_config_tags::routerAdvertisementManagedBit>()
        ->cref();
  }

  bool routerAdvertisementOtherBit() const {
    return getNdpConfig()
        ->get<switch_config_tags::routerAdvertisementOtherBit>()
        ->cref();
  }

  std::optional<std::string> routerAddress() const {
    if (auto addr = getNdpConfig()->get<switch_config_tags::routerAddress>()) {
      return addr->cref();
    }
    return std::nullopt;
  }

  std::optional<RemoteInterfaceType> getRemoteInterfaceType() const {
    if (auto remoteIntfType = cref<switch_state_tags::remoteIntfType>()) {
      return remoteIntfType->cref();
    }
    return std::nullopt;
  }

  void setRemoteInterfaceType(
      const std::optional<RemoteInterfaceType>& remoteIntfType = std::nullopt) {
    if (remoteIntfType) {
      set<switch_state_tags::remoteIntfType>(remoteIntfType.value());
    } else {
      ref<switch_state_tags::remoteIntfType>().reset();
    }
  }

  std::optional<LivenessStatus> getRemoteLivenessStatus() const {
    if (auto remoteIntfLivenessStatus =
            cref<switch_state_tags::remoteIntfLivenessStatus>()) {
      return remoteIntfLivenessStatus->cref();
    }
    return std::nullopt;
  }

  void setRemoteLivenessStatus(
      const std::optional<LivenessStatus>& remoteIntfLivenessStatus =
          std::nullopt) {
    if (remoteIntfLivenessStatus) {
      set<switch_state_tags::remoteIntfLivenessStatus>(
          remoteIntfLivenessStatus.value());
    } else {
      ref<switch_state_tags::remoteIntfLivenessStatus>().reset();
    }
  }

  void setScope(const cfg::Scope& scope) {
    set<switch_state_tags::scope>(scope);
  }

  cfg::Scope getScope() const {
    return cref<switch_state_tags::scope>()->cref();
  }

  bool isStatic() const {
    return getRemoteInterfaceType().has_value() &&
        getRemoteInterfaceType().value() == RemoteInterfaceType::STATIC_ENTRY;
  }

  /*
   * A port router interface is bound to either a physical port or an aggregate
   * port, never both. Both bindings use cfg::InterfaceType::PORT and are told
   * apart by which of portId / aggregatePortId is set, so the setters below
   * keep the invariant by clearing the other field.
   */
  void setPortID(PortID port) {
    set<switch_state_tags::portId>(port);
    ref<switch_state_tags::aggregatePortId>().reset();
  }

  void setAggregatePortID(AggregatePortID aggregatePort) {
    set<switch_state_tags::aggregatePortId>(aggregatePort);
    ref<switch_state_tags::portId>().reset();
  }

  std::optional<PortID> getPortIDf() const {
    if (auto portId = cref<switch_state_tags::portId>()) {
      return PortID(portId->cref());
    }
    return std::nullopt;
  }

  std::optional<AggregatePortID> getAggregatePortIDf() const {
    if (auto aggregatePortId = cref<switch_state_tags::aggregatePortId>()) {
      return AggregatePortID(aggregatePortId->cref());
    }
    return std::nullopt;
  }

  PortID getPortID() const {
    CHECK(getType() == cfg::InterfaceType::PORT);
    auto portID = getPortIDf();
    CHECK(portID.has_value())
        << "interface " << getID() << " is not bound to a physical port";
    return *portID;
  }

  AggregatePortID getAggregatePortID() const {
    CHECK(getType() == cfg::InterfaceType::PORT);
    auto aggregatePortID = getAggregatePortIDf();
    CHECK(aggregatePortID.has_value())
        << "interface " << getID() << " is not bound to an aggregate port";
    return *aggregatePortID;
  }

  void setDesiredPeerName(const std::string& desiredPeerName) {
    set<switch_state_tags::desiredPeerName>(desiredPeerName);
  }

  void setDesiredPeerAddressIPv6(const std::string& desiredPeerAddressIPv6) {
    set<switch_state_tags::desiredPeerAddressIPv6>(desiredPeerAddressIPv6);
  }

  std::optional<std::string> getDesiredPeerName() const {
    if (auto desiredPeerName = cref<switch_state_tags::desiredPeerName>()) {
      return desiredPeerName->cref();
    }
    return std::nullopt;
  }

  std::optional<std::string> getDesiredPeerAddressIPv6() const {
    if (auto desiredPeerAddressIPv6 =
            cref<switch_state_tags::desiredPeerAddressIPv6>()) {
      return desiredPeerAddressIPv6->cref();
    }
    return std::nullopt;
  }

  /*
   * Inherit the constructors required for clone().
   * This needs to be public, as std::make_shared requires
   * operator new() to be available.
   */
  inline static const int kDefaultMtu{1500};

 private:
  using Base::Base;
  friend class CloneAllocator;
};

template <typename NTable>
inline const std::shared_ptr<NTable> Interface::getNeighborTable() const {
  return this->template getNeighborEntryTable<typename NTable::AddressType>();
}

} // namespace facebook::fboss
