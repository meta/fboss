/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiPort.h"
#include "fboss/agent/hw/sai/api/fake/saifakeextensions.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <optional>

using facebook::fboss::FakePort;
using facebook::fboss::FakeSai;

sai_status_t create_port_fn(
    sai_object_id_t* port_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<bool> adminState;
  std::vector<uint32_t> lanes;
  std::optional<sai_uint32_t> staticModuleId;
  std::optional<sai_uint32_t> speed;
  std::optional<sai_port_fec_mode_t> fecMode;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  std::optional<bool> useExtendedFec;
  std::optional<sai_port_fec_mode_extended_t> extendedFecMode;
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  std::optional<sai_port_loopback_mode_t> loopbackMode;
#endif
  std::optional<sai_port_internal_loopback_mode_t> internalLoopbackMode;
  std::optional<bool> fabricIsolate;
  std::optional<sai_port_flow_control_mode_t> flowControlMode;
  std::optional<sai_port_media_type_t> mediaType;
  std::optional<sai_vlan_id_t> vlanId;
  std::vector<uint32_t> preemphasis;
  std::vector<sai_object_id_t> ingressMirrorList;
  std::vector<sai_object_id_t> egressMirrorList;
  sai_object_id_t ingressSamplePacket{SAI_NULL_OBJECT_ID};
  sai_object_id_t egressSamplePacket{SAI_NULL_OBJECT_ID};
  std::vector<sai_object_id_t> ingressSampleMirrorList;
  std::vector<sai_object_id_t> egressSampleMirrorList;
  sai_uint32_t mtu{1514};
  sai_uint32_t metadata{0};
  sai_object_id_t qosDscpToTcMap{SAI_NULL_OBJECT_ID};
  sai_object_id_t qosTcToQueueMap{SAI_NULL_OBJECT_ID};
  bool disableTtlDecrement{false};
  sai_port_interface_type_t interface_type{SAI_PORT_INTERFACE_TYPE_NONE};
  bool txEnable{true};
  std::vector<sai_object_id_t> tamObjectList;
  std::optional<uint32_t> prbsPolynomial;
  std::optional<int32_t> prbsConfig;
  std::optional<sai_object_id_t> ingressAcl;
  std::optional<sai_object_id_t> isolationGroup;
  std::optional<sai_object_id_t> ingressMacsecAcl;
  std::optional<sai_object_id_t> egressMacsecAcl;
  std::optional<uint16_t> systemPortId;
  std::optional<sai_port_ptp_mode_t> ptpMode;
  std::optional<sai_port_priority_flow_control_mode_t> priorityFlowControlMode;
  std::optional<sai_uint8_t> priorityFlowControl;
  std::optional<sai_uint8_t> priorityFlowControlRx;
  std::optional<sai_uint8_t> priorityFlowControlTx;
  std::vector<sai_object_id_t> ingressPriorityGroupList;
  std::optional<sai_uint32_t> numberOfIngressPriorityGroups;
  std::optional<sai_object_id_t> qosTcToPriorityGroupMap;
  std::optional<sai_object_id_t> qosTcToVcMap;
  std::optional<sai_uint32_t> cbfcSenderCreditLimit;
  std::optional<sai_uint16_t> cbfcSenderCreditSize;
  std::optional<sai_int16_t> cbfcSenderPacketOverhead;
  std::optional<sai_object_id_t> qosQueueToVcMap;
  std::optional<sai_object_id_t> qosPfcPriorityToQueueMap;
  std::optional<sai_object_id_t> qosPfcPriorityToPriorityGroupMap;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
  std::optional<sai_uint32_t> interFrameGap;
#endif
  std::optional<bool> linkTrainingEnable;
  std::optional<bool> fdrEnable;
  std::optional<bool> rxLaneSquelchEnable;
  std::vector<sai_map_t> pfcTcDldInterval;
  std::vector<sai_map_t> pfcTcDlrInterval;
  std::optional<sai_latch_status_t> portCrcErrDetect;
  std::optional<bool> ars_enable;
  std::optional<sai_uint32_t> ars_port_load_scaling_factor;
  std::optional<sai_uint32_t> ars_port_load_past_weight;
  std::optional<sai_uint32_t> ars_port_load_future_weight;
  std::optional<sai_object_id_t> fabricSystemPort;

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_PORT_ATTR_ADMIN_STATE:
        adminState = attr_list[i].value.booldata;
        break;
      case SAI_PORT_ATTR_HW_LANE_LIST: {
        for (int j = 0; j < attr_list[i].value.u32list.count; ++j) {
          lanes.push_back(attr_list[i].value.u32list.list[j]);
        }
      } break;
      case SAI_PORT_ATTR_STATIC_MODULE_ID:
        staticModuleId = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_SPEED:
        speed = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_FEC_MODE:
        fecMode = static_cast<sai_port_fec_mode_t>(attr_list[i].value.u32);
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      case SAI_PORT_ATTR_USE_EXTENDED_FEC:
        useExtendedFec = attr_list[i].value.booldata;
        break;
      case SAI_PORT_ATTR_FEC_MODE_EXTENDED:
        extendedFecMode =
            static_cast<sai_port_fec_mode_extended_t>(attr_list[i].value.u32);
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
      case SAI_PORT_ATTR_FABRIC_ISOLATE:
        fabricIsolate = attr_list[i].value.booldata;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      case SAI_PORT_ATTR_LOOPBACK_MODE:
        loopbackMode =
            static_cast<sai_port_loopback_mode_t>(attr_list[i].value.u32);
        break;
#endif
      case SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE:
        internalLoopbackMode = static_cast<sai_port_internal_loopback_mode_t>(
            attr_list[i].value.u32);
        break;
      case SAI_PORT_ATTR_MEDIA_TYPE:
        mediaType = static_cast<sai_port_media_type_t>(attr_list[i].value.s32);
        break;
      case SAI_PORT_ATTR_GLOBAL_FLOW_CONTROL_MODE:
        flowControlMode =
            static_cast<sai_port_flow_control_mode_t>(attr_list[i].value.s32);
        break;
      case SAI_PORT_ATTR_PORT_VLAN_ID:
        vlanId = attr_list[i].value.u16;
        break;
      case SAI_PORT_ATTR_SERDES_PREEMPHASIS: {
        for (int j = 0; j < attr_list[i].value.u32list.count; ++j) {
          preemphasis.push_back(attr_list[i].value.u32list.list[j]);
        }
      } break;
      case SAI_PORT_ATTR_MTU:
        mtu = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_META_DATA:
        metadata = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_QOS_DSCP_TO_TC_MAP:
        qosDscpToTcMap = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_QOS_TC_TO_QUEUE_MAP:
        qosTcToQueueMap = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_DISABLE_DECREMENT_TTL:
        disableTtlDecrement = attr_list[i].value.booldata;
        break;
      case SAI_PORT_ATTR_INTERFACE_TYPE:
        interface_type =
            static_cast<sai_port_interface_type_t>(attr_list[i].value.s32);
        break;
      case SAI_PORT_ATTR_PKT_TX_ENABLE:
        txEnable = attr_list[i].value.booldata;
        break;
      case SAI_PORT_ATTR_TAM_OBJECT:
        for (int j = 0; j < attr_list[i].value.objlist.count; ++j) {
          tamObjectList.push_back(attr_list[i].value.objlist.list[j]);
        }
        break;
      case SAI_PORT_ATTR_INGRESS_MIRROR_SESSION: {
        for (int j = 0; j < attr_list[i].value.objlist.count; ++j) {
          ingressMirrorList.push_back(attr_list[i].value.objlist.list[j]);
        }
      } break;
      case SAI_PORT_ATTR_EGRESS_MIRROR_SESSION: {
        for (int j = 0; j < attr_list[i].value.objlist.count; ++j) {
          egressMirrorList.push_back(attr_list[i].value.objlist.list[j]);
        }
      } break;
      case SAI_PORT_ATTR_INGRESS_SAMPLEPACKET_ENABLE:
        ingressSamplePacket = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_EGRESS_SAMPLEPACKET_ENABLE:
        egressSamplePacket = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_INGRESS_SAMPLE_MIRROR_SESSION: {
        for (int j = 0; j < attr_list[i].value.objlist.count; ++j) {
          ingressSampleMirrorList.push_back(attr_list[i].value.objlist.list[j]);
        }
      } break;
      case SAI_PORT_ATTR_EGRESS_SAMPLE_MIRROR_SESSION: {
        for (int j = 0; j < attr_list[i].value.objlist.count; ++j) {
          egressSampleMirrorList.push_back(attr_list[i].value.objlist.list[j]);
        }
      } break;
      case SAI_PORT_ATTR_PRBS_POLYNOMIAL:
        prbsPolynomial = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_PRBS_CONFIG:
        prbsConfig = attr_list[i].value.s32;
        break;
      case SAI_PORT_ATTR_INGRESS_ACL:
        ingressAcl = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_ISOLATION_GROUP:
        isolationGroup = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_INGRESS_MACSEC_ACL:
        ingressMacsecAcl = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_EGRESS_MACSEC_ACL:
        egressMacsecAcl = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_EXT_FAKE_SYSTEM_PORT_ID:
        systemPortId = attr_list[i].value.u16;
        break;
      case SAI_PORT_ATTR_PTP_MODE:
        ptpMode = static_cast<sai_port_ptp_mode_t>(attr_list[i].value.s32);
        break;
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_MODE:
        priorityFlowControlMode =
            static_cast<sai_port_priority_flow_control_mode_t>(
                attr_list[i].value.u32);
        break;
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL:
        priorityFlowControl = attr_list[i].value.u8;
        break;
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_RX:
        priorityFlowControlRx = attr_list[i].value.u8;
        break;
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_TX:
        priorityFlowControlTx = attr_list[i].value.u8;
        break;
      case SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST: {
        for (int j = 0; j < attr_list[i].value.objlist.count; ++j) {
          ingressPriorityGroupList.push_back(
              attr_list[i].value.objlist.list[j]);
        }
      } break;
      case SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS:
        numberOfIngressPriorityGroups = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_QOS_TC_TO_PRIORITY_GROUP_MAP:
        qosTcToPriorityGroupMap = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_QOS_TC_TO_VC_MAP:
        qosTcToVcMap = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_CBFC_SENDER_CREDIT_LIMIT:
        cbfcSenderCreditLimit = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_CBFC_SENDER_CREDIT_SIZE:
        cbfcSenderCreditSize = attr_list[i].value.u16;
        break;
      case SAI_PORT_ATTR_CBFC_SENDER_PACKET_OVERHEAD:
        cbfcSenderPacketOverhead = attr_list[i].value.s16;
        break;
      case SAI_PORT_ATTR_QOS_QUEUE_TO_VC_MAP:
        qosQueueToVcMap = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_QUEUE_MAP:
        qosPfcPriorityToQueueMap = attr_list[i].value.oid;
        break;
      case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_PRIORITY_GROUP_MAP:
        qosPfcPriorityToPriorityGroupMap = attr_list[i].value.oid;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      case SAI_PORT_ATTR_IPG:
        interFrameGap = attr_list[i].value.u32;
        break;
#endif
      case SAI_PORT_ATTR_LINK_TRAINING_ENABLE:
        linkTrainingEnable = attr_list[i].value.booldata;
        break;
      case SAI_PORT_ATTR_FDR_ENABLE:
        fdrEnable = attr_list[i].value.booldata;
        break;
      case SAI_PORT_ATTR_RX_LANE_SQUELCH_ENABLE:
        rxLaneSquelchEnable = attr_list[i].value.booldata;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
      case SAI_PORT_ATTR_CRC_ERROR_TOKEN_DETECT:
        portCrcErrDetect = attr_list[i].value.latchstatus;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      case SAI_PORT_ATTR_PFC_TC_DLD_INTERVAL:
        for (int j = 0; j < attr_list[i].value.maplist.count; ++j) {
          pfcTcDldInterval.push_back(attr_list[i].value.maplist.list[j]);
        }
        break;
      case SAI_PORT_ATTR_PFC_TC_DLR_INTERVAL:
        for (int j = 0; j < attr_list[i].value.maplist.count; ++j) {
          pfcTcDlrInterval.push_back(attr_list[i].value.maplist.list[j]);
        }
        break;
#endif
      case SAI_PORT_ATTR_ARS_ENABLE:
        ars_enable = attr_list[i].value.booldata;
        break;
      case SAI_PORT_ATTR_ARS_PORT_LOAD_SCALING_FACTOR:
        ars_port_load_scaling_factor = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_ARS_PORT_LOAD_PAST_WEIGHT:
        ars_port_load_past_weight = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_ARS_PORT_LOAD_FUTURE_WEIGHT:
        ars_port_load_future_weight = attr_list[i].value.u32;
        break;
      case SAI_PORT_ATTR_FABRIC_SYSTEM_PORT:
        fabricSystemPort = attr_list[i].value.oid;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (lanes.empty() || !speed) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *port_id = fs->portManager.create(lanes, speed.value());
  auto& port = fs->portManager.get(*port_id);
  if (adminState) {
    port.adminState = adminState.value();
  }
  if (fecMode.has_value()) {
    port.fecMode = fecMode.value();
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  if (useExtendedFec.has_value()) {
    port.useExtendedFec = useExtendedFec.value();
  }
  if (extendedFecMode.has_value()) {
    port.extendedFecMode = extendedFecMode.value();
  }
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  if (loopbackMode.has_value()) {
    port.loopbackMode = loopbackMode.value();
  }
#endif
  if (internalLoopbackMode.has_value()) {
    port.internalLoopbackMode = internalLoopbackMode.value();
  }
  if (fabricIsolate) {
    port.fabricIsolate = fabricIsolate.value();
  }
  if (vlanId.has_value()) {
    port.vlanId = vlanId.value();
  }
  if (mediaType.has_value()) {
    port.mediaType = mediaType.value();
  }
  if (vlanId.has_value()) {
    port.vlanId = vlanId.value();
  }
  if (preemphasis.size()) {
    port.preemphasis = preemphasis;
  }
  if (staticModuleId.has_value()) {
    port.staticModuleId = staticModuleId.value();
  }
  if (ingressMirrorList.size()) {
    port.ingressMirrorList = ingressMirrorList;
  }
  if (egressMirrorList.size()) {
    port.egressMirrorList = egressMirrorList;
  }
  if (ingressSampleMirrorList.size()) {
    port.ingressSampleMirrorList = ingressSampleMirrorList;
  }
  if (egressSampleMirrorList.size()) {
    port.egressSampleMirrorList = egressSampleMirrorList;
  }
  if (isolationGroup.has_value()) {
    port.isolationGroup = isolationGroup.value();
  }
  if (ingressAcl.has_value()) {
    port.ingressAcl = ingressAcl.value();
  }
  if (ingressMacsecAcl.has_value()) {
    port.ingressMacsecAcl = ingressMacsecAcl.value();
  }
  if (egressMacsecAcl.has_value()) {
    port.egressMacsecAcl = egressMacsecAcl.value();
  }
  if (systemPortId.has_value()) {
    port.systemPortId = systemPortId.value();
  }
  if (ptpMode.has_value()) {
    port.ptpMode = ptpMode.value();
  }
  port.mtu = mtu;
  port.metadata = metadata;
  port.qosDscpToTcMap = qosDscpToTcMap;
  port.qosTcToQueueMap = qosTcToQueueMap;
  port.ingressSamplePacket = ingressSamplePacket;
  port.egressSamplePacket = egressSamplePacket;
  port.disableTtlDecrement = disableTtlDecrement;
  port.txEnable = txEnable;
  port.tamObjectList = tamObjectList;
  // TODO: Use number of queues by querying SAI_SWITCH_ATTR_NUMBER_OF_QUEUES
  for (uint8_t queueId = 0; queueId < 8; queueId++) {
    auto saiQueueId = fs->queueManager.create(
        SAI_QUEUE_TYPE_UNICAST, *port_id, queueId, *port_id);
    port.queueIdList.push_back(saiQueueId);
  }
  // Real SAI auto-creates a fixed set of ingress priority groups for each port
  // at port-create time and exposes them via
  // SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST /
  // SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS. The agent never creates
  // these objects itself; it only reads them back (see
  // SaiPortManager::getIngressPriorityGroupSaiIds) and attaches buffer profiles
  // to them when programming PFC (SaiPortManager::changePfcBuffers). Emulate
  // that here so PFC buffer programming has IPGs to operate on instead of
  // dereferencing a null handle. IPGs are owned-by-adapter, mirroring the queue
  // auto-creation above.
  if (ingressPriorityGroupList.empty()) {
    constexpr uint8_t kNumIngressPriorityGroups = 8;
    for (uint8_t pgIndex = 0; pgIndex < kNumIngressPriorityGroups; ++pgIndex) {
      auto saiIpgId = fs->ingressPriorityGroupManager.create(
          *port_id, pgIndex, std::nullopt /* bufferProfile */);
      port.ingressPriorityGroupList.push_back(saiIpgId);
    }
    port.numberOfIngressPriorityGroups = port.ingressPriorityGroupList.size();
  }
  port.interface_type = interface_type;
  if (priorityFlowControlMode.has_value()) {
    port.priorityFlowControlMode = priorityFlowControlMode.value();
  }
  if (priorityFlowControl.has_value()) {
    port.priorityFlowControl = priorityFlowControl.value();
  }
  if (priorityFlowControlRx.has_value()) {
    port.priorityFlowControlRx = priorityFlowControlRx.value();
  }
  if (priorityFlowControlTx.has_value()) {
    port.priorityFlowControlTx = priorityFlowControlTx.value();
  }
  if (ingressPriorityGroupList.size()) {
    port.ingressPriorityGroupList = ingressPriorityGroupList;
  }
  if (numberOfIngressPriorityGroups.has_value()) {
    port.numberOfIngressPriorityGroups = numberOfIngressPriorityGroups.value();
  }
  if (qosTcToPriorityGroupMap.has_value()) {
    port.qosTcToPriorityGroupMap = qosTcToPriorityGroupMap.value();
  }
  if (qosTcToVcMap.has_value()) {
    port.qosTcToVcMap = qosTcToVcMap.value();
  }
  if (cbfcSenderCreditLimit.has_value()) {
    port.cbfcSenderCreditLimit = cbfcSenderCreditLimit.value();
  }
  if (cbfcSenderCreditSize.has_value()) {
    port.cbfcSenderCreditSize = cbfcSenderCreditSize.value();
  }
  if (cbfcSenderPacketOverhead.has_value()) {
    port.cbfcSenderPacketOverhead = cbfcSenderPacketOverhead.value();
  }
  if (qosQueueToVcMap.has_value()) {
    port.qosQueueToVcMap = qosQueueToVcMap.value();
  }
  if (qosPfcPriorityToQueueMap.has_value()) {
    port.qosPfcPriorityToQueueMap = qosPfcPriorityToQueueMap.value();
  }
  if (qosPfcPriorityToPriorityGroupMap.has_value()) {
    port.qosPfcPriorityToPriorityGroupMap =
        qosPfcPriorityToPriorityGroupMap.value();
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
  if (interFrameGap.has_value()) {
    port.interFrameGap = interFrameGap.value();
  }
#endif
  if (linkTrainingEnable.has_value()) {
    port.linkTrainingEnable = linkTrainingEnable.value();
  }
  if (fdrEnable.has_value()) {
    port.fdrEnable = fdrEnable.value();
  }
  if (rxLaneSquelchEnable.has_value()) {
    port.rxLaneSquelchEnable = rxLaneSquelchEnable.value();
  }
  if (pfcTcDldInterval.size()) {
    port.pfcTcDldInterval = pfcTcDldInterval;
  }
  if (pfcTcDlrInterval.size()) {
    port.pfcTcDlrInterval = pfcTcDlrInterval;
  }
  if (portCrcErrDetect.has_value()) {
    port.portCrcErrDetect = portCrcErrDetect.value();
  }
  if (ars_enable.has_value()) {
    port.ars_enable = ars_enable.value();
  }
  if (ars_port_load_scaling_factor.has_value()) {
    port.ars_port_load_scaling_factor = ars_port_load_scaling_factor.value();
  }
  if (ars_port_load_past_weight.has_value()) {
    port.ars_port_load_past_weight = ars_port_load_past_weight.value();
  }
  if (ars_port_load_future_weight.has_value()) {
    port.ars_port_load_future_weight = ars_port_load_future_weight.value();
  }
  if (fabricSystemPort.has_value()) {
    port.fabricSystemPort = fabricSystemPort.value();
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_port_fn(sai_object_id_t port_id) {
  if (FakeSai::getInstance()->getCpuPort() == port_id) {
    // ignore removing CPU port
    return SAI_STATUS_SUCCESS;
  }
  auto fs = FakeSai::getInstance();
  auto& port = fs->portManager.get(port_id);
  for (auto saiQueueId : port.queueIdList) {
    fs->queueManager.remove(saiQueueId);
  }
  fs->portManager.remove(port_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_port_attribute_fn(
    sai_object_id_t port_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& port = fs->portManager.get(port_id);
  sai_status_t res = SAI_STATUS_SUCCESS;
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  // Native BCM SDK 6.5.36 refuses to clear an LLR mode, or to attach or detach
  // a PORT_LLR_PROFILE, while the port is administratively enabled (Broadcom
  // CS00012478409). Setting a mode on an enabled port is permitted.
  if (port.adminState) {
    bool clearingMode = (attr->id == SAI_PORT_ATTR_LLR_MODE_LOCAL ||
                         attr->id == SAI_PORT_ATTR_LLR_MODE_REMOTE) &&
        !attr->value.booldata;
    bool rebindingProfile = attr->id == SAI_PORT_ATTR_LLR_PROFILE &&
        attr->value.oid != port.llrProfile;
    if (clearingMode || rebindingProfile) {
      return SAI_STATUS_OBJECT_IN_USE;
    }
  }
#endif
  switch (attr->id) {
    case SAI_PORT_ATTR_ADMIN_STATE:
      port.adminState = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_HW_LANE_LIST: {
      auto& lanes = port.lanes;
      lanes.clear();
      for (int j = 0; j < attr->value.u32list.count; ++j) {
        lanes.push_back(attr->value.u32list.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_STATIC_MODULE_ID:
      port.staticModuleId = attr->value.u32;
      break;
    case SAI_PORT_ATTR_SPEED:
      port.speed = attr->value.u32;
      break;
    case SAI_PORT_ATTR_FEC_MODE:
      port.fecMode = static_cast<sai_port_fec_mode_t>(attr->value.s32);
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    case SAI_PORT_ATTR_USE_EXTENDED_FEC:
      port.useExtendedFec = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_FEC_MODE_EXTENDED:
      port.extendedFecMode =
          static_cast<sai_port_fec_mode_extended_t>(attr->value.u32);
      break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_PORT_ATTR_LOOPBACK_MODE:
      port.loopbackMode =
          static_cast<sai_port_loopback_mode_t>(attr->value.s32);
      break;
#endif
    case SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE:
      port.internalLoopbackMode =
          static_cast<sai_port_internal_loopback_mode_t>(attr->value.s32);
      break;
    case SAI_PORT_ATTR_MEDIA_TYPE:
      port.mediaType = static_cast<sai_port_media_type_t>(attr->value.s32);
      break;
    case SAI_PORT_ATTR_GLOBAL_FLOW_CONTROL_MODE:
      port.globalFlowControlMode =
          static_cast<sai_port_flow_control_mode_t>(attr->value.s32);
      break;
    case SAI_PORT_ATTR_PORT_VLAN_ID:
      port.vlanId = static_cast<sai_vlan_id_t>(attr->value.u16);
      break;
    case SAI_PORT_ATTR_SERDES_PREEMPHASIS: {
      auto& preemphasis = port.preemphasis;
      preemphasis.clear();
      for (int j = 0; j < attr->value.u32list.count; ++j) {
        preemphasis.push_back(attr->value.u32list.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_MTU:
      port.mtu = attr->value.u32;
      break;
    case SAI_PORT_ATTR_META_DATA:
      port.metadata = attr->value.u32;
      break;
    case SAI_PORT_ATTR_QOS_DSCP_TO_TC_MAP:
      port.qosDscpToTcMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_QOS_DOT1P_TO_TC_MAP:
      port.qosDot1pToTcMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_QOS_TC_AND_COLOR_TO_DOT1P_MAP:
      port.qosTcAndColorToDot1pMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_QOS_TC_TO_QUEUE_MAP:
      port.qosTcToQueueMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_QOS_EGRESS_BUFFER_PROFILE_LIST: {
      auto& egressBufferProfileIdList = port.egressBufferProfileIdList;
      egressBufferProfileIdList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        egressBufferProfileIdList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST: {
      auto& ingressBufferProfileIdList = port.ingressBufferProfileIdList;
      ingressBufferProfileIdList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        ingressBufferProfileIdList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_DISABLE_DECREMENT_TTL:
      port.disableTtlDecrement = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_INTERFACE_TYPE:
      port.interface_type =
          static_cast<sai_port_interface_type_t>(attr->value.s32);
      break;
    case SAI_PORT_ATTR_PKT_TX_ENABLE:
      port.txEnable = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_TAM_OBJECT: {
      auto& tamObjectList = port.tamObjectList;
      tamObjectList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        tamObjectList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_INGRESS_MIRROR_SESSION: {
      auto& ingressMirrorList = port.ingressMirrorList;
      ingressMirrorList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        ingressMirrorList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_EGRESS_MIRROR_SESSION: {
      auto& egressMirrorList = port.egressMirrorList;
      egressMirrorList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        egressMirrorList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_INGRESS_SAMPLEPACKET_ENABLE:
      port.ingressSamplePacket = attr->value.oid;
      break;
    case SAI_PORT_ATTR_EGRESS_SAMPLEPACKET_ENABLE:
      port.egressSamplePacket = attr->value.oid;
      break;
    case SAI_PORT_ATTR_INGRESS_SAMPLE_MIRROR_SESSION: {
      auto& ingressSampleMirrorList = port.ingressSampleMirrorList;
      ingressSampleMirrorList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        ingressSampleMirrorList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_EGRESS_SAMPLE_MIRROR_SESSION: {
      auto& egressSampleMirrorList = port.egressSampleMirrorList;
      egressSampleMirrorList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        egressSampleMirrorList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_PRBS_POLYNOMIAL:
      port.prbsPolynomial = attr->value.u32;
      break;
    case SAI_PORT_ATTR_PRBS_CONFIG:
      port.prbsConfig = attr->value.s32;
      break;
    case SAI_PORT_ATTR_ISOLATION_GROUP:
      port.isolationGroup = attr->value.oid;
      break;
    case SAI_PORT_ATTR_INGRESS_ACL:
      port.ingressAcl = attr->value.oid;
      break;
    case SAI_PORT_ATTR_INGRESS_MACSEC_ACL:
      port.ingressMacsecAcl = attr->value.oid;
      break;
    case SAI_PORT_ATTR_EGRESS_MACSEC_ACL:
      port.egressMacsecAcl = attr->value.oid;
      break;
    case SAI_PORT_ATTR_EXT_FAKE_SYSTEM_PORT_ID:
      port.systemPortId = attr->value.u16;
      break;
    case SAI_PORT_ATTR_PTP_MODE:
      port.ptpMode = static_cast<sai_port_ptp_mode_t>(attr->value.s32);
      break;
    case SAI_PORT_ATTR_EYE_VALUES: {
      port.portEyeValues.count =
          static_cast<sai_port_eye_values_list_t>(attr->value.porteyevalues)
              .count;
      auto& portEyeValList = port.portEyeValues.list;
      auto portEyeValVector = std::vector<sai_port_lane_eye_values_t>();
      portEyeValVector.resize(port.portEyeValues.count);
      portEyeValList = portEyeValVector.data();
      for (int j = 0; j < port.portEyeValues.count; j++) {
        portEyeValList[j] =
            static_cast<sai_port_eye_values_list_t>(attr->value.porteyevalues)
                .list[j];
      }
    } break;
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
    case SAI_PORT_ATTR_RX_FREQUENCY_OFFSET_PPM: {
      port.portRxPPM.count = static_cast<sai_port_frequency_offset_ppm_list_t>(
                                 attr->value.portfrequencyoffsetppmlist)
                                 .count;
      auto& ppmList = port.portRxPPM.list;
      auto ppmVector = std::vector<sai_port_frequency_offset_ppm_values_t>();
      ppmVector.resize(port.portRxPPM.count);
      ppmList = ppmVector.data();
      for (int j = 0; j < port.portRxPPM.count; j++) {
        ppmList[j] = static_cast<sai_port_frequency_offset_ppm_list_t>(
                         attr->value.portfrequencyoffsetppmlist)
                         .list[j];
      }
    } break;
    case SAI_PORT_ATTR_RX_SNR: {
      port.portRxSNR.count =
          static_cast<sai_port_snr_list_t>(attr->value.portsnrlist).count;
      auto& snrList = port.portRxSNR.list;
      auto snrVector = std::vector<sai_port_snr_values_t>();
      snrVector.resize(port.portRxSNR.count);
      snrList = snrVector.data();
      for (int j = 0; j < port.portRxSNR.count; j++) {
        snrList[j] =
            static_cast<sai_port_snr_list_t>(attr->value.portsnrlist).list[j];
      }
    } break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
    case SAI_PORT_ATTR_RX_SIGNAL_DETECT: {
      port.portRxSignalDetect.count =
          static_cast<sai_port_lane_latch_status_list_t>(
              attr->value.portlanelatchstatuslist)
              .count;
      auto& signalDetectList = port.portRxSignalDetect.list;
      auto signalDetectVector = std::vector<sai_port_lane_latch_status_t>();
      signalDetectVector.resize(port.portRxSignalDetect.count);
      signalDetectList = signalDetectVector.data();
      for (int j = 0; j < port.portRxSignalDetect.count; j++) {
        signalDetectList[j] = static_cast<sai_port_lane_latch_status_list_t>(
                                  attr->value.portlanelatchstatuslist)
                                  .list[j];
      }
    } break;
    case SAI_PORT_ATTR_RX_LOCK_STATUS: {
      port.portRxLockStatus.count =
          static_cast<sai_port_lane_latch_status_list_t>(
              attr->value.portlanelatchstatuslist)
              .count;
      auto& rxLockStatusList = port.portRxLockStatus.list;
      auto rxLockStatusVector = std::vector<sai_port_lane_latch_status_t>();
      rxLockStatusVector.resize(port.portRxLockStatus.count);
      rxLockStatusList = rxLockStatusVector.data();
      for (int j = 0; j < port.portRxLockStatus.count; j++) {
        rxLockStatusList[j] = static_cast<sai_port_lane_latch_status_list_t>(
                                  attr->value.portlanelatchstatuslist)
                                  .list[j];
      }
    } break;
    case SAI_PORT_ATTR_FEC_ALIGNMENT_LOCK: {
      port.portFecAlignmentLockStatus.count =
          static_cast<sai_port_lane_latch_status_list_t>(
              attr->value.portlanelatchstatuslist)
              .count;
      auto& fecAMLockStatusList = port.portFecAlignmentLockStatus.list;
      auto fecAMLockStatusVector = std::vector<sai_port_lane_latch_status_t>();
      fecAMLockStatusVector.resize(port.portFecAlignmentLockStatus.count);
      fecAMLockStatusList = fecAMLockStatusVector.data();
      for (int j = 0; j < port.portFecAlignmentLockStatus.count; j++) {
        fecAMLockStatusList[j] = static_cast<sai_port_lane_latch_status_list_t>(
                                     attr->value.portlanelatchstatuslist)
                                     .list[j];
      }
    } break;
    case SAI_PORT_ATTR_PCS_RX_LINK_STATUS: {
      port.portPcsLinkStatus =
          static_cast<sai_latch_status_t>(attr->value.latchstatus);
    } break;
#endif
    case SAI_PORT_ATTR_ERR_STATUS_LIST: {
      port.portError.count =
          static_cast<sai_port_err_status_list_t>(attr->value.porterror).count;
      auto& portErrorList = port.portError.list;
      auto portErrorVector = std::vector<sai_port_err_status_t>();
      portErrorVector.resize(port.portError.count);
      portErrorList = portErrorVector.data();
      for (int j = 0; j < port.portError.count; j++) {
        portErrorList[j] =
            static_cast<sai_port_err_status_list_t>(attr->value.porterror)
                .list[j];
      }
    } break;
    case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_MODE:
      port.priorityFlowControlMode =
          static_cast<sai_port_priority_flow_control_mode_t>(attr->value.u32);
      break;
    case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL:
      port.priorityFlowControl = attr->value.u8;
      break;
    case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_RX:
      port.priorityFlowControlRx = attr->value.u8;
      break;
    case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_TX:
      port.priorityFlowControlTx = attr->value.u8;
      break;
    case SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST: {
      auto& ingressPriorityGroupList = port.ingressPriorityGroupList;
      ingressPriorityGroupList.clear();
      for (int j = 0; j < attr->value.objlist.count; ++j) {
        ingressPriorityGroupList.push_back(attr->value.objlist.list[j]);
      }
    } break;
    case SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS:
      port.numberOfIngressPriorityGroups = attr->value.u32;
      break;
    case SAI_PORT_ATTR_QOS_TC_TO_PRIORITY_GROUP_MAP:
      port.qosTcToPriorityGroupMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_QOS_TC_TO_VC_MAP:
      port.qosTcToVcMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_CBFC_SENDER_CREDIT_LIMIT:
      port.cbfcSenderCreditLimit = attr->value.u32;
      break;
    case SAI_PORT_ATTR_CBFC_SENDER_CREDIT_SIZE:
      port.cbfcSenderCreditSize = attr->value.u16;
      break;
    case SAI_PORT_ATTR_CBFC_SENDER_PACKET_OVERHEAD:
      port.cbfcSenderPacketOverhead = attr->value.s16;
      break;
    case SAI_PORT_ATTR_QOS_QUEUE_TO_VC_MAP:
      port.qosQueueToVcMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_QUEUE_MAP:
      port.qosPfcPriorityToQueueMap = attr->value.oid;
      break;
    case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_PRIORITY_GROUP_MAP:
      port.qosPfcPriorityToPriorityGroupMap = attr->value.oid;
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    case SAI_PORT_ATTR_IPG:
      port.interFrameGap = attr->value.u32;
      break;
#endif
    case SAI_PORT_ATTR_LINK_TRAINING_ENABLE:
      port.linkTrainingEnable = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_FDR_ENABLE:
      port.fdrEnable = attr->value.booldata;
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
    case SAI_PORT_ATTR_CRC_ERROR_TOKEN_DETECT:
      port.portCrcErrDetect =
          static_cast<sai_latch_status_t>(attr->value.latchstatus);
      break;
#endif
    case SAI_PORT_ATTR_RX_LANE_SQUELCH_ENABLE:
      port.rxLaneSquelchEnable = attr->value.booldata;
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
    case SAI_PORT_ATTR_FABRIC_ISOLATE:
      port.fabricIsolate = attr->value.booldata;
      break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    case SAI_PORT_ATTR_LLR_MODE_LOCAL:
      port.llrModeLocal = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_LLR_MODE_REMOTE:
      port.llrModeRemote = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_LLR_PROFILE:
      port.llrProfile = attr->value.oid;
      break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    case SAI_PORT_ATTR_PFC_TC_DLD_INTERVAL: {
      std::vector<sai_map_t> pfcTcDldInterval{};
      for (int j = 0; j < attr->value.maplist.count; ++j) {
        pfcTcDldInterval.push_back(attr->value.maplist.list[j]);
      }
      if (pfcTcDldInterval.size()) {
        port.pfcTcDldInterval = pfcTcDldInterval;
      }
    } break;
    case SAI_PORT_ATTR_PFC_TC_DLR_INTERVAL: {
      std::vector<sai_map_t> pfcTcDlrInterval{};
      for (int j = 0; j < attr->value.maplist.count; ++j) {
        pfcTcDlrInterval.push_back(attr->value.maplist.list[j]);
      }
      if (pfcTcDlrInterval.size()) {
        port.pfcTcDlrInterval = pfcTcDlrInterval;
      }
    } break;
#endif
    case SAI_PORT_ATTR_ARS_ENABLE:
      port.ars_enable = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_ARS_PORT_LOAD_SCALING_FACTOR:
      port.ars_port_load_scaling_factor = attr->value.u32;
      break;
    case SAI_PORT_ATTR_ARS_PORT_LOAD_PAST_WEIGHT:
      port.ars_port_load_past_weight = attr->value.u32;
      break;
    case SAI_PORT_ATTR_ARS_PORT_LOAD_FUTURE_WEIGHT:
      port.ars_port_load_future_weight = attr->value.u32;
      break;
    case SAI_PORT_ATTR_RESET_QUEUE_CREDIT_BALANCE:
      port.resetQueueCreditBalance = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_PFC_MONITOR_DIRECTION:
      port.pfcMonitorDirection = attr->value.s32;
      break;
    case SAI_PORT_ATTR_EXT_CABLE_PROPAGATION_DELAY_MEDIA_TYPE:
      port.cablePropagationDelayMediaType = attr->value.s32;
      break;
    case SAI_PORT_ATTR_EXT_PFC_PAUSE_DURATION_OVERRIDE:
      port.pfcPauseDurationOverride = attr->value.u16;
      break;
    case SAI_PORT_ATTR_CABLE_PROPAGATION_DELAY_MEASURE:
      port.cablePropagationDelayMeasure = attr->value.booldata;
      break;
    case SAI_PORT_ATTR_EXT_LINKSCAN_MODE:
      port.linkScanMode = attr->value.s32;
      break;
    default:
      res = SAI_STATUS_INVALID_PARAMETER;
      break;
  }
  return res;
}

sai_status_t get_port_attribute_fn(
    sai_object_id_t port_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& port = fs->portManager.get(port_id);
  for (int i = 0; i < attr_count; ++i) {
    if (port.onGetAttribute) {
      port.onGetAttribute();
    }
    switch (attr[i].id) {
      case SAI_PORT_ATTR_ADMIN_STATE:
        attr[i].value.booldata = port.adminState;
        break;
      case SAI_PORT_ATTR_HW_LANE_LIST:
        if (port.lanes.size() > attr[i].value.u32list.count) {
          attr[i].value.u32list.count = port.lanes.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.lanes.size(); ++j) {
          attr[i].value.u32list.list[j] = port.lanes[j];
        }
        attr[i].value.u32list.count = port.lanes.size();
        break;
      case SAI_PORT_ATTR_STATIC_MODULE_ID:
        attr[i].value.u32 = port.staticModuleId;
        break;
      case SAI_PORT_ATTR_SPEED:
        attr[i].value.u32 = port.speed;
        break;
      case SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES:
        attr[i].value.u32 = 8;
        break;
      case SAI_PORT_ATTR_QOS_QUEUE_LIST:
        if (port.queueIdList.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = port.queueIdList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.queueIdList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.queueIdList[j];
        }
        break;
      case SAI_PORT_ATTR_QOS_EGRESS_BUFFER_PROFILE_LIST:
        if (port.egressBufferProfileIdList.size() >
            attr[i].value.objlist.count) {
          attr[i].value.objlist.count =
              static_cast<uint32_t>(port.egressBufferProfileIdList.size());
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.egressBufferProfileIdList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.egressBufferProfileIdList[j];
        }
        attr[i].value.objlist.count =
            static_cast<uint32_t>(port.egressBufferProfileIdList.size());
        break;
      case SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST:
        if (port.ingressBufferProfileIdList.size() >
            attr[i].value.objlist.count) {
          attr[i].value.objlist.count =
              static_cast<uint32_t>(port.ingressBufferProfileIdList.size());
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.ingressBufferProfileIdList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.ingressBufferProfileIdList[j];
        }
        attr[i].value.objlist.count =
            static_cast<uint32_t>(port.ingressBufferProfileIdList.size());
        break;
      case SAI_PORT_ATTR_FEC_MODE:
        attr[i].value.s32 = static_cast<int32_t>(port.fecMode);
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      case SAI_PORT_ATTR_USE_EXTENDED_FEC:
        attr[i].value.booldata = port.useExtendedFec;
        break;
      case SAI_PORT_ATTR_FEC_MODE_EXTENDED:
        attr[i].value.u32 = static_cast<uint32_t>(port.extendedFecMode);
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      case SAI_PORT_ATTR_LOOPBACK_MODE:
        attr[i].value.s32 = static_cast<int32_t>(port.loopbackMode);
        break;
#endif
      case SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE:
        attr[i].value.s32 = static_cast<int32_t>(port.internalLoopbackMode);
        break;
      case SAI_PORT_ATTR_MEDIA_TYPE:
        attr[i].value.s32 = static_cast<int32_t>(port.mediaType);
        break;
      case SAI_PORT_ATTR_GLOBAL_FLOW_CONTROL_MODE:
        attr[i].value.s32 = static_cast<int32_t>(port.globalFlowControlMode);
        break;
      case SAI_PORT_ATTR_PORT_VLAN_ID:
        attr[i].value.u16 = port.vlanId;
        break;
      case SAI_PORT_ATTR_SERDES_PREEMPHASIS:
        if (port.preemphasis.size() > attr[i].value.u32list.count) {
          attr[i].value.u32list.count = port.preemphasis.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.preemphasis.size(); ++j) {
          attr[i].value.u32list.list[j] = port.preemphasis[j];
        }
        attr[i].value.u32list.count = port.preemphasis.size();
        break;
      case SAI_PORT_ATTR_MTU:
        attr->value.u32 = port.mtu;
        break;
      case SAI_PORT_ATTR_META_DATA:
        attr->value.u32 = port.metadata;
        break;
      case SAI_PORT_ATTR_OPER_STATUS:
        attr->value.s32 = SAI_PORT_OPER_STATUS_UP;
        break;
      case SAI_PORT_ATTR_QOS_DSCP_TO_TC_MAP:
        attr->value.oid = port.qosDscpToTcMap;
        break;
      case SAI_PORT_ATTR_QOS_DOT1P_TO_TC_MAP:
        attr->value.oid = port.qosDot1pToTcMap;
        break;
      case SAI_PORT_ATTR_QOS_TC_AND_COLOR_TO_DOT1P_MAP:
        attr->value.oid = port.qosTcAndColorToDot1pMap;
        break;
      case SAI_PORT_ATTR_QOS_TC_TO_QUEUE_MAP:
        attr->value.oid = port.qosTcToQueueMap;
        break;
      case SAI_PORT_ATTR_DISABLE_DECREMENT_TTL:
        attr->value.booldata = port.disableTtlDecrement;
        break;
      case SAI_PORT_ATTR_INTERFACE_TYPE:
        attr->value.s32 = port.interface_type;
        break;
      case SAI_PORT_ATTR_PKT_TX_ENABLE:
        attr->value.booldata = port.txEnable;
        break;
      case SAI_PORT_ATTR_TAM_OBJECT:
        if (port.tamObjectList.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = port.tamObjectList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.tamObjectList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.tamObjectList[j];
        }
        attr[i].value.objlist.count = port.tamObjectList.size();
        break;
      case SAI_PORT_ATTR_INGRESS_MIRROR_SESSION:
        if (port.ingressMirrorList.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = port.ingressMirrorList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.ingressMirrorList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.ingressMirrorList[j];
        }
        attr[i].value.objlist.count = port.ingressMirrorList.size();
        break;
      case SAI_PORT_ATTR_EGRESS_MIRROR_SESSION:
        if (port.egressMirrorList.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = port.egressMirrorList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.egressMirrorList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.egressMirrorList[j];
        }
        attr[i].value.objlist.count = port.egressMirrorList.size();
        break;
      case SAI_PORT_ATTR_INGRESS_SAMPLEPACKET_ENABLE:
        attr->value.oid = port.ingressSamplePacket;
        break;
      case SAI_PORT_ATTR_EGRESS_SAMPLEPACKET_ENABLE:
        attr->value.oid = port.egressSamplePacket;
        break;
      case SAI_PORT_ATTR_PORT_SERDES_ID:
        attr[i].value.oid = SAI_NULL_OBJECT_ID;
        break;
      case SAI_PORT_ATTR_INGRESS_SAMPLE_MIRROR_SESSION:
        if (port.ingressSampleMirrorList.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = port.ingressSampleMirrorList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.ingressSampleMirrorList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.ingressSampleMirrorList[j];
        }
        attr[i].value.objlist.count = port.ingressSampleMirrorList.size();
        break;
      case SAI_PORT_ATTR_EGRESS_SAMPLE_MIRROR_SESSION:
        if (port.egressSampleMirrorList.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = port.egressSampleMirrorList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.egressSampleMirrorList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.egressSampleMirrorList[j];
        }
        attr[i].value.objlist.count = port.egressSampleMirrorList.size();
        break;
      case SAI_PORT_ATTR_PRBS_POLYNOMIAL:
        attr[i].value.u32 = port.prbsPolynomial;
        break;
      case SAI_PORT_ATTR_PRBS_CONFIG:
        attr[i].value.s32 = port.prbsConfig;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
      case SAI_PORT_ATTR_PRBS_RX_STATE:
        attr[i].value.rx_state.rx_status = port.prbsRxState.rx_status;
        attr[i].value.rx_state.error_count = port.prbsRxState.error_count;
        break;
#endif
      case SAI_PORT_ATTR_ISOLATION_GROUP:
        attr[i].value.oid = port.isolationGroup;
        break;
      case SAI_PORT_ATTR_INGRESS_ACL:
        attr[i].value.oid = port.ingressAcl;
        break;
      case SAI_PORT_ATTR_INGRESS_MACSEC_ACL:
        attr[i].value.oid = port.ingressMacsecAcl;
        break;
      case SAI_PORT_ATTR_EGRESS_MACSEC_ACL:
        attr[i].value.oid = port.egressMacsecAcl;
        break;
      case SAI_PORT_ATTR_EXT_FAKE_SYSTEM_PORT_ID:
        attr[i].value.u16 = port.systemPortId;
        break;
      case SAI_PORT_ATTR_PTP_MODE:
        attr[i].value.s32 = static_cast<int32_t>(port.ptpMode);
        break;
      case SAI_PORT_ATTR_EYE_VALUES:
        attr[i].value.porteyevalues.count = port.portEyeValues.count;
        for (int j = 0; j < port.portEyeValues.count; j++) {
          attr[i].value.porteyevalues.list[j] = port.portEyeValues.list[j];
        }
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
      case SAI_PORT_ATTR_RX_SIGNAL_DETECT:
        attr[i].value.portlanelatchstatuslist.count =
            port.portRxSignalDetect.count;
        for (int j = 0; j < port.portRxSignalDetect.count; j++) {
          attr[i].value.portlanelatchstatuslist.list[j] =
              port.portRxSignalDetect.list[j];
        }
        break;
      case SAI_PORT_ATTR_RX_LOCK_STATUS:
        attr[i].value.portlanelatchstatuslist.count =
            port.portRxLockStatus.count;
        for (int j = 0; j < port.portRxLockStatus.count; j++) {
          attr[i].value.portlanelatchstatuslist.list[j] =
              port.portRxLockStatus.list[j];
        }
        break;
      case SAI_PORT_ATTR_FEC_ALIGNMENT_LOCK:
        attr[i].value.portlanelatchstatuslist.count =
            port.portFecAlignmentLockStatus.count;
        for (int j = 0; j < port.portFecAlignmentLockStatus.count; j++) {
          attr[i].value.portlanelatchstatuslist.list[j] =
              port.portFecAlignmentLockStatus.list[j];
        }
        break;
      case SAI_PORT_ATTR_PCS_RX_LINK_STATUS:
        attr[i].value.latchstatus = port.portPcsLinkStatus;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
      case SAI_PORT_ATTR_EXT_FAKE_OPER_STATUS_LATCH:
        attr[i].value.latchstatus = port.portExtOperStatusLatch;
        port.portExtOperStatusLatch.changed = false;
        break;
#endif
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_MODE:
        attr[i].value.u32 = static_cast<int32_t>(port.priorityFlowControlMode);
        break;
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL:
        attr[i].value.u8 = port.priorityFlowControl;
        break;
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_RX:
        attr[i].value.u8 = port.priorityFlowControlRx;
        break;
      case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_TX:
        attr[i].value.u8 = port.priorityFlowControlTx;
        break;
      case SAI_PORT_ATTR_ERR_STATUS_LIST:
        if (port.portErrStatusList.size() > attr[i].value.porterror.count) {
          attr[i].value.porterror.count = port.portErrStatusList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.portErrStatusList.size(); ++j) {
          attr[i].value.porterror.list[j] = port.portErrStatusList[j];
        }
        attr[i].value.porterror.count = port.portErrStatusList.size();
        break;
      case SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST: {
        if (port.ingressPriorityGroupList.size() >
            attr[i].value.objlist.count) {
          attr[i].value.objlist.count = port.ingressPriorityGroupList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.ingressPriorityGroupList.size(); ++j) {
          attr[i].value.objlist.list[j] = port.ingressPriorityGroupList[j];
        }
        attr[i].value.objlist.count = port.ingressPriorityGroupList.size();
      } break;
      case SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS:
        attr[i].value.u32 = port.numberOfIngressPriorityGroups;
        break;
      case SAI_PORT_ATTR_QOS_TC_TO_PRIORITY_GROUP_MAP:
        attr[i].value.oid = port.qosTcToPriorityGroupMap;
        break;
      case SAI_PORT_ATTR_QOS_TC_TO_VC_MAP:
        attr[i].value.oid = port.qosTcToVcMap;
        break;
      case SAI_PORT_ATTR_CBFC_SENDER_CREDIT_LIMIT:
        attr[i].value.u32 = port.cbfcSenderCreditLimit;
        break;
      case SAI_PORT_ATTR_CBFC_SENDER_CREDIT_SIZE:
        attr[i].value.u16 = port.cbfcSenderCreditSize;
        break;
      case SAI_PORT_ATTR_CBFC_SENDER_PACKET_OVERHEAD:
        attr[i].value.s16 = port.cbfcSenderPacketOverhead;
        break;
      case SAI_PORT_ATTR_QOS_QUEUE_TO_VC_MAP:
        attr[i].value.oid = port.qosQueueToVcMap;
        break;
      case SAI_PORT_ATTR_CBFC_RECEIVER_NATIVE_CREDIT_SIZE:
        attr[i].value.u16 = port.cbfcReceiverNativeCreditSize;
        break;
      case SAI_PORT_ATTR_CBFC_RECEIVER_NATIVE_PACKET_OVERHEAD:
        attr[i].value.s16 = port.cbfcReceiverNativePacketOverhead;
        break;
      case SAI_PORT_ATTR_CBFC_RECEIVER_NATIVE_TOTAL_CREDITS:
        attr[i].value.u16 = port.cbfcReceiverNativeTotalCredits;
        break;
      case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_QUEUE_MAP:
        attr[i].value.oid = port.qosPfcPriorityToQueueMap;
        break;
      case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_PRIORITY_GROUP_MAP:
        attr[i].value.oid = port.qosPfcPriorityToPriorityGroupMap;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      case SAI_PORT_ATTR_IPG:
        attr[i].value.u32 = port.interFrameGap;
        break;
#endif
      case SAI_PORT_ATTR_LINK_TRAINING_ENABLE:
        attr->value.booldata = port.linkTrainingEnable;
        break;
      case SAI_PORT_ATTR_LINK_TRAINING_RX_STATUS:
        attr->value.s32 = port.linkTrainingRxStatus;
        break;
      case SAI_PORT_ATTR_FDR_ENABLE:
        attr->value.booldata = port.fdrEnable;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
      case SAI_PORT_ATTR_CRC_ERROR_TOKEN_DETECT:
        attr[i].value.latchstatus = port.portCrcErrDetect;
        break;
#endif
      case SAI_PORT_ATTR_RX_LANE_SQUELCH_ENABLE:
        attr->value.booldata = port.rxLaneSquelchEnable;
        break;
      case SAI_PORT_ATTR_FABRIC_REACHABILITY:
        attr->value.reachability.reachable = true;
        break;
      case SAI_PORT_ATTR_FABRIC_ATTACHED_SWITCH_ID:
        attr->value.u32 = 0;
        break;
      case SAI_PORT_ATTR_FABRIC_ATTACHED_SWITCH_TYPE:
        attr->value.u32 = SAI_SWITCH_TYPE_VOQ;
        break;
      case SAI_PORT_ATTR_FABRIC_ATTACHED:
        attr->value.booldata = false;
        break;
      case SAI_PORT_ATTR_FABRIC_ATTACHED_PORT_INDEX:
        attr->value.u32 = 0;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
      case SAI_PORT_ATTR_FABRIC_ISOLATE:
        attr->value.booldata = port.fabricIsolate;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      case SAI_PORT_ATTR_LLR_MODE_LOCAL:
        attr->value.booldata = port.llrModeLocal;
        break;
      case SAI_PORT_ATTR_LLR_MODE_REMOTE:
        attr->value.booldata = port.llrModeRemote;
        break;
      case SAI_PORT_ATTR_LLR_PROFILE:
        attr->value.oid = port.llrProfile;
        break;
      case SAI_PORT_ATTR_LLR_TX_STATUS:
        attr->value.s32 = port.llrTxStatus;
        break;
      case SAI_PORT_ATTR_LLR_RX_STATUS:
        attr->value.s32 = port.llrRxStatus;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      case SAI_PORT_ATTR_PFC_TC_DLD_INTERVAL:
        if (port.pfcTcDldInterval.has_value()) {
          auto& pfcTcDldInterval = port.pfcTcDldInterval.value();
          if (pfcTcDldInterval.size() > attr[i].value.maplist.count) {
            attr[i].value.maplist.count = pfcTcDldInterval.size();
            return SAI_STATUS_BUFFER_OVERFLOW;
          }
          for (int j = 0; j < pfcTcDldInterval.size(); ++j) {
            attr[i].value.maplist.list[j] = pfcTcDldInterval[j];
          }
          attr[i].value.maplist.count = pfcTcDldInterval.size();
        }
        break;
      case SAI_PORT_ATTR_PFC_TC_DLR_INTERVAL:
        if (port.pfcTcDlrInterval.has_value()) {
          auto& pfcTcDlrInterval = port.pfcTcDlrInterval.value();
          if (pfcTcDlrInterval.size() > attr[i].value.maplist.count) {
            attr[i].value.maplist.count = pfcTcDlrInterval.size();
            return SAI_STATUS_BUFFER_OVERFLOW;
          }
          for (int j = 0; j < pfcTcDlrInterval.size(); ++j) {
            attr[i].value.maplist.list[j] = pfcTcDlrInterval[j];
          }
          attr[i].value.maplist.count = pfcTcDlrInterval.size();
        }
        break;
      case SAI_PORT_ATTR_PFC_TC_DLD_INTERVAL_RANGE:
      case SAI_PORT_ATTR_PFC_TC_DLR_INTERVAL_RANGE:
        attr[i].value.u32range.min = 0;
        attr[i].value.u32range.max = std::numeric_limits<uint32_t>::max();
        break;
#endif
      case SAI_PORT_ATTR_SYSTEM_PORT:
        attr[i].value.u16 = port.systemPortId;
        break;
      case SAI_PORT_ATTR_ARS_ENABLE:
        attr[i].value.booldata = port.ars_enable;
        break;
      case SAI_PORT_ATTR_ARS_PORT_LOAD_SCALING_FACTOR:
        attr[i].value.u32 = port.ars_port_load_scaling_factor;
        break;
      case SAI_PORT_ATTR_ARS_PORT_LOAD_PAST_WEIGHT:
        attr[i].value.u32 = port.ars_port_load_past_weight;
        break;
      case SAI_PORT_ATTR_ARS_PORT_LOAD_FUTURE_WEIGHT:
        attr[i].value.u32 = port.ars_port_load_future_weight;
        break;
      case SAI_PORT_ATTR_FABRIC_SYSTEM_PORT:
        attr[i].value.oid = port.fabricSystemPort;
        break;
      case SAI_PORT_ATTR_PORT_PG_PKT_DROP_STATUS:
        if (port.portPgPktDropStatus.size() > attr[i].value.maplist.count) {
          attr[i].value.maplist.count = port.portPgPktDropStatus.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (int j = 0; j < port.portPgPktDropStatus.size(); ++j) {
          attr[i].value.maplist.list[j] = port.portPgPktDropStatus[j];
        }
        attr[i].value.maplist.count = port.portPgPktDropStatus.size();
        break;
      case SAI_PORT_ATTR_RESET_QUEUE_CREDIT_BALANCE:
        attr[i].value.booldata = port.resetQueueCreditBalance;
        break;
      case SAI_PORT_ATTR_PFC_MONITOR_DIRECTION:
        attr[i].value.s32 = port.pfcMonitorDirection;
        break;
      case SAI_PORT_ATTR_EXT_CABLE_PROPAGATION_DELAY_MEDIA_TYPE:
        attr[i].value.s32 = port.cablePropagationDelayMediaType;
        break;
      case SAI_PORT_ATTR_EXT_PFC_PAUSE_DURATION_OVERRIDE:
        attr[i].value.u16 = port.pfcPauseDurationOverride;
        break;
      case SAI_PORT_ATTR_CABLE_PROPAGATION_DELAY_MEASURE:
        attr[i].value.booldata = port.cablePropagationDelayMeasure;
        break;
      case SAI_PORT_ATTR_EXT_LINKSCAN_MODE:
        attr[i].value.s32 = port.linkScanMode;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_port_stats_fn(
    sai_object_id_t /*port*/,
    uint32_t num_of_counters,
    const sai_stat_id_t* /*counter_ids*/,
    uint64_t* counters) {
  for (auto i = 0; i < num_of_counters; ++i) {
    counters[i] = 0;
  }
  return SAI_STATUS_SUCCESS;
}

/*
 * In fake sai there isn't a dataplane, so all stats
 * stay at 0. Leverage the corresponding non _ext
 * stats fn to get the stats. If stats are always 0,
 * modes (READ, READ_AND_CLEAR) don't matter
 */
sai_status_t get_port_stats_ext_fn(
    sai_object_id_t port,
    uint32_t num_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /*mode*/,
    uint64_t* counters) {
  return get_port_stats_fn(port, num_of_counters, counter_ids, counters);
}
/*
 *  noop clear stats API. Since fake doesnt have a
 *  dataplane stats are always set to 0, so
 *  no need to clear them
 */
sai_status_t clear_port_stats_fn(
    sai_object_id_t /* port_id */,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids) {
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_port_serdes_attribute_fn(
    sai_object_id_t port_serdes_id,
    const sai_attribute_t* attr);

sai_status_t create_port_serdes_fn(
    sai_object_id_t* port_serdes_id,
    sai_object_id_t /*switch_id*/,
    uint32_t count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  bool created = false;
  for (auto i = 0; i < count; i++) {
    if (attr_list[i].id == SAI_PORT_SERDES_ATTR_PORT_ID) {
      created = true;
      *port_serdes_id = fs->portSerdesManager.create(attr_list[i].value.oid);
      break;
    }
  }
  if (!created) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  for (auto i = 0; i < count; i++) {
    if (attr_list[i].id == SAI_PORT_SERDES_ATTR_PORT_ID) {
      continue;
    }
    auto status = set_port_serdes_attribute_fn(*port_serdes_id, &attr_list[i]);
    if (status != SAI_STATUS_SUCCESS) {
      return status;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_port_serdes_fn(sai_object_id_t port_serdes_id) {
  auto fs = FakeSai::getInstance();
  fs->portSerdesManager.remove(port_serdes_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_port_serdes_attribute_fn(
    sai_object_id_t port_serdes_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& portSerdes = fs->portSerdesManager.get(port_serdes_id);
  auto& port = fs->portManager.get(portSerdes.port);
  // Replace (not append) the stored values: a set attribute call overwrites
  // the serdes parameter. Using back_inserter here would accumulate across
  // repeated sets of the same attribute (create sets it once, then
  // SaiObjectStore::program re-sets it during reconciliation), doubling the
  // vector and tripping the lane-count check below on platforms whose mapping
  // carries TX FIR settings (e.g. Montblanc).
  auto fillVec = [](auto& vec, auto* list, size_t count) {
    vec.assign(list, list + count);
  };
  auto checkLanes = [&port](auto vec) {
    return port.lanes.size() == vec.size();
  };

  switch (attr->id) {
    case SAI_PORT_SERDES_ATTR_PREEMPHASIS:
      fillVec(
          portSerdes.preemphasis,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.preemphasis)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_TX_FIR_PRE1:
      fillVec(
          portSerdes.txFirPre1,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txFirPre1)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_TX_FIR_PRE2:
      fillVec(
          portSerdes.txFirPre2,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txFirPre2)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_TX_FIR_PRE3:
      fillVec(
          portSerdes.txFirPre3,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txFirPre3)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_IDRIVER:
      fillVec(
          portSerdes.iDriver,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.iDriver)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_TX_FIR_MAIN:
      fillVec(
          portSerdes.txFirMain,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txFirMain)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_TX_FIR_POST1:
      fillVec(
          portSerdes.txFirPost1,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txFirPost1)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_TX_FIR_POST2:
      fillVec(
          portSerdes.txFirPost2,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txFirPost2)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_TX_FIR_POST3:
      fillVec(
          portSerdes.txFirPost3,
          attr->value.u32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txFirPost3)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_EXT_TX_LUT_MODE:
      fillVec(
          portSerdes.txLutMode,
          attr->value.s32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.txLutMode)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_REACH:
      fillVec(
          portSerdes.rxReach,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxReach)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    // The vendor extensions alias the standard precoding attributes
    case SAI_PORT_SERDES_ATTR_EXT_FAKE_TRANSMIT_PRECODING_STATE:
      fillVec(
          portSerdes.txPrecoding,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txPrecoding)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_EXT_FAKE_RECEIVE_PRECODING_STATE:
      fillVec(
          portSerdes.rxPrecoding,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxPrecoding)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_CTLE_CODE:
      fillVec(
          portSerdes.rxCtlCode,
          attr->value.s32list.list,
          attr->value.u32list.count);
      if (!checkLanes(portSerdes.rxCtlCode)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_DSP_MODE:
      fillVec(
          portSerdes.rxDspMode,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxDspMode)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AFE_TRIM:
      fillVec(
          portSerdes.rxAfeTrim,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxAfeTrim)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;

    case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AC_COUPLING_BYPASS:
      fillVec(
          portSerdes.rxCouplingByPass,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxCouplingByPass)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AFE_ADAPTIVE_ENABLE:
      fillVec(
          portSerdes.rxAfeTrimAdaptiveEnable,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxAfeTrimAdaptiveEnable)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_DIFF_ENCODER_EN:
      fillVec(
          portSerdes.txDiffEncoderEn,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txDiffEncoderEn)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_DIG_GAIN:
      fillVec(
          portSerdes.txDigGain,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txDigGain)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_0:
      fillVec(
          portSerdes.txFfeCoeff0,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txFfeCoeff0)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_1:
      fillVec(
          portSerdes.txFfeCoeff1,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txFfeCoeff1)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_2:
      fillVec(
          portSerdes.txFfeCoeff2,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txFfeCoeff2)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_3:
      fillVec(
          portSerdes.txFfeCoeff3,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txFfeCoeff3)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_4:
      fillVec(
          portSerdes.txFfeCoeff4,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txFfeCoeff4)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_DRIVER_SWING:
      fillVec(
          portSerdes.txDriverSwing,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txDriverSwing)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_TX_LDO_BYPASS:
      fillVec(
          portSerdes.txLdoBypass,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txLdoBypass)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STRT:
      fillVec(
          portSerdes.rxInstgBoost1Start,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgBoost1Start)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STEP:
      fillVec(
          portSerdes.rxInstgBoost1Step,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgBoost1Step)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STOP:
      fillVec(
          portSerdes.rxInstgBoost1Stop,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgBoost1Stop)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STRT:
      fillVec(
          portSerdes.rxInstgBoost2OrHrStart,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgBoost2OrHrStart)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STEP:
      fillVec(
          portSerdes.rxInstgBoost2OrHrStep,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgBoost2OrHrStep)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STOP:
      fillVec(
          portSerdes.rxInstgBoost2OrHrStop,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgBoost2OrHrStop)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_START_1P7:
      fillVec(
          portSerdes.rxInstgC1Start1p7,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgC1Start1p7)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_STEP_1P7:
      fillVec(
          portSerdes.rxInstgC1Step1p7,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgC1Step1p7)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_STOP_1P7:
      fillVec(
          portSerdes.rxInstgC1Stop1p7,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgC1Stop1p7)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_START_1P7:
      fillVec(
          portSerdes.rxInstgDfeStart1p7,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgDfeStart1p7)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_STEP_1P7:
      fillVec(
          portSerdes.rxInstgDfeStep1p7,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgDfeStep1p7)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_STOP_1P7:
      fillVec(
          portSerdes.rxInstgDfeStop1p7,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgDfeStop1p7)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_SCAN_USE_SR_SETTINGS:
      fillVec(
          portSerdes.rxInstgScanUseSrSettings,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgScanUseSrSettings)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_CFG_OV_EN:
      fillVec(
          portSerdes.rxCdrCfgOvEn,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxCdrCfgOvEn)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_1ST_ORD_STEP_OV_VAL:
      fillVec(
          portSerdes.rxCdrTdet1stOrdStepOvVal,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxCdrTdet1stOrdStepOvVal)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_2ND_ORD_STEP_OV_VAL:
      fillVec(
          portSerdes.rxCdrTdet2ndOrdStepOvVal,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxCdrTdet2ndOrdStepOvVal)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_FINE_STEP_OV_VAL:
      fillVec(
          portSerdes.rxCdrTdetFineStepOvVal,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxCdrTdetFineStepOvVal)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_LDO_BYPASS:
      fillVec(
          portSerdes.rxLdoBypass,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxLdoBypass)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_DIFF_ENCODER_EN:
      fillVec(
          portSerdes.rxDiffEncoderEn,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxDiffEncoderEn)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_ENABLE_SCAN:
      fillVec(
          portSerdes.rxInstgEnableScan,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxInstgEnableScan)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_FFE_LENGTH_BITMAP:
      fillVec(
          portSerdes.rxFfeLengthBitmap,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxFfeLengthBitmap)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_EXT_RX_FFE_LMS_DYNAMIC_GATING_EN:
      fillVec(
          portSerdes.rxFfeLmsDynamicGatingEn,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxFfeLmsDynamicGatingEn)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    case SAI_PORT_SERDES_ATTR_TX_PRECODING:
      fillVec(
          portSerdes.txPrecoding,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.txPrecoding)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
    case SAI_PORT_SERDES_ATTR_RX_PRECODING:
      fillVec(
          portSerdes.rxPrecoding,
          attr->value.s32list.list,
          attr->value.s32list.count);
      if (!checkLanes(portSerdes.rxPrecoding)) {
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
      }
      break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
    case SAI_PORT_SERDES_ATTR_CUSTOM_COLLECTION:
      portSerdes.serdesCustomCollection = std::string(
          reinterpret_cast<const char*>(attr->value.json.json.list),
          attr->value.json.json.count);
      break;
#endif
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_port_serdes_attribute_fn(
    sai_object_id_t port_serdes_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& portSerdes = fs->portSerdesManager.get(port_serdes_id);
  auto checkListSize = [](auto& list, auto& vec) {
    if (list.count < vec.size()) {
      return false;
    }
    return true;
  };
  auto copyVecToList = [](auto& vec, auto& list) {
    for (auto i = 0; i < vec.size(); i++) {
      list.list[i] = vec[i];
    }
  };
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_PORT_SERDES_ATTR_PORT_ID:
        attr_list[i].value.oid = portSerdes.port;
        break;
      case SAI_PORT_SERDES_ATTR_PREEMPHASIS:
        if (!checkListSize(
                attr_list[i].value.u32list, portSerdes.preemphasis)) {
          attr_list[i].value.u32list.count = portSerdes.preemphasis.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.preemphasis, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_IDRIVER:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.iDriver)) {
          attr_list[i].value.u32list.count = portSerdes.iDriver.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.iDriver, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_TX_FIR_PRE1:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.txFirPre1)) {
          attr_list[i].value.u32list.count = portSerdes.txFirPre1.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFirPre1, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_TX_FIR_PRE2:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.txFirPre2)) {
          attr_list[i].value.u32list.count = portSerdes.txFirPre2.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFirPre2, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_TX_FIR_PRE3:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.txFirPre3)) {
          attr_list[i].value.u32list.count = portSerdes.txFirPre3.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFirPre3, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_TX_FIR_MAIN:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.txFirMain)) {
          attr_list[i].value.u32list.count = portSerdes.txFirMain.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFirMain, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_TX_FIR_POST1:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.txFirPost1)) {
          attr_list[i].value.u32list.count = portSerdes.txFirPost1.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFirPost1, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_TX_FIR_POST2:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.txFirPost2)) {
          attr_list[i].value.u32list.count = portSerdes.txFirPost2.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFirPost2, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_TX_FIR_POST3:
        if (!checkListSize(attr_list[i].value.u32list, portSerdes.txFirPost3)) {
          attr_list[i].value.u32list.count = portSerdes.txFirPost3.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFirPost3, attr_list[i].value.u32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_LUT_MODE:
        if (!checkListSize(attr_list[i].value.s32list, portSerdes.txLutMode)) {
          attr_list[i].value.s32list.count = portSerdes.txLutMode.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txLutMode, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_REACH:
        if (!checkListSize(attr_list[i].value.s32list, portSerdes.rxReach)) {
          attr_list[i].value.s32list.count = portSerdes.rxReach.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxReach, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_TRANSMIT_PRECODING_STATE:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txPrecoding)) {
          attr_list[i].value.s32list.count =
              static_cast<uint32_t>(portSerdes.txPrecoding.size());
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txPrecoding, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_RECEIVE_PRECODING_STATE:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxPrecoding)) {
          attr_list[i].value.s32list.count =
              static_cast<uint32_t>(portSerdes.rxPrecoding.size());
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxPrecoding, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_CTLE_CODE:
        if (!checkListSize(attr_list[i].value.s32list, portSerdes.rxCtlCode)) {
          attr_list[i].value.s32list.count = portSerdes.rxCtlCode.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxCtlCode, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_DSP_MODE:
        if (!checkListSize(attr_list[i].value.s32list, portSerdes.rxDspMode)) {
          attr_list[i].value.s32list.count = portSerdes.rxDspMode.size();

          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxDspMode, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AFE_TRIM:
        if (!checkListSize(attr_list[i].value.s32list, portSerdes.rxAfeTrim)) {
          attr_list[i].value.s32list.count = portSerdes.rxAfeTrim.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxAfeTrim, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AC_COUPLING_BYPASS:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxCouplingByPass)) {
          attr_list[i].value.s32list.count = portSerdes.rxCouplingByPass.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxCouplingByPass, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_FAKE_RX_AFE_ADAPTIVE_ENABLE:
        if (!checkListSize(
                attr_list[i].value.s32list,
                portSerdes.rxAfeTrimAdaptiveEnable)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxAfeTrimAdaptiveEnable.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxAfeTrimAdaptiveEnable, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_DIFF_ENCODER_EN:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txDiffEncoderEn)) {
          attr_list[i].value.s32list.count = portSerdes.txDiffEncoderEn.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txDiffEncoderEn, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_DIG_GAIN:
        if (!checkListSize(attr_list[i].value.s32list, portSerdes.txDigGain)) {
          attr_list[i].value.s32list.count = portSerdes.txDigGain.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txDigGain, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_0:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txFfeCoeff0)) {
          attr_list[i].value.s32list.count = portSerdes.txFfeCoeff0.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFfeCoeff0, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_1:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txFfeCoeff1)) {
          attr_list[i].value.s32list.count = portSerdes.txFfeCoeff1.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFfeCoeff1, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_2:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txFfeCoeff2)) {
          attr_list[i].value.s32list.count = portSerdes.txFfeCoeff2.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFfeCoeff2, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_3:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txFfeCoeff3)) {
          attr_list[i].value.s32list.count = portSerdes.txFfeCoeff3.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFfeCoeff3, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_FFE_COEFF_4:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txFfeCoeff4)) {
          attr_list[i].value.s32list.count = portSerdes.txFfeCoeff4.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txFfeCoeff4, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_DRIVER_SWING:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txDriverSwing)) {
          attr_list[i].value.s32list.count = portSerdes.txDriverSwing.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txDriverSwing, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_TX_LDO_BYPASS:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txLdoBypass)) {
          attr_list[i].value.s32list.count = portSerdes.txLdoBypass.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txLdoBypass, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STRT:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgBoost1Start)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgBoost1Start.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxInstgBoost1Start, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STEP:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgBoost1Step)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgBoost1Step.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgBoost1Step, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST1_STOP:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgBoost1Stop)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgBoost1Stop.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgBoost1Stop, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STRT:
        if (!checkListSize(
                attr_list[i].value.s32list,
                portSerdes.rxInstgBoost2OrHrStart)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgBoost2OrHrStart.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxInstgBoost2OrHrStart, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STEP:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgBoost2OrHrStep)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgBoost2OrHrStep.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxInstgBoost2OrHrStep, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_BOOST2_OR_HR_STOP:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgBoost2OrHrStop)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgBoost2OrHrStop.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxInstgBoost2OrHrStop, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_START_1P7:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgC1Start1p7)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgC1Start1p7.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgC1Start1p7, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_STEP_1P7:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgC1Step1p7)) {
          attr_list[i].value.s32list.count = portSerdes.rxInstgC1Step1p7.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgC1Step1p7, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_C1_STOP_1P7:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgC1Stop1p7)) {
          attr_list[i].value.s32list.count = portSerdes.rxInstgC1Stop1p7.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgC1Stop1p7, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_START_1P7:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgDfeStart1p7)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgDfeStart1p7.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxInstgDfeStart1p7, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_STEP_1P7:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgDfeStep1p7)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgDfeStep1p7.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgDfeStep1p7, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_DFE_STOP_1P7:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgDfeStop1p7)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgDfeStop1p7.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgDfeStop1p7, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_SCAN_USE_SR_SETTINGS:
        if (!checkListSize(
                attr_list[i].value.s32list,
                portSerdes.rxInstgScanUseSrSettings)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgScanUseSrSettings.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxInstgScanUseSrSettings, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_CFG_OV_EN:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxCdrCfgOvEn)) {
          attr_list[i].value.s32list.count = portSerdes.rxCdrCfgOvEn.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxCdrCfgOvEn, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_1ST_ORD_STEP_OV_VAL:
        if (!checkListSize(
                attr_list[i].value.s32list,
                portSerdes.rxCdrTdet1stOrdStepOvVal)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxCdrTdet1stOrdStepOvVal.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxCdrTdet1stOrdStepOvVal, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_2ND_ORD_STEP_OV_VAL:
        if (!checkListSize(
                attr_list[i].value.s32list,
                portSerdes.rxCdrTdet2ndOrdStepOvVal)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxCdrTdet2ndOrdStepOvVal.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxCdrTdet2ndOrdStepOvVal, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_CDR_TDET_FINE_STEP_OV_VAL:
        if (!checkListSize(
                attr_list[i].value.s32list,
                portSerdes.rxCdrTdetFineStepOvVal)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxCdrTdetFineStepOvVal.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxCdrTdetFineStepOvVal, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_LDO_BYPASS:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxLdoBypass)) {
          attr_list[i].value.s32list.count = portSerdes.rxLdoBypass.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxLdoBypass, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_DIFF_ENCODER_EN:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxDiffEncoderEn)) {
          attr_list[i].value.s32list.count = portSerdes.rxDiffEncoderEn.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxDiffEncoderEn, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_INSTG_ENABLE_SCAN:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxInstgEnableScan)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxInstgEnableScan.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxInstgEnableScan, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_FFE_LENGTH_BITMAP:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxFfeLengthBitmap)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxFfeLengthBitmap.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxFfeLengthBitmap, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_EXT_RX_FFE_LMS_DYNAMIC_GATING_EN:
        if (!checkListSize(
                attr_list[i].value.s32list,
                portSerdes.rxFfeLmsDynamicGatingEn)) {
          attr_list[i].value.s32list.count =
              portSerdes.rxFfeLmsDynamicGatingEn.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(
            portSerdes.rxFfeLmsDynamicGatingEn, attr_list[i].value.s32list);
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      case SAI_PORT_SERDES_ATTR_TX_PRECODING:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.txPrecoding)) {
          attr_list[i].value.s32list.count = portSerdes.txPrecoding.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.txPrecoding, attr_list[i].value.s32list);
        break;
      case SAI_PORT_SERDES_ATTR_RX_PRECODING:
        if (!checkListSize(
                attr_list[i].value.s32list, portSerdes.rxPrecoding)) {
          attr_list[i].value.s32list.count = portSerdes.rxPrecoding.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        copyVecToList(portSerdes.rxPrecoding, attr_list[i].value.s32list);
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
      case SAI_PORT_SERDES_ATTR_CUSTOM_COLLECTION:
        if (portSerdes.serdesCustomCollection.size() >
            attr_list[i].value.json.json.count) {
          attr_list[i].value.json.json.count =
              portSerdes.serdesCustomCollection.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        std::copy(
            portSerdes.serdesCustomCollection.begin(),
            portSerdes.serdesCustomCollection.end(),
            attr_list[i].value.json.json.list);
        attr_list[i].value.json.json.count =
            portSerdes.serdesCustomCollection.size();
        break;
#endif
      default:
        return SAI_STATUS_NOT_IMPLEMENTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_port_connector_fn(
    sai_object_id_t* port_connector_id,
    sai_object_id_t /*switch_id*/,
    uint32_t count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  sai_object_id_t linePort, sysPort;

  for (auto i = 0; i < count; i++) {
    switch (attr_list[i].id) {
      case SAI_PORT_CONNECTOR_ATTR_LINE_SIDE_PORT_ID:
        linePort = attr_list[i].value.oid;
        break;
      case SAI_PORT_CONNECTOR_ATTR_SYSTEM_SIDE_PORT_ID:
        sysPort = attr_list[i].value.oid;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  *port_connector_id = fs->portConnectorManager.create(linePort, sysPort);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_port_connector_fn(sai_object_id_t port_connector_id) {
  auto fs = FakeSai::getInstance();
  fs->portConnectorManager.remove(port_connector_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_port_connector_attribute_fn(
    sai_object_id_t port_connector_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& portConnector = fs->portConnectorManager.get(port_connector_id);

  switch (attr->id) {
    case SAI_PORT_CONNECTOR_ATTR_LINE_SIDE_PORT_ID:
      portConnector.linePort = attr->value.oid;
      break;
    case SAI_PORT_CONNECTOR_ATTR_SYSTEM_SIDE_PORT_ID:
      portConnector.systemPort = attr->value.oid;
      break;
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_port_connector_attribute_fn(
    sai_object_id_t port_connector_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& portConnector = fs->portConnectorManager.get(port_connector_id);

  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_PORT_CONNECTOR_ATTR_LINE_SIDE_PORT_ID:
        attr_list[i].value.oid = portConnector.linePort;
        break;
      case SAI_PORT_CONNECTOR_ATTR_SYSTEM_SIDE_PORT_ID:
        attr_list[i].value.oid = portConnector.systemPort;
        break;
      default:
        return SAI_STATUS_NOT_IMPLEMENTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
sai_status_t create_port_llr_profile_fn(
    sai_object_id_t* port_llr_profile_id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  sai_uint32_t outstandingFramesMax = 0;
  sai_uint32_t outstandingBytesMax = 0;
  sai_uint32_t replayTimerMax = 0;
  sai_uint8_t replayCountMax = 0;
  sai_uint32_t pcsLostTimeout = 0;
  sai_uint32_t dataAgeTimeout = 0;
  sai_int32_t initLlrFrameAction = 0;
  sai_int32_t flushLlrFrameAction = 0;
  bool reInitOnFlush = false;
  sai_uint16_t ctlosTargetSpacing = 0;
  for (uint32_t i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_FRAMES_MAX:
        outstandingFramesMax = attr_list[i].value.u32;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_BYTES_MAX:
        outstandingBytesMax = attr_list[i].value.u32;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_REPLAY_TIMER_MAX:
        replayTimerMax = attr_list[i].value.u32;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_REPLAY_COUNT_MAX:
        replayCountMax = attr_list[i].value.u8;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_PCS_LOST_TIMEOUT:
        pcsLostTimeout = attr_list[i].value.u32;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_DATA_AGE_TIMEOUT:
        dataAgeTimeout = attr_list[i].value.u32;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_INIT_LLR_FRAME_ACTION:
        initLlrFrameAction = attr_list[i].value.s32;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_FLUSH_LLR_FRAME_ACTION:
        flushLlrFrameAction = attr_list[i].value.s32;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_RE_INIT_ON_FLUSH:
        reInitOnFlush = attr_list[i].value.booldata;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_CTLOS_TARGET_SPACING:
        ctlosTargetSpacing = attr_list[i].value.u16;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  *port_llr_profile_id = fs->portLlrProfileManager.create(
      outstandingFramesMax,
      outstandingBytesMax,
      replayTimerMax,
      replayCountMax,
      pcsLostTimeout,
      dataAgeTimeout,
      initLlrFrameAction,
      flushLlrFrameAction,
      reInitOnFlush,
      ctlosTargetSpacing);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_port_llr_profile_fn(sai_object_id_t port_llr_profile_id) {
  auto fs = FakeSai::getInstance();
  fs->portLlrProfileManager.remove(port_llr_profile_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_port_llr_profile_attribute_fn(
    sai_object_id_t port_llr_profile_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& profile = fs->portLlrProfileManager.get(port_llr_profile_id);
  switch (attr->id) {
    case SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_FRAMES_MAX:
      profile.outstandingFramesMax = attr->value.u32;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_BYTES_MAX:
      profile.outstandingBytesMax = attr->value.u32;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_REPLAY_TIMER_MAX:
      profile.replayTimerMax = attr->value.u32;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_REPLAY_COUNT_MAX:
      profile.replayCountMax = attr->value.u8;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_PCS_LOST_TIMEOUT:
      profile.pcsLostTimeout = attr->value.u32;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_DATA_AGE_TIMEOUT:
      profile.dataAgeTimeout = attr->value.u32;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_INIT_LLR_FRAME_ACTION:
      profile.initLlrFrameAction = attr->value.s32;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_FLUSH_LLR_FRAME_ACTION:
      profile.flushLlrFrameAction = attr->value.s32;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_RE_INIT_ON_FLUSH:
      profile.reInitOnFlush = attr->value.booldata;
      break;
    case SAI_PORT_LLR_PROFILE_ATTR_CTLOS_TARGET_SPACING:
      profile.ctlosTargetSpacing = attr->value.u16;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_port_llr_profile_attribute_fn(
    sai_object_id_t port_llr_profile_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& profile = fs->portLlrProfileManager.get(port_llr_profile_id);
  for (uint32_t i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_FRAMES_MAX:
        attr_list[i].value.u32 = profile.outstandingFramesMax;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_BYTES_MAX:
        attr_list[i].value.u32 = profile.outstandingBytesMax;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_REPLAY_TIMER_MAX:
        attr_list[i].value.u32 = profile.replayTimerMax;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_REPLAY_COUNT_MAX:
        attr_list[i].value.u8 = profile.replayCountMax;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_PCS_LOST_TIMEOUT:
        attr_list[i].value.u32 = profile.pcsLostTimeout;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_DATA_AGE_TIMEOUT:
        attr_list[i].value.u32 = profile.dataAgeTimeout;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_INIT_LLR_FRAME_ACTION:
        attr_list[i].value.s32 = profile.initLlrFrameAction;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_FLUSH_LLR_FRAME_ACTION:
        attr_list[i].value.s32 = profile.flushLlrFrameAction;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_RE_INIT_ON_FLUSH:
        attr_list[i].value.booldata = profile.reInitOnFlush;
        break;
      case SAI_PORT_LLR_PROFILE_ATTR_CTLOS_TARGET_SPACING:
        attr_list[i].value.u16 = profile.ctlosTargetSpacing;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}
#endif

namespace facebook::fboss {

static sai_port_api_t _port_api;

void populate_port_api(sai_port_api_t** port_api) {
  _port_api.create_port = &create_port_fn;
  _port_api.remove_port = &remove_port_fn;
  _port_api.set_port_attribute = &set_port_attribute_fn;
  _port_api.get_port_attribute = &get_port_attribute_fn;
  _port_api.get_port_stats = &get_port_stats_fn;
  _port_api.get_port_stats_ext = &get_port_stats_ext_fn;
  _port_api.clear_port_stats = &clear_port_stats_fn;
  _port_api.create_port_serdes = &create_port_serdes_fn;
  _port_api.remove_port_serdes = &remove_port_serdes_fn;
  _port_api.set_port_serdes_attribute = &set_port_serdes_attribute_fn;
  _port_api.get_port_serdes_attribute = &get_port_serdes_attribute_fn;
  _port_api.create_port_connector = &create_port_connector_fn;
  _port_api.remove_port_connector = &remove_port_connector_fn;
  _port_api.set_port_connector_attribute = &set_port_connector_attribute_fn;
  _port_api.get_port_connector_attribute = &get_port_connector_attribute_fn;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  _port_api.create_port_llr_profile = &create_port_llr_profile_fn;
  _port_api.remove_port_llr_profile = &remove_port_llr_profile_fn;
  _port_api.set_port_llr_profile_attribute = &set_port_llr_profile_attribute_fn;
  _port_api.get_port_llr_profile_attribute = &get_port_llr_profile_attribute_fn;
#endif
  *port_api = &_port_api;
}

} // namespace facebook::fboss
