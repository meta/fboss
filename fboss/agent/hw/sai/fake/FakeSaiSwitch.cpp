/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiSwitch.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

using facebook::fboss::FakeSai;

namespace {
static constexpr uint16_t kDefaultVlanId = 4095;
static constexpr uint64_t kDefaultVirtualRouterId = 0;
static constexpr uint64_t kDefault1QBridgeId = 0;
static constexpr uint32_t kMaxPortUnicastQueues = 8;
static constexpr uint32_t kMaxPortMulticastQueues = 8;
static constexpr uint32_t kMaxPortQueues =
    kMaxPortUnicastQueues + kMaxPortMulticastQueues;
static constexpr uint32_t kMaxCpuQueues = 8;
static constexpr sai_object_id_t kEcmpHashId = 1234;
static constexpr sai_object_id_t kLagHashId = 1234;
static constexpr uint32_t kDefaultAclEntryMinimumPriority = 0;
static constexpr uint32_t kDefaultAclEntryMaximumPriority =
    std::numeric_limits<uint32_t>::max();

static constexpr uint32_t kDefaultFdbDstUserMetaDataRangeMin = 0;
static constexpr uint32_t kDefaultFdbDstUserMetaDataRangeMax =
    std::numeric_limits<uint32_t>::max();
static constexpr uint32_t kDefaultRouteDstUserMetaDataRangeMin = 0;
static constexpr uint32_t kDefaultRouteDstUserMetaDataRangeMax =
    std::numeric_limits<uint32_t>::max();
static constexpr uint32_t kDefaultNeighborDstUserMetaDataRangeMin = 0;
static constexpr uint32_t kDefaultNeighborDstUserMetaDataRangeMax =
    std::numeric_limits<uint32_t>::max();
static constexpr uint32_t kDefaultPortUserMetaDataRangeMin = 0;
static constexpr uint32_t kDefaultPortUserMetaDataRangeMax = 63;

sai_status_t fillDropTypeList(
    const std::vector<sai_int32_t>& dropTypes,
    sai_s32_list_t* list) {
  if (dropTypes.size() > list->count) {
    list->count = static_cast<uint32_t>(dropTypes.size());
    return SAI_STATUS_BUFFER_OVERFLOW;
  }
  list->count = static_cast<uint32_t>(dropTypes.size());
  for (size_t i = 0; i < dropTypes.size(); ++i) {
    list->list[i] = dropTypes[i];
  }
  return SAI_STATUS_SUCCESS;
}

} // namespace

sai_status_t facebook::fboss::FakeSwitch::setLed(const sai_attribute_t* attr) {
  switch (attr->id) {
    case SAI_SWITCH_ATTR_EXT_FAKE_LED: {
      if (attr->value.u32list.count != 4) {
        return SAI_STATUS_INVALID_PARAMETER;
      }
      auto ledId = attr->value.u32list.list[0];
      auto ledState = ledState_.find(ledId);
      if (ledState == ledState_.end()) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
      }
      auto pgm = attr->value.u32list.list[1];
      if (pgm) {
        auto offset = attr->value.u32list.list[2];
        if (offset > 255) {
          return SAI_STATUS_INVALID_ATTR_VALUE_0 + 2;
        }
        ledState->second.program[offset] =
            static_cast<sai_uint8_t>(attr->value.u32list.list[3]);
      } else {
        auto index = attr->value.u32list.list[2];
        ledState->second.data[index] = attr->value.u32list.list[3];
      }
    } break;
    case SAI_SWITCH_ATTR_EXT_FAKE_LED_RESET: {
      if (attr->value.u32list.count != 2) {
        return SAI_STATUS_INVALID_PARAMETER;
      }
      auto ledId = attr->value.u32list.list[0];
      auto ledState = ledState_.find(ledId);
      if (ledState == ledState_.end()) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
      }
      ledState->second.reset = bool(attr->value.u32list.list[1]);
    } break;
    default:
      return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t facebook::fboss::FakeSwitch::getLed(sai_attribute_t* attr) const {
  switch (attr->id) {
    case SAI_SWITCH_ATTR_EXT_FAKE_LED: {
      if (attr->value.u32list.count != 4) {
        attr->value.u32list.count = 4;
        return SAI_STATUS_BUFFER_OVERFLOW;
      }
      auto ledId = attr->value.u32list.list[0];
      auto ledState = ledState_.find(ledId);
      if (ledState == ledState_.end()) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
      }
      auto pgm = attr->value.u32list.list[1];
      if (pgm) {
        auto offset = attr->value.u32list.list[2];
        if (offset > 255) {
          return SAI_STATUS_INVALID_ATTR_VALUE_0;
        }
        attr->value.u32list.list[3] = ledState->second.program[offset];
      } else {
        auto index = attr->value.u32list.list[2];
        auto dataIter = ledState->second.data.find(index);
        attr->value.u32list.list[3] =
            (dataIter != ledState->second.data.end()) ? dataIter->second : 0;
      }
    } break;
    case SAI_SWITCH_ATTR_EXT_FAKE_LED_RESET: {
      if (attr->value.u32list.count != 2) {
        attr->value.u32list.count = 2;
        return SAI_STATUS_BUFFER_OVERFLOW;
      }
      auto ledId = attr->value.u32list.list[0];
      auto ledState = ledState_.find(ledId);
      if (ledState == ledState_.end()) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
      }
      attr->value.u32list.list[1] = ((ledState->second.reset) ? 1 : 0);
    } break;
    default:
      return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
  }
  return SAI_STATUS_SUCCESS;
}

#if 0
sai_status_t facebook::fboss::FakeSwitch::setPtpMode(
    const sai_attribute_t* attr) {
  return SAI_STATUS_SUCCESS;
}
sai_status_t facebook::fboss::FakeSwitch::getPtpMode(
    sai_attribute_t* attr) const {
  return SAI_STATUS_SUCCESS;
}

sai_status_t
facebook::fboss::FakeSwitch::setRegFatalSwitchAsicSdkHealthCategory(
    const sai_attribute_t* attr) {
  return SAI_STATUS_SUCCESS;
}
sai_status_t
facebook::fboss::FakeSwitch::getRegFatalSwitchAsicSdkHealthCategory(
    sai_attribute_t* attr) const {
  return SAI_STATUS_SUCCESS;
}

sai_status_t
facebook::fboss::FakeSwitch::setRegNoticeSwitchAsicSdkHealthCategory(
    const sai_attribute_t* attr) {
  return SAI_STATUS_SUCCESS;
}
sai_status_t
facebook::fboss::FakeSwitch::getRegNoticeSwitchAsicSdkHealthCategory(
    sai_attribute_t* attr) const {
  return SAI_STATUS_SUCCESS;
}
#endif

sai_status_t set_switch_attribute_fn(
    sai_object_id_t switch_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& sw = fs->switchManager.get(switch_id);
  sai_status_t res;
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  res = SAI_STATUS_SUCCESS;
  switch (attr->id) {
    case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
      sw.setSrcMac(attr->value.mac);
      break;
    case SAI_SWITCH_ATTR_INIT_SWITCH:
      sw.setInitStatus(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO:
      sw.setHwInfo(
          std::vector<int8_t>(
              attr->value.s8list.list,
              attr->value.s8list.list + attr->value.s8list.count));
      break;
    case SAI_SWITCH_ATTR_SWITCH_SHELL_ENABLE:
      sw.setShellStatus(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_ECMP_HASH_IPV4:
      sw.setEcmpHashV4Id(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_ECMP_HASH_IPV6:
      sw.setEcmpHashV6Id(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_LAG_HASH_IPV4:
      sw.setLagHashV4Id(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_LAG_HASH_IPV6:
      sw.setLagHashV6Id(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
      sw.setEcmpSeed(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
      sw.setLagSeed(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
      sw.setEcmpAlgorithm(attr->value.s32);
      break;
    case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
      sw.setLagAlgorithm(attr->value.s32);
      break;
    case SAI_SWITCH_ATTR_QOS_DSCP_TO_TC_MAP:
      sw.setDscpToTc(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_QOS_TC_TO_QUEUE_MAP:
      sw.setTcToQueue(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_QOS_MPLS_EXP_TO_TC_MAP:
      sw.setExpToTc(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_QOS_TC_AND_COLOR_TO_MPLS_EXP_MAP:
      sw.setTcToExp(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_RESTART_WARM:
      sw.setRestartWarm(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_PRE_SHUTDOWN:
      break;
    case SAI_SWITCH_ATTR_FDB_AGING_TIME:
      sw.setMacAgingTime(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_ECN_ECT_THRESHOLD_ENABLE:
      sw.setUseEcnThresholds(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_INGRESS_ACL:
      sw.setIngressAcl(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_ARS_PROFILE:
      sw.setArsProfile(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_EXT_FAKE_LED:
    case SAI_SWITCH_ATTR_EXT_FAKE_LED_RESET:
      return sw.setLed(attr);
    case SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY:
    case SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY:
    case SAI_SWITCH_ATTR_PACKET_EVENT_NOTIFY:
    case SAI_SWITCH_ATTR_TAM_EVENT_NOTIFY:
    case SAI_SWITCH_ATTR_QUEUE_PFC_DEADLOCK_NOTIFY:
    case SAI_SWITCH_ATTR_SWITCH_ASIC_SDK_HEALTH_EVENT_NOTIFY:
      // No callback implementation in SAI
      break;
    case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
      sw.setCounterRefreshInterval(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_FIRMWARE_PATH_NAME:
      sw.setFirmwarePathName(
          std::vector<int8_t>(
              attr->value.s8list.list,
              attr->value.s8list.list + attr->value.s8list.count));
      break;
    case SAI_SWITCH_ATTR_FIRMWARE_LOAD_METHOD:
      sw.setFirmwareLoadMethod(attr->value.s32);
      break;
    case SAI_SWITCH_ATTR_FIRMWARE_LOAD_TYPE:
      sw.setFirmwareLoadType(attr->value.s32);
      break;
    case SAI_SWITCH_ATTR_WARM_RECOVER:
      sw.setWarmRecover(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_HARDWARE_ACCESS_BUS:
      sw.setHardwareAccessBus(attr->value.s32);
      break;
    case SAI_SWITCH_ATTR_PLATFROM_CONTEXT:
      sw.setPlatformContext(attr->value.u64);
      break;
    case SAI_SWITCH_ATTR_SWITCH_PROFILE_ID:
      sw.setProfileId(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_SWITCH_ID:
      sw.setSwitchId(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_MAX_SYSTEM_CORES:
      sw.setMaxSystemCores(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_SYSTEM_PORT_CONFIG_LIST:
      sw.setSysPortConfigList(
          std::vector<sai_system_port_config_t>(
              attr->value.sysportconfiglist.list,
              attr->value.sysportconfiglist.list +
                  attr->value.sysportconfiglist.count));
      break;
    case SAI_SWITCH_ATTR_TYPE:
      sw.setSwitchType(attr->value.u32);
      break;
    case SAI_SWITCH_ATTR_REGISTER_READ:
      sw.setReadFn(attr->value.ptr);
      break;
    case SAI_SWITCH_ATTR_REGISTER_WRITE:
      sw.setWriteFn(attr->value.ptr);
      break;
    case SAI_SWITCH_ATTR_EXT_FAKE_HW_ECC_ERROR_INITIATE:
      // no-op
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    case SAI_SWITCH_ATTR_ECMP_MEMBER_COUNT:
      sw.setEcmpMemberCount(attr->value.u32);
      break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_SWITCH_ATTR_CREDIT_WD:
      sw.setCreditWatchdogEnable(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_CREDIT_WD_TIMER:
      sw.setCreditWatchdogMs(attr->value.u32);
      break;
#endif
    case SAI_SWITCH_ATTR_PFC_DLR_PACKET_ACTION:
      sw.setPfcDlrPacketAction(
          static_cast<sai_packet_action_t>(attr->value.s32));
      break;
    case SAI_SWITCH_ATTR_TAM_OBJECT_ID:
      sw.setTamObjectId(attr->value.oid);
      break;
    case SAI_SWITCH_ATTR_PORT_PTP_MODE:
      // TODO: implement if required
      break;
    case SAI_SWITCH_ATTR_REG_FATAL_SWITCH_ASIC_SDK_HEALTH_CATEGORY:
      // TODO: implement if required
      break;
    case SAI_SWITCH_ATTR_REG_NOTICE_SWITCH_ASIC_SDK_HEALTH_CATEGORY:
      // TODO: implement if required
      break;
    case SAI_SWITCH_ATTR_PFC_MONITOR_ENABLE:
      sw.setPfcMonitorEnable(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_CABLE_PROPAGATION_DELAY_MEASUREMENT:
      sw.setCablePropagationDelayMeasurement(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_PORT_CL72_RETRY_ENABLE:
      sw.setPortCl72RetryEnable(attr->value.booldata);
      break;
    case SAI_SWITCH_ATTR_ACL_DROP_SNOOP_DISABLED:
      sw.setAclDropSnoopDisabled(attr->value.booldata);
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    case SAI_SWITCH_ATTR_LINK_UP_DEBOUNCE_TIMEOUT:
      sw.setLinkUpDebounceTimeout(attr->value.u32);
      break;
#endif
    case SAI_SWITCH_ATTR_SWITCHING_MODE:
      sw.setSwitchingMode(attr->value.s32);
      break;
    default:
      res = SAI_STATUS_INVALID_PARAMETER;
      break;
  }
  return res;
}

sai_status_t create_switch_fn(
    sai_object_id_t* switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  *switch_id = fs->switchManager.create();
  for (int i = 0; i < attr_count; ++i) {
    set_switch_attribute_fn(*switch_id, &attr_list[i]);
  }
  fs->switchManager.get(*switch_id)
      .setDefaultVlanId(fs->vlanManager.create(kDefaultVlanId));
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_switch_fn(sai_object_id_t switch_id) {
  auto fs = FakeSai::getInstance();
  fs->switchManager.remove(switch_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_switch_attribute_fn(
    sai_object_id_t switch_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& sw = fs->switchManager.get(switch_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID:
        attr[i].value.oid = kDefault1QBridgeId;
        break;
      case SAI_SWITCH_ATTR_DEFAULT_VIRTUAL_ROUTER_ID:
        attr[i].value.oid = kDefaultVirtualRouterId;
        break;
      case SAI_SWITCH_ATTR_DEFAULT_VLAN_ID:
        attr[i].value.oid = sw.getDefaultVlanId();
        break;
      case SAI_SWITCH_ATTR_CPU_PORT:
        attr[i].value.oid = fs->getCpuPort();
        break;
      case SAI_SWITCH_ATTR_PORT_NUMBER:
        attr[i].value.u32 = fs->portManager.map().size();
        break;
      case SAI_SWITCH_ATTR_PORT_CONNECTOR_LIST:
      case SAI_SWITCH_ATTR_PORT_LIST: {
        if (fs->portManager.map().size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = fs->portManager.map().size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr[i].value.objlist.count = fs->portManager.map().size();
        int j = 0;
        for (const auto& p : fs->portManager.map()) {
          attr[i].value.objlist.list[j++] = p.first;
        }
      } break;
      case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
        std::copy_n(sw.srcMac().bytes(), 6, std::begin(attr[i].value.mac));
        break;
      case SAI_SWITCH_ATTR_INIT_SWITCH:
        attr[i].value.booldata = sw.isInitialized();
        break;
      case SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO:
        attr[i].value.s8list.count = sw.hwInfo().size();
        attr[i].value.s8list.list = sw.hwInfoData();
        break;
      case SAI_SWITCH_ATTR_SWITCH_SHELL_ENABLE:
        attr[i].value.booldata = sw.isShellEnabled();
        break;
      case SAI_SWITCH_ATTR_NUMBER_OF_UNICAST_QUEUES:
        attr[i].value.u32 = kMaxPortUnicastQueues;
        break;
      case SAI_SWITCH_ATTR_NUMBER_OF_MULTICAST_QUEUES:
        attr[i].value.u32 = kMaxPortMulticastQueues;
        break;
      case SAI_SWITCH_ATTR_NUMBER_OF_QUEUES:
        attr[i].value.u32 = kMaxPortQueues;
        break;
      case SAI_SWITCH_ATTR_NUMBER_OF_CPU_QUEUES:
        attr[i].value.u32 = kMaxCpuQueues;
        break;
      case SAI_SWITCH_ATTR_ECMP_HASH:
        attr[i].value.oid = kEcmpHashId;
        break;
      case SAI_SWITCH_ATTR_LAG_HASH:
        attr[i].value.oid = kLagHashId;
        break;
      case SAI_SWITCH_ATTR_ECMP_HASH_IPV4:
        attr[i].value.oid = sw.ecmpHashV4Id();
        break;
      case SAI_SWITCH_ATTR_ECMP_HASH_IPV6:
        attr[i].value.oid = sw.ecmpHashV6Id();
        break;
      case SAI_SWITCH_ATTR_LAG_HASH_IPV4:
        attr[i].value.oid = sw.lagHashV4Id();
        break;
      case SAI_SWITCH_ATTR_LAG_HASH_IPV6:
        attr[i].value.oid = sw.lagHashV6Id();
        break;
      case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
        attr[i].value.u32 = sw.ecmpSeed();
        break;
      case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
        attr[i].value.u32 = sw.lagSeed();
        break;
      case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
        attr[i].value.s32 = sw.ecmpAlgorithm();
        break;
      case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
        attr[i].value.s32 = sw.lagAlgorithm();
        break;
      case SAI_SWITCH_ATTR_RESTART_WARM:
        attr[i].value.booldata = sw.restartWarm();
        break;
      case SAI_SWITCH_ATTR_QOS_DSCP_TO_TC_MAP:
        attr[i].value.oid = sw.dscpToTc();
        break;
      case SAI_SWITCH_ATTR_QOS_TC_TO_QUEUE_MAP:
        attr[i].value.oid = sw.tcToQueue();
        break;
      case SAI_SWITCH_ATTR_QOS_MPLS_EXP_TO_TC_MAP:
        attr[i].value.oid = sw.expToTc();
        break;
      case SAI_SWITCH_ATTR_QOS_TC_AND_COLOR_TO_MPLS_EXP_MAP:
        attr[i].value.oid = sw.tcToExp();
        break;
      case SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY:
        attr[i].value.oid = kDefaultAclEntryMinimumPriority;
        break;
      case SAI_SWITCH_ATTR_ACL_ENTRY_MAXIMUM_PRIORITY:
        attr[i].value.oid = kDefaultAclEntryMaximumPriority;
        break;
      case SAI_SWITCH_ATTR_FDB_AGING_TIME:
        attr[i].value.u32 = sw.getMacAgingTime();
        break;
      case SAI_SWITCH_ATTR_FDB_DST_USER_META_DATA_RANGE:
        attr[i].value.u32range.min = kDefaultFdbDstUserMetaDataRangeMin;
        attr[i].value.u32range.max = kDefaultFdbDstUserMetaDataRangeMax;
        break;
      case SAI_SWITCH_ATTR_ROUTE_DST_USER_META_DATA_RANGE:
        attr[i].value.u32range.min = kDefaultRouteDstUserMetaDataRangeMin;
        attr[i].value.u32range.max = kDefaultRouteDstUserMetaDataRangeMax;
        break;
      case SAI_SWITCH_ATTR_NEIGHBOR_DST_USER_META_DATA_RANGE:
        attr[i].value.u32range.min = kDefaultNeighborDstUserMetaDataRangeMin;
        attr[i].value.u32range.max = kDefaultNeighborDstUserMetaDataRangeMax;
        break;
      case SAI_SWITCH_ATTR_PORT_USER_META_DATA_RANGE:
        attr[i].value.u32range.min = kDefaultPortUserMetaDataRangeMin;
        attr[i].value.u32range.max = kDefaultPortUserMetaDataRangeMax;
        break;
      case SAI_SWITCH_ATTR_ECN_ECT_THRESHOLD_ENABLE:
        attr[i].value.booldata = sw.getUseEcnThresholds();
        break;
      case SAI_SWITCH_ATTR_EXT_FAKE_LED:
      case SAI_SWITCH_ATTR_EXT_FAKE_LED_RESET:
        return sw.getLed(attr);
      case SAI_SWITCH_ATTR_DEFAULT_EGRESS_BUFFER_POOL_SHARED_SIZE:
        attr[i].value.u32 = 1'000'000;
        break;
      case SAI_SWITCH_ATTR_AVAILABLE_IPV4_ROUTE_ENTRY:
      case SAI_SWITCH_ATTR_AVAILABLE_IPV6_ROUTE_ENTRY:
      case SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEXTHOP_ENTRY:
      case SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEXTHOP_ENTRY:
      case SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_ENTRY:
      case SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_MEMBER_ENTRY:
      case SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEIGHBOR_ENTRY:
      case SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEIGHBOR_ENTRY:
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      case SAI_SWITCH_ATTR_AVAILABLE_MY_SID_ENTRY:
#endif
        // Why not
        attr[i].value.u32 = 1'000'000;
        break;
      case SAI_SWITCH_ATTR_INGRESS_ACL:
        attr[i].value.oid = sw.getIngressAcl();
        break;
      case SAI_SWITCH_ATTR_ARS_PROFILE:
        attr[i].value.oid = sw.getArsProfile();
        break;
      case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
        attr[i].value.u32 = sw.getCounterRefreshInterval();
        break;
      case SAI_SWITCH_ATTR_FIRMWARE_PATH_NAME:
        attr[i].value.s8list.count = sw.firmwarePath().size();
        attr[i].value.s8list.list = sw.firmwarePathData();
        break;
      case SAI_SWITCH_ATTR_FIRMWARE_LOAD_METHOD:
        attr[i].value.s32 = sw.firmwareLoadMethod();
        break;
      case SAI_SWITCH_ATTR_FIRMWARE_LOAD_TYPE:
        attr[i].value.s32 = sw.firmwareLoadType();
        break;
      case SAI_SWITCH_ATTR_WARM_RECOVER:
        attr[i].value.booldata = sw.warmRecover();
        break;
      case SAI_SWITCH_ATTR_HARDWARE_ACCESS_BUS:
        attr[i].value.s32 = sw.hardwareAccessBus();
        break;
      case SAI_SWITCH_ATTR_PLATFROM_CONTEXT:
        attr[i].value.u64 = sw.platformContext();
        break;
      case SAI_SWITCH_ATTR_SWITCH_PROFILE_ID:
        attr[i].value.u32 = sw.profileId();
        break;
      case SAI_SWITCH_ATTR_FIRMWARE_STATUS:
        attr[i].value.booldata = true;
        break;
      case SAI_SWITCH_ATTR_FIRMWARE_MAJOR_VERSION:
      case SAI_SWITCH_ATTR_FIRMWARE_MINOR_VERSION:
        attr[i].value.u32 = 0;
        break;
      case SAI_SWITCH_ATTR_SWITCH_ID:
        attr[i].value.u32 = sw.switchId();
        break;
      case SAI_SWITCH_ATTR_MAX_SYSTEM_CORES:
        attr[i].value.u32 = sw.maxSystemCores();
        break;
      case SAI_SWITCH_ATTR_SYSTEM_PORT_CONFIG_LIST:
        attr[i].value.sysportconfiglist.count = sw.sysPortConfigList().size();
        attr[i].value.sysportconfiglist.list = sw.sysPortConfigListData();
        break;
      case SAI_SWITCH_ATTR_TYPE:
        attr[i].value.u32 = sw.switchType();
        break;
      case SAI_SWITCH_ATTR_REGISTER_READ:
        attr[i].value.ptr = sw.readFn();
        break;
      case SAI_SWITCH_ATTR_REGISTER_WRITE:
        attr[i].value.ptr = sw.writeFn();
        break;
      case SAI_SWITCH_ATTR_EXT_FAKE_HW_ECC_ERROR_INITIATE:
        // noop;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      case SAI_SWITCH_ATTR_MAX_ECMP_MEMBER_COUNT:
        attr[i].value.u32 = sw.getMaxEcmpMemberCount();
        break;
      case SAI_SWITCH_ATTR_ECMP_MEMBER_COUNT:
        attr[i].value.u32 = sw.getEcmpMemberCount();
        break;
#endif
      case SAI_SWITCH_ATTR_NUMBER_OF_FABRIC_PORTS:
        attr[i].value.u32 = 0;
        break;
      case SAI_SWITCH_ATTR_FABRIC_PORT_LIST:
        attr[i].value.objlist.count = 0;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      case SAI_SWITCH_ATTR_CREDIT_WD:
        attr[i].value.booldata = sw.getCreditWatchdogEnable();
        break;
      case SAI_SWITCH_ATTR_CREDIT_WD_TIMER:
        attr[i].value.u32 = sw.getCreditWatchdogMs();
        break;
#endif
      case SAI_SWITCH_ATTR_PFC_DLR_PACKET_ACTION:
        attr[i].value.s32 = sw.getPfcDlrPacketAction();
        break;
      case SAI_SWITCH_ATTR_TAM_OBJECT_ID:
        attr[i].value.oid = sw.getTamObjectId();
        break;
      case SAI_SWITCH_ATTR_PORT_PTP_MODE:
        attr[i].value.s32 = 0;
        break;
      case SAI_SWITCH_ATTR_REG_FATAL_SWITCH_ASIC_SDK_HEALTH_CATEGORY:
      case SAI_SWITCH_ATTR_REG_NOTICE_SWITCH_ASIC_SDK_HEALTH_CATEGORY:
        attr[i].value.s32list.count = 0;
        break;
      case SAI_SWITCH_ATTR_PFC_MONITOR_ENABLE:
        attr[i].value.booldata = sw.getPfcMonitorEnable();
        break;
      case SAI_SWITCH_ATTR_CABLE_PROPAGATION_DELAY_MEASUREMENT:
        attr[i].value.booldata = sw.getCablePropagationDelayMeasurement();
        break;
      case SAI_SWITCH_ATTR_PORT_CL72_RETRY_ENABLE:
        attr[i].value.booldata = sw.getPortCl72RetryEnable();
        break;
      case SAI_SWITCH_ATTR_ACL_DROP_SNOOP_DISABLED:
        attr[i].value.booldata = sw.getAclDropSnoopDisabled();
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      case SAI_SWITCH_ATTR_LINK_UP_DEBOUNCE_TIMEOUT:
        attr[i].value.u32 = sw.getLinkUpDebounceTimeout();
        break;
#endif
      case SAI_SWITCH_ATTR_SWITCHING_MODE:
        attr[i].value.s32 = sw.getSwitchingMode();
        break;
      case SAI_SWITCH_ATTR_EXT_PACKET_DROP_TYPE_INGRESS_LIST: {
        auto status = fillDropTypeList(
            sw.getPacketDropTypeIngressList(), &attr[i].value.s32list);
        if (status != SAI_STATUS_SUCCESS) {
          return status;
        }
      } break;
      case SAI_SWITCH_ATTR_EXT_PACKET_DROP_TYPE_EGRESS_LIST: {
        auto status = fillDropTypeList(
            sw.getPacketDropTypeEgressList(), &attr[i].value.s32list);
        if (status != SAI_STATUS_SUCCESS) {
          return status;
        }
      } break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_switch_stats_fn(
    sai_object_id_t /*switchId*/,
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
sai_status_t get_switch_stats_ext_fn(
    sai_object_id_t switchId,
    uint32_t num_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /*mode*/,
    uint64_t* counters) {
  return get_switch_stats_fn(switchId, num_of_counters, counter_ids, counters);
}

/*
 *  noop clear stats API. Since fake doesnt have a
 *  dataplane stats are always set to 0, so
 *  no need to clear them
 */
sai_status_t clear_switch_stats_fn(
    sai_object_id_t /*switch_id*/,
    uint32_t /*number_of_counters*/,
    const sai_stat_id_t* /*counter_ids*/) {
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_switch_api_t _switch_api;

void populate_switch_api(sai_switch_api_t** switch_api) {
  _switch_api.create_switch = &create_switch_fn;
  _switch_api.remove_switch = &remove_switch_fn;
  _switch_api.set_switch_attribute = &set_switch_attribute_fn;
  _switch_api.get_switch_attribute = &get_switch_attribute_fn;
  _switch_api.get_switch_stats = &get_switch_stats_fn;
  _switch_api.get_switch_stats_ext = &get_switch_stats_ext_fn;
  _switch_api.clear_switch_stats = &clear_switch_stats_fn;
  *switch_api = &_switch_api;
}

} // namespace facebook::fboss
