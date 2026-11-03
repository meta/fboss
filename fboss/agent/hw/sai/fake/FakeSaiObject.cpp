/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

sai_status_t sai_get_object_count(
    sai_object_id_t /* switch_id */,
    sai_object_type_t object_type,
    uint32_t* count) {
  *count = 0;
  auto fs = facebook::fboss::FakeSai::getInstance();
  switch (object_type) {
    case SAI_OBJECT_TYPE_PORT:
      // All ports excluding CPU port
      *count = fs->portManager.map().size() - 1;
      break;
    case SAI_OBJECT_TYPE_PORT_SERDES:
      *count = fs->portSerdesManager.map().size();
      break;
    case SAI_OBJECT_TYPE_PORT_CONNECTOR:
      *count = fs->portConnectorManager.map().size();
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    case SAI_OBJECT_TYPE_PORT_LLR_PROFILE:
      *count = fs->portLlrProfileManager.map().size();
      break;
#endif
    case SAI_OBJECT_TYPE_VIRTUAL_ROUTER:
      *count = fs->virtualRouteManager.map().size();
      break;
    case SAI_OBJECT_TYPE_NEXT_HOP:
      *count = fs->nextHopManager.map().size();
      break;
    case SAI_OBJECT_TYPE_NEXT_HOP_GROUP:
      *count = fs->nextHopGroupManager.map().size();
      break;
    case SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER: {
      for (const auto& nhg : fs->nextHopGroupManager.map()) {
        *count += nhg.second.fm().map().size();
      }
      break;
    }
    case SAI_OBJECT_TYPE_ROUTER_INTERFACE:
      *count = fs->routeInterfaceManager.map().size();
      break;
    case SAI_OBJECT_TYPE_HASH:
      *count = fs->hashManager.map().size();
      break;
    case SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP:
      *count = fs->hostifTrapGroupManager.map().size();
      break;
    case SAI_OBJECT_TYPE_FDB_ENTRY:
      *count = fs->fdbManager.map().size();
      break;
    case SAI_OBJECT_TYPE_SWITCH:
      *count = fs->switchManager.map().size();
      break;
    case SAI_OBJECT_TYPE_HOSTIF_TRAP:
      *count = fs->hostIfTrapManager.map().size();
      break;
    case SAI_OBJECT_TYPE_HOSTIF_USER_DEFINED_TRAP:
      *count = fs->hostIfUserDefinedTrapManager.map().size();
      break;
    case SAI_OBJECT_TYPE_NEIGHBOR_ENTRY:
      *count = fs->neighborManager.map().size();
      break;
    case SAI_OBJECT_TYPE_ROUTE_ENTRY:
      *count = fs->routeManager.map().size();
      break;
    case SAI_OBJECT_TYPE_VLAN:
      *count = fs->vlanManager.map().size();
      break;
    case SAI_OBJECT_TYPE_VLAN_MEMBER: {
      for (const auto& v : fs->vlanManager.map()) {
        *count += v.second.fm().map().size();
      }
      break;
    }
    case SAI_OBJECT_TYPE_ISOLATION_GROUP:
      *count = fs->isolationGroupManager.map().size();
      break;
    case SAI_OBJECT_TYPE_BRIDGE:
      *count = fs->bridgeManager.map().size();
      break;
    case SAI_OBJECT_TYPE_BUFFER_POOL:
      *count = fs->bufferPoolManager.map().size();
      break;
    case SAI_OBJECT_TYPE_BUFFER_PROFILE:
      *count = fs->bufferProfileManager.map().size();
      break;
    case SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP:
      *count = fs->ingressPriorityGroupManager.map().size();
      break;
    case SAI_OBJECT_TYPE_BRIDGE_PORT: {
      for (const auto& br : fs->bridgeManager.map()) {
        *count += br.second.fm().map().size();
      }
      break;
    }
    case SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER: {
      for (const auto& g : fs->isolationGroupManager.map()) {
        *count += g.second.fm().map().size();
      }
      break;
    }
    case SAI_OBJECT_TYPE_QOS_MAP:
      *count = fs->qosMapManager.map().size();
      break;
    case SAI_OBJECT_TYPE_QUEUE:
      *count = fs->queueManager.map().size();
      break;
    case SAI_OBJECT_TYPE_SAMPLEPACKET:
      *count = fs->samplePacketManager.map().size();
      break;
    case SAI_OBJECT_TYPE_MY_MAC:
      *count = static_cast<uint32_t>(fs->myMacManager.map().size());
      break;
    case SAI_OBJECT_TYPE_SCHEDULER:
      *count = fs->scheduleManager.map().size();
      break;
    case SAI_OBJECT_TYPE_INSEG_ENTRY:
      *count = fs->inSegEntryManager.map().size();
      break;
    case SAI_OBJECT_TYPE_ACL_TABLE_GROUP:
      *count = fs->aclTableGroupManager.map().size();
      break;
    case SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER: {
      for (const auto& aclTableGroupMember : fs->aclTableGroupManager.map()) {
        *count += aclTableGroupMember.second.fm().map().size();
      }
      break;
    }
    case SAI_OBJECT_TYPE_ACL_TABLE:
      *count = fs->aclTableManager.map().size();
      break;
    case SAI_OBJECT_TYPE_ACL_ENTRY: {
      *count = fs->aclEntryManager.map().size();
      break;
    }
    case SAI_OBJECT_TYPE_ACL_COUNTER:
      *count = fs->aclCounterManager.map().size();
      break;
    case SAI_OBJECT_TYPE_ACL_RANGE:
      *count = fs->aclRangeManager.map().size();
      break;
    case SAI_OBJECT_TYPE_ARS:
      *count = fs->arsManager.map().size();
      break;
    case SAI_OBJECT_TYPE_ARS_PROFILE:
      *count = fs->arsProfileManager.map().size();
      break;
    case SAI_OBJECT_TYPE_COUNTER:
      *count = fs->counterManager.map().size();
      break;
    case SAI_OBJECT_TYPE_DEBUG_COUNTER:
      *count = fs->debugCounterManager.map().size();
      break;
    case SAI_OBJECT_TYPE_WRED:
      *count = fs->wredManager.map().size();
      break;
    case SAI_OBJECT_TYPE_VIRTUAL_CHANNEL:
      *count = static_cast<uint32_t>(fs->virtualChannelManager.map().size());
      break;
    case SAI_OBJECT_TYPE_CBFC_CREDIT_PROFILE:
      *count = static_cast<uint32_t>(fs->cbfcCreditProfileManager.map().size());
      break;
    case SAI_OBJECT_TYPE_TAM_COLLECTOR:
      *count = fs->tamCollectorManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TAM_TRANSPORT:
      *count = fs->tamTransportManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TAM_REPORT:
      *count = fs->tamReportManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TAM_EVENT_ACTION:
      *count = fs->tamEventActionManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TAM_EVENT:
      *count = fs->tamEventManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TAM:
      *count = fs->tamManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD:
      *count = static_cast<uint32_t>(fs->tamEventThresholdManager.map().size());
      break;
    case SAI_OBJECT_TYPE_TUNNEL:
      *count = fs->tunnelManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY:
      *count = fs->tunnelTermManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TUNNEL_MAP:
      *count = fs->tunnelMapManager.map().size();
      break;
    case SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY:
      *count = fs->tunnelMapEntryManager.map().size();
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_OBJECT_TYPE_SRV6_SIDLIST:
      *count = static_cast<uint32_t>(fs->srv6SidListManager.map().size());
      break;
    case SAI_OBJECT_TYPE_MY_SID_ENTRY:
      *count = static_cast<uint32_t>(fs->mySidEntryManager.map().size());
      break;
#endif
    case SAI_OBJECT_TYPE_MIRROR_SESSION:
      *count = fs->mirrorManager.map().size();
      break;
    case SAI_OBJECT_TYPE_LAG:
      *count = fs->lagManager.map().size();
      break;
    case SAI_OBJECT_TYPE_LAG_MEMBER: {
      for (const auto& obj : fs->lagManager.map()) {
        *count += obj.second.fm().map().size();
      }
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC: {
      *count = fs->macsecManager.map().size();
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_PORT: {
      *count = fs->macsecPortManager.map().size();
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_SA: {
      *count = fs->macsecSAManager.map().size();
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_SC: {
      *count = fs->macsecSCManager.map().size();
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_FLOW: {
      *count = fs->macsecFlowManager.map().size();
      break;
    }
    case SAI_OBJECT_TYPE_SYSTEM_PORT: {
      *count = fs->systemPortManager.map().size();
      break;
    }
    case SAI_OBJECT_TYPE_UDF:
      *count = fs->udfManager.map().size();
      break;
    case SAI_OBJECT_TYPE_UDF_GROUP:
      *count = fs->udfGroupManager.map().size();
      break;
    case SAI_OBJECT_TYPE_UDF_MATCH:
      *count = fs->udfMatchManager.map().size();
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t sai_get_object_key(
    sai_object_id_t switch_id,
    sai_object_type_t object_type,
    uint32_t* count,
    sai_object_key_t* object_list) {
  auto fs = facebook::fboss::FakeSai::getInstance();
  uint32_t c = 0;
  sai_get_object_count(switch_id, object_type, &c);
  if (c > *count) {
    // TODO(borisb): how should we best handle this overflow?
    // feels a little weird to not signal back.
    return SAI_STATUS_BUFFER_OVERFLOW;
  }
  int i = 0;
  switch (object_type) {
    case SAI_OBJECT_TYPE_PORT: {
      for (const auto& p : fs->portManager.map()) {
        if (fs->cpuPortId == p.second.id) {
          continue;
        }
        object_list[i++].key.object_id = p.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_PORT_SERDES: {
      for (const auto& serdes : fs->portSerdesManager.map()) {
        object_list[i++].key.object_id = serdes.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_PORT_CONNECTOR: {
      for (const auto& conn : fs->portConnectorManager.map()) {
        object_list[i++].key.object_id = conn.second.id;
      }
      break;
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    case SAI_OBJECT_TYPE_PORT_LLR_PROFILE: {
      for (const auto& profile : fs->portLlrProfileManager.map()) {
        object_list[i++].key.object_id = profile.second.id;
      }
      break;
    }
#endif
    case SAI_OBJECT_TYPE_VIRTUAL_ROUTER: {
      for (const auto& vr : fs->virtualRouteManager.map()) {
        object_list[i++].key.object_id = vr.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_NEXT_HOP: {
      for (const auto& nh : fs->nextHopManager.map()) {
        object_list[i++].key.object_id = nh.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_NEXT_HOP_GROUP: {
      for (const auto& nhg : fs->nextHopGroupManager.map()) {
        object_list[i++].key.object_id = nhg.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER: {
      for (const auto& nhg : fs->nextHopGroupManager.map()) {
        for (const auto& nextHopGroupManager : nhg.second.fm().map()) {
          object_list[i++].key.object_id = nextHopGroupManager.second.id;
        }
      }
      break;
    }
    case SAI_OBJECT_TYPE_ROUTER_INTERFACE: {
      for (const auto& rif : fs->routeInterfaceManager.map()) {
        object_list[i++].key.object_id = rif.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP: {
      for (const auto& htg : fs->hostifTrapGroupManager.map()) {
        object_list[i++].key.object_id = htg.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_HASH: {
      *count = fs->hashManager.map().size();
      for (const auto& hash : fs->hashManager.map()) {
        object_list[i++].key.object_id = hash.first;
      }
      break;
    }
    case SAI_OBJECT_TYPE_FDB_ENTRY: {
      for (const auto& fdbe : fs->fdbManager.map()) {
        object_list[i].key.fdb_entry.switch_id = std::get<0>(fdbe.first);
        object_list[i].key.fdb_entry.bv_id = std::get<1>(fdbe.first);
        facebook::fboss::toSaiMacAddress(
            std::get<2>(fdbe.first), object_list[i].key.fdb_entry.mac_address);
        ++i;
      }
      break;
    }
    case SAI_OBJECT_TYPE_SWITCH: {
      for (const auto& sw : fs->switchManager.map()) {
        object_list[i++].key.object_id = sw.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_HOSTIF_TRAP: {
      for (const auto& ht : fs->hostIfTrapManager.map()) {
        object_list[i++].key.object_id = ht.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_NEIGHBOR_ENTRY: {
      for (const auto& nbr : fs->neighborManager.map()) {
        object_list[i].key.neighbor_entry.switch_id = std::get<0>(nbr.first);
        object_list[i].key.neighbor_entry.rif_id = std::get<1>(nbr.first);
        object_list[i].key.neighbor_entry.ip_address =
            facebook::fboss::toSaiIpAddress(std::get<2>(nbr.first));
        ++i;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ROUTE_ENTRY: {
      for (const auto& route : fs->routeManager.map()) {
        object_list[i].key.route_entry.switch_id = std::get<0>(route.first);
        object_list[i].key.route_entry.vr_id = std::get<1>(route.first);
        object_list[i].key.route_entry.destination =
            facebook::fboss::toSaiIpPrefix(std::get<2>(route.first));
        ++i;
      }
      break;
    }
    case SAI_OBJECT_TYPE_VLAN: {
      for (const auto& v : fs->vlanManager.map()) {
        object_list[i++].key.object_id = v.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_VLAN_MEMBER: {
      for (const auto& v : fs->vlanManager.map()) {
        for (const auto& vlanManager : v.second.fm().map()) {
          object_list[i++].key.object_id = vlanManager.second.id;
        }
      }
      break;
    }
    case SAI_OBJECT_TYPE_ISOLATION_GROUP: {
      for (const auto& g : fs->isolationGroupManager.map()) {
        object_list[i++].key.object_id = g.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER: {
      for (const auto& g : fs->isolationGroupManager.map()) {
        for (const auto& member : g.second.fm().map()) {
          object_list[i++].key.object_id = member.second.id;
        }
      }
      break;
    }
    case SAI_OBJECT_TYPE_BRIDGE: {
      for (const auto& b : fs->bridgeManager.map()) {
        object_list[i++].key.object_id = b.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_BRIDGE_PORT: {
      for (const auto& br : fs->bridgeManager.map()) {
        for (const auto& brp : br.second.fm().map()) {
          object_list[i++].key.object_id = brp.second.id;
        }
      }
      break;
    }
    case SAI_OBJECT_TYPE_BUFFER_POOL: {
      for (const auto& b : fs->bufferPoolManager.map()) {
        object_list[i++].key.object_id = b.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP: {
      for (const auto& b : fs->ingressPriorityGroupManager.map()) {
        object_list[i++].key.object_id = b.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_BUFFER_PROFILE: {
      for (const auto& b : fs->bufferProfileManager.map()) {
        object_list[i++].key.object_id = b.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_QOS_MAP: {
      for (const auto& qm : fs->qosMapManager.map()) {
        object_list[i++].key.object_id = qm.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_QUEUE: {
      for (const auto& q : fs->queueManager.map()) {
        object_list[i++].key.object_id = q.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_SAMPLEPACKET: {
      for (const auto& sp : fs->samplePacketManager.map()) {
        object_list[i++].key.object_id = sp.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_MY_MAC: {
      for (const auto& myMac : fs->myMacManager.map()) {
        object_list[i++].key.object_id = myMac.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_SCHEDULER: {
      for (const auto& sc : fs->scheduleManager.map()) {
        object_list[i++].key.object_id = sc.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_INSEG_ENTRY: {
      for (const auto& entry : fs->inSegEntryManager.map()) {
        object_list[i++].key.inseg_entry = entry.first.sai_inseg_entry;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ACL_TABLE_GROUP: {
      for (const auto& aclTableGroup : fs->aclTableGroupManager.map()) {
        object_list[i++].key.object_id = aclTableGroup.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER: {
      for (const auto& aclTableGroup : fs->aclTableGroupManager.map()) {
        for (const auto& aclTableGroupMember :
             aclTableGroup.second.fm().map()) {
          object_list[i++].key.object_id = aclTableGroupMember.second.id;
        }
      }
      break;
    }
    case SAI_OBJECT_TYPE_ACL_TABLE: {
      for (const auto& aclTable : fs->aclTableManager.map()) {
        object_list[i++].key.object_id = aclTable.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ACL_ENTRY: {
      for (const auto& aclEntry : fs->aclEntryManager.map()) {
        object_list[i++].key.object_id = aclEntry.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ACL_COUNTER: {
      for (const auto& aclCounter : fs->aclCounterManager.map()) {
        object_list[i++].key.object_id = aclCounter.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ACL_RANGE: {
      for (const auto& aclRange : fs->aclRangeManager.map()) {
        object_list[i++].key.object_id = aclRange.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ARS: {
      for (const auto& ars : fs->arsManager.map()) {
        object_list[i++].key.object_id = ars.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_ARS_PROFILE: {
      for (const auto& arsProfile : fs->arsProfileManager.map()) {
        object_list[i++].key.object_id = arsProfile.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_COUNTER: {
      *count = fs->counterManager.map().size();
      for (const auto& counter : fs->counterManager.map()) {
        object_list[i++].key.object_id = counter.first;
      }
      break;
    }
    case SAI_OBJECT_TYPE_DEBUG_COUNTER: {
      *count = fs->debugCounterManager.map().size();
      for (const auto& debugCounter : fs->debugCounterManager.map()) {
        object_list[i++].key.object_id = debugCounter.first;
      }
      break;
    }
    case SAI_OBJECT_TYPE_WRED: {
      for (const auto& wred : fs->wredManager.map()) {
        object_list[i++].key.object_id = wred.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_VIRTUAL_CHANNEL: {
      for (const auto& virtualChannel : fs->virtualChannelManager.map()) {
        object_list[i++].key.object_id = virtualChannel.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_CBFC_CREDIT_PROFILE: {
      for (const auto& profile : fs->cbfcCreditProfileManager.map()) {
        object_list[i++].key.object_id = profile.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TAM_COLLECTOR: {
      for (const auto& ob : fs->tamCollectorManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TAM_TRANSPORT: {
      for (const auto& ob : fs->tamTransportManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TAM_REPORT: {
      for (const auto& ob : fs->tamReportManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TAM_EVENT_ACTION: {
      for (const auto& ob : fs->tamEventActionManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TAM_EVENT: {
      for (const auto& ob : fs->tamEventManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TAM: {
      for (const auto& ob : fs->tamManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD: {
      for (const auto& ob : fs->tamEventThresholdManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TUNNEL: {
      for (const auto& ob : fs->tunnelManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY: {
      for (const auto& ob : fs->tunnelTermManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TUNNEL_MAP: {
      for (const auto& ob : fs->tunnelMapManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY: {
      for (const auto& ob : fs->tunnelMapEntryManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_OBJECT_TYPE_SRV6_SIDLIST: {
      for (const auto& ob : fs->srv6SidListManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_MY_SID_ENTRY: {
      for (const auto& entry : fs->mySidEntryManager.map()) {
        object_list[i++].key.my_sid_entry = entry.first.sai_my_sid_entry;
      }
      break;
    }
#endif
    case SAI_OBJECT_TYPE_MIRROR_SESSION: {
      for (const auto& ob : fs->mirrorManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_LAG: {
      for (const auto& ob : fs->lagManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_LAG_MEMBER: {
      for (const auto& ob : fs->lagManager.map()) {
        for (const auto& member : ob.second.fm().map()) {
          object_list[i++].key.object_id = member.second.id;
        }
      }
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC: {
      for (const auto& ob : fs->macsecManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_PORT: {
      for (const auto& ob : fs->macsecPortManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_SA: {
      for (const auto& ob : fs->macsecSAManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_SC: {
      for (const auto& ob : fs->macsecSCManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_MACSEC_FLOW: {
      for (const auto& ob : fs->macsecFlowManager.map()) {
        object_list[i++].key.object_id = ob.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_SYSTEM_PORT: {
      for (const auto& s : fs->systemPortManager.map()) {
        object_list[i++].key.object_id = s.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_UDF: {
      for (const auto& udf : fs->udfManager.map()) {
        object_list[i++].key.object_id = udf.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_UDF_GROUP: {
      for (const auto& udfGroup : fs->udfGroupManager.map()) {
        object_list[i++].key.object_id = udfGroup.second.id;
      }
      break;
    }
    case SAI_OBJECT_TYPE_UDF_MATCH: {
      for (const auto& udfMatch : fs->udfMatchManager.map()) {
        object_list[i++].key.object_id = udfMatch.second.id;
      }
      break;
    }
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}
