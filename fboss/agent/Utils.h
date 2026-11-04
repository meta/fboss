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

#include <string>
#include <type_traits> // To use 'std::integral_constant'.

#include <boost/container/flat_map.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/iterator/filter_iterator.hpp>

#include <folly/FileUtil.h>
#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/Range.h>
#include <folly/lang/Bits.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/HwSwitchMatcher.h"

#include <thrift/lib/cpp2/protocol/BinaryProtocol.h>

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/DsfNodeMap.h"
#include "fboss/agent/types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

#include <chrono>

DECLARE_string(mac);
DECLARE_uint64(egress_buffer_pool_size);
DECLARE_uint64(ingress_egress_buffer_pool_size);
DECLARE_bool(allow_zero_headroom_for_lossless_pg);
DECLARE_string(mod_dest_mac_override);
DECLARE_bool(allow_nif_port_for_mod);
DECLARE_bool(allow_eventor_send_packet);
DECLARE_int32(sflow_egress_port_id);

namespace folly {
struct dynamic;
}

namespace facebook::fboss {

namespace cfg {
class SwitchConfig;
}

namespace utility {

inline const std::string kUdfHashDstQueuePairGroupName("dstQueuePair");
inline const std::string kUdfAclRoceOpcodeGroupName("roceOpcode");
inline const std::string kUdfL4UdpRocePktMatcherName("l4UdpRoce");
inline const std::string kUdfAclRoceOpcodeName("udf-roce-ack");
inline const std::string kUdfAclRoceOpcodeStats("udf-roce-ack-stats");
inline const int kUdfAclRoceOpcodeStartOffsetInBytes(8);
inline const int kUdfAclRoceOpcodeFieldSizeInBytes(1);
inline const int kUdfL4DstPort(4791);
inline const int kRandomUdfL4SrcPort(62946);
inline const int kUdfRoceOpcodeAck(17);
inline const signed char kUdfRoceOpcodeMask(0xFF);
inline const int kUdfRoceOpcodeWriteImmediate(11);
inline const std::string kRoceUdfFlowletGroupName("roceUdfFlowlet");
inline const int kRoceUdfFlowletStartOffsetInBytes(16);
inline const int kRoceUdfFlowletFieldSizeInBytes(1);
inline const int kRoceReserved(0x40); // offset 16
inline const std::string kFlowletAclName("test-udf-flowlet_acl");
inline const std::string kFlowletAclCounterName("test-udf-flowlet-acl-stats");
inline const std::string kUdfAclAethNakGroupName("aethNak");
inline const int kUdfAclAethNakStartOffsetInBytes(20);
inline const int kUdfAclAethNakFieldSizeInBytes(1);
inline const int kAethSyndromeWithNak(0x60);
inline const std::string kUdfAclRethWrImmZeroGroupName("wrImmZero");
// DMA length is 4 bytes. Typical length is less than 4K, so last 2 bytes match
// is enough to save on ACL table TCAM
inline const int kUdfAclRethDmaLenOffsetInBytes(34);
inline const int kUdfAclRethDmaLenFieldSizeInBytes(2);
} // namespace utility

class SwitchState;
class Interface;
class Port;
class SwitchSettings;
class PlatformMapping;
struct AgentConfig;
class HwAsic;
class HwSwitchFb303Stats;

constexpr auto kRecyclePortIdOffset = 1;
constexpr auto kOperDeltaHwSwitchMatcherPathIndex = 1;

template <typename T>
inline T readBuffer(const uint8_t* buffer, uint32_t pos, size_t buffSize) {
  CHECK_LE(pos + sizeof(T), buffSize);
  T tmp;
  memcpy(&tmp, buffer + pos, sizeof(tmp));
  return folly::Endian::big(tmp);
}

/*
 * Create a directory, recursively creating all parents if necessary.
 *
 * If the directory already exists this function is a no-op.
 * Throws an exception on error.
 */
void utilCreateDir(folly::StringPiece path);

/**
 * Helper function to get an IPv4 address for a particular vlan
 * Used to set src IP address for DHCP and ICMP packets
 * throw an FbossError in case no IP address exists.
 */
folly::IPAddressV4 getSwitchVlanIP(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlan);

/**
 * Helper function to get an IPv4 address for a particular interface
 * Used to set src IP address for DHCP and ICMP packets
 * throw an FbossError in case no IP address exists.
 */
folly::IPAddressV4 getSwitchIntfIP(
    const std::shared_ptr<SwitchState>& state,
    InterfaceID intfID);

/**
 * Helper function to get an IPv4 address of any(first) interface.
 */
folly::IPAddressV4 getAnyIntfIP(const std::shared_ptr<SwitchState>& state);

/**
 * Helper function to get an IPv6 address for a particular vlan
 * used to set src IP address for DHCPv6 and ICMPv6 packets
 * throw an FbossError in case no IPv6 address exists.
 */
folly::IPAddressV6 getSwitchVlanIPv6(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlan);

/**
 * Helper function to get an IPv6 address for a particular interface
 * used to set src IP address for DHCPv6 and ICMPv6 packets
 * throw an FbossError in case no IPv6 address exists.
 */
folly::IPAddressV6 getSwitchIntfIPv6(
    const std::shared_ptr<SwitchState>& state,
    InterfaceID intfID);

/**
 * Helper function to get an IPvv6 address of any(first) interface.
 */
folly::IPAddressV6 getAnyIntfIPv6(const std::shared_ptr<SwitchState>& state);

/*
 * Increases the nice value of the calling thread by increment. Note that this
 * code relies on the fact that Linux is not POSIX compliant. Otherwise, there
 * is no way to set per-thread priorities without root privileges.
 *
 * @param[in]    increment       How much to add to the current nice value. Must
 *                               be positive because we don't have the
 *                               privileges to decrease our niceness.
 */
void incNiceValue(const uint32_t increment);

/*
 * Serialize folly dynamic to JSON and write to file
 */
bool dumpStateToFile(const std::string& filename, const folly::dynamic& json);

/*
 * Whether thrift state is valid for sw switch state recovery
 */
bool isValidThriftStateFile(
    const std::string& follyStateFileName,
    const std::string& thriftStateFileName);

std::vector<ClientID> AllClientIDs();

/*
 * Report our hostname
 */
std::string getLocalHostname();
std::string getLocalHostnameUqdn();

void initThread(folly::StringPiece name);

/*
 * Utility wrapper around filter iterator
 */
template <
    typename Key,
    typename Value,
    typename Map = boost::container::flat_map<Key, Value>>
class MapFilter {
 public:
  using Entry = std::pair<Key, Value>;
  using Predicate = std::function<bool(const Entry&)>;
  using Iterator = typename Map::const_iterator;
  using FilterIterator = boost::iterators::filter_iterator<Predicate, Iterator>;

  MapFilter(const Map& map, Predicate pred)
      : map_(map), pred_(std::move(pred)) {}

  FilterIterator begin() {
    return boost::make_filter_iterator(pred_, map_.begin(), map_.end());
  }

  FilterIterator end() {
    return boost::make_filter_iterator(pred_, map_.end(), map_.end());
  }

 private:
  const Map& map_;
  Predicate pred_;
};

std::string switchRunStateStr(SwitchRunState runState);

folly::MacAddress getLocalMacAddress();

std::vector<NextHopThrift> thriftNextHopsFromAddresses(
    const std::vector<network::thrift::BinaryAddress>& addrs);

IpPrefix toIpPrefix(const folly::CIDRNetwork& nw);

UnicastRoute makeDropUnicastRoute(
    const folly::CIDRNetwork& nw,
    AdminDistance admin = AdminDistance::EBGP);
UnicastRoute makeToCpuUnicastRoute(
    const folly::CIDRNetwork& nw,
    AdminDistance admin = AdminDistance::EBGP);
UnicastRoute makeUnicastRoute(
    const folly::CIDRNetwork& nw,
    const std::vector<folly::IPAddress>& nhops,
    AdminDistance admin = AdminDistance::EBGP);

bool isAnyInterfacePortInLoopbackMode(
    std::shared_ptr<SwitchState> swState,
    const std::shared_ptr<Interface> interface);

bool isAnyInterfacePortRecycleOrEventorPort(
    std::shared_ptr<SwitchState> swState,
    const std::shared_ptr<Interface> interface);

PortID getPortID(
    SystemPortID sysPortId,
    const std::shared_ptr<SwitchState>& state);

// Same as above, but for a system port that may not live in the regular
// SystemPort map (e.g. a fabric link monitoring system port, which is kept in a
// separate map). The caller supplies the switchId under which the system port
// was allocated -- for fabric link monitoring ports that is the local ASIC
// switchId, NOT the SystemPort node's switchId (which holds the fabric-link
// id).
PortID getPortID(
    const SystemPortID& sysPortId,
    SwitchID switchId,
    const std::shared_ptr<SwitchState>& state);

SystemPortID getSystemPortID(
    const PortID& portId,
    cfg::Scope portScope,
    const std::map<int64_t, cfg::SwitchInfo>& switchToSwitchInfo,
    SwitchID switchId);

SystemPortID getSystemPortID(
    const PortID& portId,
    const std::shared_ptr<SwitchState>& state,
    SwitchID switchId);

// Legacy offset-based system port ID allocation for fabric link monitoring,
// used when fabric port logical IDs are NOT relocated into the local port-ID
// range (FLAGS_fabric_ports_uniform_local_offset off).
SystemPortID getFabricLinkMonitoringSystemPortID(
    const PortID& portId,
    const std::shared_ptr<SwitchSettings>& switchSettings);

PortID getPortIdFromFabricLinkMonSystemPortID(
    const SystemPortID& systemPortId,
    const int32_t fabricLinkMonitoringSystemPortOffset);

SystemPortID getInbandSystemPortID(
    const std::shared_ptr<SwitchState>& state,
    SwitchID switchId);

SystemPortID getInbandSystemPortID(
    const std::map<int64_t, cfg::SwitchInfo>& switchToSwitchInfo,
    SwitchID switchId);

cfg::Range64 getFirstSwitchSystemPortIdRange(
    const std::map<int64_t, cfg::SwitchInfo>& switchToSwitchInfo);

cfg::Range64 getCoveringSysPortRange(
    InterfaceID intf,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo);

cfg::Range64 getCoveringSysPortRange(
    SystemPortID intf,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo);

std::vector<PortID> getPortsForInterface(
    InterfaceID intf,
    const std::shared_ptr<SwitchState>& state);

InterfaceID getInterfaceIDForPort(
    PortID port,
    const std::shared_ptr<SwitchState>& state);

/*
 * An NPU switch injects a broadcast message such as neighbor solicitation or
 * advertisement via pipeline lookup. ASIC forwards these messages to all the
 * ports in the broadcast domain viz. same VLAN.
 *
 * A VOQ switch does not use VLANs. If a broadcast message such as neighbor
 * solicitation or advertisement is injected via pipeline lookup, ASIC would
 * not know which port(s) to send it out on, and thus would drop the packet.
 * Thus, for VOQ switch:
 *  - determine the interface for the given IP address
 *  - compute corresponding system port ID
 *  - get corresponding portID
 *
 * This helper function takes the ipAddress as argument and returns
 * PortDescriptor for the systemPort to send otu the packet with pipeline
 * bypass on.
 */
std::optional<PortID> getInterfacePortToReach(
    const std::shared_ptr<SwitchState>& state,
    const folly::IPAddress& ipAddr);

class StopWatch {
 public:
  StopWatch(std::optional<std::string> name, bool json);
  ~StopWatch();

  std::chrono::duration<double, std::milli> msecsElapsed() const {
    return std::chrono::steady_clock::now() - startTime_;
  }

 private:
  const std::optional<std::string> name_;
  bool json_;
  std::chrono::time_point<std::chrono::steady_clock> startTime_;
};

inline constexpr uint8_t kGetNetworkControlTrafficClass() {
  // Network Control << ECN-bits
  return 48 << 2;
}

void enableExactMatch(std::string& yamlCfg);

/*
 * Serialize thrift struct to binary and write to file
 */
template <typename ThriftT>
bool dumpBinaryThriftToFile(
    const std::string& filename,
    const ThriftT& thrift) {
  // create parent path if doesn't exist
  utilCreateDir(boost::filesystem::path(filename).parent_path().string());
  apache::thrift::BinaryProtocolWriter writer;
  folly::IOBufQueue queue;
  writer.setOutput(&queue);
  auto bytesWritten = thrift.write(&writer);
  std::vector<std::byte> result;
  result.resize(bytesWritten);
  // @lint-ignore CLANGTIDY
  folly::io::Cursor(queue.front()).pull(result.data(), bytesWritten);
  return folly::writeFile(result, filename.c_str());
}

/*
 * Deserialize thrift struct to from file and write into thriftState
 */
template <typename ThriftT>
bool readThriftFromBinaryFile(
    const std::string& filename,
    ThriftT& thriftState) {
  std::vector<std::byte> serializedThrift;
  auto ret = folly::readFile(filename.c_str(), serializedThrift);
  if (ret) {
    auto buf = folly::IOBuf::copyBuffer(
        serializedThrift.data(), serializedThrift.size());
    apache::thrift::BinaryProtocolReader reader;
    reader.setInput(buf.get());
    thriftState.read(&reader);
    return true;
  }
  return false;
}
/*
 * Helper function to get neighbor entry for specified IP from interface's
 * neighbor table.
 */
template <typename NeighborEntryT>
std::shared_ptr<NeighborEntryT> getNeighborEntryForIP(
    const std::shared_ptr<SwitchState>& state,
    const std::shared_ptr<Interface>& intf,
    const folly::IPAddress& ipAddr);

template <typename NeighborEntryT>
std::shared_ptr<NeighborEntryT> getNeighborEntryForIPAndIntf(
    const std::shared_ptr<Interface>& intf,
    const folly::IPAddress& ipAddr);

template <typename VlanOrIntfT>
std::optional<VlanID> getVlanIDFromVlanOrIntf(
    const std::shared_ptr<VlanOrIntfT>& vlanOrIntf);

template <typename NTableT>
std::shared_ptr<NTableT> getNeighborTableForVlan(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlanID);

class OperDeltaFilter {
 public:
  explicit OperDeltaFilter(SwitchID switchId);
  std::optional<fsdb::OperDelta> filterWithSwitchStateRootPath(
      const fsdb::OperDelta& delta) const {
    // Index 0 is the root of the Agent State Delta, where index 1 is the
    // HwMatcher string. Therefore, filter on the matcher string to only send
    // deltas related to the specific switchID down to hardware.
    return filter(delta, kOperDeltaHwSwitchMatcherPathIndex);
  }

  std::optional<fsdb::OperDelta> filter(const fsdb::OperDelta& delta, int index)
      const;

  // In place: drops other switches' units without copying the rest.
  std::optional<fsdb::OperDelta> filterWithSwitchStateRootPath(
      fsdb::OperDelta&& delta) const {
    return filter(std::move(delta), kOperDeltaHwSwitchMatcherPathIndex);
  }
  std::optional<fsdb::OperDelta> filter(fsdb::OperDelta&& delta, int index)
      const;

 private:
  bool matches(const fsdb::OperDeltaUnit& change, int index) const;
  SwitchID switchId_;
  mutable std::map<std::string, HwSwitchMatcher> matchersCache_;
};

AdminDistance getAdminDistanceForClientId(
    const cfg::SwitchConfig& config,
    int clientId);

size_t getNumActiveFabricPorts(
    const std::shared_ptr<SwitchState>& state,
    const HwSwitchMatcher& matcher);

bool isSwitchErrorFirmwareIsolate(
    const std::optional<uint32_t>& numActiveFabricPortsAtFwIsolate,
    const std::shared_ptr<SwitchSettings>& switchSettings);

cfg::SwitchDrainState computeActualSwitchDrainState(
    const std::shared_ptr<SwitchSettings>& switchSettings,
    int numActiveFabricPorts);

uint64_t getMacOui(const folly::MacAddress macAddress);

std::unordered_map<SwitchID, SwitchIndex> computeSwitchIdToSwitchIndex(
    const std::shared_ptr<MultiSwitchDsfNodeMap>& dsfNodeMap);

std::set<SwitchID> getAllSwitchIDsForSwitch(
    const std::shared_ptr<MultiSwitchDsfNodeMap>& dsfNodeMap,
    const SwitchID& switchID);

uint32_t getRemotePortOffset(const PlatformType platformType);

std::string runShellCmd(const std::string& cmd);

InterfaceID getInbandPortIntfID(
    const std::shared_ptr<SwitchState>& state,
    const SwitchID& switchId);

std::pair<std::string, std::string> getExpectedNeighborAndPortName(
    const cfg::Port& port);

const facebook::fboss::PlatformMapping* FOLLY_NULLABLE
getPlatformMappingForPlatformType(
    const facebook::fboss::PlatformType platformType);

int getRemoteSwitchID(
    const cfg::SwitchConfig* cfg,
    const cfg::Port& port,
    const std::unordered_map<std::string, std::vector<uint32_t>>&
        switchNameToSwitchIds);

CpuCosQueueId hwQueueIdToCpuCosQueueId(
    uint8_t hwQueueId,
    const HwAsic* asic,
    HwSwitchFb303Stats* hwswitchStats);
int numFabricLevels(const std::map<int64_t, cfg::DsfNode>& dsfNodes);

std::set<PortID> getL2ConnectedL1FabricPorts(
    const std::shared_ptr<SwitchState>& state);

const std::vector<cfg::AclLookupClass>& getToCpuClassIds();

bool isStringInFile(
    const std::string& filename,
    const std::string& str,
    int maxLinesToSearch);

std::optional<VlanID> getDefaultTxVlanIdIf(
    const std::shared_ptr<SwitchSettings>& settings);

std::optional<VlanID> getDefaultTxVlanId(
    const std::shared_ptr<SwitchSettings>& settings);

/**
 * Check if a port is drained.
 * A port is considered drained if either the port itself is drained
 * OR the switch is drained (logical OR).
 *
 * @param state The current switch state
 * @param port The port to check
 * @param portSwitchId The switch ID for the port
 * @return true if the port or switch is drained, false otherwise
 */
bool isPortDrained(
    const std::shared_ptr<SwitchState>& state,
    const Port* port,
    SwitchID portSwitchId);

} // namespace facebook::fboss
