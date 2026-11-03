// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/DsfStateUpdaterUtil.h"

#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/StateDelta.h"

namespace {

facebook::fboss::StateDelta updateFibForRemoteConnectedRoutes(
    const facebook::fboss::SwitchIdScopeResolver* resolver,
    facebook::fboss::RouterID vrf,
    const facebook::fboss::IPv4NetworkToRouteMap& v4NetworkToRoute,
    const facebook::fboss::IPv6NetworkToRouteMap& v6NetworkToRoute,
    const facebook::fboss::LabelToRouteMap& labelToRoute,
    facebook::fboss::NextHopIDManager const* nextHopIDManager,
    const facebook::fboss::MySidTable& mySidTable,
    void* cookie,
    bool vrfDeleted) {
  facebook::fboss::RibToSwitchStateUpdater ribToSwitchStateUpdater(
      resolver,
      vrf,
      v4NetworkToRoute,
      v6NetworkToRoute,
      labelToRoute,
      nextHopIDManager,
      mySidTable,
      facebook::fboss::RibToSwitchStateUpdater::kAllActions,
      vrfDeleted);

  auto nextStatePtr =
      static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);

  ribToSwitchStateUpdater(*nextStatePtr);
  auto lastDelta = ribToSwitchStateUpdater.getLastDelta();
  CHECK(lastDelta.has_value());
  return facebook::fboss::StateDelta(lastDelta->oldState(), *nextStatePtr);
}
} // namespace

namespace facebook::fboss {

template <typename TableT>
void DsfStateUpdaterUtil::updateNeighborEntry(
    const TableT& oldTable,
    const TableT& clonedTable) {
  auto skipProgramming = [&](const auto& nbrEntryIter) -> bool {
    // Local neighbor entry on one DSF node is remote neighbor entry on
    // every other DSF node. Thus, for neighbor entry received from other
    // DSF nodes, set isLocal = False before programming it.
    // Also, link local only has significance for Servers directly
    // connected to Interface Node. Thus, skip programming remote link
    // local neighbors.
    if (nbrEntryIter->second->getIP().isLinkLocal()) {
      return true;
    }

    // Only program neighbor entries that are REACHABLE on the DSF node
    // that resolved it.
    if (nbrEntryIter->second->getState() != NeighborState::REACHABLE) {
      return true;
    }

    return false;
  };

  auto updateResolvedTimestamp = [&](const auto& oldTable,
                                     const auto& nbrEntryIter) {
    // If dynamic neighbor entry got added the first time, update
    // the resolved timestamp.
    if (nbrEntryIter->second->getType() ==
            state::NeighborEntryType::DYNAMIC_ENTRY &&
        (!oldTable ||
         (std::as_const(*oldTable).find(nbrEntryIter->second->getID())) ==
             oldTable->cend())) {
      nbrEntryIter->second->setResolvedSince(
          static_cast<int64_t>(std::time(nullptr)));
    } else {
      // Retain the resolved timestamp from the old entry.
      if (std::as_const(*oldTable).find(nbrEntryIter->second->getID()) !=
          oldTable->cend()) {
        nbrEntryIter->second->setResolvedSince(
            oldTable->at(nbrEntryIter->second->getID())->getResolvedSince());
      }
    }
  };

  auto nbrEntryIter = clonedTable->begin();
  while (nbrEntryIter != clonedTable->end()) {
    if (skipProgramming(nbrEntryIter)) {
      XLOG(DBG2) << "Skip programming remote neighbor: "
                 << nbrEntryIter->second->str();
      XLOG(DBG3) << "Skip remote neighbor " << nbrEntryIter->second->getIP()
                 << " on intf " << nbrEntryIter->second->getIntfID()
                 << " (linkLocal="
                 << nbrEntryIter->second->getIP().isLinkLocal() << ", state="
                 << static_cast<int>(nbrEntryIter->second->getState()) << ")";
      nbrEntryIter = clonedTable->erase(nbrEntryIter);
    } else {
      // Entries received from remote are non-Local on current node
      nbrEntryIter->second->setIsLocal(false);
      // Entries received from remote always need to be programmed
      nbrEntryIter->second->setNoHostRoute(false);
      updateResolvedTimestamp(oldTable, nbrEntryIter);
      XLOG(DBG2) << "Program remote neighbor: " << nbrEntryIter->second->str();
      XLOG(DBG3) << "Program remote neighbor " << nbrEntryIter->second->getIP()
                 << " on intf " << nbrEntryIter->second->getIntfID()
                 << " mac=" << nbrEntryIter->second->getMac() << " isNew="
                 << (!oldTable ||
                     std::as_const(*oldTable).find(
                         nbrEntryIter->second->getID()) == oldTable->cend());
      ++nbrEntryIter;
    }
  }
}

std::shared_ptr<SwitchState> DsfStateUpdaterUtil::getUpdatedState(
    const std::shared_ptr<SwitchState>& in,
    const SwitchIdScopeResolver* scopeResolver,
    RoutingInformationBase* rib,
    const std::map<SwitchID, std::shared_ptr<SystemPortMap>>&
        switchId2SystemPorts,
    const std::map<SwitchID, std::shared_ptr<InterfaceMap>>& switchId2Intfs) {
  bool changed{false};
  auto out = in->clone();
  IntfRouteTable remoteIntfRoutesToAdd;
  RouterIDToPrefixes remoteIntfRoutesToDel;

  auto makeRemoteSysPort = [&](const auto& oldNode, const auto& newNode) {
    /*
     * RemoteSystemPorts synced from a remote peer R contain:
     *  o RemoteSystemPorts corresponding to remote recycle ports.
     *    - These are already programmed statically based on DSF node config
     *      and are used to bring up the control plane. Thus, don't overwrite
     *      statically programmed entries.
     *  o RemoteSystemPorts corresponding to remote NIF ports.
     *    - remoteSystemPortType and remoteLivenessStatus is applicable only for
     *      remoteSystemPorts.
     *    - However, these ports are localSystemPorts for peer R, and thus
     *      remoteSystemPortType and remoteLivenessStatus is not set. Set those.
     *    - remoteSystemPortType = DYNAMIC as these are synced dynamically.
     *    - remoteLivenessStatus = LIVE as these are synced from control plane.
     *      The remoteLivenessStatus will be changed to STALE on GR timeout.
     */
    CHECK(!oldNode || oldNode->getID() == newNode->getID());
    if (oldNode && oldNode->getRemoteSystemPortType().has_value() &&
        oldNode->getRemoteSystemPortType().value() ==
            RemoteSystemPortType::STATIC_ENTRY) {
      XLOG(DBG2)
          << "Skip overwriting STATIC remoteSystemPorts: " << " STATIC: "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 oldNode->toThrift())
          << " non-STATIC: "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 newNode->toThrift());
      return oldNode;
    }
    if (newNode && newNode->getScope() == cfg::Scope::LOCAL) {
      XLOG(DBG3)
          << "Ignore remote system port of local type "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 newNode->toThrift());
      return oldNode;
    }

    auto clonedNode = newNode->isPublished() ? newNode->clone() : newNode;
    clonedNode->setRemoteSystemPortType(RemoteSystemPortType::DYNAMIC_ENTRY);
    clonedNode->setRemoteLivenessStatus(LivenessStatus::LIVE);

    return clonedNode;
  };
  auto makeRemoteRif = [&](const auto& oldNode, const auto& newNode) {
    CHECK(!oldNode || oldNode->getID() == newNode->getID());
    if (oldNode && oldNode->getRemoteInterfaceType().has_value() &&
        oldNode->getRemoteInterfaceType().value() ==
            RemoteInterfaceType::STATIC_ENTRY) {
      XLOG(DBG2)
          << "Skip overwriting STATIC remoteInterface: " << " STATIC: "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 oldNode->toThrift())
          << " non-STATIC: "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 newNode->toThrift());
      return oldNode;
    }
    if (newNode && newNode->getScope() == cfg::Scope::LOCAL) {
      XLOG(DBG3)
          << "Ignore remote rif of local type "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 newNode->toThrift());
      return oldNode;
    }

    auto clonedNode = newNode->clone();
    clonedNode->setRemoteInterfaceType(RemoteInterfaceType::DYNAMIC_ENTRY);
    clonedNode->setRemoteLivenessStatus(LivenessStatus::LIVE);

    if (newNode->isPublished()) {
      clonedNode->setArpTable(newNode->getArpTable()->toThrift());
      clonedNode->setNdpTable(newNode->getNdpTable()->toThrift());
    }

    updateNeighborEntry(
        oldNode ? oldNode->getArpTable() : nullptr, clonedNode->getArpTable());
    updateNeighborEntry(
        oldNode ? oldNode->getNdpTable() : nullptr, clonedNode->getNdpTable());

    return clonedNode;
  };

  auto processDelta = [&]<typename MapT>(
                          auto& delta,
                          MapT* mapToUpdate,
                          auto& makeRemote,
                          const HwSwitchMatcher& matcher) {
    DeltaFunctions::forEachChanged(
        delta,
        [&](const auto& oldNode, const auto& newNode) {
          if (*oldNode != *newNode) {
            // Compare contents as we reconstructed
            // map from deserialized FSDB
            // subscriptions. So can't just rely on
            // pointer comparison here.
            auto clonedNode = makeRemote(oldNode, newNode);
            if constexpr (std::is_same_v<MapT, MultiSwitchSystemPortMap>) {
              mapToUpdate->updateNode(clonedNode, matcher);
            } else {
              // Only process routes if addresses or routerID changed.
              // Neighbor-only changes don't affect routes.
              if (oldNode->getAddresses()->toThrift() !=
                      newNode->getAddresses()->toThrift() ||
                  oldNode->getRouterID() != newNode->getRouterID()) {
                processRemoteInterfaceRoutes(
                    oldNode,
                    out,
                    false /* add */,
                    remoteIntfRoutesToAdd,
                    remoteIntfRoutesToDel);
                processRemoteInterfaceRoutes(
                    newNode,
                    out,
                    true /* add */,
                    remoteIntfRoutesToAdd,
                    remoteIntfRoutesToDel);
              }
              mapToUpdate->updateNode(clonedNode, matcher);
            }
            changed = true;
          }
        },
        [&](const auto& newNode) {
          auto clonedNode =
              makeRemote(std::decay_t<decltype(newNode)>{nullptr}, newNode);
          if (!clonedNode) {
            return;
          }
          if constexpr (std::is_same_v<MapT, MultiSwitchSystemPortMap>) {
            mapToUpdate->addNode(clonedNode, matcher);
          } else {
            processRemoteInterfaceRoutes(
                clonedNode,
                out,
                true /* add */,
                remoteIntfRoutesToAdd,
                remoteIntfRoutesToDel);
            mapToUpdate->addNode(clonedNode, matcher);
          }
          changed = true;
        },
        [&](const auto& rmNode) {
          if (rmNode->getScope() == cfg::Scope::LOCAL) {
            XLOG(DBG3) << "Skip removing LOCAL:: "
                       << static_cast<int>(rmNode->getID()) << " "
                       << rmNode->getName();

            return;
          }
          if (rmNode->isStatic()) {
            XLOG(DBG3) << "Skip removing STATIC:: "
                       << static_cast<int>(rmNode->getID()) << " "
                       << rmNode->getName();
            return;
          }

          if constexpr (std::is_same_v<MapT, MultiSwitchInterfaceMap>) {
            processRemoteInterfaceRoutes(
                rmNode,
                out,
                false /* add */,
                remoteIntfRoutesToAdd,
                remoteIntfRoutesToDel);
          }
          mapToUpdate->removeNode(rmNode);
          changed = true;
        });
  };

  const auto localVoqSwitchMatcher = scopeResolver->scope(cfg::SwitchType::VOQ);

  for (const auto& [nodeSwitchId, newSysPorts] : switchId2SystemPorts) {
    XLOG(DBG2) << "SwitchId: " << static_cast<int64_t>(nodeSwitchId)
               << " updated # of sys ports: " << newSysPorts->size();

    auto origSysPorts = in->getSystemPorts(nodeSwitchId);
    ThriftMapDelta<SystemPortMap> delta(origSysPorts.get(), newSysPorts.get());
    auto remoteSysPorts = out->getRemoteSystemPorts()->modify(&out);
    processDelta(
        delta, remoteSysPorts, makeRemoteSysPort, localVoqSwitchMatcher);
  }

  for (const auto& [nodeSwitchId, newRifs] : switchId2Intfs) {
    XLOG(DBG2) << "SwitchId: " << static_cast<int64_t>(nodeSwitchId)
               << " updated # of intfs: " << newRifs->size();

    auto origRifs = in->getInterfaces(nodeSwitchId);
    InterfaceMapDelta delta(origRifs.get(), newRifs.get());
    auto remoteRifs = out->getRemoteInterfaces()->modify(&out);
    processDelta(delta, remoteRifs, makeRemoteRif, localVoqSwitchMatcher);
  }

  if (!remoteIntfRoutesToAdd.empty() || !remoteIntfRoutesToDel.empty()) {
    rib->updateRemoteInterfaceRoutes(
        scopeResolver,
        remoteIntfRoutesToAdd,
        remoteIntfRoutesToDel,
        &updateFibForRemoteConnectedRoutes,
        static_cast<void*>(&out));
  }

  if (changed) {
    return out;
  }
  return std::shared_ptr<SwitchState>{};
}

} // namespace facebook::fboss
