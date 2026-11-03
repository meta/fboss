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

#include "fboss/agent/hw/sai/fake/FakeManager.h"

#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>

extern "C" {
#include <sai.h>
}
namespace facebook::fboss {

class FakeAclTableGroupMember {
 public:
  explicit FakeAclTableGroupMember(
      sai_object_id_t tableGroupId,
      sai_object_id_t tableId,
      sai_uint32_t priority)
      : tableGroupId(tableGroupId), tableId(tableId), priority(priority) {}

  sai_object_id_t tableGroupId;
  sai_object_id_t tableId;
  sai_uint32_t priority;

  sai_object_id_t id;
};

class FakeAclTableGroup {
 public:
  explicit FakeAclTableGroup(
      sai_int32_t stage,
      std::vector<sai_int32_t> bindPointTypeList,
      sai_int32_t type)
      : stage(stage), bindPointTypeList(bindPointTypeList), type(type) {}

  sai_object_id_t id;

  sai_int32_t stage;
  std::vector<sai_int32_t> bindPointTypeList;
  sai_int32_t type;

  FakeManager<sai_object_id_t, FakeAclTableGroupMember>& fm() {
    return fm_;
  }
  const FakeManager<sai_object_id_t, FakeAclTableGroupMember>& fm() const {
    return fm_;
  }

 private:
  FakeManager<sai_object_id_t, FakeAclTableGroupMember> fm_;
};

using FakeAclTableGroupManager =
    FakeManagerWithMembers<FakeAclTableGroup, FakeAclTableGroupMember>;

class FakeAclEntry {
 public:
  explicit FakeAclEntry(sai_object_id_t tableId) : tableId(tableId) {}

  sai_object_id_t tableId;
  sai_uint32_t priority;
  bool enabled{true};

  bool fieldSrcIpV6Enable{false};
  folly::IPAddressV6 fieldSrcIpV6Data;
  folly::IPAddressV6 fieldSrcIpV6Mask;

  bool fieldDstIpV6Enable{false};
  folly::IPAddressV6 fieldDstIpV6Data;
  folly::IPAddressV6 fieldDstIpV6Mask;

  bool fieldDstIpV6Word3Enable{false};
  folly::IPAddressV6 fieldDstIpV6Word3Data;
  folly::IPAddressV6 fieldDstIpV6Word3Mask;

  bool fieldDstIpV6Word2Enable{false};
  folly::IPAddressV6 fieldDstIpV6Word2Data;
  folly::IPAddressV6 fieldDstIpV6Word2Mask;

  bool fieldSrcIpV4Enable{false};
  folly::IPAddressV4 fieldSrcIpV4Data;
  folly::IPAddressV4 fieldSrcIpV4Mask;

  bool fieldDstIpV4Enable{false};
  folly::IPAddressV4 fieldDstIpV4Data;
  folly::IPAddressV4 fieldDstIpV4Mask;

  /*
   * Mask is not needed for sai_object_id_t Acl Entry field.
   * Thus, there is no oid field in sai_acl_field_data_mask_t.
   * oid is u64, but unfortunately, u64 was not added to
   * sai_acl_field_data_mask_t till SAI 1.6, so use u32.
   * This will be ignored by the implementation anyway.
   */

  bool fieldSrcPortEnable{false};
  sai_object_id_t fieldSrcPortData;
  sai_uint32_t fieldSrcPortMask;

  bool fieldOutPortEnable{false};
  sai_object_id_t fieldOutPortData;
  sai_uint32_t fieldOutPortMask;

  bool fieldRouteDestinationEnable{false};
  sai_object_id_t fieldRouteDestinationData{};
  sai_uint32_t fieldRouteDestinationMask{};

  bool fieldL4SrcPortEnable{false};
  sai_uint16_t fieldL4SrcPortData;
  sai_uint16_t fieldL4SrcPortMask;

  bool fieldL4DstPortEnable{false};
  sai_uint16_t fieldL4DstPortData;
  sai_uint16_t fieldL4DstPortMask;

  bool fieldIpProtocolEnable{false};
  sai_uint8_t fieldIpProtocolData;
  sai_uint8_t fieldIpProtocolMask;

  bool fieldTcpFlagsEnable{false};
  sai_uint8_t fieldTcpFlagsData;
  sai_uint8_t fieldTcpFlagsMask;

  bool fieldIpFragEnable{false};
  sai_uint8_t fieldIpFragData;
  sai_uint8_t fieldIpFragMask;

  bool fieldIcmpV4TypeEnable{false};
  sai_uint8_t fieldIcmpV4TypeData;
  sai_uint8_t fieldIcmpV4TypeMask;

  bool fieldIcmpV4CodeEnable{false};
  sai_uint8_t fieldIcmpV4CodeData;
  sai_uint8_t fieldIcmpV4CodeMask;

  bool fieldIcmpV6TypeEnable{false};
  sai_uint8_t fieldIcmpV6TypeData;
  sai_uint8_t fieldIcmpV6TypeMask;

  bool fieldIcmpV6CodeEnable{false};
  sai_uint8_t fieldIcmpV6CodeData;
  sai_uint8_t fieldIcmpV6CodeMask;

  bool fieldDscpEnable{false};
  sai_uint8_t fieldDscpData;
  sai_uint8_t fieldDscpMask;

  bool fieldTcEnable{false};
  sai_uint8_t fieldTcData{};
  sai_uint8_t fieldTcMask{};

  bool fieldDstMacEnable{false};
  folly::MacAddress fieldDstMacData;
  folly::MacAddress fieldDstMacMask;

  bool fieldIpTypeEnable{false};
  sai_uint8_t fieldIpTypeData;
  sai_uint8_t fieldIpTypeMask;

  bool fieldTtlEnable{false};
  sai_uint8_t fieldTtlData;
  sai_uint8_t fieldTtlMask;

  bool fieldMplsLabel0TtlEnable{false};
  sai_uint8_t fieldMplsLabel0TtlData{};
  sai_uint8_t fieldMplsLabel0TtlMask{};

  bool fieldFdbDstUserMetaEnable{false};
  sai_uint32_t fieldFdbDstUserMetaData;
  sai_uint32_t fieldFdbDstUserMetaMask;

  bool fieldRouteDstUserMetaEnable{false};
  sai_uint32_t fieldRouteDstUserMetaData;
  sai_uint32_t fieldRouteDstUserMetaMask;

  bool fieldNeighborDstUserMetaEnable{false};
  sai_uint32_t fieldNeighborDstUserMetaData;
  sai_uint32_t fieldNeighborDstUserMetaMask;

  bool fieldPortUserMetaEnable{false};
  sai_uint32_t fieldPortUserMetaData{};
  sai_uint32_t fieldPortUserMetaMask{};

  bool fieldEtherTypeEnable{false};
  sai_uint16_t fieldEtherTypeData;
  sai_uint16_t fieldEtherTypeMask;

  bool fieldOuterVlanIdEnable{false};
  sai_uint16_t fieldOuterVlanIdData;
  sai_uint16_t fieldOuterVlanIdMask;

  bool fieldAclRangeTypeEnable{false};
  std::vector<sai_object_id_t> fieldAclRangeTypeData;

  bool fieldBthOpcodeEnable{false};
  sai_uint8_t fieldBthOpcodeData;
  sai_uint8_t fieldBthOpcodeMask;

  bool fieldIpv6NextHeaderEnable{false};
  sai_uint8_t fieldIpv6NextHeaderData;
  sai_uint8_t fieldIpv6NextHeaderMask;

  bool userDefinedFieldGroupMinEnable{false};
  std::vector<sai_uint8_t> userDefinedFieldGroupMinData;
  std::vector<sai_uint8_t> userDefinedFieldGroupMinMask;

  bool userDefinedFieldGroupMin1Enable{false};
  std::vector<sai_uint8_t> userDefinedFieldGroupMin1Data;
  std::vector<sai_uint8_t> userDefinedFieldGroupMin1Mask;

  bool userDefinedFieldGroupMin2Enable{false};
  std::vector<sai_uint8_t> userDefinedFieldGroupMin2Data;
  std::vector<sai_uint8_t> userDefinedFieldGroupMin2Mask;

  bool userDefinedFieldGroupMin3Enable{false};
  std::vector<sai_uint8_t> userDefinedFieldGroupMin3Data;
  std::vector<sai_uint8_t> userDefinedFieldGroupMin3Mask;

  bool userDefinedFieldGroupMin4Enable{false};
  std::vector<sai_uint8_t> userDefinedFieldGroupMin4Data;
  std::vector<sai_uint8_t> userDefinedFieldGroupMin4Mask;

  bool actionPacketActionEnable{false};
  sai_uint32_t actionPacketActionData;

  bool actionRedirectEnable{false};
  sai_object_id_t actionRedirectData{};

  bool actionCounterEnable{false};
  sai_object_id_t actionCounterData;

  bool actionSetTCEnable{false};
  sai_uint8_t actionSetTCData;

  bool actionSetDSCPEnable{false};
  sai_uint8_t actionSetDSCPData;

  bool actionMirrorIngressEnable{false};
  std::vector<sai_object_id_t> actionMirrorIngressData;

  bool actionMirrorEgressEnable{false};
  std::vector<sai_object_id_t> actionMirrorEgressData;

  bool actionMacsecFlowEnable{false};
  sai_object_id_t actionMacsecFlowData;

  bool actionSetUserTrapEnable{false};
  sai_object_id_t actionSetUserTrapData;

  bool actionSetArsObjectEnable{false};
  sai_object_id_t actionSetArsObjectData{};

  bool actionDisableArsForwarding{false};

  bool actionSetEcmpHashAlgorithmEnable{false};
  sai_object_id_t actionSetEcmpHashAlgorithmData{};

  bool actionL3SwitchCancelEnable{false};
  bool actionL3SwitchCancelData{false};

  sai_object_id_t id;
};

class FakeAclTable {
 public:
  FakeAclTable(
      sai_int32_t stage,
      std::vector<sai_int32_t> bindPointTypeList,
      std::vector<sai_int32_t> actionTypeList,
      bool fieldSrcIpV6,
      bool fieldDstIpV6,
      bool fieldDstIpV6Word3,
      bool fieldDstIpV6Word2,
      bool fieldSrcIpV4,
      bool fieldDstIpV4,
      bool fieldL4SrcPort,
      bool fieldL4DstPort,
      bool fieldIpProtocol,
      bool fieldTcpFlags,
      bool fieldSrcPort,
      bool fieldOutPort,
      bool fieldIpFrag,
      bool fieldIcmpV4Type,
      bool fieldIcmpV4Code,
      bool fieldIcmpV6Type,
      bool fieldIcmpV6Code,
      bool fieldDscp,
      bool fieldTc,
      bool fieldDstMac,
      bool fieldIpType,
      bool fieldTtl,
      bool fieldMplsLabel0Ttl,
      bool fieldFdbDstUserMeta,
      bool fieldRouteDstUserMeta,
      bool fieldNeighborDstUserMeta,
      bool fieldPortUserMeta,
      bool fieldEthertype,
      bool fieldOuterVlanId,
      std::vector<sai_int32_t> fieldAclRangeType,
      bool fieldBthOpcode,
      bool fieldIpv6NextHeader,
      sai_object_id_t userDefinedFieldGroupMin,
      sai_object_id_t userDefinedFieldGroupMin1,
      sai_object_id_t userDefinedFieldGroupMin2,
      sai_object_id_t userDefinedFieldGroupMin3,
      sai_object_id_t userDefinedFieldGroupMin4)
      : stage(stage),
        bindPointTypeList(bindPointTypeList),
        actionTypeList(actionTypeList),
        fieldSrcIpV6(fieldSrcIpV6),
        fieldDstIpV6(fieldDstIpV6),
        fieldDstIpV6Word3(fieldDstIpV6Word3),
        fieldDstIpV6Word2(fieldDstIpV6Word2),
        fieldSrcIpV4(fieldSrcIpV4),
        fieldDstIpV4(fieldDstIpV4),
        fieldL4SrcPort(fieldL4SrcPort),
        fieldL4DstPort(fieldL4DstPort),
        fieldIpProtocol(fieldIpProtocol),
        fieldTcpFlags(fieldTcpFlags),
        fieldSrcPort(fieldSrcPort),
        fieldOutPort(fieldOutPort),
        fieldIpFrag(fieldIpFrag),
        fieldIcmpV4Type(fieldIcmpV4Type),
        fieldIcmpV4Code(fieldIcmpV4Code),
        fieldIcmpV6Type(fieldIcmpV6Type),
        fieldIcmpV6Code(fieldIcmpV6Code),
        fieldDscp(fieldDscp),
        fieldTc(fieldTc),
        fieldDstMac(fieldDstMac),
        fieldIpType(fieldIpType),
        fieldTtl(fieldTtl),
        fieldMplsLabel0Ttl(fieldMplsLabel0Ttl),
        fieldFdbDstUserMeta(fieldFdbDstUserMeta),
        fieldRouteDstUserMeta(fieldRouteDstUserMeta),
        fieldNeighborDstUserMeta(fieldNeighborDstUserMeta),
        fieldPortUserMeta(fieldPortUserMeta),
        fieldEthertype(fieldEthertype),
        fieldOuterVlanId(fieldOuterVlanId),
        fieldAclRangeType(fieldAclRangeType),
        fieldBthOpcode(fieldBthOpcode),
        fieldIpv6NextHeader(fieldIpv6NextHeader),
        userDefinedFieldGroupMin(userDefinedFieldGroupMin),
        userDefinedFieldGroupMin1(userDefinedFieldGroupMin1),
        userDefinedFieldGroupMin2(userDefinedFieldGroupMin2),
        userDefinedFieldGroupMin3(userDefinedFieldGroupMin3),
        userDefinedFieldGroupMin4(userDefinedFieldGroupMin4) {}

  bool entryFieldSupported(const sai_attribute_t& attr) const;
  static sai_acl_api_t* kApi();

  sai_object_id_t id;

  sai_int32_t stage;
  std::vector<sai_int32_t> bindPointTypeList;
  std::vector<sai_int32_t> actionTypeList;
  bool fieldSrcIpV6;
  bool fieldDstIpV6;
  bool fieldDstIpV6Word3;
  bool fieldDstIpV6Word2;
  bool fieldSrcIpV4;
  bool fieldDstIpV4;
  bool fieldL4SrcPort;
  bool fieldL4DstPort;
  bool fieldIpProtocol;
  bool fieldTcpFlags;
  bool fieldSrcPort;
  bool fieldOutPort;
  bool fieldIpFrag;
  bool fieldIcmpV4Type;
  bool fieldIcmpV4Code;
  bool fieldIcmpV6Type;
  bool fieldIcmpV6Code;
  bool fieldDscp;
  bool fieldTc;
  bool fieldDstMac;
  bool fieldIpType;
  bool fieldTtl;
  bool fieldMplsLabel0Ttl;
  bool fieldFdbDstUserMeta;
  bool fieldRouteDstUserMeta;
  bool fieldNeighborDstUserMeta;
  bool fieldPortUserMeta;
  bool fieldEthertype;
  bool fieldOuterVlanId;
  std::vector<sai_int32_t> fieldAclRangeType;
  bool fieldBthOpcode;
  bool fieldIpv6NextHeader;
  sai_object_id_t userDefinedFieldGroupMin;
  sai_object_id_t userDefinedFieldGroupMin1;
  sai_object_id_t userDefinedFieldGroupMin2;
  sai_object_id_t userDefinedFieldGroupMin3;
  sai_object_id_t userDefinedFieldGroupMin4;
  bool fieldRouteDestination{false};
};

class FakeAclCounter {
 public:
  explicit FakeAclCounter(sai_object_id_t tableId) : tableId(tableId) {}

  sai_object_id_t tableId;

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
  std::array<char, 32> label;
#endif

  bool enablePacketCount{false};
  bool enableByteCount{false};

  sai_uint64_t counterPackets{0};
  sai_uint64_t counterBytes{0};

  sai_object_id_t id;
};

class FakeAclRange {
 public:
  FakeAclRange(sai_int32_t type, sai_u32_range_t limit)
      : type(type), limit(limit) {}

  sai_int32_t type;
  sai_u32_range_t limit;
  sai_object_id_t id;
};

using FakeAclEntryManager = FakeManager<sai_object_id_t, FakeAclEntry>;
using FakeAclCounterManager = FakeManager<sai_object_id_t, FakeAclCounter>;
using FakeAclRangeManager = FakeManager<sai_object_id_t, FakeAclRange>;
using FakeAclTableManager = FakeManager<sai_object_id_t, FakeAclTable>;

void populate_acl_api(sai_acl_api_t** acl_api);

} // namespace facebook::fboss
