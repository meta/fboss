// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/tracer/TunnelApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/TunnelApi.h"
#include "fboss/agent/hw/sai/api/TunnelMapApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _TunnelMap{
    SAI_ATTR_MAP(IpInIpTunnel, Type),
    SAI_ATTR_MAP(IpInIpTunnel, UnderlayInterface),
    SAI_ATTR_MAP(IpInIpTunnel, OverlayInterface),
    SAI_ATTR_MAP(IpInIpTunnel, DecapTtlMode),
    SAI_ATTR_MAP(IpInIpTunnel, DecapDscpMode),
    SAI_ATTR_MAP(IpInIpTunnel, DecapEcnMode),
    SAI_ATTR_MAP(IpInIpTunnel, EncapSrcIp),
    SAI_ATTR_MAP(IpInIpTunnel, EncapTtlMode),
    SAI_ATTR_MAP(IpInIpTunnel, EncapDscpMode),
    SAI_ATTR_MAP(IpInIpTunnel, EncapEcnMode),
    SAI_ATTR_MAP(VxlanTunnel, EncapMappers),
    SAI_ATTR_MAP(VxlanTunnel, DecapMappers),
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    // SRv6 tunnels share the SAI_TUNNEL_ATTR_* ids with IP-in-IP but are
    // distinct AttributeName types (default getters), so they need their own
    // replayer entries.
    SAI_ATTR_MAP(Srv6Tunnel, UnderlayInterface),
    SAI_ATTR_MAP(Srv6Tunnel, EncapSrcIp),
    SAI_ATTR_MAP(Srv6Tunnel, EncapTtlMode),
    SAI_ATTR_MAP(Srv6Tunnel, EncapDscpMode),
    SAI_ATTR_MAP(Srv6Tunnel, EncapEcnMode),
    SAI_ATTR_MAP(Srv6Tunnel, DecapTtlMode),
    SAI_ATTR_MAP(Srv6Tunnel, DecapDscpMode),
    SAI_ATTR_MAP(Srv6Tunnel, DecapEcnMode),
    SAI_ATTR_MAP(Srv6Tunnel, DecapQosDscpToTcMap),
#endif
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TunnelTermMap{
    SAI_ATTR_MAP(P2MPTunnelTerm, Type),
    SAI_ATTR_MAP(P2MPTunnelTerm, VrId),
    SAI_ATTR_MAP(P2MPTunnelTerm, DstIp),
    SAI_ATTR_MAP(P2MPTunnelTerm, DstIpMask),
    SAI_ATTR_MAP(P2MPTunnelTerm, SrcIp),
    SAI_ATTR_MAP(P2MPTunnelTerm, SrcIpMask),
    SAI_ATTR_MAP(P2MPTunnelTerm, TunnelType),
    SAI_ATTR_MAP(P2MPTunnelTerm, ActionTunnelId),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TunnelMapMap{
    SAI_ATTR_MAP(TunnelMap, Type),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TunnelMapEntryMap{
    SAI_ATTR_MAP(VniToVrTunnelMapEntry, TunnelMapType),
    SAI_ATTR_MAP(VniToVrTunnelMapEntry, TunnelMap),
    SAI_ATTR_MAP(VniToVrTunnelMapEntry, VniIdKey),
    SAI_ATTR_MAP(VniToVrTunnelMapEntry, VirtualRouterIdValue),
    SAI_ATTR_MAP(VrToVniTunnelMapEntry, VirtualRouterIdKey),
    SAI_ATTR_MAP(VrToVniTunnelMapEntry, VniIdValue),
};
} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(tunnel, SAI_OBJECT_TYPE_TUNNEL, tunnel);
WRAP_REMOVE_FUNC(tunnel, SAI_OBJECT_TYPE_TUNNEL, tunnel);
WRAP_SET_ATTR_FUNC(tunnel, SAI_OBJECT_TYPE_TUNNEL, tunnel);
WRAP_GET_ATTR_FUNC(tunnel, SAI_OBJECT_TYPE_TUNNEL, tunnel);

WRAP_CREATE_FUNC(
    tunnel_term_table_entry,
    SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY,
    tunnel);
WRAP_REMOVE_FUNC(
    tunnel_term_table_entry,
    SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY,
    tunnel);
WRAP_SET_ATTR_FUNC(
    tunnel_term_table_entry,
    SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY,
    tunnel);
WRAP_GET_ATTR_FUNC(
    tunnel_term_table_entry,
    SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY,
    tunnel);

WRAP_CREATE_FUNC(tunnel_map, SAI_OBJECT_TYPE_TUNNEL_MAP, tunnel);
WRAP_REMOVE_FUNC(tunnel_map, SAI_OBJECT_TYPE_TUNNEL_MAP, tunnel);
WRAP_SET_ATTR_FUNC(tunnel_map, SAI_OBJECT_TYPE_TUNNEL_MAP, tunnel);
WRAP_GET_ATTR_FUNC(tunnel_map, SAI_OBJECT_TYPE_TUNNEL_MAP, tunnel);

WRAP_CREATE_FUNC(tunnel_map_entry, SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, tunnel);
WRAP_REMOVE_FUNC(tunnel_map_entry, SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, tunnel);
WRAP_SET_ATTR_FUNC(tunnel_map_entry, SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, tunnel);
WRAP_GET_ATTR_FUNC(tunnel_map_entry, SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, tunnel);

sai_tunnel_api_t* wrappedTunnelApi() {
  static sai_tunnel_api_t tunnelWrappers;

  tunnelWrappers.create_tunnel = &wrap_create_tunnel;
  tunnelWrappers.remove_tunnel = &wrap_remove_tunnel;
  tunnelWrappers.set_tunnel_attribute = &wrap_set_tunnel_attribute;
  tunnelWrappers.get_tunnel_attribute = &wrap_get_tunnel_attribute;

  tunnelWrappers.create_tunnel_term_table_entry =
      &wrap_create_tunnel_term_table_entry;
  tunnelWrappers.remove_tunnel_term_table_entry =
      &wrap_remove_tunnel_term_table_entry;
  tunnelWrappers.get_tunnel_term_table_entry_attribute =
      &wrap_get_tunnel_term_table_entry_attribute;
  tunnelWrappers.set_tunnel_term_table_entry_attribute =
      &wrap_set_tunnel_term_table_entry_attribute;

  tunnelWrappers.create_tunnel_map = &wrap_create_tunnel_map;
  tunnelWrappers.remove_tunnel_map = &wrap_remove_tunnel_map;
  tunnelWrappers.set_tunnel_map_attribute = &wrap_set_tunnel_map_attribute;
  tunnelWrappers.get_tunnel_map_attribute = &wrap_get_tunnel_map_attribute;

  tunnelWrappers.create_tunnel_map_entry = &wrap_create_tunnel_map_entry;
  tunnelWrappers.remove_tunnel_map_entry = &wrap_remove_tunnel_map_entry;
  tunnelWrappers.set_tunnel_map_entry_attribute =
      &wrap_set_tunnel_map_entry_attribute;
  tunnelWrappers.get_tunnel_map_entry_attribute =
      &wrap_get_tunnel_map_entry_attribute;

  return &tunnelWrappers;
}

SET_SAI_ATTRIBUTES(Tunnel);
SET_SAI_ATTRIBUTES(TunnelTerm);
SET_SAI_ATTRIBUTES(TunnelMap);
SET_SAI_ATTRIBUTES(TunnelMapEntry);

} // namespace facebook::fboss
