/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

int getLocalPortNumVoqs(cfg::PortType portType, cfg::Scope portScope) {
  if (!isDualStage3Q2QMode()) {
    return 8;
  }
  if (FLAGS_dual_stage_edsw_3q_2q) {
    return 3;
  }
  CHECK(FLAGS_dual_stage_rdsw_3q_2q);
  if (portType == cfg::PortType::MANAGEMENT_PORT ||
      (portType == cfg::PortType::RECYCLE_PORT &&
       portScope == cfg::Scope::GLOBAL)) {
    return 2;
  }
  return 3;
}

int getRemotePortNumVoqs(
    HwAsic::InterfaceNodeRole intfRole,
    cfg::PortType portType) {
  if (intfRole == HwAsic::InterfaceNodeRole::DUAL_STAGE_EDGE_NODE) {
    CHECK(isDualStage3Q2QMode());
    return 3;
  }
  if (isDualStage3Q2QMode()) {
    if (portType == cfg::PortType::MANAGEMENT_PORT ||
        portType == cfg::PortType::RECYCLE_PORT) {
      return 2;
    }
    return 3;
  }
  return 8;
}

void processRemoteInterfaceRoutes(
    const std::shared_ptr<Interface>& remoteIntf,
    const std::shared_ptr<SwitchState>& state,
    bool add,
    IntfRouteTable& remoteIntfRoutesToAdd,
    RouterIDToPrefixes& remoteIntfRoutesToDel) {
  // On the same box, interfaces of both npu0 and npu1 will be added
  // as local interface. They will be used for route resolution as directly
  // connected routes. Hence no need to process as remote interfaces.
  if (state->getInterfaces()->getNodeIf(remoteIntf->getID())) {
    XLOG(DBG3) << "processRemoteInterfaceRoutes: skip local interface "
               << remoteIntf->getID() << " (" << remoteIntf->getName() << ")";
    return;
  }

  const auto remoteVrf = remoteIntf->getRemoteIntfRouterID();
  for (const auto& [addr, mask] : std::as_const(*remoteIntf->getAddresses())) {
    const auto ipAddr = folly::IPAddress(addr);
    // Skip link-local addresses in directly-connected routes
    if (ipAddr.isV6() && ipAddr.isLinkLocal()) {
      continue;
    }
    auto prefix = folly::IPAddress::createNetwork(
        folly::to<std::string>(addr, "/", static_cast<int>(mask->cref())));
    auto prefixStr =
        folly::to<std::string>(prefix.first.str(), "/", prefix.second);
    IntfAddress intfAddr = std::make_pair(remoteIntf->getID(), ipAddr);
    if (add) {
      // Cancel any pending delete for this prefix to prevent the delete
      // from removing the route after the add (RIB processes adds then
      // deletes). Always add to toAdd so addOrReplaceRouteImpl re-points
      // the route to the correct interface.
      auto& toDel = remoteIntfRoutesToDel[remoteVrf];
      auto iter = std::find_if(
          toDel.begin(), toDel.end(), [&prefix](const auto& entry) {
            return entry.first == prefix;
          });
      if (iter != toDel.end()) {
        toDel.erase(iter);
        XLOG(DBG3) << "processRemoteInterfaceRoutes: add route " << prefixStr
                   << " for intf " << remoteIntf->getID() << " ("
                   << remoteIntf->getName()
                   << "), cancelled pending delete for same prefix";
      } else {
        XLOG(DBG3) << "processRemoteInterfaceRoutes: add route " << prefixStr
                   << " for intf " << remoteIntf->getID() << " ("
                   << remoteIntf->getName() << ")";
      }
      remoteIntfRoutesToAdd[remoteVrf].emplace(prefix, intfAddr);
    } else {
      // Check if route already in remoteIntfRoutesToAdd
      auto& toAdd = remoteIntfRoutesToAdd[remoteVrf];
      auto iter = toAdd.find(prefix);
      if (iter != toAdd.end() && iter->second.first == remoteIntf->getID()) {
        // Same interface: cancel the add (route stays unchanged)
        XLOG(DBG3) << "processRemoteInterfaceRoutes: cancel pending add for "
                   << prefixStr << " on same intf " << remoteIntf->getID()
                   << " (" << remoteIntf->getName() << ")";
        toAdd.erase(iter);
      } else if (iter == toAdd.end()) {
        // No pending add: schedule a delete with interface ID so the RIB
        // can verify ownership before actually removing the route.
        XLOG(DBG3) << "processRemoteInterfaceRoutes: delete route " << prefixStr
                   << " for intf " << remoteIntf->getID() << " ("
                   << remoteIntf->getName() << ")";
        remoteIntfRoutesToDel[remoteVrf].emplace_back(
            prefix, remoteIntf->getID());
      } else {
        // Pending add for different interface — the add will replace
        // the route via addOrReplaceRouteImpl, so no delete needed
        XLOG(DBG3) << "processRemoteInterfaceRoutes: skip delete for "
                   << prefixStr << " from intf " << remoteIntf->getID() << " ("
                   << remoteIntf->getName()
                   << "), pending add exists for different intf "
                   << iter->second.first;
      }
    }
  }
}

// Check if the remote device connected to the local device over
// a port is a VoQ switch.
bool isConnectedToVoqSwitch(
    const cfg::SwitchConfig* config,
    const SwitchID& remoteSwitchId) {
  auto dsfNodeIt = config->dsfNodes()->find(remoteSwitchId);
  CHECK(dsfNodeIt != config->dsfNodes()->end())
      << "DSF node missing for switchId: " << remoteSwitchId;
  const auto& dsfNode = dsfNodeIt->second;
  return *dsfNode.type() == cfg::DsfNodeType::INTERFACE_NODE;
}
} // namespace facebook::fboss
