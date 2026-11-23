// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiVendorSwitchManager.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/DropReasonUtils.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

DEFINE_int32(
    vendor_switch_interrupt_log_throttle_timeout_secs,
    60,
    "Vendor switch interrupt should be logged only once in this interval");

namespace facebook::fboss {

SaiVendorSwitchManager::SaiVendorSwitchManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::VENDOR_SWITCH_NOTIFICATION)) {
    // Unsupported
    return;
  }
  std::vector<sai_map_t> eventIdToOptions;
  // Create VendorSwitch with all interrupts disabled
  eventIdToOptions.reserve(getInterruptEventsToBeEnabled().size());
  for (auto interruptEventId : getInterruptEventsToBeEnabled()) {
    sai_map_t mapping{};
    mapping.key = interruptEventId;
    // Keep interrupts disabled
    mapping.value = 0;
    eventIdToOptions.push_back(mapping);
  }
  auto& vendorSwitchStore = saiStore_->get<SaiVendorSwitchTraits>();
  auto vendorSwitchTraits =
      SaiVendorSwitchTraits::CreateAttributes{eventIdToOptions};
  vendorSwitch_ =
      vendorSwitchStore.setObject(std::monostate{}, vendorSwitchTraits);
  // Initialize the vector for interrupt event time tracking
  lastInterruptEventTime_.resize(getNumberOfVendorSwitchInterruptEventsIds());
  initWarningInterruptEvents();
#endif
}

void SaiVendorSwitchManager::setVendorSwitchEventEnableState(bool enable) {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  std::vector<uint32_t> interruptsOfInterest = getInterruptEventsToBeEnabled();
  if (enable) {
    // Enable all interrupt events
    SaiApiTable::getInstance()->vendorSwitchApi().setAttribute(
        vendorSwitch_->adapterKey(),
        SaiVendorSwitchTraits::Attributes::EnableEventList{
            interruptsOfInterest});
  } else {
    // Disable all interrupt events
    SaiApiTable::getInstance()->vendorSwitchApi().setAttribute(
        vendorSwitch_->adapterKey(),
        SaiVendorSwitchTraits::Attributes::DisableEventList{
            interruptsOfInterest});
  }
#endif
}

void SaiVendorSwitchManager::logVendorSwitchEvent(
    uint32_t eventId,
    uint32_t time) {
  // Throttle the logging as per the timeout specified
  if (time > lastInterruptEventTime_.at(eventId) +
          FLAGS_vendor_switch_interrupt_log_throttle_timeout_secs) {
    lastInterruptEventTime_.at(eventId) = time;
    if (isVendorSwitchWarningEventId(eventId)) {
      XLOG(DBG2) << "WARNING INTERRUPT: " << getVendorSwitchEventName(eventId);
    } else {
      XLOG(WARNING) << "ERROR INTERRUPT: " << getVendorSwitchEventName(eventId);
    }
  }
}

void SaiVendorSwitchManager::vendorSwitchEventNotificationCallback(
    sai_size_t /*bufferSize*/,
    const void* buffer,
    uint32_t /*eventType*/) {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  sai_vendor_switch_event_info_t* eventInfo =
      (sai_vendor_switch_event_info_t*)(buffer);
  logVendorSwitchEvent(eventInfo->event_id, eventInfo->time);
  incrementInterruptEventCounter(eventInfo->event_id);
#endif
}

void SaiVendorSwitchManager::logCgmErrors() const {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  sai_uint64_t cgmRejectBitmap =
      SaiApiTable::getInstance()->vendorSwitchApi().getAttribute(
          vendorSwitch_->adapterKey(),
          SaiVendorSwitchTraits::Attributes::CgmRejectStatusBitmap{});
  const auto originalCgmRejectBitmap = cgmRejectBitmap;
  std::vector<std::string> reasons;
  while (cgmRejectBitmap != 0) {
    // Brian Kernighan's algorithm to find set bits in the bitmap.
    uint64_t rightmostSetBit = cgmRejectBitmap & -cgmRejectBitmap;
    reasons.emplace_back(
        getCgmDropReasonName(std::countr_zero(rightmostSetBit)));
    cgmRejectBitmap -= rightmostSetBit;
  }
  for (const auto& line :
       formatDropReasonLogLines("CGM", reasons, originalCgmRejectBitmap)) {
    XLOG(WARNING) << line;
  }
#endif
}

} // namespace facebook::fboss
