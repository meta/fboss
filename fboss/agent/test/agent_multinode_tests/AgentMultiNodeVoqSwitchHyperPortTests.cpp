// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>

#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeDsfUtils.h"
#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeTest.h"
#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeUtils.h"

#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/thrift_client_utils/ThriftClientUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

class AgentMultiNodeVoqSwitchHyperPortTest : public AgentMultiNodeTest {
 private:
  struct PrimarySwitchFlapSnapshot {
    std::map<std::string, int64_t> hyperPortFlapCounts;
    std::map<std::string, int64_t> memberLinkStateFlapCounts;
    std::map<std::string, std::optional<int64_t>>
        remoteHyperPortNdpResolvedSince;
  };

  std::pair<folly::IPAddressV6, int16_t> kGetRoutePrefixAndPrefixLength()
      const {
    return std::make_pair(folly::IPAddressV6("2001:0db8:85a3::"), 64);
  }

  std::string getRemoteEdsw(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo) const {
    auto myHostname = topologyInfo->getMyHostname();
    for (const auto& edsw : topologyInfo->getEdsws()) {
      if (edsw != myHostname) {
        return edsw;
      }
    }
    throw FbossError("No remote EDSW found for ", myHostname);
  }

  // Get non-link-local IPv6 IPs for hyper port interfaces on the given EDSW.
  // Returns pairs of (interfaceID, IP) for each hyper port.
  std::vector<std::pair<int32_t, folly::IPAddressV6>> getHyperPortInterfaceIPs(
      const std::string& edsw) const {
    // Get aggregate port keys to identify hyper port interfaces
    std::vector<AggregatePortThrift> aggregatePorts;
    auto swAgentClient = utility::getSwAgentThriftClient(edsw);
    swAgentClient->sync_getAggregatePortTable(aggregatePorts);

    std::set<int32_t> aggPortKeys;
    for (const auto& aggPort : aggregatePorts) {
      aggPortKeys.insert(*aggPort.key());
    }

    // Get system port range to compute hyper port interface IDs
    auto switchNameToSystemPortRanges =
        topologyInfo_->getSwitchNameToSystemPortRanges();
    CHECK(switchNameToSystemPortRanges.count(edsw))
        << "No system port ranges for EDSW: " << edsw;
    auto ranges = switchNameToSystemPortRanges.at(edsw);
    CHECK_GE(ranges.systemPortRanges()->size(), 1);
    auto systemPortMin = *ranges.systemPortRanges()->front().minimum();

    // Hyper port intfID = systemPortMin + aggregatePortKey
    std::set<int32_t> hyperPortIntfIDs;
    for (auto key : aggPortKeys) {
      hyperPortIntfIDs.insert(int32_t(systemPortMin) + key);
    }

    // Get IPs for those interfaces
    std::vector<std::pair<int32_t, folly::IPAddressV6>> result;
    for (const auto& [_, rif] : utility::getIntfIdToIntf(edsw)) {
      if (hyperPortIntfIDs.count(*rif.interfaceId()) == 0) {
        continue;
      }
      for (const auto& ipPrefix : *rif.address()) {
        auto ip = folly::IPAddress::fromBinary(
            folly::ByteRange(
                reinterpret_cast<const unsigned char*>(
                    ipPrefix.ip()->addr()->data()),
                ipPrefix.ip()->addr()->size()));
        if (ip.isV6() && !ip.isLinkLocal()) {
          result.emplace_back(*rif.interfaceId(), ip.asV6());
          break;
        }
      }
    }

    return result;
  }

  PrimarySwitchFlapSnapshot getPrimarySwitchFlapSnapshot(
      const std::string& primaryEdsw,
      const std::vector<std::pair<int32_t, folly::IPAddressV6>>& remoteIPs)
      const {
    PrimarySwitchFlapSnapshot snapshot;

    std::vector<AggregatePortThrift> aggregatePorts;
    auto swAgentClient = utility::getSwAgentThriftClient(primaryEdsw);
    swAgentClient->sync_getAggregatePortTable(aggregatePorts);
    auto portIdToPortInfo = utility::getPortIdToPortInfo(primaryEdsw);
    auto counters = utility::getCounterNameToCount(primaryEdsw);

    for (const auto& aggPort : aggregatePorts) {
      auto aggName = aggPort.name().value();
      snapshot.hyperPortFlapCounts[aggName] =
          utility::getFb303CounterValue(counters, aggName, "flaps");

      for (const auto& member : aggPort.memberPorts().value()) {
        auto portId = member.memberPortID().value();
        auto portInfoIt = portIdToPortInfo.find(portId);
        CHECK(portInfoIt != portIdToPortInfo.end())
            << "Port " << portId << " not found on " << primaryEdsw;
        auto portName = portInfoIt->second.name().value();
        snapshot.memberLinkStateFlapCounts[portName] =
            utility::getFb303CounterValue(
                counters, portName, "link_state.flap");
      }
    }

    auto ndpEntries = utility::getNdpEntries(primaryEdsw);
    for (const auto& [_, remoteIP] : remoteIPs) {
      auto remoteIpStr = remoteIP.str();
      auto ndpEntryIt = std::find_if(
          ndpEntries.begin(), ndpEntries.end(), [&remoteIP](const auto& entry) {
            auto entryIP = folly::IPAddress::fromBinary(
                folly::ByteRange(
                    folly::StringPiece(entry.ip().value().addr().value())));
            return entryIP == remoteIP;
          });
      CHECK(ndpEntryIt != ndpEntries.end())
          << "Missing NDP entry for remote hyper port IP " << remoteIpStr
          << " on " << primaryEdsw;

      snapshot.remoteHyperPortNdpResolvedSince[remoteIpStr] =
          ndpEntryIt->resolvedSince().has_value()
          ? std::make_optional(*ndpEntryIt->resolvedSince())
          : std::nullopt;
    }

    return snapshot;
  }

  bool verifyPrimarySwitchFlapSnapshotUnchanged(
      const std::string& primaryEdsw,
      const PrimarySwitchFlapSnapshot& baselineSnapshot,
      const PrimarySwitchFlapSnapshot& currentSnapshot) const {
    if (currentSnapshot.hyperPortFlapCounts !=
        baselineSnapshot.hyperPortFlapCounts) {
      XLOG(DBG2) << "Hyper port flap counters changed on " << primaryEdsw;
      return false;
    }
    if (currentSnapshot.memberLinkStateFlapCounts !=
        baselineSnapshot.memberLinkStateFlapCounts) {
      XLOG(DBG2) << "Hyper port member link_state.flap counters changed on "
                 << primaryEdsw;
      return false;
    }
    if (currentSnapshot.remoteHyperPortNdpResolvedSince !=
        baselineSnapshot.remoteHyperPortNdpResolvedSince) {
      XLOG(DBG2) << "Hyper port NDP resolvedSince changed on " << primaryEdsw;
      return false;
    }

    return true;
  }

  // Compute one neighbor per hyper port (aggregate port) on the given EDSW.
  // Used by the traffic distribution test to create ECMP nexthops.
  std::vector<utility::Neighbor> computeNeighborsForEdswHyperPorts(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo,
      const std::string& edsw) const {
    auto hyperPortIPs = getHyperPortInterfaceIPs(edsw);

    // Get aggregate ports for port ID mapping
    std::vector<AggregatePortThrift> aggregatePorts;
    auto swAgentClient = utility::getSwAgentThriftClient(edsw);
    swAgentClient->sync_getAggregatePortTable(aggregatePorts);

    auto switchNameToSystemPortRanges =
        topologyInfo->getSwitchNameToSystemPortRanges();
    auto ranges = switchNameToSystemPortRanges.at(edsw);
    auto systemPortMin = *ranges.systemPortRanges()->front().minimum();

    // Router MAC from a known interface on this EDSW
    CHECK(!hyperPortIPs.empty());
    auto neighborMac = utility::getInterfaceMac(
        getSw()->getState(), InterfaceID(hyperPortIPs.front().first));

    std::vector<utility::Neighbor> neighbors;
    for (const auto& aggPort : aggregatePorts) {
      auto aggregatePortKey = *aggPort.key();
      auto intfID = int32_t(systemPortMin) + aggregatePortKey;

      // Find the IP for this interface
      auto it = std::find_if(
          hyperPortIPs.begin(), hyperPortIPs.end(), [intfID](const auto& p) {
            return p.first == intfID;
          });
      if (it == hyperPortIPs.end()) {
        continue;
      }

      utility::Neighbor neighbor;
      neighbor.portID = aggregatePortKey;
      neighbor.intfID = intfID;
      // Derive neighbor IP by adding offset to interface IP
      constexpr auto kOffset = 0x10;
      auto bytes = it->second.toByteArray();
      bytes[15] += kOffset;
      neighbor.ip = folly::IPAddressV6::fromBinary(bytes);
      neighbor.mac = neighborMac;
      neighbors.push_back(neighbor);
    }

    return neighbors;
  }

  // Inject traffic with varying source ports for ECMP/LAG hash distribution.
  // Packets use router MAC as dstMac so they get routed, creating a
  // self-sustaining loop with TTL forwarding enabled.
  void injectTraffic(int32_t egressPortID, const folly::MacAddress& routerMac)
      const {
    auto static kSrcIP = folly::IPAddressV6("2001:0db8:85a0::");
    auto [prefix, _] = kGetRoutePrefixAndPrefixLength();
    for (int i = 0; i < 1000; i++) {
      auto txPacket = utility::makeUDPTxPacket(
          getSw(),
          std::nullopt, // vlanIDForTx
          folly::MacAddress("00:02:00:00:01:01"), // srcMac
          routerMac, // dstMac
          kSrcIP,
          prefix, // dstIP
          8000 + (i % 256), // vary source port for hash distribution
          8001,
          0, // ECN
          255, // TTL
          std::vector<uint8_t>(1200, 0xff));

      getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket), PortID(egressPortID));
    }
  }

  // Verify traffic is evenly distributed:
  //   - Across hyper ports (inter-hyper-port ECMP spray)
  //   - Within each hyper port across member ports (intra-hyper-port LAG spray)
  bool verifyHyperPortTrafficSpray(const std::string& edsw) const {
    auto verifyHelper = [&edsw]() {
      std::vector<AggregatePortThrift> aggregatePorts;
      auto swAgentClient = utility::getSwAgentThriftClient(edsw);
      swAgentClient->sync_getAggregatePortTable(aggregatePorts);

      auto portIdToPortInfo = utility::getPortIdToPortInfo(edsw);
      auto counterNameToCount = utility::getCounterNameToCount(edsw);

      constexpr double kMaxDeviation = 0.25;

      std::vector<int64_t> hyperPortOutBytes;
      for (const auto& aggPort : aggregatePorts) {
        int64_t totalOutBytes = 0;
        std::vector<int64_t> memberOutBytes;

        XLOG(DBG2) << "Hyper port " << aggPort.name().value() << " on " << edsw
                   << " member port traffic:";
        for (const auto& member : aggPort.memberPorts().value()) {
          auto it = portIdToPortInfo.find(member.memberPortID().value());
          CHECK(it != portIdToPortInfo.end());
          auto counterName = it->second.name().value() + ".out_bytes.sum";
          auto cntIt = counterNameToCount.find(counterName);
          CHECK(cntIt != counterNameToCount.end());
          XLOG(DBG2) << "  member " << it->second.name().value() << " (port "
                     << member.memberPortID().value()
                     << "): out_bytes=" << cntIt->second;
          memberOutBytes.push_back(cntIt->second);
          totalOutBytes += cntIt->second;
        }
        XLOG(DBG2) << "  total out_bytes=" << totalOutBytes;

        // Verify intra-hyper-port distribution (within each hyper port)
        if (!memberOutBytes.empty()) {
          auto minVal =
              *std::min_element(memberOutBytes.begin(), memberOutBytes.end());
          auto maxVal =
              *std::max_element(memberOutBytes.begin(), memberOutBytes.end());
          if (maxVal == 0) {
            XLOG(DBG2) << "No traffic on member ports of hyper port "
                       << aggPort.name().value() << " on " << edsw;
            return false;
          }
          auto deviation = static_cast<double>(maxVal - minVal) / maxVal;
          XLOG(DBG2) << "  intra-hyper-port deviation=" << deviation
                     << " (max allowed=" << kMaxDeviation << ")";
          if (deviation > kMaxDeviation) {
            XLOG(DBG2) << "Uneven intra-hyper-port traffic on "
                       << aggPort.name().value() << " on " << edsw
                       << " min: " << minVal << " max: " << maxVal
                       << " deviation: " << deviation;
            return false;
          }
        }

        hyperPortOutBytes.push_back(totalOutBytes);
      }

      // Log and verify inter-hyper-port distribution (across hyper ports)
      XLOG(DBG2) << "Inter-hyper-port traffic on " << edsw << ":";
      for (size_t i = 0; i < hyperPortOutBytes.size(); i++) {
        XLOG(DBG2) << "  hyper port " << i
                   << ": out_bytes=" << hyperPortOutBytes[i];
      }

      if (hyperPortOutBytes.empty()) {
        XLOG(DBG2) << "No hyper ports found on " << edsw;
        return false;
      }
      auto minVal =
          *std::min_element(hyperPortOutBytes.begin(), hyperPortOutBytes.end());
      auto maxVal =
          *std::max_element(hyperPortOutBytes.begin(), hyperPortOutBytes.end());
      if (maxVal == 0) {
        XLOG(DBG2) << "No traffic across hyper ports on " << edsw;
        return false;
      }
      auto deviation = static_cast<double>(maxVal - minVal) / maxVal;
      XLOG(DBG2) << "  inter-hyper-port deviation=" << deviation
                 << " (max allowed=" << kMaxDeviation << ")";
      if (deviation > kMaxDeviation) {
        XLOG(DBG2) << "Uneven inter-hyper-port traffic on " << edsw
                   << " min: " << minVal << " max: " << maxVal
                   << " deviation: " << deviation;
        return false;
      }

      return true;
    };

    return checkWithRetryErrorReturn(
        verifyHelper,
        60 /* num retries */,
        std::chrono::milliseconds(1000) /* sleep between retries */,
        true /* retry on exception */);
  }

 public:
  // Verify NDP resolution behind hyper ports using ping.
  // From the test driver EDSW, ping each hyper port interface IP on the
  // remote EDSW to trigger NDP resolution, then verify NDP entries are
  // REACHABLE on the test driver.
  //
  // Example: edsw1_1 has 3101::1/64 on hyp1/1/1, edsw1_2 has 3101::2/64
  // on hyp1/1/1. Pinging 3101::2 from edsw1_1 resolves NDP for 3101::2
  // on edsw1_1's hyp1/1/1 interface.
  bool verifyNdpBehindHyperPorts(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo) const {
    XLOG(DBG2) << "Verifying NDP behind hyper ports via ping";

    auto myHostname = topologyInfo->getMyHostname();
    auto localIPs = getHyperPortInterfaceIPs(myHostname);
    CHECK(!localIPs.empty()) << "No hyper port IPs on test driver";

    // Find remote EDSW
    std::string remoteEdsw;
    for (const auto& edsw : topologyInfo->getEdsws()) {
      if (edsw != myHostname) {
        remoteEdsw = edsw;
        break;
      }
    }
    CHECK(!remoteEdsw.empty()) << "No remote EDSW found";
    auto remoteIPs = getHyperPortInterfaceIPs(remoteEdsw);
    CHECK(!remoteIPs.empty()) << "No hyper port IPs on remote EDSW";

    // Ping each remote hyper port IP from the matching local IP
    // (same /64 subnet). This triggers NDP resolution on the local EDSW.
    for (const auto& [remoteIntfID, remoteIP] : remoteIPs) {
      // Find local IP in same /64 subnet
      std::optional<folly::IPAddressV6> localIP;
      for (const auto& [localIntfID, lip] : localIPs) {
        if (lip.mask(64) == remoteIP.mask(64)) {
          localIP = lip;
          break;
        }
      }
      CHECK(localIP.has_value())
          << "No local IP in same subnet as " << remoteIP.str();

      auto cmd =
          "ping -6 -c 5 -W 2 -I " + localIP->str() + " " + remoteIP.str();
      XLOG(DBG2) << "Pinging: " << cmd;
      auto ret = std::system(cmd.c_str());
      if (ret != 0) {
        XLOG(DBG2) << "Ping failed with exit code " << ret << ": " << cmd;
        return false;
      }
    }

    // Verify NDP entries for all remote IPs are REACHABLE on local EDSW
    auto verifyNdpHelper = [&myHostname, &remoteIPs]() {
      auto ndpEntries = utility::getNdpEntriesOfType(myHostname, {"REACHABLE"});
      XLOG(DBG2) << "NDP REACHABLE entries on " << myHostname << ":";
      for (const auto& ndpEntry : ndpEntries) {
        auto ip = folly::IPAddress::fromBinary(
            folly::ByteRange(
                folly::StringPiece(ndpEntry.ip().value().addr().value())));
        XLOG(DBG2) << "  ip=" << ip.str() << " mac=" << ndpEntry.mac().value()
                   << " intfID=" << ndpEntry.interfaceID().value()
                   << " state=" << ndpEntry.state().value();
      }
      for (const auto& [_, remoteIP] : remoteIPs) {
        bool found = false;
        for (const auto& ndpEntry : ndpEntries) {
          auto entryIP = folly::IPAddress::fromBinary(
              folly::ByteRange(
                  folly::StringPiece(ndpEntry.ip().value().addr().value())));
          if (entryIP == remoteIP) {
            found = true;
            break;
          }
        }
        if (!found) {
          XLOG(DBG2) << "NDP entry not REACHABLE for " << remoteIP.str()
                     << " on " << myHostname;
          return false;
        }
      }
      return true;
    };

    return checkWithRetryErrorReturn(
        verifyNdpHelper,
        10 /* num retries */,
        std::chrono::milliseconds(1000) /* sleep between retries */,
        true /* retry on exception */);
  }

  // Create ECMP route across hyper port nexthops on both EDSWs, inject
  // traffic to create a traffic loop, then verify:
  //   - Traffic is evenly distributed across four hyper ports
  //   - Within each hyper port, traffic is evenly distributed across
  //     four member ports
  bool verifyHyperPortTrafficDistribution(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo) const {
    XLOG(DBG2) << "Verifying hyper port traffic distribution";

    const auto& edsws = topologyInfo->getEdsws();
    CHECK_GE(edsws.size(), 2) << "Need at least 2 EDSWs for traffic loop";

    auto [prefix, prefixLength] = kGetRoutePrefixAndPrefixLength();

    auto myHostname = topologyInfo->getMyHostname();
    auto localIPs = getHyperPortInterfaceIPs(myHostname);
    CHECK(!localIPs.empty()) << "No hyper port IPs on test driver";

    // Find remote EDSW
    std::string remoteEdsw;
    for (const auto& edsw : edsws) {
      if (edsw != myHostname) {
        remoteEdsw = edsw;
        break;
      }
    }
    CHECK(!remoteEdsw.empty()) << "No remote EDSW found";
    auto remoteIPs = getHyperPortInterfaceIPs(remoteEdsw);
    CHECK(!remoteIPs.empty()) << "No hyper port IPs on remote EDSW";

    // Step 1: Resolve neighbors on both EDSWs via ping.
    // Pinging from test driver to remote EDSW resolves NDP on both sides:
    // - Test driver learns remote EDSW's MAC (from NDP response)
    // - Remote EDSW learns test driver's MAC (from NDP request)
    for (const auto& [remoteIntfID, remoteIP] : remoteIPs) {
      std::optional<folly::IPAddressV6> localIP;
      for (const auto& [localIntfID, lip] : localIPs) {
        if (lip.mask(64) == remoteIP.mask(64)) {
          localIP = lip;
          break;
        }
      }
      CHECK(localIP.has_value())
          << "No local IP in same subnet as " << remoteIP.str();

      auto cmd = fmt::format(
          "ping -6 -c 5 -W 2 -I {} {}", localIP->str(), remoteIP.str());
      XLOG(DBG2) << "Pinging: " << cmd;
      auto ret = std::system(cmd.c_str());
      if (ret != 0) {
        XLOG(DBG2) << "Ping failed with exit code " << ret << ": " << cmd;
        return false;
      }
    }

    // Step 2: Create ECMP routes for traffic loop between two EDSWs.
    // Each EDSW routes to the other EDSW's hyper port IPs as nexthops.
    auto getRemoteIPsAsNexthops =
        [](const std::vector<std::pair<int32_t, folly::IPAddressV6>>& ips) {
          std::vector<folly::IPAddress> nexthops;
          for (const auto& [_, ip] : ips) {
            nexthops.emplace_back(ip);
          }
          return nexthops;
        };

    XLOG(DBG2) << "Adding ECMP route on " << myHostname << " with "
               << remoteIPs.size() << " nexthops";
    utility::addRoute(
        myHostname, prefix, prefixLength, getRemoteIPsAsNexthops(remoteIPs));
    if (!utility::verifyRoutePresent(myHostname, prefix, prefixLength)) {
      XLOG(DBG2) << "Route verification failed on " << myHostname;
      return false;
    }

    XLOG(DBG2) << "Adding ECMP route on " << remoteEdsw << " with "
               << localIPs.size() << " nexthops";
    utility::addRoute(
        remoteEdsw, prefix, prefixLength, getRemoteIPsAsNexthops(localIPs));
    if (!utility::verifyRoutePresent(remoteEdsw, prefix, prefixLength)) {
      XLOG(DBG2) << "Route verification failed on " << remoteEdsw;
      return false;
    }

    // Step 3: Inject traffic from test driver to create traffic loop.
    // Use the first local hyper port's aggregate port key as egress port.
    auto localNeighbors =
        computeNeighborsForEdswHyperPorts(topologyInfo, myHostname);
    CHECK(!localNeighbors.empty()) << "No hyper port neighbors on test driver";
    auto routerMac = utility::getInterfaceMac(
        getSw()->getState(), InterfaceID(localIPs.front().first));
    injectTraffic(localNeighbors.front().portID, routerMac);

    // Step 4: Verify traffic distribution across and within hyper ports
    for (const auto& edsw : edsws) {
      if (!verifyHyperPortTrafficSpray(edsw)) {
        XLOG(DBG2) << "Traffic distribution verification failed on " << edsw;
        return false;
      }
    }

    return true;
  }

  bool verifyNdpAfterHyperPortMemberFlap(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo) const {
    XLOG(DBG2) << "Verifying NDP after stress flapping hyper port members";

    auto myHostname = topologyInfo->getMyHostname();

    std::vector<AggregatePortThrift> aggregatePorts;
    {
      auto swAgentClient = utility::getSwAgentThriftClient(myHostname);
      swAgentClient->sync_getAggregatePortTable(aggregatePorts);
    }
    CHECK(!aggregatePorts.empty()) << "No aggregate ports on " << myHostname;

    constexpr int kNumFlaps = 5;

    for (const auto& aggPort : aggregatePorts) {
      for (const auto& member : aggPort.memberPorts().value()) {
        auto memberPortID = member.memberPortID().value();
        XLOG(DBG2) << "Flapping member port " << memberPortID
                   << " of hyper port " << aggPort.name().value() << " "
                   << kNumFlaps << " times";
        for (int i = 0; i < kNumFlaps; i++) {
          XLOG(DBG2) << "  Flap " << (i + 1) << "/" << kNumFlaps;
          utility::adminDisablePort(myHostname, memberPortID);
          utility::adminEnablePort(myHostname, memberPortID);
        }
      }
    }

    auto verifyHyperPortsUp = [&myHostname]() {
      std::vector<AggregatePortThrift> aggPorts;
      auto swAgentClient = utility::getSwAgentThriftClient(myHostname);
      swAgentClient->sync_getAggregatePortTable(aggPorts);
      for (const auto& aggPort : aggPorts) {
        if (!aggPort.isUp().value()) {
          XLOG(DBG2) << "Hyper port " << aggPort.name().value()
                     << " is not up on " << myHostname;
          return false;
        }
        for (const auto& member : aggPort.memberPorts().value()) {
          if (!member.isForwarding().value()) {
            XLOG(DBG2) << "Member port " << member.memberPortID().value()
                       << " of hyper port " << aggPort.name().value()
                       << " is not forwarding on " << myHostname;
            return false;
          }
        }
      }
      return true;
    };

    if (!checkWithRetryErrorReturn(
            verifyHyperPortsUp, 180, std::chrono::milliseconds(1000), true)) {
      XLOG(DBG2) << "Hyper ports did not recover after flapping on "
                 << myHostname;
      return false;
    }

    XLOG(DBG2) << "All hyper ports recovered after stress flapping, "
               << "verifying NDP/ping";
    return verifyNdpBehindHyperPorts(topologyInfo);
  }

  bool verifyNoControlPlaneFlapOnRemoteWarmboot(
      const std::unique_ptr<utility::TopologyInfo>& topologyInfo) const {
    XLOG(DBG2) << "Verifying no hyper-port control plane flap on remote "
               << "warmboot restart";

    auto myHostname = topologyInfo->getMyHostname();
    auto remoteEdsw = getRemoteEdsw(topologyInfo);
    auto remoteIPs = getHyperPortInterfaceIPs(remoteEdsw);
    CHECK(!remoteIPs.empty())
        << "No hyper port IPs on remote EDSW " << remoteEdsw;

    auto baselineSnapshot = getPrimarySwitchFlapSnapshot(myHostname, remoteIPs);
    if (!utility::restartServiceForSwitches(
            {remoteEdsw},
            [](const std::string& switchName) {
              utility::triggerGracefulAgentRestart(switchName);
            },
            utility::getAgentAliveSinceEpoch,
            utility::verifySwSwitchRunState,
            SwitchRunState::CONFIGURED)) {
      XLOG(DBG2) << "Failed to gracefully restart remote switch " << remoteEdsw;
      return false;
    }

    auto currentSnapshot = getPrimarySwitchFlapSnapshot(myHostname, remoteIPs);
    if (!verifyPrimarySwitchFlapSnapshotUnchanged(
            myHostname, baselineSnapshot, currentSnapshot)) {
      return false;
    }

    return verifyNdpBehindHyperPorts(topologyInfo);
  }
};

TEST_F(AgentMultiNodeVoqSwitchHyperPortTest, verifyNdpBehindHyperPorts) {
  if (!FLAGS_hyper_port) {
    GTEST_SKIP() << "Skipping: hyper_port flag is not set";
  }
  runTestWithVerifyCluster([this](const auto& topologyInfo) {
    switch (topologyInfo->getTopologyType()) {
      case utility::TopologyInfo::TopologyType::DSF:
        return this->verifyNdpBehindHyperPorts(topologyInfo);
    }
  });
}

TEST_F(
    AgentMultiNodeVoqSwitchHyperPortTest,
    verifyHyperPortTrafficDistribution) {
  if (!FLAGS_hyper_port) {
    GTEST_SKIP() << "Skipping: hyper_port flag is not set";
  }
  runTestWithVerifyCluster([this](const auto& topologyInfo) {
    switch (topologyInfo->getTopologyType()) {
      case utility::TopologyInfo::TopologyType::DSF:
        return this->verifyHyperPortTrafficDistribution(topologyInfo);
    }
  });
}

TEST_F(
    AgentMultiNodeVoqSwitchHyperPortTest,
    verifyNdpAfterHyperPortMemberFlap) {
  if (!FLAGS_hyper_port) {
    GTEST_SKIP() << "Skipping: hyper_port flag is not set";
  }
  ASSERT_TRUE(checkWithRetryErrorReturn(
      [this]() { return verifyNdpBehindHyperPorts(topologyInfo_); },
      120,
      std::chrono::milliseconds(5000),
      true));
  ASSERT_TRUE(verifyNdpAfterHyperPortMemberFlap(topologyInfo_));
}

TEST_F(
    AgentMultiNodeVoqSwitchHyperPortTest,
    verifyNoControlPlaneFlapOnRemoteWarmboot) {
  if (!FLAGS_hyper_port) {
    GTEST_SKIP() << "Skipping: hyper_port flag is not set";
  }
  ASSERT_TRUE(checkWithRetryErrorReturn(
      [this]() { return verifyNdpBehindHyperPorts(topologyInfo_); },
      120,
      std::chrono::milliseconds(5000),
      true));
  ASSERT_TRUE(verifyNoControlPlaneFlapOnRemoteWarmboot(topologyInfo_));
}

} // namespace facebook::fboss
