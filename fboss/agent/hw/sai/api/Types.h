/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/types.h"

#include "fboss/agent/PortDescriptorTemplate.h"

extern "C" {
#include <sai.h>
}

FBOSS_STRONG_TYPE(sai_object_id_t, AclTableGroupSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, AclTableGroupMemberSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, AclTableSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, AclEntrySaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, AclCounterSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, AclRangeSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, ArsSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, ArsProfileSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, BridgeSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, BridgePortSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, BufferPoolSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, BufferProfileSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, CbfcCreditProfileSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, CounterSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, DebugCounterSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, FirmwareSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, HashSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, HostifTrapGroupSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, HostifTrapSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, HostifUserDefinedTrapSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, IngressPriorityGroupSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, IsolationGroupSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, IsolationGroupMemberSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, LagSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, LagMemberSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, MirrorSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, NextHopSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, NextHopGroupSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, NextHopGroupMemberSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, PortSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, PortSerdesSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, PortConnectorSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, PortLlrProfileSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, MyMacSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, QosMapSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, QueueSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, RouterInterfaceSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, SamplePacketSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, SchedulerSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, SwitchSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, SystemPortSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, UdfSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, UdfGroupSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, UdfMatchSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, VirtualChannelSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, VirtualRouterSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, VlanSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, VlanMemberSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, WredSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TamCollectorSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TamTransportSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TamReportSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TamEventActionSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TamEventAgingGroupSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TamEventSaiId);
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
FBOSS_STRONG_TYPE(sai_object_id_t, TamEventThresholdSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TamSaiId);
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
FBOSS_STRONG_TYPE(sai_object_id_t, Srv6SidListSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TunnelSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TunnelTermSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TunnelMapSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, TunnelMapEntrySaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, MacsecSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, MacsecPortSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, MacsecSASaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, MacsecSCSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, MacsecFlowSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, VendorSwitchSaiId);
FBOSS_STRONG_TYPE(sai_object_id_t, SwitchPipelineSaiId);

// Macsec secure channel identifier (SCI) - 48 bit mac address + 16 bit port ID
FBOSS_STRONG_TYPE(sai_uint64_t, MacsecSecureChannelId);
FBOSS_STRONG_TYPE(sai_uint32_t, MacsecShortSecureChannelId)

namespace facebook::fboss {

using SaiCharArray32 = std::array<char, 32>;

// Converts a sai type alias of T[x] to std::array<T, x>
template <
    typename ArrayT,
    typename MemberT = typename std::remove_extent<ArrayT>::type,
    size_t MemberCount = std::extent<ArrayT>::value>
struct SaiArrayType {
  using type = std::array<MemberT, MemberCount>;
};

using SaiMacsecAuthKey = SaiArrayType<sai_macsec_auth_key_t>::type;
using SaiMacsecSak = SaiArrayType<sai_macsec_sak_t>::type;
using SaiMacsecSalt = SaiArrayType<sai_macsec_salt_t>::type;

template <typename SaiId>
sai_object_id_t* rawSaiId(SaiId* id) {
  return reinterpret_cast<sai_object_id_t*>(id);
}

using SaiPortDescriptor = BasePortDescriptor;
using PortDescriptorSaiId =
    PortDescriptorTemplate<PortSaiId, LagSaiId, SystemPortSaiId>;

using SaiTimeSpec = std::timespec;
SaiTimeSpec fromSaiTimeSpec(sai_timespec_t sai_timespec);
void toSaiTimeSpec(const SaiTimeSpec& spec, sai_timespec_t& sai_timespec);

} // namespace facebook::fboss

namespace std {
template <>
struct hash<facebook::fboss::PortDescriptorSaiId> {
  size_t operator()(const facebook::fboss::PortDescriptorSaiId& key) const;
};
} // namespace std
