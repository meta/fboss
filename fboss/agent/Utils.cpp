/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/Utils.h"

#include <sys/resource.h>
#include <sys/syscall.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FsdbHelper.h"
#include "fboss/agent/SysError.h"
#include "fboss/agent/hw/HwSwitchFb303Stats.h"
#include "fboss/agent/state/ArpEntry.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/NdpEntry.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"

#include "fboss/agent/platforms/common/janga800bic/Janga800bicPlatformMapping.h"
#include "fboss/agent/platforms/common/meru800bfa/Meru800bfaP1PlatformMapping.h"
#include "fboss/agent/platforms/common/meru800bfa/Meru800bfaPlatformMapping.h"
#include "fboss/agent/platforms/common/meru800bia/Meru800biaPlatformMapping.h"

#include <folly/FileUtil.h>
#include <folly/Subprocess.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>

#include <boost/filesystem/operations.hpp>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <re2/re2.h>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

using folly::IPAddressV4;
using folly::IPAddressV6;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

DEFINE_string(mac, "", "The local MAC address for this switch");
DEFINE_string(mgmt_if, "eth0", "name of management interface");
DEFINE_uint64(
    egress_buffer_pool_size,
    0,
    "Egress buffer pool size override for testing");
DEFINE_uint64(
    ingress_egress_buffer_pool_size,
    0,
    "Common ingress/egress buffer pool size override");
DEFINE_bool(
    allow_zero_headroom_for_lossless_pg,
    false,
    "Allow lossless PG to have headroom as zero");
DEFINE_string(
    mod_dest_mac_override,
    "",
    "Destination MAC override for Mirror-on-Drop packets");
DEFINE_bool(allow_nif_port_for_mod, false, "Allow NIF port to be used for MOD");
DEFINE_bool(
    allow_eventor_send_packet,
    false,
    "A test-only flag to allow sending packets directly out of the eventor");
DEFINE_int32(sflow_egress_port_id, 0, "Force sflow to egress from a port");

namespace facebook::fboss {

namespace {

UnicastRoute makeUnicastRouteHelper(
    const folly::CIDRNetwork& nw,
    RouteForwardAction action,
    AdminDistance admin) {
  UnicastRoute nr;
  nr.dest() = toIpPrefix(nw);
  nr.action() = action;
  nr.adminDistance() = admin;
  return nr;
}

template <typename AddrT>
AddrT getIPAddress(InterfaceID intfID, const Interface::AddressesType addrs) {
  for (auto iter : std::as_const(*addrs)) {
    auto address = folly::IPAddress(iter.first);
    if constexpr (std::is_same_v<AddrT, IPAddressV4>) {
      if (address.isV4()) {
        return address.asV4();
      }
    } else {
      if (address.isV6()) {
        return address.asV6();
      }
    }
  }
  throw FbossError("Cannot find IP address for interface ", intfID);
}

cfg::Range64 getCoveringSysPortRange(
    int64_t id,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo) {
  for (const auto& [_, switchInfo] : switchIdToSwitchInfo) {
    for (const auto& sysPortRange :
         *switchInfo.systemPortRanges()->systemPortRanges()) {
      if (id >= sysPortRange.minimum() && id <= sysPortRange.maximum()) {
        return sysPortRange;
      }
    }
  }
  throw FbossError("Id: ", id, " not witihin any local sys port range");
}

} // namespace
  //

void utilCreateDir(folly::StringPiece path) {
  try {
    boost::filesystem::create_directories(path.str());
  } catch (...) {
    throw SysError(errno, "failed to create directory \"", path, "\"");
  }
}

IPAddressV4 getAnyIntfIP(const std::shared_ptr<SwitchState>& state) {
  IPAddressV4 intfIp;
  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (const auto& intfIter : std::as_const(*intfMap)) {
      const auto& intf = intfIter.second;
      for (auto iter : std::as_const(*intf->getAddresses())) {
        auto address = folly::IPAddress(iter.first);
        if (address.isV4()) {
          return address.asV4();
        }
      }
    }
  }
  throw FbossError("Cannot find IPv4 address on any interface");
}

IPAddressV6 getAnyIntfIPv6(const std::shared_ptr<SwitchState>& state) {
  IPAddressV6 intfIp;
  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (const auto& intfIter : std::as_const(*intfMap)) {
      const auto& intf = intfIter.second;
      for (auto iter : std::as_const(*intf->getAddresses())) {
        auto address = folly::IPAddress(iter.first);
        if (address.isV6()) {
          return address.asV6();
        }
      }
    }
  }
  throw FbossError("Cannot find IPv6 address on any interface");
}

IPAddressV4 getSwitchVlanIP(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlan) {
  IPAddressV4 switchIp;
  auto vlanInterface = state->getInterfaces()->getInterfaceInVlan(vlan);

  return getIPAddress<IPAddressV4>(
      vlanInterface->getID(), vlanInterface->getAddresses());
}

IPAddressV4 getSwitchIntfIP(
    const std::shared_ptr<SwitchState>& state,
    InterfaceID intfID) {
  auto interface = state->getInterfaces()->getNode(intfID);

  return getIPAddress<IPAddressV4>(intfID, interface->getAddresses());
}

IPAddressV6 getSwitchVlanIPv6(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlan) {
  auto vlanInterface = state->getInterfaces()->getInterfaceInVlan(vlan);

  return getIPAddress<IPAddressV6>(
      vlanInterface->getID(), vlanInterface->getAddresses());
}

IPAddressV6 getSwitchIntfIPv6(
    const std::shared_ptr<SwitchState>& state,
    InterfaceID intfID) {
  auto interface = state->getInterfaces()->getNode(intfID);

  return getIPAddress<IPAddressV6>(intfID, interface->getAddresses());
}

void incNiceValue(const uint32_t increment) {
  if (increment == 0) {
    XLOG(WARNING) << "Nice value increment is 0. Returning now.";
    return;
  }
  pid_t tid;
  tid = syscall(SYS_gettid);
  // Need to reset errno because getpriority sometimes returns -1 for real
  auto oldErrno = errno;
  errno = 0;

  int niceness = getpriority(PRIO_PROCESS, tid);
  if (niceness == -1 && errno) {
    XLOG(ERR) << "Error while getting current thread niceness: "
              << strerror(errno);
  } else if (setpriority(PRIO_PROCESS, tid, niceness + increment) == -1) {
    XLOG(ERR) << "Error while setting thread niceness: " << strerror(errno);
  }

  errno = oldErrno;
}

bool dumpStateToFile(const std::string& filename, const folly::dynamic& json) {
  return folly::writeFile(folly::toPrettyJson(json), filename.c_str());
}

bool isValidThriftStateFile(
    const std::string& follyStateFileName,
    const std::string& thriftStateFileName) {
  // Folly state is dumped before thrift state. Hence if folly state has a newer
  // timestamp than thrift state, thrift state should be dumped by previous
  // warmboot exits. This could happen during trunk->prod->trunk or
  // prod->trunk->prod.
  if (boost::filesystem::exists(thriftStateFileName) &&
      boost::filesystem::last_write_time(thriftStateFileName) >=
          boost::filesystem::last_write_time(follyStateFileName)) {
    return true;
  }
  XLOG(DBG2) << "Thrift state file does not exist or has timestamp older "
             << "than folly state. Using only folly file.";
  return false;
}

std::string getLocalHostname() {
  const size_t kHostnameMaxLen = 256; // from gethostname man page
  char hostname[kHostnameMaxLen];
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    XLOG(ERR) << "gethostname() failed";
    return "";
  }

  return hostname;
}

std::vector<ClientID> AllClientIDs() {
  return std::vector<ClientID>{
      ClientID::BGPD,
      ClientID::STATIC_ROUTE,
      ClientID::INTERFACE_ROUTE,
      ClientID::REMOTE_INTERFACE_ROUTE,
      ClientID::LINKLOCAL_ROUTE,
      ClientID::STATIC_INTERNAL,
      ClientID::OPENR,
  };
}

std::string switchRunStateStr(SwitchRunState runState) {
  return apache::thrift::util::enumNameSafe(runState);
}

folly::MacAddress getLocalMacAddress() {
  static std::mutex macComputeLock;
  static std::optional<folly::MacAddress> localMac;
  std::lock_guard g(macComputeLock);
  if (localMac) {
    return *localMac;
  }
  if (!FLAGS_mac.empty()) {
    localMac = folly::MacAddress(FLAGS_mac);
    return *localMac;
  }
  // TODO(t4543375): Get the base MAC address from the BMC.
  //
  // For now, we take the MAC address from eth0, and enable the
  // "locally administered" bit.  This MAC should be unique, and it's fine for
  // us to use a locally administered address for now.
  steady_clock::time_point begin = steady_clock::now();
  std::vector<std::string> cmd{"/sbin/ip", "address", "ls", FLAGS_mgmt_if};
  folly::Subprocess p(cmd, folly::Subprocess::Options().pipeStdout());
  auto out = p.communicate();
  p.waitChecked();
  auto idx = out.first.find("link/ether ");
  if (idx == std::string::npos) {
    throw std::runtime_error("unable to determine local mac address");
  }
  folly::MacAddress eth0Mac(out.first.substr(idx + 11, 17));
  localMac = folly::MacAddress::fromHBO(eth0Mac.u64HBO() | 0x0000020000000000);
  XLOG(DBG2) << "Calculating local mac address=" << *localMac << " took "
             << duration_cast<milliseconds>(steady_clock::now() - begin).count()
             << "ms";
  return *localMac;
}

std::vector<NextHopThrift> thriftNextHopsFromAddresses(
    const std::vector<network::thrift::BinaryAddress>& addrs) {
  std::vector<NextHopThrift> nhs;
  nhs.reserve(addrs.size());
  for (const auto& addr : addrs) {
    NextHopThrift nh;
    nh.address() = addr;
    nh.weight() = 0;
    nhs.emplace_back(std::move(nh));
  }
  return nhs;
}

IpPrefix toIpPrefix(const folly::CIDRNetwork& nw) {
  IpPrefix pfx;
  pfx.ip() = network::toBinaryAddress(nw.first);
  pfx.prefixLength() = nw.second;
  return pfx;
}

UnicastRoute makeDropUnicastRoute(
    const folly::CIDRNetwork& nw,
    AdminDistance admin) {
  return makeUnicastRouteHelper(nw, RouteForwardAction::DROP, admin);
}
UnicastRoute makeToCpuUnicastRoute(
    const folly::CIDRNetwork& nw,
    AdminDistance admin) {
  return makeUnicastRouteHelper(nw, RouteForwardAction::TO_CPU, admin);
}
UnicastRoute makeUnicastRoute(
    const folly::CIDRNetwork& nw,
    const std::vector<folly::IPAddress>& nhops,
    AdminDistance admin) {
  auto route = makeUnicastRouteHelper(nw, RouteForwardAction::NEXTHOPS, admin);
  std::vector<network::thrift::BinaryAddress> addrs;
  addrs.reserve(nhops.size());
  std::for_each(
      nhops.begin(), nhops.end(), [&addrs](const folly::IPAddress& ip) {
        addrs.emplace_back(facebook::network::toBinaryAddress(ip));
      });
  route.nextHops() = thriftNextHopsFromAddresses(addrs);
  return route;
}

PortID getPortID(
    SystemPortID sysPortId,
    const std::shared_ptr<SwitchState>& state) {
  auto sysPort = state->getSystemPorts()->getNode(sysPortId);
  return getPortID(sysPortId, sysPort->getSwitchId(), state);
}

PortID getPortID(
    const SystemPortID& sysPortId,
    SwitchID switchId,
    const std::shared_ptr<SwitchState>& state) {
  auto switchIdToSwitchInfo = state->getSwitchSettings()
                                  ->getSwitchSettings(HwSwitchMatcher(
                                      std::unordered_set<SwitchID>({switchId})))
                                  ->getSwitchIdToSwitchInfo();
  auto switchInfo = switchIdToSwitchInfo.find(switchId);
  if (switchInfo == switchIdToSwitchInfo.end()) {
    throw FbossError(
        "switchId: ", switchId, " not found in switchToSwitchInfo");
  }
  for (const auto& [matcher, ports] : std::as_const(*state->getPorts())) {
    if (HwSwitchMatcher(matcher).switchId() != switchId) {
      continue;
    }
    for (const auto& [_, port] : std::as_const(*ports)) {
      // Without relocation, fabric ports do not have a getSystemPortID-based
      // system port (they use the legacy offset scheme), so skip them here.
      if (!FLAGS_fabric_ports_uniform_local_offset &&
          port->getPortType() == cfg::PortType::FABRIC_PORT) {
        continue;
      }
      if (sysPortId ==
          getSystemPortID(
              port->getID(),
              port->getScope(),
              switchIdToSwitchInfo,
              switchId)) {
        return port->getID();
      }
    }
  }
  throw FbossError("No port found for sys port: ", sysPortId);
}

SystemPortID getSystemPortID(
    const PortID& portId,
    cfg::Scope portScope,
    const std::map<int64_t, cfg::SwitchInfo>& switchToSwitchInfo,
    SwitchID switchId) {
  auto switchInfo = switchToSwitchInfo.find(static_cast<int64_t>(switchId));
  if (switchInfo == switchToSwitchInfo.end()) {
    throw FbossError(
        "switchId: ", switchId, " not found in switchToSwitchInfo");
  }
  auto offset = portScope == cfg::Scope::GLOBAL
      ? switchInfo->second.globalSystemPortOffset()
      : switchInfo->second.localSystemPortOffset();
  if (!offset.has_value()) {
    throw FbossError("Global/local offset not set");
  }
  /*
   * System port is 1:1 with every interface and recycle port.
   * Interface is 1:1 with system port.
   * InterfaceID is chosen to be the same as systemPortID. Thus:
   * For multi ASIC switches, the the port ID range minimum must
   * taken into account while computing the interface or system port
   * IDs
   *
   * For eg:
   *
   * ----------------------------------------------
   * |   config        |   asic 0   |   asic 1    |
   * ----------------------------------------------
   * | sys port range  |   100-199  |  200-299    |
   * ----------------------------------------------
   * | port range      |   0-2047   | 2048-4096   |
   * ----------------------------------------------
   *
   * Interface of recycle port on asic 1 is 201.
   * Port ID of a recycle port in platform mapping will be 2049
   * Interface ID of recycle port = 200 + 2049 - 2048 = 201
   */
  auto portIdRange = *switchInfo->second.portIdRange();
  auto systemPortId =
      static_cast<int64_t>(portId) + *offset - *portIdRange.minimum();
  return SystemPortID(systemPortId);
}

SystemPortID getSystemPortID(
    const PortID& portId,
    const std::shared_ptr<SwitchState>& state,
    SwitchID switchId) {
  return getSystemPortID(
      portId,
      state->getPorts()->getNode(portId)->getScope(),
      state->getSwitchSettings()
          ->getSwitchSettings(
              HwSwitchMatcher(std::unordered_set<SwitchID>({switchId})))
          ->getSwitchIdToSwitchInfo(),
      switchId);
}

// API to get SystemPortID associated with fabric link monitoring fabric port
// IDs
SystemPortID getFabricLinkMonitoringSystemPortID(
    const PortID& portId,
    const std::shared_ptr<SwitchSettings>& switchSettings) {
  CHECK(switchSettings->getFabricLinkMonitoringSystemPortOffset().has_value());
  return SystemPortID(
      portId + *switchSettings->getFabricLinkMonitoringSystemPortOffset());
}

// API to derive the PortID associated with fabric link monitoring fabric
// SystemPortID
PortID getPortIdFromFabricLinkMonSystemPortID(
    const SystemPortID& systemPortId,
    const int32_t fabricLinkMonitoringSystemPortOffset) {
  return PortID(systemPortId - fabricLinkMonitoringSystemPortOffset);
}

SystemPortID getInbandSystemPortID(
    const std::shared_ptr<SwitchState>& state,
    SwitchID switchId) {
  const auto& switchId2Info = state->getSwitchSettings()
                                  ->getSwitchSettings(HwSwitchMatcher(
                                      std::unordered_set<SwitchID>({switchId})))
                                  ->getSwitchIdToSwitchInfo();
  auto switchInfoItr = switchId2Info.find(switchId);
  if (switchInfoItr == switchId2Info.end()) {
    throw FbossError("Unable to lookup switch info for : ", switchId);
  }
  return getInbandSystemPortID(switchId2Info, switchId);
}

SystemPortID getInbandSystemPortID(
    const std::map<int64_t, cfg::SwitchInfo>& switchId2Info,
    SwitchID switchId) {
  auto switchInfoItr = switchId2Info.find(static_cast<int64_t>(switchId));
  if (switchInfoItr == switchId2Info.end()) {
    throw FbossError("Unable to lookup switch info for : ", switchId);
  }
  if (!switchInfoItr->second.inbandPortId().has_value()) {
    throw FbossError("Inband port id not set for: ", switchId);
  }
  if (!switchInfoItr->second.globalSystemPortOffset().has_value()) {
    throw FbossError("Global sys port offset not set for  ", switchId);
  }
  /*
   * We need to derive inband port Ids in 2 scenarios.
   * Local switchIds - here we can use the generic getSystemPortID functions
   * which leverages port Id range information (we have that for local
   * switches). Remote switchIds - here we don't have the remote switch id's
   * port range. So we can't leverage the generic getSystemPortID function.
   * Alternatives
   * 1. Embed inband port id offset (instead of inband port ID)  in the
   * inbandPortId field in config. Rename the field as inbandPortOffset.
   * 2. Embed port id range in DsfNode struct.
   *
   * 1 is lighter weight and hence used right now.
   */
  return SystemPortID(
      *switchInfoItr->second.globalSystemPortOffset() +
      *switchInfoItr->second.inbandPortId());
}

// FIXME 2-stage DSF. First sys port range concep does
// not apply for 2-stage DSF
cfg::Range64 getFirstSwitchSystemPortIdRange(
    const std::map<int64_t, cfg::SwitchInfo>& switchToSwitchInfo) {
  for (const auto& [switchId, switchInfo] : switchToSwitchInfo) {
    // Only VOQ switches have system ports
    if (switchInfo.switchType() == cfg::SwitchType::VOQ &&
        switchInfo.switchIndex() == 0) {
      CHECK(switchInfo.systemPortRanges()->systemPortRanges()->size());
      return *switchInfo.systemPortRanges()->systemPortRanges()->begin();
    }
  }
  throw FbossError("No VOQ switch with switchIndex 0 found");
}

cfg::Range64 getCoveringSysPortRange(
    InterfaceID intf,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo) {
  return getCoveringSysPortRange(
      static_cast<int64_t>(intf), switchIdToSwitchInfo);
}

cfg::Range64 getCoveringSysPortRange(
    SystemPortID sysportId,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo) {
  return getCoveringSysPortRange(
      static_cast<int64_t>(sysportId), switchIdToSwitchInfo);
}

std::vector<PortID> getPortsForInterface(
    InterfaceID intfId,
    const std::shared_ptr<SwitchState>& state) {
  auto intf = state->getInterfaces()->getNodeIf(intfId);
  if (!intf) {
    return {};
  }
  std::vector<PortID> ports;
  switch (intf->getType()) {
    case cfg::InterfaceType::VLAN: {
      auto vlanId = intf->getVlanID();
      auto vlan = state->getVlans()->getNodeIf(vlanId);
      if (vlan) {
        for (const auto& memberPort : vlan->getPortsInfo()) {
          ports.emplace_back(memberPort.first);
        }
      }
    } break;
    case cfg::InterfaceType::SYSTEM_PORT:
      ports.push_back(getPortID(intf->getSystemPortID().value(), state));
      break;
    case cfg::InterfaceType::PORT:
      // A port router interface is bound to either a physical port or an
      // aggregate port, and an aggregate stands for all of its members.
      if (auto aggregatePortID = intf->getAggregatePortIDf()) {
        auto aggPort = state->getAggregatePorts()->getNodeIf(*aggregatePortID);
        if (aggPort) {
          for (const auto& subport : aggPort->sortedSubports()) {
            ports.push_back(subport.portID);
          }
        }
      } else {
        ports.push_back(intf->getPortID());
      }
      break;
  }
  return ports;
}

std::optional<PortID> getInterfacePortToReach(
    const std::shared_ptr<SwitchState>& state,
    const folly::IPAddress& ipAddr) {
  auto intf = state->getInterfaces()->getIntfToReach(RouterID(0), ipAddr);
  if (!intf) {
    return std::nullopt;
  }
  auto intfType = intf->getType();
  std::optional<PortID> port{};
  switch (intfType) {
    case cfg::InterfaceType::VLAN:
      break;
    case cfg::InterfaceType::SYSTEM_PORT:
      port = getPortID(intf->getSystemPortID().value(), state);
      break;
    case cfg::InterfaceType::PORT:
      // For an aggregate port router interface, any member port reaches it.
      if (auto aggregatePortID = intf->getAggregatePortIDf()) {
        auto aggPort = state->getAggregatePorts()->getNodeIf(*aggregatePortID);
        if (aggPort && !aggPort->sortedSubports().empty()) {
          port = aggPort->sortedSubports().begin()->portID;
        }
      } else {
        port = intf->getPortID();
      }
      break;
  }
  return port;
}

bool isAnyInterfacePortInLoopbackMode(
    std::shared_ptr<SwitchState> swState,
    const std::shared_ptr<Interface> interface) {
  // walk all ports for the given interface and ensure that there are no
  // loopbacks configured This is mostly for the agent tests for which we dont
  // want to flood grat arp when we are in loopback resulting in these pkts
  // getting looped back forever
  for (auto portId : getPortsForInterface(interface->getID(), swState)) {
    auto port = swState->getPorts()->getNodeIf(portId);
    if (port && port->getLoopbackMode() != cfg::PortLoopbackMode::NONE) {
      XLOG(DBG2) << "Port: " << port->getName()
                 << " in interface: " << interface->getID()
                 << " is in loopback mode";
      return true;
    }
  }
  return false;
}

bool isAnyInterfacePortRecycleOrEventorPort(
    std::shared_ptr<SwitchState> swState,
    const std::shared_ptr<Interface> interface) {
  for (auto portId : getPortsForInterface(interface->getID(), swState)) {
    auto port = swState->getPorts()->getNodeIf(portId);
    if (port &&
        (port->getPortType() == cfg::PortType::RECYCLE_PORT ||
         port->getPortType() == cfg::PortType::EVENTOR_PORT)) {
      return true;
    }
  }
  return false;
}

StopWatch::StopWatch(std::optional<std::string> name, bool json)
    : name_(name), json_(json), startTime_(std::chrono::steady_clock::now()) {}

StopWatch::~StopWatch() {
  if (!name_) {
    return;
  }
  auto durationMillseconds = msecsElapsed().count();
  if (json_) {
    folly::dynamic time = folly::dynamic::object;
    time[*name_] = durationMillseconds;
    std::cout << time << std::endl;
  } else {
    XLOG(DBG2) << *name_ << " : " << durationMillseconds << " msecs";
  }
}

void enableExactMatch(std::string& yamlCfg) {
  std::string globalSt("global:\n");
  std::string emSt("fpem_mem_entries:");
  std::string emWidthSt("fpem_mem_entries_width:");
  std::size_t glPos = yamlCfg.find(globalSt);
  std::size_t emPos = yamlCfg.find(emSt);
  std::size_t emWidthPos = yamlCfg.find(emWidthSt);
  static const re2::RE2 emPattern(
      "(fpem_mem_entries: )(0x[0-9a-fA-F]+|[0-9]+)(\n)");
  static const re2::RE2 emWidthPattern(
      "(fpem_mem_entries_width: )(0x[0-9a-fA-F]+|[0-9]+)(\n)");
  if (glPos != std::string::npos) {
    if (emPos == std::string::npos && emWidthPos == std::string::npos) {
      yamlCfg.replace(
          glPos,
          globalSt.length(),
          "global:\n      fpem_mem_entries_width: 1\n      fpem_mem_entries: 65536\n");
    } else if (emPos != std::string::npos && emWidthPos == std::string::npos) {
      yamlCfg.replace(
          glPos,
          globalSt.length(),
          "global:\n      fpem_mem_entries_width: 1\n");
      re2::RE2::Replace(&yamlCfg, emPattern, "fpem_mem_entries: 65536\n");
    } else {
      if (emPos != std::string::npos) {
        re2::RE2::Replace(&yamlCfg, emPattern, "fpem_mem_entries: 65536\n");
      }
      if (emWidthPos != std::string::npos) {
        re2::RE2::Replace(
            &yamlCfg, emWidthPattern, "fpem_mem_entries_width: 1\n");
      }
    }
  }
}

// Avoid template linker errors by listing possible template instantiations:
// https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
template std::shared_ptr<ArpEntry> getNeighborEntryForIP<ArpEntry>(
    const std::shared_ptr<SwitchState>& state,
    const std::shared_ptr<Interface>& intf,
    const folly::IPAddress& ipAddr);
template std::shared_ptr<NdpEntry> getNeighborEntryForIP<NdpEntry>(
    const std::shared_ptr<SwitchState>& state,
    const std::shared_ptr<Interface>& intf,
    const folly::IPAddress& ipAddr);

template <typename NeighborEntryT>
std::shared_ptr<NeighborEntryT> getNeighborEntryForIPAndIntf(
    const std::shared_ptr<Interface>& intf,
    const folly::IPAddress& ipAddr) {
  std::shared_ptr<NeighborEntryT> entry{nullptr};

  if constexpr (std::is_same_v<NeighborEntryT, ArpEntry>) {
    const auto& arpTable = intf->getArpTable();
    entry = arpTable->getEntryIf(ipAddr.asV4());
  } else {
    const auto& nbrTable = intf->getNdpTable();
    entry = nbrTable->getEntryIf(ipAddr.asV6());
  }
  return entry;
}

template <typename NeighborEntryT>
std::shared_ptr<NeighborEntryT> getNeighborEntryForIP(
    const std::shared_ptr<SwitchState>& /* state */,
    const std::shared_ptr<Interface>& intf,
    const folly::IPAddress& ipAddr) {
  return getNeighborEntryForIPAndIntf<NeighborEntryT>(intf, ipAddr);
}

template <typename VlanOrIntfT>
std::optional<VlanID> getVlanIDFromVlanOrIntf(
    const std::shared_ptr<VlanOrIntfT>& vlanOrIntf) {
  std::optional<VlanID> vlanID{std::nullopt};

  if (vlanOrIntf) {
    if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
      vlanID = vlanOrIntf->getID();
    } else {
      switch (vlanOrIntf->getType()) {
        case cfg::InterfaceType::VLAN:
          return vlanOrIntf->getVlanID();
        case cfg::InterfaceType::PORT:
        case cfg::InterfaceType::SYSTEM_PORT:
          return std::nullopt;
      }
    }
  }

  return vlanID;
}

// Explicit instantiation to avoid linker errors
// https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl

template std::optional<VlanID> getVlanIDFromVlanOrIntf<Vlan>(
    const std::shared_ptr<Vlan>& vlanOrIntf);
template std::optional<VlanID> getVlanIDFromVlanOrIntf<Interface>(
    const std::shared_ptr<Interface>& vlanOrIntf);

template <typename NTableT>
std::shared_ptr<NTableT> getNeighborTableForVlan(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlanID) {
  auto vlan = state->getVlans()->getNode(vlanID);
  auto intf = state->getInterfaces()->getNode(vlan->getInterfaceID());
  if constexpr (std::is_same_v<NTableT, ArpTable>) {
    return intf->getArpTable();
  } else {
    return intf->getNdpTable();
  }
}

// Explicit instantiation to avoid linker errors
// https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
template std::shared_ptr<ArpTable> getNeighborTableForVlan(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlanID);
template std::shared_ptr<NdpTable> getNeighborTableForVlan(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlanID);

OperDeltaFilter::OperDeltaFilter(SwitchID switchId) : switchId_(switchId) {}

std::optional<fsdb::OperDelta> OperDeltaFilter::filter(
    const fsdb::OperDelta& delta,
    int index) const {
  fsdb::OperDelta result{};
  result.protocol() = *delta.protocol();
  if (delta.metadata().has_value()) {
    result.metadata() = *delta.metadata();
  }

  for (const auto& change : *delta.changes()) {
    auto& path = *change.path()->raw();
    if (path.size() < index + 1) {
      continue;
    }
    const auto& matcherStr = path[index];
    auto iter = matchersCache_.find(matcherStr);
    if (iter == matchersCache_.end()) {
      // if matcher string is not found in cache, cache it.
      try {
        HwSwitchMatcher matcher(matcherStr);
        auto emplaced =
            matchersCache_.emplace(matcherStr, HwSwitchMatcher(matcherStr));
        iter = emplaced.first;
      } catch (const FbossError& error) {
        XLOG(ERR) << "Error while processing an oper delta token for path : "
                  << getOperPath(path) << ": " << error.what();
        continue;
      }
    }
    CHECK(iter != matchersCache_.end());
    if (iter->second.has(switchId_)) {
      result.changes()->push_back(change);
    }
  }
  if (result.changes()->empty()) {
    return std::nullopt;
  }
  return result;
}

AdminDistance getAdminDistanceForClientId(
    const cfg::SwitchConfig& config,
    int clientId) {
  auto distance = config.clientIdToAdminDistance()->find(clientId);
  if (distance == config.clientIdToAdminDistance()->end()) {
    // In case we get a client id we don't know about
    XLOG(ERR) << "No admin distance mapping available for client id "
              << clientId << ". Using default distance - MAX_ADMIN_DISTANCE";
    return AdminDistance::MAX_ADMIN_DISTANCE;
  }

  if (XLOG_IS_ON(DBG3)) {
    auto clientName = apache::thrift::util::enumNameSafe(ClientID(clientId));
    auto distanceString = apache::thrift::util::enumNameSafe(
        static_cast<AdminDistance>(distance->second));
    XLOG(DBG3) << "Mapping client id " << clientId << " (" << clientName
               << ") to admin distance " << distance->second << " ("
               << distanceString << ").";
  }

  return static_cast<AdminDistance>(distance->second);
}

size_t getNumActiveFabricPorts(
    const std::shared_ptr<SwitchState>& state,
    const HwSwitchMatcher& matcher) {
  auto portMap = state->getPorts()->getMapNodeIf(matcher);
  if (!portMap) {
    return 0;
  }

  return std::count_if(
      std::begin(std::as_const(*portMap)),
      std::end(std::as_const(*portMap)),
      [](const auto& port) {
        return port.second->getPortType() == cfg::PortType::FABRIC_PORT &&
            port.second->isActive().has_value() &&
            port.second->isActive().value() == true;
      });
}

bool isSwitchErrorFirmwareIsolate(
    const std::optional<uint32_t>& numActiveFabricPortsAtFwIsolate,
    const std::shared_ptr<SwitchSettings>& switchSettings) {
  // This is invoked from Firmware Isolate context, and thus
  // numActiveFabricPortsAtFwIsolate must always have value
  CHECK(numActiveFabricPortsAtFwIsolate.has_value());

  // If the Firmware isolated the device even though the number of active
  // fabric links is more than the min links to remain in the VOQ domain,
  // then treat it as error. This will be implemented unrecoverable error.
  //
  // If min links to remain in the VOQ domain is not set, allow to recover.
  // In practice, this threshold will always be set.
  return switchSettings->getMinLinksToRemainInVOQDomain().has_value()
      ? numActiveFabricPortsAtFwIsolate.value() >
          switchSettings->getMinLinksToRemainInVOQDomain().value()
      : false;
}

/*
 * SwitchDrainState can be modified from configuration.
 * However, some VOQ switch implementations require that the switch must be
 * initialized as DRAINED and can be in UNDRAINED state iff certain
 * number (pre-configured threshold) of fabric links are ACTIVE.
 *
 * Thus, if configured switch state is
 *    - DRAINED => return DRAINED.
 *    - UNDRAINED => compute DRAINED/UNDRAINED based on the number of
 *                   fabric links and threshold and return that value.
 */
cfg::SwitchDrainState computeActualSwitchDrainState(
    const std::shared_ptr<SwitchSettings>& switchSettings,
    int numActiveFabricPorts) {
  CHECK(switchSettings);

  // For non-VOQ switches, actual switch drain state is the same as desired
  // switch drain state.
  if (switchSettings->getSwitchIdsOfType(cfg::SwitchType::VOQ).size() == 0) {
    return switchSettings->getSwitchDrainState();
  }

  // TODO(skhare)
  // Once SwitchSettingsFields are made unique for HwSwitch,
  // SwitchSettingsFields will carry switchInfo instead of
  // switchIdToSwitchInfo. At that time, ASSERT for SwitchType VOQ here.

  cfg::SwitchDrainState newSwitchDrainState;

  switch (switchSettings->getSwitchDrainState()) {
    case cfg::SwitchDrainState::DRAINED:
      // If the desired (configured) state is DRAINED, actual state will be
      // DRAINED.
      newSwitchDrainState = cfg::SwitchDrainState::DRAINED;
      break;
    case cfg::SwitchDrainState::UNDRAINED:
      // If the desired (configured) state is UNDRAINED, actual state will be
      // DRAINED/UNDRAINED depending on the threshold.
      switch (switchSettings->getActualSwitchDrainState()) {
        case cfg::SwitchDrainState::UNDRAINED:
          if (switchSettings->getMinLinksToRemainInVOQDomain().has_value() &&
              numActiveFabricPorts <
                  switchSettings->getMinLinksToRemainInVOQDomain().value()) {
            newSwitchDrainState = cfg::SwitchDrainState::DRAINED;
          } else {
            newSwitchDrainState = cfg::SwitchDrainState::UNDRAINED;
          }
          break;
        case cfg::SwitchDrainState::DRAINED:
          if (switchSettings->getMinLinksToJoinVOQDomain().has_value() &&
              numActiveFabricPorts >=
                  switchSettings->getMinLinksToJoinVOQDomain().value()) {
            newSwitchDrainState = cfg::SwitchDrainState::UNDRAINED;
          } else {
            newSwitchDrainState = cfg::SwitchDrainState::DRAINED;
          }
          break;
        case cfg::SwitchDrainState::DRAINED_DUE_TO_ASIC_ERROR:
          // This is not a recoverable drain state.
          newSwitchDrainState =
              cfg::SwitchDrainState::DRAINED_DUE_TO_ASIC_ERROR;
          break;
      }
      break;
    case cfg::SwitchDrainState::DRAINED_DUE_TO_ASIC_ERROR:
      throw FbossError("Valid desired DRAINED states are {DRAINED, UNDRAINED}");
  }

  return newSwitchDrainState;
}

uint64_t getMacOui(const folly::MacAddress macAddress) {
  return macAddress.u64HBO() & 0x0000FFFFFF000000;
}

/*
 * For Multi-NPU devices, each NPU appears once in the DsfNodeMap with
 * identical name but different switchID.
 * Build a switchName to (sorted switchIDs) map.
 * e.g. rdsw002 => [4, 8]
 * assign switchIndex to switchID starting 0 i.e.
 * [4 => 0], [8 => 1]...
 */
std::unordered_map<SwitchID, SwitchIndex> computeSwitchIdToSwitchIndex(
    const std::shared_ptr<MultiSwitchDsfNodeMap>& dsfNodeMap) {
  std::unordered_map<std::string, std::set<SwitchID>> switchNameToSwitchIDs;
  for (const auto& [_, dsfNodes] : std::as_const(*dsfNodeMap)) {
    for (const auto& [_, node] : std::as_const(*dsfNodes)) {
      switchNameToSwitchIDs[node->getName()].insert(node->getSwitchId());
    }
  }

  std::unordered_map<SwitchID, SwitchIndex> switchIdToSwitchIndex;
  for (const auto& [switchName, switchIDs] : switchNameToSwitchIDs) {
    auto switchIndex = 0;
    for (const auto& switchID : switchIDs) {
      switchIdToSwitchIndex[switchID] = switchIndex++;
    }
  }

  return switchIdToSwitchIndex;
}

std::set<SwitchID> getAllSwitchIDsForSwitch(
    const std::shared_ptr<MultiSwitchDsfNodeMap>& dsfNodeMap,
    const SwitchID& switchID) {
  auto dsfNode = dsfNodeMap->getNodeIf(switchID);
  CHECK(dsfNode);

  auto switchName = dsfNode->getName();
  std::set<SwitchID> allSwitchIDs;
  for (const auto& [_, dsfNodes] : std::as_const(*dsfNodeMap)) {
    for (const auto& [_, node] : std::as_const(*dsfNodes)) {
      if (node->getName() == switchName) {
        allSwitchIDs.insert(node->getSwitchId());
      }
    }
  }

  return allSwitchIDs;
}

uint32_t getRemotePortOffset(const PlatformType platformType) {
  switch (platformType) {
    case PlatformType::PLATFORM_MERU800BFA:
    case PlatformType::PLATFORM_MERU800BFA_P1:
      return 0;
    case PlatformType::PLATFORM_MERU800BIA:
    case PlatformType::PLATFORM_MERU800BIAB:
    case PlatformType::PLATFORM_MERU800BIAC:
    case PlatformType::PLATFORM_JANGA800BIC:
      return 1024;

    default:
      return 0;
  }
}

std::string runShellCmd(const std::string& cmd) {
  std::array<char, 4096> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(
      popen(cmd.c_str(), "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }

  XLOG(DBG4) << "Command: [ " << cmd << " ] output: [ " << result << " ]";
  return result;
}

InterfaceID getInbandPortIntfID(
    const std::shared_ptr<SwitchState>& state,
    const SwitchID& switchId) {
  auto dsfNode = state->getDsfNodes()->getNodeIf(switchId);
  CHECK(dsfNode);

  // Inband port is of Global scope by definition
  auto globalSystemPortOffset = dsfNode->getGlobalSystemPortOffset();
  CHECK(globalSystemPortOffset.has_value());

  auto recyclePortId =
      InterfaceID(*globalSystemPortOffset + kRecyclePortIdOffset);

  return recyclePortId;
}

std::pair<std::string, std::string> getExpectedNeighborAndPortName(
    const cfg::Port& port) {
  CHECK_EQ(port.expectedNeighborReachability()->size(), 1);
  const auto& expectedNeighbor = port.expectedNeighborReachability()->cbegin();
  std::string neighborName = *expectedNeighbor->remoteSystem();
  std::string neighborPortName = *expectedNeighbor->remotePort();
  return std::make_pair(neighborName, neighborPortName);
};

const facebook::fboss::PlatformMapping* FOLLY_NULLABLE
getPlatformMappingForPlatformType(
    const facebook::fboss::PlatformType platformType) {
  switch (platformType) {
    case facebook::fboss::PlatformType::PLATFORM_MERU800BFA: {
      static facebook::fboss::Meru800bfaPlatformMapping meru800bfa{
          true /*multiNpuPlatformMapping*/};
      return &meru800bfa;
    }
    case facebook::fboss::PlatformType::PLATFORM_MERU800BFA_P1: {
      static facebook::fboss::Meru800bfaP1PlatformMapping meru800bfa{
          true /*multiNpuPlatformMapping*/};
      return &meru800bfa;
    }
    case facebook::fboss::PlatformType::PLATFORM_MERU800BIA:
    case facebook::fboss::PlatformType::PLATFORM_MERU800BIAB:
    case facebook::fboss::PlatformType::PLATFORM_MERU800BIAC: {
      static facebook::fboss::Meru800biaPlatformMapping meru800bia;
      return &meru800bia;
    }
    case facebook::fboss::PlatformType::PLATFORM_JANGA800BIC: {
      static facebook::fboss::Janga800bicPlatformMapping janga800bic{
          !FLAGS_type_dctype1_janga /*multiNpuPlatformMapping*/
      };
      return &janga800bic;
    }
    default:
      break;
  }
  return nullptr;
}

int getRemoteSwitchID(
    const cfg::SwitchConfig* cfg,
    const cfg::Port& port,
    const std::unordered_map<std::string, std::vector<uint32_t>>&
        switchNameToSwitchIds) {
  auto [neighborName, neighborPortName] = getExpectedNeighborAndPortName(port);
  CHECK(!switchNameToSwitchIds.at(neighborName).empty());
  auto baseSwitchId = *std::min_element(
      switchNameToSwitchIds.at(neighborName).cbegin(),
      switchNameToSwitchIds.at(neighborName).cend());
  auto dsfNodeItr = cfg->dsfNodes()->find(baseSwitchId);
  CHECK(dsfNodeItr != cfg->dsfNodes()->end());

  const auto platformMapping =
      getPlatformMappingForPlatformType(*dsfNodeItr->second.platformType());

  if (!platformMapping) {
    throw FbossError(
        "Unable to find platform mapping for port: ",
        *port.logicalID(),
        " expected neighbor");
  }
  auto virtualDeviceId = platformMapping->getVirtualDeviceID(neighborPortName);
  if (!virtualDeviceId.has_value()) {
    throw FbossError(
        "Unable to find virtual device id for port: ",
        *port.logicalID(),
        " expected neighbor");
  }

  const auto& hwAsic = getHwAsicForAsicType(*dsfNodeItr->second.asicType());
  auto numVirtualDevices = hwAsic.getVirtualDevices();
  auto remoteSwitchId = baseSwitchId +
      (virtualDeviceId.value() / numVirtualDevices) * numVirtualDevices;

  return remoteSwitchId;
};

CpuCosQueueId hwQueueIdToCpuCosQueueId(
    uint8_t hwQueueId,
    const HwAsic* asic,
    HwSwitchFb303Stats* switchStats) {
  if (hwQueueId == asic->getHiPriCpuQueueId()) {
    return CpuCosQueueId::HIPRI;
  } else if (hwQueueId == asic->getMidPriCpuQueueId()) {
    return CpuCosQueueId::MIDPRI;
  } else if (hwQueueId == static_cast<uint8_t>(CpuCosQueueId::LOPRI)) {
    return CpuCosQueueId::LOPRI;
  } else if (hwQueueId == static_cast<uint8_t>(CpuCosQueueId::DEFAULT)) {
    return CpuCosQueueId::DEFAULT;
  }
  XLOG_EVERY_N(ERR, 10000) << "Got Invalid hwQueueId " << hwQueueId;
  switchStats->invalidQueueRxPackets();
  return CpuCosQueueId::LOPRI;
}

int numFabricLevels(const std::map<int64_t, cfg::DsfNode>& dsfNodes) {
  int maxFabricLevel{0};
  std::for_each(
      dsfNodes.begin(),
      dsfNodes.end(),
      [&maxFabricLevel](const auto& idAndDsfNode) {
        const auto& dsfNode = idAndDsfNode.second;
        int nodeFabricLevel = dsfNode.fabricLevel().value_or(0);
        if (nodeFabricLevel == 0 &&
            dsfNode.type() == cfg::DsfNodeType::FABRIC_NODE) {
          nodeFabricLevel = 1;
        }
        maxFabricLevel = std::max(maxFabricLevel, nodeFabricLevel);
      });
  return maxFabricLevel;
}

// Get set of L1 fabric ports that are connected to L2 switches by examining DSF
// nodes in switch state. This function is only valid for L1 fabric nodes in a
// dual stage topology. First identifies all L2 nodes (fabricLevel == 2),
// then finds fabric ports whose expected neighbor matches an L2 node name.
// Returns a set of PortIDs for efficient lookup. Returns empty set with error
// log if called on non-L1 fabric node or in non-dual stage topology.
std::set<PortID> getL2ConnectedL1FabricPorts(
    const std::shared_ptr<SwitchState>& state) {
  std::set<PortID> l2ConnectedPorts;
  std::set<std::string> l2NodeNames;

  // Find all DSF nodes with fabricLevel == 2 (L2 nodes)
  bool isDualStage = false;
  bool isL1FabricNode = false;
  for (const auto& matcherAndNodes : std::as_const(*state->getDsfNodes())) {
    for (const auto& [switchId, dsfNode] :
         std::as_const(*matcherAndNodes.second)) {
      if (dsfNode->getType() == cfg::DsfNodeType::FABRIC_NODE &&
          dsfNode->getFabricLevel().has_value()) {
        if (dsfNode->getFabricLevel().value() == 2) {
          isDualStage = true;
          l2NodeNames.insert(dsfNode->getName());
        } else if (dsfNode->getFabricLevel().value() == 1) {
          isL1FabricNode = true;
        }
      }
    }
  }

  // Check if this is a dual stage topology and return early otherwise
  if (!isDualStage) {
    XLOG(DBG3) << "getL2ConnectedL1FabricPorts called on non-dual stage "
               << "topology. No L2 fabric nodes found!";
    return l2ConnectedPorts;
  }

  if (!isL1FabricNode) {
    XLOG(ERR) << "getL2ConnectedL1FabricPorts called on non-L1 fabric node."
              << " This function is only valid for L1 fabric nodes in dual "
              << "stage topology.";
    return l2ConnectedPorts;
  }

  // Find all fabric ports that have expected neighbor as L2 node
  for (const auto& portMap : std::as_const(*state->getPorts())) {
    for (const auto& [portIdRaw, port] : std::as_const(*portMap.second)) {
      PortID portId = PortID(portIdRaw);
      if (port->getPortType() == cfg::PortType::FABRIC_PORT &&
          !port->getExpectedNeighborValues()->empty()) {
        const auto& expectedNeighbors =
            port->getExpectedNeighborValues()->toThrift();
        const auto& neighbor = expectedNeighbors.front();
        if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
                neighbor.remoteSystem()) &&
            l2NodeNames.contains(*neighbor.remoteSystem())) {
          l2ConnectedPorts.insert(portId);
        }
      }
    }
  }

  return l2ConnectedPorts;
}

const std::vector<cfg::AclLookupClass>& getToCpuClassIds() {
  static const std::vector<cfg::AclLookupClass> toCpuClassIds = {
      cfg::AclLookupClass::DST_CLASS_L3_LOCAL_1,
      cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2,
  };
  return toCpuClassIds;
}

bool isStringInFile(
    const std::string& filename,
    const std::string& str,
    int maxLinesToSearch) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    return false;
  }
  std::string line;
  for (int i = 0; i < maxLinesToSearch && std::getline(file, line); ++i) {
    if (line.find(str) != std::string::npos) {
      return true;
    }
  }

  return false;
}

std::optional<VlanID> getDefaultTxVlanIdIf(
    const std::shared_ptr<SwitchSettings>& settings) {
  if (auto defaultVlan = settings->getDefaultVlan()) {
    return VlanID(*defaultVlan);
  }
  return std::nullopt;
}

std::optional<VlanID> getDefaultTxVlanId(
    const std::shared_ptr<SwitchSettings>& settings) {
  auto vlanId = getDefaultTxVlanIdIf(settings);
  if (!vlanId) {
    throw FbossError("default tx vlan not found");
  }
  return vlanId;
}

InterfaceID getInterfaceIDForPort(
    PortID portID,
    const std::shared_ptr<SwitchState>& state) {
  auto port = state->getPorts()->getNodeIf(portID);
  if (!port) {
    throw FbossError("Port ", portID, " not found");
  }

  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (const auto& intfIter : std::as_const(*intfMap)) {
      const auto& intf = intfIter.second;
      switch (intf->getType()) {
        case cfg::InterfaceType::SYSTEM_PORT: {
          auto sysPortID = intf->getSystemPortID();
          CHECK(sysPortID.has_value());
          if (portID == getPortID(sysPortID.value(), state)) {
            return intf->getID();
          }
        } break;
        case cfg::InterfaceType::VLAN:
          if (intf->getVlanID() == port->getIngressVlan()) {
            return intf->getID();
          }
          break;
        case cfg::InterfaceType::PORT:
          if (auto aggregatePortID = intf->getAggregatePortIDf()) {
            // An aggregate port router interface covers all of its members.
            auto aggPort =
                state->getAggregatePorts()->getNodeIf(*aggregatePortID);
            if (aggPort && aggPort->isMemberPort(portID)) {
              return intf->getID();
            }
          } else if (intf->getPortID() == portID) {
            return intf->getID();
          }
          break;
      }
    }
  }

  throw FbossError("Interface not found for port ", portID);
}

bool isPortDrained(
    const std::shared_ptr<SwitchState>& state,
    const Port* port,
    SwitchID portSwitchId) {
  const auto& switchSettings = state->getSwitchSettings()->getSwitchSettings(
      HwSwitchMatcher(std::unordered_set<SwitchID>({portSwitchId})));
  return switchSettings->isSwitchDrained() || port->isDrained();
}

} // namespace facebook::fboss
