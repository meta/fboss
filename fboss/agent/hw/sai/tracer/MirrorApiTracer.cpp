/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/MirrorApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/MirrorApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _MirrorSessionMap{
    SAI_ATTR_MAP(EnhancedRemoteMirror, Type),
    SAI_ATTR_MAP(EnhancedRemoteMirror, Policer),
    SAI_ATTR_MAP(EnhancedRemoteMirror, MonitorPort),
    SAI_ATTR_MAP(EnhancedRemoteMirror, TruncateSize),
    SAI_ATTR_MAP(EnhancedRemoteMirror, Tos),
    SAI_ATTR_MAP(EnhancedRemoteMirror, ErspanEncapsulationType),
    SAI_ATTR_MAP(EnhancedRemoteMirror, GreProtocolType),
    SAI_ATTR_MAP(EnhancedRemoteMirror, Ttl),
    SAI_ATTR_MAP(EnhancedRemoteMirror, SrcIpAddress),
    SAI_ATTR_MAP(EnhancedRemoteMirror, DstIpAddress),
    SAI_ATTR_MAP(EnhancedRemoteMirror, SrcMacAddress),
    SAI_ATTR_MAP(EnhancedRemoteMirror, DstMacAddress),
    SAI_ATTR_MAP(EnhancedRemoteMirror, IpHeaderVersion),
    SAI_ATTR_MAP(EnhancedRemoteMirror, SampleRate),
    SAI_ATTR_MAP(SflowMirror, UdpSrcPort),
    SAI_ATTR_MAP(SflowMirror, UdpDstPort),
};

void handleExtensionAttributes() {
  SAI_EXT_ATTR_MAP_2(MirrorSession, LocalMirror, TcBufferLimit)
  SAI_EXT_ATTR_MAP_2(MirrorSession, EnhancedRemoteMirror, TcBufferLimit)
  SAI_EXT_ATTR_MAP_2(MirrorSession, SflowMirror, TcBufferLimit)
}

} // namespace
namespace facebook::fboss {

WRAP_CREATE_FUNC(mirror_session, SAI_OBJECT_TYPE_MIRROR_SESSION, mirror);
WRAP_REMOVE_FUNC(mirror_session, SAI_OBJECT_TYPE_MIRROR_SESSION, mirror);
WRAP_SET_ATTR_FUNC(mirror_session, SAI_OBJECT_TYPE_MIRROR_SESSION, mirror);
WRAP_GET_ATTR_FUNC(mirror_session, SAI_OBJECT_TYPE_MIRROR_SESSION, mirror);

sai_mirror_api_t* wrappedMirrorApi() {
  handleExtensionAttributes();
  static sai_mirror_api_t mirrorWrappers;
  mirrorWrappers.create_mirror_session = &wrap_create_mirror_session;
  mirrorWrappers.remove_mirror_session = &wrap_remove_mirror_session;
  mirrorWrappers.set_mirror_session_attribute =
      &wrap_set_mirror_session_attribute;
  mirrorWrappers.get_mirror_session_attribute =
      &wrap_get_mirror_session_attribute;
  return &mirrorWrappers;
}

SET_SAI_ATTRIBUTES(MirrorSession)

} // namespace facebook::fboss
