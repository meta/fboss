/*
 * This file is used to generate class interfaces and implementations for
 * `NeighborUpdater`, `NeighborUpdaterImpl` and `NeighborUpdaterNoopImpl`.
 * Having the definition in a centralized place ensures that these classes are
 * in sync and agree on the argument types being used. This file defines a
 * list of calls to `NEIGHBOR_UPDATER_METHOD()`. This macro is *not* defined in
 * this file; it's the job of the caller to define a `NEIGHBOR_UPDATER_METHOD()`
 * first, and *then* include this file.
 *
 * See https://en.wikipedia.org/wiki/X_Macro for a more detailed explanation of
 * this technique.
 */

// clang-format off

// The method table is intentionally expanded by includers that define
// NEIGHBOR_UPDATER_METHOD differently.
// NOLINTBEGIN(facebook-modularize-issue-check)

NEIGHBOR_UPDATER_METHOD(public, flushEntry, uint32_t, (VlanID vlan, folly::IPAddress ip))
NEIGHBOR_UPDATER_METHOD(public, flushEntryForIntf, uint32_t, (InterfaceID intfID, folly::IPAddress ip))

// Ndp events
// TODO(skhare) Remove after completely migrating to intfCaches_
NEIGHBOR_UPDATER_METHOD(public, sentNeighborSolicitation, void, (VlanID vlan, folly::IPAddressV6 ip))
NEIGHBOR_UPDATER_METHOD(public, receivedNdpMine, void, (VlanID vlan, folly::IPAddressV6 ip, folly::MacAddress mac, PortDescriptor port, ICMPv6Type type, uint32_t flags))
NEIGHBOR_UPDATER_METHOD(public, receivedNdpNotMine, void, (VlanID vlan, folly::IPAddressV6 ip, folly::MacAddress mac, PortDescriptor port, ICMPv6Type type, uint32_t flags))

NEIGHBOR_UPDATER_METHOD(public, sentNeighborSolicitationForIntf, void, (InterfaceID intfID, folly::IPAddressV6 ip))
NEIGHBOR_UPDATER_METHOD(public, resolveNdpForIntf, void, (InterfaceID intfID, folly::IPAddressV6 ip))
NEIGHBOR_UPDATER_METHOD(public, receivedNdpMineForIntf, void, (InterfaceID intfID, folly::IPAddressV6 ip, folly::MacAddress mac, PortDescriptor port, ICMPv6Type type, uint32_t flags))
NEIGHBOR_UPDATER_METHOD(public, receivedNdpNotMineForIntf, void, (InterfaceID intfID, folly::IPAddressV6 ip, folly::MacAddress mac, PortDescriptor port, ICMPv6Type type, uint32_t flags))

// Arp events
// TODO(skhare) Remove after completely migrating to intfCaches_
NEIGHBOR_UPDATER_METHOD(public, sentArpRequest, void, (VlanID vlan, folly::IPAddressV4 ip))
NEIGHBOR_UPDATER_METHOD(public, receivedArpMine, void, (VlanID vlan, folly::IPAddressV4 ip, folly::MacAddress mac, PortDescriptor port, ArpOpCode op))
NEIGHBOR_UPDATER_METHOD(public, receivedArpNotMine, void, (VlanID vlan, folly::IPAddressV4 ip, folly::MacAddress mac, PortDescriptor port, ArpOpCode op))

NEIGHBOR_UPDATER_METHOD(public, sentArpRequestForIntf, void, (InterfaceID intfID, folly::IPAddressV4 ip))
NEIGHBOR_UPDATER_METHOD(public, receivedArpMineForIntf, void, (InterfaceID intfID, folly::IPAddressV4 ip, folly::MacAddress mac, PortDescriptor port, ArpOpCode op))
NEIGHBOR_UPDATER_METHOD(public, receivedArpNotMineForIntf, void, (InterfaceID intfID, folly::IPAddressV4 ip, folly::MacAddress mac, PortDescriptor port, ArpOpCode op))

NEIGHBOR_UPDATER_METHOD(public, portDown, void, (PortDescriptor port))
NEIGHBOR_UPDATER_METHOD(public, portFlushEntries, void, (PortDescriptor port))

// TODO(skhare) Remove after completely migrating to intfCaches_
NEIGHBOR_UPDATER_METHOD(public, getArpCacheData, std::list<ArpEntryThrift>, ())
NEIGHBOR_UPDATER_METHOD(public, getNdpCacheData, std::list<NdpEntryThrift>, ())

NEIGHBOR_UPDATER_METHOD(public, getArpCacheDataForIntf, std::list<ArpEntryThrift>, ())
NEIGHBOR_UPDATER_METHOD(public, getNdpCacheDataForIntf, std::list<NdpEntryThrift>, ())

NEIGHBOR_UPDATER_METHOD(public, getProbesLeft, uint32_t, (const InterfaceID& intfID, const folly::IPAddressV6& ip))
NEIGHBOR_UPDATER_METHOD(public, getProbesLeftIPv4, uint32_t, (const InterfaceID& intfID, const folly::IPAddressV4& ip))
NEIGHBOR_UPDATER_METHOD(public, getMaxNeighborProbes, uint32_t, (const InterfaceID& intfID))

// Enable access to neighbor caches from NeighborUpdater to help imitate neighbor learning during testing
NEIGHBOR_UPDATER_METHOD(private, getArpCacheForIntf, std::shared_ptr<ArpCache>, (InterfaceID intfID))
NEIGHBOR_UPDATER_METHOD(private, getNdpCacheForIntf, std::shared_ptr<NdpCache>, (InterfaceID intfID))

NEIGHBOR_UPDATER_METHOD(private, getArpCacheFor, std::shared_ptr<ArpCache>, (VlanID vlan))
NEIGHBOR_UPDATER_METHOD(private, getNdpCacheFor, std::shared_ptr<NdpCache>, (VlanID vlan))

// State update helpers
// TODO(skhare) Remove after completely migrating to intfCaches_
NEIGHBOR_UPDATER_METHOD(private, vlanAdded, void, (VlanID vlanID, const std::shared_ptr<SwitchState> state))
NEIGHBOR_UPDATER_METHOD(private, vlanDeleted, void, (VlanID vlanID))
NEIGHBOR_UPDATER_METHOD(private, vlanChanged, void, (VlanID vlanID, InterfaceID intfID, std::string vlanName))

NEIGHBOR_UPDATER_METHOD(private, interfaceAdded, void, (InterfaceID intfID, const std::shared_ptr<SwitchState> state))
NEIGHBOR_UPDATER_METHOD(private, interfaceRemoved, void, (InterfaceID intfID))

NEIGHBOR_UPDATER_METHOD(private, timeoutsChanged, void, (std::chrono::seconds arpTimeout, std::chrono::seconds ndpTimeout, std::chrono::seconds staleEntryInterval, uint32_t maxNeighborProbes))

// Lookup class updaters
// TODO(skhare) Remove after completely migrating to intfCaches_
NEIGHBOR_UPDATER_METHOD(private, updateArpEntryClassID, void, (VlanID vlan, folly::IPAddressV4 ip, std::optional<cfg::AclLookupClass> classID))
NEIGHBOR_UPDATER_METHOD(private, updateNdpEntryClassID, void, (VlanID vlan, folly::IPAddressV6 ip, std::optional<cfg::AclLookupClass> classID))

NEIGHBOR_UPDATER_METHOD(private, updateArpEntryClassIDForIntf, void, (InterfaceID intfID, folly::IPAddressV4 ip, std::optional<cfg::AclLookupClass> classID))
NEIGHBOR_UPDATER_METHOD(private, updateNdpEntryClassIDForIntf, void, (InterfaceID intfID, folly::IPAddressV6 ip, std::optional<cfg::AclLookupClass> classID))

// NOLINTEND(facebook-modularize-issue-check)
