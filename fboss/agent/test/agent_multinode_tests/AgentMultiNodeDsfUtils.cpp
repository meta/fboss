// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeDsfUtils.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/test/agent_multinode_tests/AgentMultiNodeUtils.h"
#include "fboss/agent/test/thrift_client_utils/ThriftClientUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <gtest/gtest.h>

namespace {
using namespace facebook::fboss::utility;
using facebook::fboss::AggregatePortThrift;
using facebook::fboss::checkAlwaysTrueWithRetryErrorReturn;
using facebook::fboss::checkWithRetryErrorReturn;
using facebook::fboss::DsfSessionState;
using facebook::fboss::DsfSessionThrift;
using facebook::fboss::FabricEndpoint;
using facebook::fboss::InterfaceDetail;
using facebook::fboss::NdpEntryThrift;
using facebook::fboss::PortActiveState;
using facebook::fboss::PortInfoThrift;
using facebook::fboss::RemoteInterfaceType;
using facebook::fboss::RemoteSystemPortType;
using facebook::fboss::SystemPortThrift;
using facebook::fboss::cfg::PortType;
using facebook::fboss::cfg::Scope;

void logFabricEndpoint(
    const std::string& switchName,
    const FabricEndpoint& fabricEndpoint) {
  XLOG(DBG2) << "From " << " switchName: " << switchName
             << " actualPeerSwitchId: " << fabricEndpoint.switchId().value()
             << " actualPeerSwitchName: "
             << fabricEndpoint.switchName().value_or("none")
             << " actualPeerPortId: " << fabricEndpoint.portId().value()
             << " actualPortName: "
             << fabricEndpoint.portName().value_or("none")
             << " expectedPeerSwitchId: "
             << fabricEndpoint.expectedSwitchId().value_or(-1)
             << " expectedPeerSwitchName: "
             << fabricEndpoint.expectedSwitchName().value_or("none")
             << " expectedPeerPortId: "
             << fabricEndpoint.expectedPortId().value_or(-1)
             << " expectedPeerPortName: "
             << fabricEndpoint.expectedPortName().value_or("none");
}

void logReachability(
    const std::string& switchName,
    const std::string& remoteSwitchName,
    const std::vector<std::string>& reachablePorts) {
  XLOG(DBG2) << "From " << switchName
             << " remoteSwitchName: " << remoteSwitchName
             << " reachablePorts: " << folly::join(",", reachablePorts);
};

void logDsfSession(const std::string& rdsw, const DsfSessionThrift& session) {
  XLOG(DBG2) << "From " << rdsw << " session: " << *session.remoteName()
             << " state: "
             << apache::thrift::util::enumNameSafe(*session.state())
             << " lastEstablishedAt: "
             << session.lastEstablishedAt().value_or(0)
             << " lastDisconnectedAt: "
             << session.lastDisconnectedAt().value_or(0);
}

bool verifyFabricConnectivityHelper(
    const std::string& switchToVerify,
    const std::set<std::string>& expectedConnectedSwitches) {
  std::set<std::string> gotConnectedSwitches;
  for (const auto& [portName, fabricEndpoint] :
       getFabricPortToFabricEndpoint(switchToVerify)) {
    if (fabricEndpoint.isAttached().value()) {
      logFabricEndpoint(switchToVerify, fabricEndpoint);
      auto actualRemoteSwitchId = fabricEndpoint.switchId().value();
      auto expectedRemoteSwitchId =
          fabricEndpoint.expectedSwitchId().value_or(-1);
      auto actualRemoteSwitchName = fabricEndpoint.switchName();
      auto expectedRemoteSwitchName = fabricEndpoint.expectedSwitchName();

      auto actualRemotePortId = fabricEndpoint.portId().value();
      auto expectedRemotePortId = fabricEndpoint.expectedPortId().value_or(-1);
      auto actualRemotePortName = fabricEndpoint.portName();
      auto expectedRemotePortName = fabricEndpoint.portName();

      // Expected switch/port ID/name must match for every entry
      if (!(expectedRemoteSwitchId == actualRemoteSwitchId &&
            expectedRemoteSwitchName == actualRemoteSwitchName &&
            expectedRemotePortId == actualRemotePortId &&
            expectedRemotePortName == actualRemotePortName)) {
        return false;
      }
      if (fabricEndpoint.switchName().has_value()) {
        gotConnectedSwitches.insert(fabricEndpoint.switchName().value());
      }
    }
  }

  XLOG(DBG2) << "From " << switchToVerify << " Expected Connected Switches: "
             << folly::join(",", expectedConnectedSwitches)
             << " Got Connected Switches: "
             << folly::join(",", gotConnectedSwitches);

  return expectedConnectedSwitches == gotConnectedSwitches;
}

std::set<std::string> getConnectedFabricPorts(const std::string& switchName) {
  std::set<std::string> connectedFabricPorts;
  for (const auto& [portName, fabricEndpoint] :
       getFabricPortToFabricEndpoint(switchName)) {
    if (fabricEndpoint.isAttached().value()) {
      connectedFabricPorts.insert(portName);
    }
  }

  return connectedFabricPorts;
}

std::map<std::string, PortInfoThrift> getFabricPortNameToPortInfo(
    const std::string& switchName) {
  std::map<std::string, PortInfoThrift> fabricPortNameToPortInfo;
  for (const auto& [_, portInfo] : getPortIdToPortInfo(switchName)) {
    if (portInfo.portType().value() == PortType::FABRIC_PORT) {
      fabricPortNameToPortInfo.emplace(portInfo.name().value(), portInfo);
    }
  }

  return fabricPortNameToPortInfo;
}

std::map<std::string, PortInfoThrift> getActiveFabricPortNameToPortInfo(
    const std::string& switchName) {
  std::map<std::string, PortInfoThrift> activeFabricPortNameToPortInfo;
  for (const auto& [_, portInfo] : getFabricPortNameToPortInfo(switchName)) {
    if (portInfo.activeState().has_value() &&
        portInfo.activeState().value() == PortActiveState::ACTIVE) {
      activeFabricPortNameToPortInfo.emplace(portInfo.name().value(), portInfo);
    }
  }

  return activeFabricPortNameToPortInfo;
}

std::set<std::string> getActiveFabricPorts(const std::string& switchName) {
  auto activeFabricPortNameToPortInfo =
      getActiveFabricPortNameToPortInfo(switchName);
  std::set<std::string> activePorts;

  std::transform(
      activeFabricPortNameToPortInfo.begin(),
      activeFabricPortNameToPortInfo.end(),
      std::inserter(activePorts, activePorts.begin()),
      [](const auto& pair) {
        const auto& portInfo = pair.second;
        return portInfo.name().value();
      });

  return activePorts;
}

bool verifyPortActiveState(
    const std::string& switchName,
    std::set<std::string>& fabricPorts,
    const PortActiveState& activeState) {
  auto verifyPortActiveStateHelper = [switchName, fabricPorts, activeState] {
    auto fabricPortNameToPortInfo = getFabricPortNameToPortInfo(switchName);
    return std::all_of(
        fabricPorts.begin(), fabricPorts.end(), [&](const auto& fabricPort) {
          auto iter = fabricPortNameToPortInfo.find(fabricPort);
          CHECK(iter != fabricPortNameToPortInfo.end());
          return iter->second.activeState().has_value() &&
              iter->second.activeState().value() == activeState;
        });
  };

  return checkWithRetryErrorReturn(
      verifyPortActiveStateHelper,
      10 /* num retries */,
      std::chrono::milliseconds(1000) /* sleep between retries */,
      true /* retry on exception */);
}

std::set<std::string> getGlobalSystemPortsOfType(
    const std::string& rdsw,
    const std::set<RemoteSystemPortType>& types) {
  auto matchesSystemPortType =
      [&types](const facebook::fboss::SystemPortThrift& systemPort) {
        if (systemPort.remoteSystemPortType().has_value()) {
          return types.find(systemPort.remoteSystemPortType().value()) !=
              types.end();
        } else {
          return types.empty();
        }
      };

  std::set<std::string> systemPortsOfType;
  for (const auto& [_, systemPort] : getSystemPortdIdToSystemPort(rdsw)) {
    if (*systemPort.scope() == Scope::GLOBAL &&
        matchesSystemPortType(systemPort)) {
      systemPortsOfType.insert(systemPort.portName().value());
    }
  }
  return systemPortsOfType;
}

std::map<std::string, std::vector<SystemPortThrift>> getRdswToSystemPorts(
    const std::set<std::string>& rdsws) {
  std::map<std::string, std::vector<SystemPortThrift>> rdswToSystemPorts;

  for (const auto& rdsw : rdsws) {
    auto systemPortIdToSystemPort = getSystemPortdIdToSystemPort(rdsw);

    std::transform(
        systemPortIdToSystemPort.begin(),
        systemPortIdToSystemPort.end(),
        std::back_inserter(rdswToSystemPorts[rdsw]),
        [](const auto& pair) { return pair.second; });
  }

  return rdswToSystemPorts;
}

std::set<int> getGlobalRifsOfType(
    const std::string& rdsw,
    const std::set<RemoteInterfaceType>& types) {
  auto matchesRifType = [&types](const facebook::fboss::InterfaceDetail& rif) {
    if (rif.remoteIntfType().has_value()) {
      return types.find(rif.remoteIntfType().value()) != types.end();
    } else {
      return types.empty();
    }
  };

  std::set<int> rifsOfType;
  for (const auto& [_, rif] : getIntfIdToIntf(rdsw)) {
    if (*rif.scope() == Scope::GLOBAL && matchesRifType(rif)) {
      rifsOfType.insert(rif.interfaceId().value());
    }
  }
  return rifsOfType;
}

std::map<std::string, std::vector<InterfaceDetail>> getRdswToRifs(
    const std::set<std::string>& rdsws) {
  std::map<std::string, std::vector<InterfaceDetail>> rdswToRifs;
  for (const auto& rdsw : rdsws) {
    auto intfIdToIntf = getIntfIdToIntf(rdsw);

    std::transform(
        intfIdToIntf.begin(),
        intfIdToIntf.end(),
        std::back_inserter(rdswToRifs[rdsw]),
        [](const auto& pair) { return pair.second; });
  }

  return rdswToRifs;
}

std::map<std::string, DsfSessionThrift> getPeerToDsfSession(
    const std::string& rdsw) {
  std::map<std::string, DsfSessionThrift> peerToDsfSession;
  for (const auto& session : getDsfSessions(rdsw)) {
    logDsfSession(rdsw, session);
    // remoteName format: peerName::peerIP, extract peerName.
    size_t pos = (*session.remoteName()).find("::");
    if (pos != std::string::npos) {
      auto peer = (*session.remoteName()).substr(0, pos);
      peerToDsfSession.emplace(peer, session);
    }
  }

  return peerToDsfSession;
}

std::set<std::string> getPeersWithEstablishedDsfSessions(
    const std::string& rdsw) {
  std::set<std::string> peersWithEstablishedDsfSessions;
  for (const auto& [peer, session] : getPeerToDsfSession(rdsw)) {
    if (session.state() == DsfSessionState::ESTABLISHED) {
      peersWithEstablishedDsfSessions.insert(peer);
    }
  }

  return peersWithEstablishedDsfSessions;
}

bool verifyNoSessionsFlap(
    const std::string& rdswToVerify,
    const std::map<std::string, DsfSessionThrift>& baselinePeerToDsfSession,
    const std::optional<std::string>& rdswToExclude = std::nullopt) {
  auto noSessionFlap = [rdswToVerify, baselinePeerToDsfSession, rdswToExclude] {
    auto currentPeerToDsfSession = getPeerToDsfSession(rdswToVerify);
    // All entries must be identical i.e.
    // DSF Session state (ESTABLISHED or not) is the same.
    // For any session the establishedAt and connnectedAt is the same.
    if (rdswToExclude.has_value()) {
      currentPeerToDsfSession.erase(rdswToExclude.value());
    }

    return baselinePeerToDsfSession == currentPeerToDsfSession;
  };

  // It may take several (> 15) seconds for ESTABLISHED => CONNECT.
  // Thus, keep retrying for several seconds to ensure that the session
  // stays ESTABLISHED.
  return checkAlwaysTrueWithRetryErrorReturn(
      noSessionFlap, 30 /* num retries */);
}

bool verifyAllSessionsEstablished(const std::string& rdswToVerify) {
  auto allSessionsEstablished = [rdswToVerify] {
    for (const auto& [peer, session] : getPeerToDsfSession(rdswToVerify)) {
      if (session.state() != facebook::fboss::DsfSessionState::ESTABLISHED) {
        return false;
      }
    }

    return true;
  };

  // It may take several seconds(>15) for ESTABLISHED => CONNECT. Thus,
  // try for several seconds and check if the session transitions to
  // ESTABLISHED.
  return checkWithRetryErrorReturn(
      allSessionsEstablished, 30 /* num retries */);
}

bool verifyNoSessionsEstablished(const std::string& rdswToVerify) {
  auto noSessionsEstablished = [rdswToVerify]() {
    for (const auto& [peer, session] : getPeerToDsfSession(rdswToVerify)) {
      if (session.state() == facebook::fboss::DsfSessionState::ESTABLISHED) {
        return false;
      }
    }

    return true;
  };

  // It may take several (> 15) seconds for ESTABLISHED => CONNECT. Thus,
  // try for several seconds and check if the session transitions to
  // CONNECT.
  return checkWithRetryErrorReturn(noSessionsEstablished, 30 /* num retries */);
}

std::set<folly::IPAddress> getLoopbackIpsInDsfCluster(
    const std::string& switchName) {
  // Each switch conains DSF Node map. The DSF Node map contains loopbackIP
  // information for every device in that cluster. Thus, we can build a set of
  // all the loopbackIPs in the cluster by querying any switch in the cluster.
  std::set<folly::IPAddress> loopbackIps;
  for (const auto& [switchId, dsfNode] : getSwitchIdToDsfNode(switchName)) {
    for (const auto& loopbackIp : *dsfNode.loopbackIps()) {
      auto network = folly::IPAddress::createNetwork(
          loopbackIp, -1 /* defaultCidr */, false /* mask */);
      loopbackIps.insert(network.first);
    }
  }
  return loopbackIps;
}

} // namespace

namespace facebook::fboss::utility {

bool verifyFabricConnectivityForRdsw(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    int clusterId,
    const std::string& rdswToVerify) {
  // Every RDSW is connected to all FDSWs in its cluster
  std::set<std::string> expectedConnectedSwitches(
      std::begin(topologyInfo->getClusterIdToFdsws().at(clusterId)),
      std::end(topologyInfo->getClusterIdToFdsws().at(clusterId)));

  return verifyFabricConnectivityHelper(
      rdswToVerify, expectedConnectedSwitches);
}

bool verifyFabricConnectivityForRdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Fabric Connectivity for RDSWs";
  for (const auto& [clusterId, rdsws] :
       std::as_const(topologyInfo->getClusterIdToRdsws())) {
    for (const auto& rdsw : rdsws) {
      if (!verifyFabricConnectivityForRdsw(topologyInfo, clusterId, rdsw)) {
        return false;
      }
    }
  }

  return true;
}

bool verifyFabricConnectivityForFdsw(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    int clusterId,
    const std::string& fdswToVerify) {
  // Every FDSW is connected to all RDSWs in its cluster and all SDSWs
  std::set<std::string> expectedConnectedSwitches(
      std::begin(topologyInfo->getClusterIdToRdsws().at(clusterId)),
      std::end(topologyInfo->getClusterIdToRdsws().at(clusterId)));
  expectedConnectedSwitches.insert(
      topologyInfo->getSdsws().begin(), topologyInfo->getSdsws().end());

  return verifyFabricConnectivityHelper(
      fdswToVerify, expectedConnectedSwitches);
}

bool verifyFabricConnectivityForFdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Fabric Connectivity for FDSWs";
  for (const auto& [clusterId, fdsws] :
       std::as_const(topologyInfo->getClusterIdToFdsws())) {
    for (const auto& fdsw : fdsws) {
      if (!verifyFabricConnectivityForFdsw(topologyInfo, clusterId, fdsw)) {
        return false;
      }
    }
  }

  return true;
}

bool verifyFabricConnectivityForSdsw(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& sdswToVerify) {
  // Every SDSW is connected to all FDSWs in all clusters
  return verifyFabricConnectivityHelper(sdswToVerify, topologyInfo->getFdsws());
}

bool verifyFabricConnectivityForSdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Fabric Connectivity for SDSWs";
  for (const auto& sdsw : std::as_const(topologyInfo->getSdsws())) {
    if (!verifyFabricConnectivityForSdsw(topologyInfo, sdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyFabricConnectivity(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Fabric Connectivity";
  return verifyFabricConnectivityForRdsws(topologyInfo) &&
      verifyFabricConnectivityForFdsws(topologyInfo) &&
      verifyFabricConnectivityForSdsws(topologyInfo);
}

bool verifyFabricReachabilityForRdsw(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& rdswToVerify) {
  // Every remote RDSW across all clusters is reachable from the local RDSW
  std::vector<std::string> remoteSwitchNames;
  std::copy_if(
      topologyInfo->getRdsws().begin(),
      topologyInfo->getRdsws().end(),
      std::back_inserter(remoteSwitchNames),
      [rdswToVerify](const std::string& switchName) {
        return switchName != rdswToVerify; // exclude self
      });

  auto remoteSwitchToReachablePorts =
      getRemoteSwitchToReachablePorts(rdswToVerify, remoteSwitchNames);

  // Every remote RDSW must be reachable via every local active port.
  // TODO: This assertion is not true when Input Balanced Mode is enabled.
  // We will enhance this check for DSF Dual Stage where Input Balanced Mode
  // is enabled.
  auto activePorts = getActiveFabricPorts(rdswToVerify);

  for (auto& [remoteSwitchName, reachablePorts] :
       remoteSwitchToReachablePorts) {
    logReachability(rdswToVerify, remoteSwitchName, reachablePorts);
    std::set<std::string> reachablePortsSet(
        reachablePorts.begin(), reachablePorts.end());
    XLOG(DBG2) << "From RDSW:: " << rdswToVerify
               << " Expected reachable ports (Active Ports): "
               << folly::join(",", activePorts) << " Got reachable ports: "
               << folly::join(",", reachablePortsSet);

    if (activePorts != reachablePortsSet) {
      return false;
    }
  }

  return true;
}

bool verifyFabricReachability(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Fabric Reachability for RDSWs";
  for (const auto& rdsw : topologyInfo->getRdsws()) {
    if (!verifyFabricReachabilityForRdsw(topologyInfo, rdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyPortActiveStateForSwitch(const std::string& switchName) {
  // Every Connected Fabric Port must be Active
  XLOG(DBG2) << "Verifying Fabric Port Active State: " << switchName;
  auto connectedPorts = getConnectedFabricPorts(switchName);
  auto activePorts = getActiveFabricPorts(switchName);

  XLOG(DBG2) << "From " << switchName
             << " Expected Active Ports (connected fabric ports): "
             << folly::join(",", connectedPorts)
             << " Got Active Ports: " << folly::join(",", activePorts);

  return connectedPorts == activePorts;
}

bool verifyNoPortErrorsForSwitch(const std::string& switchName) {
  // No connected fabric ports should have errors
  XLOG(DBG2) << "Verifying Fabric Port No Error: " << switchName;
  auto connectedPorts = getConnectedFabricPorts(switchName);
  for (const auto& [portName, portInfo] :
       getFabricPortNameToPortInfo(switchName)) {
    if (connectedPorts.count(portName) == 0) {
      continue;
    }
    if (portInfo.activeErrors()->size() != 0) {
      XLOG(DBG2) << "From " << switchName
                 << " Port: " << portInfo.name().value() << " has errors: "
                 << folly::join(",", *portInfo.activeErrors());
      return false;
    }
  }

  return true;
}

bool verifyPortCableLength(const std::string& switchName) {
  // Cable length query (i.e. cable propagation delay attribute) works only on
  // below fabric ports::
  // DSF Single Stage system:
  //   o RDSW Fabric ports towards FDSW
  // DSF Dual Stage system:
  //   o RDSW Fabric ports towards FDSW
  //   o FDSW Fabric ports towards SDSW
  //
  // TODO Enhance this check for DSF Dual stage:
  //    FDSW Fabric ports towards SDSW

  // Verify if all connected fabric ports have valid cable length
  auto verifyCableLengthHelper = [switchName]() {
    XLOG(DBG2) << "Verifying Fabric Port Cable Length: " << switchName;
    for (const auto& [_, portInfo] :
         getActiveFabricPortNameToPortInfo(switchName)) {
      if (portInfo.cableLengthMeters().has_value() &&
          portInfo.cableLengthMeters().value() >= 0) {
        // Cable length may vary on different test setups. Thus, verify
        // if the Cable length query returns a valid non-0 cable length.
        continue;
      }

      XLOG(DBG2) << "From " << switchName
                 << " Port: " << portInfo.name().value()
                 << " has invalid cable length: "
                 << portInfo.cableLengthMeters().value_or(-1);

      return false;
    }

    return true;
  };

  // Cable length is collected during periodic stats collection, not immediately
  // when port comes up. For Netcastle runs, it requires additional time to
  // retrieve cable length stats after the agent is up. Thus, retry with a
  // dedicated loop to allow stats collection to complete.
  return checkWithRetryErrorReturn(
      verifyCableLengthHelper,
      30 /* num retries */,
      std::chrono::milliseconds(2000) /* sleep between retries */,
      true /* retry on exception */);
}

bool verifyFabricPorts(const std::string& switchName) {
  XLOG(DBG2) << "Verifying Fabric Ports: " << switchName;
  return verifyPortActiveStateForSwitch(switchName) &&
      verifyNoPortErrorsForSwitch(switchName);
}

std::vector<AggregatePortThrift> getAggregatePortTable(
    const std::string& switchName) {
  std::vector<AggregatePortThrift> aggregatePorts;
  auto swAgentClient = getSwAgentThriftClient(switchName);
  swAgentClient->sync_getAggregatePortTable(aggregatePorts);
  return aggregatePorts;
}

bool verifyHyperPortMembersUpForSwitch(const std::string& switchName) {
  auto aggregatePorts = getAggregatePortTable(switchName);
  auto portIdToPortInfo = getPortIdToPortInfo(switchName);

  for (const auto& aggPort : aggregatePorts) {
    for (const auto& member : aggPort.memberPorts().value()) {
      auto it = portIdToPortInfo.find(member.memberPortID().value());
      if (it == portIdToPortInfo.end()) {
        XLOG(DBG2) << "Member port " << member.memberPortID().value()
                   << " of hyper port " << aggPort.name().value()
                   << " not found in port info on " << switchName;
        return false;
      }
      if (it->second.operState().value() != PortOperState::UP) {
        XLOG(DBG2) << "Member port " << it->second.name().value()
                   << " of hyper port " << aggPort.name().value()
                   << " is not up on " << switchName;
        return false;
      }
    }
  }

  return true;
}

bool verifyHyperPortLacpStateForSwitch(const std::string& switchName) {
  auto aggregatePorts = getAggregatePortTable(switchName);

  for (const auto& aggPort : aggregatePorts) {
    for (const auto& member : aggPort.memberPorts().value()) {
      if (!member.isForwarding().value()) {
        XLOG(DBG2) << "Member port " << member.memberPortID().value()
                   << " of hyper port " << aggPort.name().value()
                   << " LACP forwarding state is false on " << switchName;
        return false;
      }
    }
  }

  return true;
}

bool verifyHyperPortUpForSwitch(const std::string& switchName) {
  auto aggregatePorts = getAggregatePortTable(switchName);

  for (const auto& aggPort : aggregatePorts) {
    if (!aggPort.isUp().value()) {
      XLOG(DBG2) << "Hyper port " << aggPort.name().value() << " is not up on "
                 << switchName;
      return false;
    }
  }

  return true;
}

bool verifyHyperPortMembersUp(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Hyper Port Members Up";
  for (const auto& edsw : topologyInfo->getEdsws()) {
    if (!verifyHyperPortMembersUpForSwitch(edsw)) {
      return false;
    }
  }
  return true;
}

bool verifyHyperPortLacpState(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Hyper Port LACP Forwarding State";
  for (const auto& edsw : topologyInfo->getEdsws()) {
    if (!verifyHyperPortLacpStateForSwitch(edsw)) {
      return false;
    }
  }
  return true;
}

bool verifyHyperPortUp(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Hyper Port Up";
  for (const auto& edsw : topologyInfo->getEdsws()) {
    if (!verifyHyperPortUpForSwitch(edsw)) {
      return false;
    }
  }
  return true;
}

bool verifyPortsForRdsws(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Ports for RDSWs";
  for (const auto& rdsw : topologyInfo->getRdsws()) {
    if (!verifyFabricPorts(rdsw)) {
      return false;
    }
    if (!verifyPortCableLength(rdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyHyperPortMemberCableLengthForSwitch(const std::string& switchName) {
  auto verifyCableLengthHelper = [switchName]() {
    XLOG(DBG2) << "Verifying Hyper Port Member Cable Length: " << switchName;
    auto aggregatePorts = getAggregatePortTable(switchName);
    auto portIdToPortInfo = getPortIdToPortInfo(switchName);

    for (const auto& aggPort : aggregatePorts) {
      for (const auto& member : aggPort.memberPorts().value()) {
        auto it = portIdToPortInfo.find(member.memberPortID().value());
        if (it == portIdToPortInfo.end()) {
          XLOG(DBG2) << "Member port " << member.memberPortID().value()
                     << " of hyper port " << aggPort.name().value()
                     << " not found in port info on " << switchName;
          return false;
        }

        if (it->second.operState().value() != PortOperState::UP) {
          XLOG(DBG2) << "From " << switchName
                     << " hyper port member: " << it->second.name().value()
                     << " of hyper port " << aggPort.name().value()
                     << " is not up";
          return false;
        }

        if (it->second.cableLengthMeters().has_value() &&
            it->second.cableLengthMeters().value() > 0) {
          XLOG(DBG2) << "From " << switchName
                     << " hyper port member: " << it->second.name().value()
                     << " of hyper port " << aggPort.name().value()
                     << " has cable length: "
                     << it->second.cableLengthMeters().value();
          continue;
        }

        XLOG(DBG2) << "From " << switchName
                   << " hyper port member: " << it->second.name().value()
                   << " of hyper port " << aggPort.name().value()
                   << " has invalid cable length: "
                   << it->second.cableLengthMeters().value_or(-1);
        return false;
      }
    }

    return true;
  };

  // Cable length is collected during periodic stats collection, not immediately
  // when port comes up. Thus, retry to allow stats collection to complete.
  return checkWithRetryErrorReturn(
      verifyCableLengthHelper,
      30 /* num retries */,
      std::chrono::milliseconds(2000) /* sleep between retries */,
      true /* retry on exception */);
}

bool verifyHyperPortMemberCableLength(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Hyper Port Member Cable Length";
  for (const auto& edsw : topologyInfo->getEdsws()) {
    if (!verifyHyperPortMemberCableLengthForSwitch(edsw)) {
      return false;
    }
  }
  return true;
}

bool verifyPortsForEdsws(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Ports for EDSWs";
  if (FLAGS_hyper_port) {
    if (!verifyHyperPortMembersUp(topologyInfo)) {
      return false;
    }
    if (!verifyHyperPortLacpState(topologyInfo)) {
      return false;
    }
    if (!verifyHyperPortUp(topologyInfo)) {
      return false;
    }
    if (!verifyHyperPortMemberCableLength(topologyInfo)) {
      return false;
    }
  }

  return true;
}

bool verifyPortsForFdsws(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Ports for FDSWs";
  for (const auto& fdsw : topologyInfo->getFdsws()) {
    if (!verifyFabricPorts(fdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyPortsForSdsws(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Ports for SDSWs";
  for (const auto& sdsw : topologyInfo->getSdsws()) {
    if (!verifyFabricPorts(sdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyPorts(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying Ports";
  return verifyPortsForRdsws(topologyInfo) &&
      verifyPortsForFdsws(topologyInfo) && verifyPortsForSdsws(topologyInfo) &&
      verifyPortsForEdsws(topologyInfo);
}

bool verifySystemPortsForRdsw(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& rdswToVerify) {
  // Every GLOBAL system port of every remote RDSW is either a STATIC_ENTRY
  // or DYNAMIC_ENTRY of the local RDSW
  XLOG(DBG2) << "Verifying System Ports for RDSW: " << rdswToVerify;
  std::set<std::string> gotSystemPorts;
  std::set<std::string> expectedSystemPorts;
  for (const auto& [clusterId, rdsws] :
       std::as_const(topologyInfo->getClusterIdToRdsws())) {
    for (const auto& rdsw : std::as_const(rdsws)) {
      if (rdsw == rdswToVerify) {
        gotSystemPorts = getGlobalSystemPortsOfType(
            rdswToVerify,
            {RemoteSystemPortType::STATIC_ENTRY,
             RemoteSystemPortType::
                 DYNAMIC_ENTRY} /* Get Global Remote ports of self */);
      } else {
        auto systemPortsForRdsw = getGlobalSystemPortsOfType(
            rdsw, {} /* Get Global ports of Remote RDSW */);
        expectedSystemPorts.insert(
            systemPortsForRdsw.begin(), systemPortsForRdsw.end());
      }
    }
  }

  XLOG(DBG2) << "From " << rdswToVerify << " Expected System Ports: "
             << folly::join(",", expectedSystemPorts)
             << " Got System Ports: " << folly::join(",", gotSystemPorts);

  return expectedSystemPorts == gotSystemPorts;
}

bool verifySystemPorts(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying System Ports";
  for (const auto& rdsw : topologyInfo->getRdsws()) {
    if (!verifySystemPortsForRdsw(topologyInfo, rdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyRifsForRdsw(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& rdswToVerify) {
  // Every GLOBAL rif of every remote RDSW is either a STATIC_ENTRY or
  // DYNAMIC_ENTRY of the local RDSW
  XLOG(DBG2) << "Verifying RIFs for RDSW: " << rdswToVerify;
  std::set<int> gotRifs;
  std::set<int> expectedRifs;
  for (const auto& [clusterId, rdsws] :
       std::as_const(topologyInfo->getClusterIdToRdsws())) {
    for (const auto& rdsw : std::as_const(rdsws)) {
      if (rdsw == rdswToVerify) {
        gotRifs = getGlobalRifsOfType(
            rdswToVerify,
            {RemoteInterfaceType::STATIC_ENTRY,
             RemoteInterfaceType::
                 DYNAMIC_ENTRY} /* Get Global Remote rifs of self */);
      } else {
        auto rifsForRdsw =
            getGlobalRifsOfType(rdsw, {} /* Get Global rifs of Remote RDSW */);
        expectedRifs.insert(rifsForRdsw.begin(), rifsForRdsw.end());
      }
    }
  }

  XLOG(DBG2) << "From " << rdswToVerify
             << " Expected Rifs: " << folly::join(",", expectedRifs)
             << " Got Rifs: " << folly::join(",", gotRifs);

  return expectedRifs == gotRifs;
}

bool verifyRifs(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying RIFs";
  for (const auto& rdsw : topologyInfo->getRdsws()) {
    if (!verifyRifsForRdsw(topologyInfo, rdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyStaticNdpEntriesForRdsw(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& rdswToVerify) {
  // Every loopbackIP in the DSF cluster must have STATIC NDP entry in the RDSW.
  auto loopbackIps = getLoopbackIpsInDsfCluster(topologyInfo->getMyHostname());

  auto staticNdpEntries = getNdpEntriesOfType(rdswToVerify, {"STATIC"});
  std::set<folly::IPAddress> staticNdpIps;
  std::transform(
      staticNdpEntries.begin(),
      staticNdpEntries.end(),
      std::inserter(staticNdpIps, staticNdpIps.begin()),
      [](const auto& ndpEntry) {
        return folly::IPAddress::fromBinary(
            folly::ByteRange(
                folly::StringPiece(ndpEntry.ip().value().addr().value())));
      });

  if (staticNdpIps != loopbackIps) {
    XLOG(ERR) << "STATIC NDP Entries from " << rdswToVerify
              << folly::join(",", staticNdpIps)
              << " LoopbackIps in DSF Cluster:"
              << folly::join(",", loopbackIps);

    return false;
  }

  return true;
}

bool verifyStaticNdpEntries(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying static NDP entries";
  for (const auto& rdsw : topologyInfo->getRdsws()) {
    if (!verifyStaticNdpEntriesForRdsw(topologyInfo, rdsw)) {
      return false;
    }
  }

  return true;
}

bool verifyDsfSessions(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  // Every RDSW must have an ESTABLISHED DSF Session with every other RDSW
  XLOG(DBG2) << "Verifying DSF sessions";
  for (const auto& rdsw : topologyInfo->getRdsws()) {
    auto expectedRdsws = topologyInfo->getRdsws();
    expectedRdsws.erase(rdsw); // exclude self
    auto gotRdsws = getPeersWithEstablishedDsfSessions(rdsw);
    if (expectedRdsws != gotRdsws) {
      XLOG(DBG2) << "DSF ESTABLISHED sessions from " << rdsw
                 << " Expected RDSWs: " << folly::join(",", expectedRdsws)
                 << " Got RDSWs: " << folly::join(",", gotRdsws);
      return false;
    }
  }

  return true;
}

void verifyDsfCluster(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(5000), {
    if (FLAGS_hyper_port) {
      EXPECT_EVENTUALLY_TRUE(verifyPorts(topologyInfo));
    } else {
      EXPECT_EVENTUALLY_TRUE(verifyFabricConnectivity(topologyInfo));
      EXPECT_EVENTUALLY_TRUE(verifyFabricReachability(topologyInfo));
      EXPECT_EVENTUALLY_TRUE(verifyPorts(topologyInfo));
      EXPECT_EVENTUALLY_TRUE(verifySystemPorts(topologyInfo));
      EXPECT_EVENTUALLY_TRUE(verifyRifs(topologyInfo));
      EXPECT_EVENTUALLY_TRUE(verifyStaticNdpEntries(topologyInfo));
      EXPECT_EVENTUALLY_TRUE(verifyDsfSessions(topologyInfo));
    }
  });
}

bool verifyDsfAgentRestartForRdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    bool triggerGracefulRestart) {
  auto myHostname = topologyInfo->getMyHostname();
  auto baselinePeerToDsfSession = getPeerToDsfSession(myHostname);

  // For any one RDSW in every remote cluster issue Agent restart
  for (const auto& [_, rdsws] :
       std::as_const(topologyInfo->getClusterIdToRdsws())) {
    for (const auto& rdsw : std::as_const(rdsws)) {
      if (rdsw == myHostname) { // exclude self
        continue;
      }

      // Trigger graceful or ungraceful Agent restart
      if (!restartServiceForSwitches(
              {rdsw},
              [triggerGracefulRestart](const std::string& switchName) {
                if (triggerGracefulRestart) {
                  triggerGracefulAgentRestart(switchName);
                } else {
                  triggerUngracefulAgentRestart(switchName);
                }
              },
              getAgentAliveSinceEpoch,
              verifySwSwitchRunState,
              SwitchRunState::CONFIGURED)) {
        XLOG(ERR) << "Failed to restart Agent on RDSW: " << rdsw;
        return false;
      }

      // Sessions to RDSW that was just restarted are expected to flap.
      // This is regardless of graceful or ungraceful Agent restart.
      // Verify no other sessions flap.
      auto expectedPeerToDsfSession = baselinePeerToDsfSession;
      expectedPeerToDsfSession.erase(rdsw);
      if (!verifyNoSessionsFlap(
              myHostname /* rdswToVerify */,
              expectedPeerToDsfSession,
              rdsw /* rdswToExclude */)) {
        return false;
      }

      // Verify all DSF sessions are established for the RDSW that was
      // restarted
      if (!verifyAllSessionsEstablished(rdsw)) {
        return false;
      }

      // Restart only one remote RDSW per cluster
      break;
    }
  }

  return true;
}

bool verifyDsfGracefulAgentRestartForRdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Graceful Agent Restart for RDSWs";
  return verifyDsfAgentRestartForRdsws(
      topologyInfo, true /* triggerGracefulRestart */);
}

bool verifyDsfAgentRestartForFdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    bool triggerGracefulRestart) {
  auto myHostname = topologyInfo->getMyHostname();
  auto baselinePeerToDsfSession = getPeerToDsfSession(myHostname);

  // For any one FDSW in every remote cluster issue graceful restart
  for (const auto& [_, fdsws] :
       std::as_const(topologyInfo->getClusterIdToFdsws())) {
    // Gracefully restart only one remote FDSW per cluster
    if (!fdsws.empty()) {
      auto fdsw = fdsws.front();

      // Trigger graceful or ungraceful Agent restart
      if (!restartServiceForSwitches(
              {fdsw},
              [triggerGracefulRestart](const std::string& switchName) {
                if (triggerGracefulRestart) {
                  triggerGracefulAgentRestart(switchName);
                } else {
                  triggerUngracefulAgentRestart(switchName);
                }
              },
              getAgentAliveSinceEpoch,
              verifySwSwitchRunState,
              SwitchRunState::CONFIGURED)) {
        XLOG(ERR) << "Failed to restart Agent on FDSW: " << fdsw;
        return false;
      }
    }
  }

  // verify no flaps is expensive.
  // Thus, only verify after warmboot restarting one FDSW from each cluster.
  // There is no loss of signal due to this approach as if the sessions flap
  // due to an intermediate warmboot, it will be detected by this check
  // anyway.
  if (!verifyNoSessionsFlap(myHostname, baselinePeerToDsfSession)) {
    return false;
  }

  return true;
}

bool verifyDsfGracefulAgentRestartForFdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Graceful Agent Restart for FDSWs";
  return verifyDsfAgentRestartForFdsws(
      topologyInfo, true /* triggerGracefulRestart */);
}

bool verifyDsfGracefulAgentRestartForSdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  return true;
}

bool verifyDsfGracefulAgentRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  return verifyDsfGracefulAgentRestartForRdsws(topologyInfo) &&
      verifyDsfGracefulAgentRestartForFdsws(topologyInfo) &&
      verifyDsfGracefulAgentRestartForSdsws(topologyInfo);
}

bool verifyDsfUngracefulAgentRestartForRdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Ungraceful Agent Restart for RDSWs";
  return verifyDsfAgentRestartForRdsws(
      topologyInfo, false /* triggerGracefulRestart */);
}

bool verifyDsfUngracefulAgentRestartForFdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Ungraceful Agent Restart for FDSWs";
  return verifyDsfAgentRestartForFdsws(
      topologyInfo, false /* triggerGracefulRestart */);
}

bool verifyDsfUngracefulAgentRestartForSdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  return true;
}

bool verifyDsfUngracefulAgentRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  return verifyDsfUngracefulAgentRestartForRdsws(topologyInfo) &&
      verifyDsfUngracefulAgentRestartForFdsws(topologyInfo) &&
      verifyDsfUngracefulAgentRestartForSdsws(topologyInfo);
}

std::set<std::string> triggerGracefulAgentRestartWithDelayForRdsws(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  const auto kGracefulRestartTimeout = FLAGS_dsf_gr_hold_time;
  if (kGracefulRestartTimeout == 0) {
    throw facebook::fboss::FbossError(
        "FLAGS_dsf_gr_hold_time is 0 (default); ",
        "DSF graceful restart test requires it set in materialized config");
  }
  // Buffer is bounded above by verifyLiveSystemPorts's retry budget (also
  // 30 x 5000ms = 150s): the delayed restart must fire before verifyLive
  // gives up. Caller flow is verifyStale -> verifyLive, both starting at
  // t=0 and t~=GR respectively, so verifyStale never races the restart in
  // practice (it returns on first STALE observation around t=GR). +60s
  // gives ~30s post-verifyStale slack without pushing restart past
  // verifyLive's deadline (~t=GR+150).
  const auto kDelayBetweenRestarts = kGracefulRestartTimeout + 60;

  auto myHostname = topologyInfo->getMyHostname();

  // For any one RDSW in every remote cluster issue delayed Agent restart
  std::set<std::string> restartedRdsws;
  for (const auto& [_, rdsws] :
       std::as_const(topologyInfo->getClusterIdToRdsws())) {
    for (const auto& rdsw : std::as_const(rdsws)) {
      if (rdsw == myHostname) { // exclude self
        continue;
      }
      triggerGracefulAgentRestartWithDelay(rdsw, kDelayBetweenRestarts);
      restartedRdsws.insert(rdsw);

      // Gracefully restart only one remote RDSW per cluster
      break;
    }
  }

  return restartedRdsws;
}

bool verifyStaleSystemPorts(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::set<std::string>& restartedRdsws) {
  if (restartedRdsws.empty()) {
    XLOG(ERR) << "verifyStaleSystemPorts: called with empty restartedRdsws";
    return false;
  }
  std::set<SwitchID> restartedSwitchIds;
  for (const auto& rdsw : restartedRdsws) {
    auto it = topologyInfo->getSwitchNameToSwitchIds().find(rdsw);
    if (it == topologyInfo->getSwitchNameToSwitchIds().end()) {
      XLOG(ERR) << "verifyStaleSystemPorts: restarted RDSW '" << rdsw
                << "' not in topology switchNameToSwitchIds";
      return false;
    }
    restartedSwitchIds.insert(it->second.begin(), it->second.end());
  }

  // Query every non-restarted RDSW. We skip the restarted ones because:
  // (a) their sw_agent is dead during most of the restart window (Thrift
  //     CONNECT_REFUSED), and (b) even briefly after recovery, the freshly
  //     restarted agent is a fresh subscriber that hasn't seen the disconnect
  //     — it would report other restarted RDSWs as LIVE rather than STALE.
  // With dsf_gr_hold_time set cluster-wide, every non-restarted subscriber
  // marks the restarted RDSWs' system ports as STALE after the GR hold timer.
  std::set<std::string> queryRdsws = topologyInfo->getRdsws();
  for (const auto& rdsw : restartedRdsws) {
    queryRdsws.erase(rdsw);
  }
  if (queryRdsws.empty()) {
    XLOG(ERR) << "verifyStaleSystemPorts: no RDSWs to query (every RDSW is "
                 "marked restarted)";
    return false;
  }

  auto staleSystemPorts = [&queryRdsws, &restartedSwitchIds] {
    bool failed = false;
    std::map<SwitchID, int> observedFromRestarted;
    for (const auto& sid : restartedSwitchIds) {
      observedFromRestarted[sid] = 0;
    }
    for (const auto& [observingRdsw, systemPorts] :
         getRdswToSystemPorts(queryRdsws)) {
      for (const auto& systemPort : systemPorts) {
        auto livenessStatus = systemPort.remoteSystemPortLivenessStatus();
        if (!livenessStatus.has_value()) {
          continue;
        }

        SwitchID sid(*systemPort.switchId());
        bool portFromRestartedSwitch = restartedSwitchIds.count(sid);

        if (portFromRestartedSwitch) {
          observedFromRestarted[sid]++;
          if (livenessStatus.value() != LivenessStatus::STALE) {
            XLOG(DBG2) << "verifyStaleSystemPorts: observingRdsw="
                       << observingRdsw << " expected STALE got "
                       << apache::thrift::util::enumNameSafe(*livenessStatus)
                       << " on " << *systemPort.portName()
                       << " switchId=" << *systemPort.switchId();
            failed = true;
          }
        } else {
          if (livenessStatus.value() != LivenessStatus::LIVE) {
            XLOG(DBG2) << "verifyStaleSystemPorts: observingRdsw="
                       << observingRdsw << " expected LIVE got "
                       << apache::thrift::util::enumNameSafe(*livenessStatus)
                       << " on " << *systemPort.portName()
                       << " switchId=" << *systemPort.switchId();
            failed = true;
          }
        }
      }
    }
    // Every restarted switch must have at least one liveness-bearing port
    // observed; otherwise the test could vacuously pass before subscribers
    // reported any state for the restarted RDSW.
    for (const auto& [sid, count] : observedFromRestarted) {
      if (count == 0) {
        XLOG(ERR) << "verifyStaleSystemPorts: zero ports with liveness "
                     "observed for restarted switchId="
                  << sid;
        failed = true;
      }
    }
    return !failed;
  };

  return checkWithRetryErrorReturn(
      staleSystemPorts,
      30 /* num retries */,
      std::chrono::milliseconds(5000) /* sleep between retries */,
      true /* retry on exception */);
}

bool verifyStaleRifs(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::set<std::string>& restartedRdsws) {
  if (restartedRdsws.empty()) {
    XLOG(ERR) << "verifyStaleRifs: called with empty restartedRdsws";
    return false;
  }
  // rifs don't carry their owning switchId in the Thrift response, so we
  // classify by interfaceId range instead. Each switch owns a contiguous
  // systemPortRange, and by FBOSS DSF convention a rif's interfaceId falls
  // in its owning switch's range. Track (rdsw, lo, hi) so we can later
  // count observations per-RDSW.
  std::vector<std::tuple<std::string, int64_t, int64_t>> restartedIntfIdRanges;
  const auto& nameToRanges = topologyInfo->getSwitchNameToSystemPortRanges();
  for (const auto& rdsw : restartedRdsws) {
    auto it = nameToRanges.find(rdsw);
    if (it == nameToRanges.end()) {
      XLOG(ERR) << "verifyStaleRifs: restarted RDSW '" << rdsw
                << "' not in topology switchNameToSystemPortRanges";
      return false;
    }
    for (const auto& range : *it->second.systemPortRanges()) {
      restartedIntfIdRanges.emplace_back(
          rdsw, *range.minimum(), *range.maximum());
    }
  }

  // Query every non-restarted RDSW. We skip the restarted ones because:
  // (a) their sw_agent is dead during most of the restart window (Thrift
  //     CONNECT_REFUSED), and (b) even briefly after recovery, the freshly
  //     restarted agent is a fresh subscriber that hasn't seen the disconnect
  //     — it would report other restarted RDSWs as LIVE rather than STALE.
  // With dsf_gr_hold_time set cluster-wide, every non-restarted subscriber
  // marks the restarted RDSWs' rifs as STALE after the GR hold timer.
  std::set<std::string> queryRdsws = topologyInfo->getRdsws();
  for (const auto& rdsw : restartedRdsws) {
    queryRdsws.erase(rdsw);
  }
  if (queryRdsws.empty()) {
    XLOG(ERR) << "verifyStaleRifs: no RDSWs to query (every RDSW is marked "
                 "restarted)";
    return false;
  }

  auto staleRifs = [&queryRdsws, &restartedIntfIdRanges, &restartedRdsws] {
    bool failed = false;
    std::map<std::string, int> observedFromRestarted;
    for (const auto& rdsw : restartedRdsws) {
      observedFromRestarted[rdsw] = 0;
    }
    for (const auto& [observingRdsw, rifs] : getRdswToRifs(queryRdsws)) {
      for (const auto& rif : rifs) {
        auto livenessStatus = rif.remoteIntfLivenessStatus();
        if (!livenessStatus.has_value()) {
          continue;
        }

        auto intfId = *rif.interfaceId();
        const std::string* owningRdsw = nullptr;
        for (const auto& [rdsw, lo, hi] : restartedIntfIdRanges) {
          if (intfId >= lo && intfId <= hi) {
            owningRdsw = &rdsw;
            break;
          }
        }

        if (owningRdsw != nullptr) {
          observedFromRestarted[*owningRdsw]++;
          if (livenessStatus.value() != LivenessStatus::STALE) {
            XLOG(DBG2) << "verifyStaleRifs: observingRdsw=" << observingRdsw
                       << " expected STALE got "
                       << apache::thrift::util::enumNameSafe(*livenessStatus)
                       << " on " << *rif.interfaceName()
                       << " interfaceId=" << intfId;
            failed = true;
          }
        } else {
          if (livenessStatus.value() != LivenessStatus::LIVE) {
            XLOG(DBG2) << "verifyStaleRifs: observingRdsw=" << observingRdsw
                       << " expected LIVE got "
                       << apache::thrift::util::enumNameSafe(*livenessStatus)
                       << " on " << *rif.interfaceName()
                       << " interfaceId=" << intfId;
            failed = true;
          }
        }
      }
    }
    // Every restarted RDSW must have at least one liveness-bearing rif
    // observed; otherwise the test could vacuously pass before subscribers
    // reported any state for that restarted RDSW.
    for (const auto& [rdsw, count] : observedFromRestarted) {
      if (count == 0) {
        XLOG(ERR) << "verifyStaleRifs: zero rifs with liveness observed for "
                     "restarted RDSW="
                  << rdsw;
        failed = true;
      }
    }
    return !failed;
  };

  return checkWithRetryErrorReturn(
      staleRifs,
      30 /* num retries */,
      std::chrono::milliseconds(5000) /* sleep between retries */,
      true /* retry on exception */);
}

bool verifyLiveSystemPorts(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  auto allRdsws = topologyInfo->getRdsws();
  auto liveSystemPorts = [allRdsws] {
    for (const auto& [rdsw, systemPorts] : getRdswToSystemPorts(allRdsws)) {
      for (const auto& systemPort : systemPorts) {
        auto livenessStatus = systemPort.remoteSystemPortLivenessStatus();
        if (!livenessStatus.has_value()) {
          continue;
        }

        if (livenessStatus.value() != LivenessStatus::LIVE) {
          return false;
        }
      }
    }

    return true;
  };

  return checkWithRetryErrorReturn(
      liveSystemPorts,
      30 /* num retries */,
      std::chrono::milliseconds(5000) /* sleep between retries */,
      true /* retry on exception */);
}

bool verifyLiveRifs(const std::unique_ptr<TopologyInfo>& topologyInfo) {
  auto allRdsws = topologyInfo->getRdsws();
  auto liveRifs = [allRdsws] {
    for (const auto& [_, rifs] : getRdswToRifs(allRdsws)) {
      for (const auto& rif : rifs) {
        auto livenessStatus = rif.remoteIntfLivenessStatus();
        if (!livenessStatus.has_value()) {
          continue;
        }

        if (livenessStatus.value() != LivenessStatus::LIVE) {
          return false;
        }
      }
    }

    return true;
  };

  return checkWithRetryErrorReturn(
      liveRifs,
      30 /* num retries */,
      std::chrono::milliseconds(5000) /* sleep between retries */,
      true /* retry on exception */);
}

bool verifyDsfGracefulAgentRestartTimeoutRecovery(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Graceful Agent Restart Timeout and Recovery";

  // This test can be written as:
  //  - Stop Agent
  //  - Wait for 120s i.e. GR timeout
  //  - Verify entries are STALE
  //  - Start Agent
  //  - Verify entries are LIVE
  // However, in order to implement above sequence, we need a mechanism for
  // the test to invoke "Start Agent" when Agent thrift server is not
  // running.
  //
  // This can be accomplished by test client (this code) logging into the
  // remote device running Agent. But then the test needs to worry about
  // login credentials. Alternatively, the remote device can run a Thrift
  // server for the test purpose along, but that is an overkill for this use
  // case.
  //
  // This test logic solves it with the following approach:
  //  - Restart Agent API with delay: Test API supported by remote device
  //    - Stop Agent,
  //    - Sleep for 120s (GR Timeout) + 60s (time to verify STALE state)
  //    - Start Agent
  //  - Validate STALE state... while Agent is stopped.
  //  - Validate LIVE state.... after Agent has restarted.
  auto restartedRdsws =
      triggerGracefulAgentRestartWithDelayForRdsws(topologyInfo);

  // First: Agent stops, GR times out, Sys ports and Rifs turn STALE.
  if (!verifyStaleSystemPorts(topologyInfo, restartedRdsws)) {
    XLOG(ERR) << "Failed to verify Stale system ports";
    return false;
  }

  if (!verifyStaleRifs(topologyInfo, restartedRdsws)) {
    XLOG(ERR) << "Failed to verify Stale rifs";
    return false;
  }

  // Later: Agent restarts, Sys ports and Rifs resync and turn LIVE.
  if (!verifyLiveSystemPorts(topologyInfo)) {
    XLOG(ERR) << "Failed to verify Live system ports";
    return false;
  }

  if (!verifyLiveRifs(topologyInfo)) {
    XLOG(ERR) << "Failed to verify Live rifs";
    return false;
  }

  return true;
}

bool verifyDsfQsfpRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    bool triggerGracefulRestart) {
  XLOG(DBG2) << "Verifying DSF "
             << (triggerGracefulRestart ? "Graceful" : "Ungraceful")
             << " QSFP Restart";

  auto myHostname = topologyInfo->getMyHostname();
  auto baselinePeerToDsfSession = getPeerToDsfSession(myHostname);

  // For every device (RDSW, FDSW, SDSW) issue QSFP graceful restart
  if (!restartServiceForSwitches(
          topologyInfo->getAllSwitches(),
          triggerGracefulRestart ? triggerGracefulQsfpRestart
                                 : triggerUngracefulQsfpRestart,
          getQsfpAliveSinceEpoch,
          verifyQsfpServiceRunState,
          QsfpServiceRunState::ACTIVE)) {
    XLOG(ERR) << "Failed to restart QSFP on: "
              << folly::join(",", topologyInfo->getAllSwitches());
    return false;
  }

  // No session flaps are expected for QSFP graceful restart.
  if (!verifyNoSessionsFlap(myHostname, baselinePeerToDsfSession)) {
    return false;
  }

  return true;
}

bool verifyDsfGracefulQsfpRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Graceful QSFP Restart";
  return verifyDsfQsfpRestart(topologyInfo, true /* triggerGracefulRestart */);
}

bool verifyDsfUngracefulQsfpRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Ungraceful QSFP Restart";
  return verifyDsfQsfpRestart(topologyInfo, false /* triggerGracefulRestart */);
}

bool verifyDsfFSDBRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    bool triggerGracefulRestart) {
  XLOG(DBG2) << "Verifying DSF "
             << (triggerGracefulRestart ? "Graceful" : "Ungraceful")
             << " FSDB Restart";

  auto myHostname = topologyInfo->getMyHostname();
  auto baselinePeerToDsfSession = getPeerToDsfSession(myHostname);

  // For any one RDSW in every remote cluster issue graceful FSDB restart
  for (const auto& [_, rdsws] :
       std::as_const(topologyInfo->getClusterIdToRdsws())) {
    for (const auto& rdsw : std::as_const(rdsws)) {
      if (rdsw == myHostname) { // exclude self
        continue;
      }

      if (!restartServiceForSwitches(
              {rdsw},
              triggerGracefulRestart ? triggerGracefulFsdbRestart
                                     : triggerUngracefulFsdbRestart,
              getFsdbAliveSinceEpoch,
              verifyFsdbIsUp)) {
        XLOG(ERR) << "Failed to restart FSDB on: "
                  << folly::join(",", topologyInfo->getAllSwitches());
        return false;
      }

      // Sessions to RDSW whose FSDB just restarted are expected to flap.
      // Verify no other sessions flap.
      auto expectedPeerToDsfSession = baselinePeerToDsfSession;
      expectedPeerToDsfSession.erase(rdsw);
      if (!verifyNoSessionsFlap(
              myHostname /* rdswToVerify */,
              expectedPeerToDsfSession,
              rdsw /* rdswToExclude */)) {
        return false;
      }

      // Verify all DSF sessions are established for the RDSW that was
      // restarted
      if (!verifyAllSessionsEstablished(rdsw)) {
        return false;
      }

      // Restart only one remote RDSW FSDB per cluster
      break;
    }
  }

  return true;
}

bool verifyDsfGracefulFSDBRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Graceful FSDB Restart";
  return verifyDsfFSDBRestart(topologyInfo, true /* triggerGracefulRestart */);
}

bool verifyDsfUngracefulFSDBRestart(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Ungraceful FSDB Restart";
  return verifyDsfFSDBRestart(topologyInfo, false /* triggerGracefulRestart */);
}

bool verifyDsfGracefulFabricLinkDisableOrDrain(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& rdswToVerify,
    const std::map<std::string, PortInfoThrift>& activeFabricPortNameToPortInfo,
    const std::function<void(const std::string&, int32_t)>&
        portDisableOrDrainFunc) {
  XLOG(DBG2) << "Verifying DSF Graceful Fabric link Down";

  CHECK_GT(activeFabricPortNameToPortInfo.size(), 2);
  auto rIter = activeFabricPortNameToPortInfo.rbegin();
  auto lastActivePort = rIter->first;
  auto secondLastActivePort = std::next(rIter)->first;
  auto baselinePeerToDsfSession = getPeerToDsfSession(rdswToVerify);

  // Admin disable all Active fabric ports
  // Verify that all sessions stay established till last port is disabled.
  // TODO: modify the config to set minLink threshold, and then extend the
  // logic to verify that the sessions stay established while the minLink
  // requirement is met.
  for (const auto& [portName, portInfo] : activeFabricPortNameToPortInfo) {
    XLOG(DBG2) << __func__
               << " Admin disabling port:: " << portInfo.name().value()
               << " portID: " << portInfo.portId().value();
    portDisableOrDrainFunc(rdswToVerify, portInfo.portId().value());

    bool checkPassed = true;
    if (portName == lastActivePort) {
      checkPassed = verifyNoSessionsEstablished(rdswToVerify);
    } else if (portName == secondLastActivePort) {
      // verify no flaps is expensive.
      // Thus, only verify just before disabling the last port.
      // There is no loss of signal due to this approach as if the sessions
      // flap due to an intermediate port admin disable, it will be detected
      // by this check failure anyway.
      checkPassed =
          verifyNoSessionsFlap(rdswToVerify, baselinePeerToDsfSession);
    }
    if (!checkPassed) {
      return false;
    }
  }

  return true;
}

bool verifyDsfGracefulFabricLinkEnableOrUndrain(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& rdswToVerify,
    const std::map<std::string, PortInfoThrift>& activeFabricPortNameToPortInfo,
    const std::function<void(const std::string&, int32_t)>&
        portEnableOrUndrainFunc) {
  XLOG(DBG2) << "Verifying DSF Graceful Fabric link Up";

  CHECK_GT(activeFabricPortNameToPortInfo.size(), 2);
  auto firstActivePort = activeFabricPortNameToPortInfo.begin()->first;
  auto rIter = activeFabricPortNameToPortInfo.rbegin();
  auto lastActivePort = rIter->first;
  // Admin Re-enable these fabric ports
  for (const auto& [portName, portInfo] : activeFabricPortNameToPortInfo) {
    XLOG(DBG2) << __func__
               << " Admin enabling port:: " << portInfo.name().value()
               << " portID: " << portInfo.portId().value();
    portEnableOrUndrainFunc(rdswToVerify, portInfo.portId().value());

    bool checkPassed = true;
    if (portName == firstActivePort || portName == lastActivePort) {
      checkPassed = verifyAllSessionsEstablished(rdswToVerify);
    }

    if (!checkPassed) {
      return false;
    }
  }

  return true;
}

bool verifyDsfGracefulFabricLinkDownUp(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Graceful Fabric link Down then Up";

  auto myHostname = topologyInfo->getMyHostname();
  auto activeFabricPortNameToPortInfo =
      getActiveFabricPortNameToPortInfo(myHostname);

  if (!verifyDsfGracefulFabricLinkDisableOrDrain(
          topologyInfo,
          myHostname,
          activeFabricPortNameToPortInfo,
          [](const auto& rdswToVerify, int32_t portID) {
            adminDisablePort(rdswToVerify, portID);
          })) {
    XLOG(ERR) << "Failed to verify DSF Graceful Fabric link Down";
    return false;
  }

  if (!verifyDsfGracefulFabricLinkEnableOrUndrain(
          topologyInfo,
          myHostname,
          activeFabricPortNameToPortInfo,
          [](const auto& rdswToVerify, int32_t portID) {
            adminEnablePort(rdswToVerify, portID);
          })) {
    XLOG(ERR) << "Failed to verify DSF Ungraceful Fabric link Up";
    return false;
  }

  return true;
}

bool verifyDsfFabricLinkDrainUndrain(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  XLOG(DBG2) << "Verifying DSF Fabric link Drain then Undrain";

  auto myHostname = topologyInfo->getMyHostname();
  auto activeFabricPortNameToPortInfo =
      getActiveFabricPortNameToPortInfo(myHostname);
  std::set<std::string> activeFabricPorts;
  for (const auto& [port, _] : activeFabricPortNameToPortInfo) {
    activeFabricPorts.insert(port);
  }

  if (!verifyDsfGracefulFabricLinkDisableOrDrain(
          topologyInfo,
          myHostname,
          activeFabricPortNameToPortInfo,
          [](const auto& rdswToVerify, int32_t portID) {
            drainPort(rdswToVerify, portID);
          })) {
    XLOG(ERR) << "Failed to verify DSF Fabric link Drain";
    return false;
  }

  // Post DRAIN, the ports should turn ACTIVE => INACTIVE
  if (!verifyPortActiveState(
          myHostname, activeFabricPorts, PortActiveState::INACTIVE)) {
    XLOG(ERR) << "All Drained ports should be Inactive: ",
        folly::join(",", activeFabricPorts);
    return false;
  }

  if (!verifyDsfGracefulFabricLinkEnableOrUndrain(
          topologyInfo,
          myHostname,
          activeFabricPortNameToPortInfo,
          [](const auto& rdswToVerify, int32_t portID) {
            undrainPort(rdswToVerify, portID);
          })) {
    XLOG(ERR) << "Failed to verify DSF Fabric link Undrain";
    return false;
  }

  // Post UNDRAIN, the same ports should turn INACTIVE => ACTIVE again
  if (!verifyPortActiveState(
          myHostname, activeFabricPorts, PortActiveState::ACTIVE)) {
    XLOG(ERR) << "All Drained ports should be Inactive: ",
        folly::join(",", activeFabricPorts);
    return false;
  }

  return true;
}

bool verifyFabricSpray(const std::string& switchName) {
  auto verifyFabricSprayHelper = [switchName]() {
    int64_t lowestMbps = std::numeric_limits<int64_t>::max();
    int64_t highestMbps = std::numeric_limits<int64_t>::min();

    auto counterNameToCount = getCounterNameToCount(switchName);
    for (const auto& [_, portInfo] :
         getActiveFabricPortNameToPortInfo(switchName)) {
      auto portName = portInfo.name().value();
      auto counterName = portName + ".out_bytes.rate.60";
      auto outSpeedMbps = counterNameToCount[counterName] * 8 / 1000000;

      lowestMbps = std::min(lowestMbps, outSpeedMbps);
      highestMbps = std::max(highestMbps, outSpeedMbps);

      XLOG(DBG2) << "Switch: " << switchName
                 << " Active Fabric port: " << portInfo.name().value()
                 << " outSpeedMbps: " << outSpeedMbps
                 << " lowestMbps: " << lowestMbps
                 << " highestMbps: " << highestMbps;
    }

    return isDeviationWithinThreshold(
        lowestMbps, highestMbps, 5 /* maxDeviationPct */);
  };

  return checkWithRetryErrorReturn(
      verifyFabricSprayHelper,
      30 /* num retries */,
      std::chrono::milliseconds(1000) /* sleep between retries */,
      true /* retry on exception */);
}

std::map<std::string, std::map<std::string, DsfSessionThrift>>
getPeerToDsfSessionForRdsws(const std::set<std::string>& rdsws) {
  return forEachWithRetVal(rdsws, getPeerToDsfSession);
}

bool verifyNoSessionsFlapForRdsws(
    const std::set<std::string>& rdsws,
    std::map<std::string, std::map<std::string, DsfSessionThrift>>&
        baselineRdswToPeerAndDsfSession) {
  for (const auto& [rdsw, baselinePeerToDsfSession] :
       baselineRdswToPeerAndDsfSession) {
    if (!verifyNoSessionsFlap(rdsw, baselinePeerToDsfSession)) {
      XLOG(DBG2) << "DSF Sessions flapped from rdsw: " << rdsw;
      return false;
    }
  }

  return true;
}

bool verifyNoReassemblyErrorsForAllSwitches(
    const std::unique_ptr<utility::TopologyInfo>& topologyInfo) {
  const auto& switches = topologyInfo->getAllSwitches();
  const auto& switchToAsicType = topologyInfo->getSwitchNameToAsicType();
  auto verifyNoReassemblyErrorsForAllSwitchesHelper = [switches,
                                                       switchToAsicType]() {
    auto getCounterName = [switchToAsicType](const auto& switchName) {
      auto iter = switchToAsicType.find(switchName);
      CHECK(iter != switchToAsicType.end());
      auto asicType = iter->second;
      auto vendorName = getHwAsicForAsicType(asicType).getVendor();

      return vendorName + ".reassembly.errors.sum";
    };

    for (const auto& switchName : switches) {
      auto counterNameToCount = getCounterNameToCount(switchName);
      auto counterName = getCounterName(switchName);
      auto reassemblyErrors = counterNameToCount[counterName];
      if (reassemblyErrors > 0) {
        XLOG(DBG2) << "Switch: " << switchName
                   << " counterName: " << counterName
                   << " reassemblyErrors: " << reassemblyErrors;
        return false;
      }
    }

    return true;
  };

  // The reassembly errors may happen after a few seeconds.
  // Thus, keep retrying for several seconds to ensure no reassembly errors.
  return checkAlwaysTrueWithRetryErrorReturn(
      verifyNoReassemblyErrorsForAllSwitchesHelper, 10 /* num retries */);
}

std::set<std::string> getOneFabricSwitchForEachCluster(
    const std::unique_ptr<utility::TopologyInfo>& topologyInfo) {
  // Get one FDSW from each cluster + one SDSW
  std::set<std::string> fabricSwitchesToTest;
  for (const auto& [_, fdsws] :
       std::as_const(topologyInfo->getClusterIdToFdsws())) {
    if (!fdsws.empty()) {
      fabricSwitchesToTest.insert(fdsws.front());
    }
  }

  if (!topologyInfo->getSdsws().empty()) {
    fabricSwitchesToTest.insert(*topologyInfo->getSdsws().begin());
  }

  return fabricSwitchesToTest;
}

int32_t getFirstActiveFabricPort(const std::string& switchName) {
  auto activeFabricPortNameToPortInfo =
      getActiveFabricPortNameToPortInfo(switchName);
  CHECK_GT(activeFabricPortNameToPortInfo.size(), 0);
  auto portInfo = activeFabricPortNameToPortInfo.cbegin()->second;

  return portInfo.portId().value();
}

int64_t getSystemPortMin(
    const std::unique_ptr<TopologyInfo>& topologyInfo,
    const std::string& switchName) {
  CHECK(
      topologyInfo->getSwitchNameToSwitchIds().find(switchName) !=
      topologyInfo->getSwitchNameToSwitchIds().end());
  auto switchId =
      *topologyInfo->getSwitchNameToSwitchIds().at(switchName).begin();
  auto switchIdToDsfNode = getSwitchIdToDsfNode(switchName);
  CHECK(switchIdToDsfNode.find(switchId) != switchIdToDsfNode.end());
  auto ranges = switchIdToDsfNode.at(switchId).systemPortRanges();

  // TODO: Extend to work with multiple system port ranges
  CHECK_GE(ranges->systemPortRanges()->size(), 1);
  auto systemPortMin = *ranges->systemPortRanges()->front().minimum();

  return systemPortMin;
}

std::map<std::string, std::set<std::string>> getRdswConnectedFabricPorts(
    const std::unique_ptr<TopologyInfo>& topologyInfo) {
  std::map<std::string, std::set<std::string>> rdswToConnectedPorts;

  // RDSWs are VoQ switches that should have fabric ports connected to FDSWs
  for (const auto& rdsw : topologyInfo->getRdsws()) {
    auto connectedPorts = getConnectedFabricPorts(rdsw);
    CHECK(!connectedPorts.empty())
        << "RDSW " << rdsw << " has no connected fabric ports";
    XLOG(DBG2) << "RDSW: " << rdsw << " has " << connectedPorts.size()
               << " connected fabric ports";
    rdswToConnectedPorts[rdsw] = std::move(connectedPorts);
  }

  return rdswToConnectedPorts;
}

} // namespace facebook::fboss::utility
