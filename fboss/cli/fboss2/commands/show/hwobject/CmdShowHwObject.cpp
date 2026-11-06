/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowHwObject.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/CmdClientUtils.h"

namespace facebook::fboss {

std::string queryHwObjects(
    const HostInfo& hostInfo,
    const CmdShowHwObjectTraits::ObjectArgType& queriedHwObjectTypes,
    bool cached) {
  std::string hwObjectInfo;

  if (utils::isMultiSwitchEnabled(hostInfo)) {
    auto hwAgentQueryFn =
        [&hwObjectInfo, queriedHwObjectTypes, cached](
            apache::thrift::Client<facebook::fboss::FbossHwCtrl>& client) {
          std::string hwAgentObjectInfo;
          client.sync_listHwObjects(
              hwAgentObjectInfo, queriedHwObjectTypes.data(), cached);
          hwObjectInfo += hwAgentObjectInfo;
        };
    utils::runOnAllHwAgents(hostInfo, hwAgentQueryFn);
  } else {
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    client->sync_listHwObjects(
        hwObjectInfo, queriedHwObjectTypes.data(), cached);
  }

  return hwObjectInfo;
}

CmdShowHwObject::RetType CmdShowHwObject::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedHwObjectTypes) {
  return queryHwObjects(hostInfo, queriedHwObjectTypes, true /*cached*/);
}

void CmdShowHwObject::printOutput(
    const RetType& hwObjectInfo,
    std::ostream& out) {
  out << hwObjectInfo << std::endl;
}

std::string_view CmdShowHwObjectTraits::description() {
  return "Displays the raw SAI hardware-object state for a given object type. Requires an object-type argument, one of: PORT, LAG, VIRTUAL_ROUTER, NEXT_HOP, NEXT_HOP_GROUP, ROUTER_INTERFACE, CPU_TRAP, HASH, MIRROR, QOS_MAP, QUEUE, SCHEDULER, L2_ENTRY, NEIGHBOR_ENTRY, ROUTE_ENTRY, VLAN, BRIDGE, BUFFER, ACL, DEBUG_COUNTER, TELEMETRY, LABEL_ENTRY, MACSEC, SAI_MANAGED_OBJECTS, IPTUNNEL, SYSTEM_PORT, FIRMWARE, SRV6, NEXT_HOP_GROUP_MEMBER, SAMPLE_PACKET, MY_MAC (the HwObjectType enum). The output format is specific to each object type. Use it for low-level SAI/ASIC debugging.";
}

CmdShowHwObject::RetType CmdShowHwObject::sampleModel() {
  return R"(Object type: port
PortSaiId(4294967646): (HwLaneList: [509, 510, 511, 512], Speed: 400000, AdminState: true, FecMode: 1, PortLoopbackMode: 0, MediaType: 4, GlobalFlowControlMode: 0, PortVlanId: 2128, Mtu: 9412, PrbsConfig: 0, PtpMode: 1, FdrEnable: true, ...)
PortSaiId(4294967645): (HwLaneList: [505, 506, 507, 508], Speed: 400000, AdminState: true, FecMode: 1, PortLoopbackMode: 0, MediaType: 4, GlobalFlowControlMode: 0, PortVlanId: 2127, Mtu: 9412, PrbsConfig: 0, PtpMode: 1, FdrEnable: true, ...)
)";
}

// Explicit template instantiation
template void CmdHandler<CmdShowHwObject, CmdShowHwObjectTraits>::run();

} // namespace facebook::fboss
