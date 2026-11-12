/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/api/SwitchApi.h"

#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
extern "C" {
#include <experimental/saiswitchextensions.h>
}
#endif

namespace facebook::fboss {

void SwitchApi::registerRxCallback(
    SwitchSaiId id,
    sai_packet_event_notification_fn rx_cb) const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_PACKET_EVENT_NOTIFY;
  attr.value.ptr = (void*)rx_cb;
  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiApiCheckError(
      rv,
      ApiType,
      "Unable to ",
      rx_cb ? "register" : "unregister",
      " rx callback");
}

void SwitchApi::registerPortStateChangeCallback(
    SwitchSaiId id,
    sai_port_state_change_notification_fn port_state_change_cb) const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY;
  attr.value.ptr = (void*)port_state_change_cb;
  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiApiCheckError(
      rv,
      ApiType,
      "Unable to ",
      port_state_change_cb ? "register" : "unregister",
      " port state change callback");
}

void SwitchApi::registerFdbEventCallback(
    SwitchSaiId id,
    sai_fdb_event_notification_fn fdb_event_cb) const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY;
  attr.value.ptr = (void*)fdb_event_cb;
  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiApiCheckError(
      rv,
      ApiType,
      "Unable to ",
      fdb_event_cb ? "register" : "unregister",
      " FDB event callback");
}

void SwitchApi::registerTamEventCallback(
    SwitchSaiId id,
    sai_tam_event_notification_fn tam_event_cb) const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_TAM_EVENT_NOTIFY;
  attr.value.ptr = (void*)tam_event_cb;
  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiLogError(
      rv,
      ApiType,
      "Unable to ",
      tam_event_cb ? "register" : "unregister",
      " TAM event callback");
}

void SwitchApi::registerQueuePfcDeadlockNotificationCallback(
    SwitchSaiId id,
    sai_queue_pfc_deadlock_notification_fn queue_pfc_deadlock_notification_cb)
    const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_QUEUE_PFC_DEADLOCK_NOTIFY;
  attr.value.ptr = (void*)queue_pfc_deadlock_notification_cb;
  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiLogError(
      rv,
      ApiType,
      "Unable to ",
      queue_pfc_deadlock_notification_cb ? "register" : "unregister",
      " Queue PFC deadlock notification callback");
}

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
void SwitchApi::registerTxReadyStatusChangeCallback(
    SwitchSaiId id,
    sai_port_host_tx_ready_notification_fn tx_ready_status_cb) const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_PORT_HOST_TX_READY_NOTIFY;
  attr.value.ptr = (void*)tx_ready_status_cb;
  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiApiCheckError(
      rv,
      ApiType,
      "Unable to ",
      tx_ready_status_cb ? "register" : "unregister",
      " tx ready status change callback");
}
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
void SwitchApi::registerSwitchAsicSdkHealthEventCallback(
    const SwitchSaiId& id,
    sai_switch_asic_sdk_health_event_notification_fn function) const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_SWITCH_ASIC_SDK_HEALTH_EVENT_NOTIFY;
  attr.value.ptr = (void*)function;

  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiApiCheckError(
      rv,
      ApiType,
      "Unable to ",
      function ? "register" : "unregister",
      " switch asic sdk health event  notify callback");
}
#endif

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
void SwitchApi::registerVendorSwitchEventNotifyCallback(
    const SwitchSaiId& id,
    sai_vendor_switch_event_notification_fn event_notify_cb) const {
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_VENDOR_SWITCH_EVENT_NOTIFY;
  attr.value.ptr = (void*)event_notify_cb;

  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiApiCheckError(
      rv,
      ApiType,
      "Unable to ",
      event_notify_cb ? "register" : "unregister",
      " vendor switch event notify callback");
}
#endif
void SwitchApi::registerSwitchHardResetNotifyCallback(
    const SwitchSaiId& id,
    sai_pointer_t event_notify_cb) const {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
  sai_attribute_t attr;
  attr.id = SAI_SWITCH_ATTR_SWITCH_HARD_RESET_EVENT_NOTIFY;
  attr.value.ptr = event_notify_cb;
  auto g{SaiApiLock::getInstance()->lock()};
  auto rv = _setAttribute(id, &attr);
  saiApiCheckError(
      rv,
      ApiType,
      "Unable to ",
      event_notify_cb ? "register" : "unregister",
      "switch hard reset callback");
#endif
}
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)

SaiHealthNotification::SaiHealthNotification(
    SaiTimeSpec saiTimeSpec,
    const sai_switch_asic_sdk_health_severity_t&
        sai_switch_asic_sdk_health_severity,
    const sai_switch_asic_sdk_health_category_t&
        sai_switch_asic_sdk_health_category,
    const sai_switch_health_data_t& data,
    const sai_u8_list_t& sai_u8_list)
    : timeSpec(std::move(saiTimeSpec)),
      severity(sai_switch_asic_sdk_health_severity),
      category(sai_switch_asic_sdk_health_category),
      saiHealthData{},
      description(sai_u8_list.list, sai_u8_list.list + sai_u8_list.count) {
  switch (data.data_type) {
    case SAI_HEALTH_DATA_TYPE_GENERAL:
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 15, 0)
    case SAI_HEALTH_DATA_TYPE_SER:
      saiHealthData = SaiSerHealthData(data.data.ser);
      break;
#endif
  }
}

bool SaiHealthNotification::corrParityError() const {
#if defined(CHENAB_SAI_SDK)
  return (severity == SAI_SWITCH_ASIC_SDK_HEALTH_SEVERITY_NOTICE);
#else
  return false;
#endif
}

bool SaiHealthNotification::uncorrParityError() const {
#if defined(CHENAB_SAI_SDK)
  return (severity == SAI_SWITCH_ASIC_SDK_HEALTH_SEVERITY_FATAL);
#else
  return false;
#endif
}

bool SaiHealthNotification::asicError() const {
  // TODO(Chenab): Add support for asic error
  return true;
}

std::string SaiHealthNotification::toString() const {
  return fmt::format("{}", *this);
}
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 15, 0)
std::string SaiSerHealthData::toString() const {
  return fmt::format("{}", *this);
}
#endif

} // namespace facebook::fboss
