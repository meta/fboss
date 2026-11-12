/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/NextHopApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _NextHopMap{
    SAI_ATTR_MAP(IpNextHop, Type),
    SAI_ATTR_MAP(IpNextHop, RouterInterfaceId),
    SAI_ATTR_MAP(IpNextHop, Ip),
    SAI_ATTR_MAP(MplsNextHop, LabelStack),
    SAI_ATTR_MAP(MplsNextHop, OutsegType),
    SAI_ATTR_MAP(IpNextHop, DisableTtlDecrement),
    SAI_ATTR_MAP(TunnelEncapNextHop, TunnelId),
    SAI_ATTR_MAP(TunnelEncapNextHop, OverlayDstMac),
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    SAI_ATTR_MAP(Srv6SidlistNextHop, Srv6SidlistId),
#endif
};
} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(next_hop, SAI_OBJECT_TYPE_NEXT_HOP, nextHop);
WRAP_REMOVE_FUNC(next_hop, SAI_OBJECT_TYPE_NEXT_HOP, nextHop);
WRAP_SET_ATTR_FUNC(next_hop, SAI_OBJECT_TYPE_NEXT_HOP, nextHop);
WRAP_GET_ATTR_FUNC(next_hop, SAI_OBJECT_TYPE_NEXT_HOP, nextHop);

sai_next_hop_api_t* wrappedNextHopApi() {
  static sai_next_hop_api_t nextHopWrappers;

  nextHopWrappers.create_next_hop = &wrap_create_next_hop;
  nextHopWrappers.remove_next_hop = &wrap_remove_next_hop;
  nextHopWrappers.set_next_hop_attribute = &wrap_set_next_hop_attribute;
  nextHopWrappers.get_next_hop_attribute = &wrap_get_next_hop_attribute;

  return &nextHopWrappers;
}

SET_SAI_ATTRIBUTES(NextHop)

} // namespace facebook::fboss
