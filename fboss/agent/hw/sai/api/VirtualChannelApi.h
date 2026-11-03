// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/hw/sai/api/SaiVersion.h"

#if defined(SAI_CBFC_SUPPORTED)

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <optional>
#include <tuple>
#include <vector>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class VirtualChannelApi;

struct SaiVirtualChannelTraits {
  static constexpr sai_api_t ApiType = SAI_API_VIRTUAL_CHANNEL;
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_VIRTUAL_CHANNEL;
  using SaiApiT = VirtualChannelApi;
  struct Attributes {
    using EnumType = sai_virtual_channel_attr_t;
    using Port =
        SaiAttribute<EnumType, SAI_VIRTUAL_CHANNEL_ATTR_PORT, SaiObjectIdT>;
    using Index =
        SaiAttribute<EnumType, SAI_VIRTUAL_CHANNEL_ATTR_INDEX, sai_uint8_t>;
    // READ_ONLY: the per-VC credit limit hardware advertises to the peer.
    using CbfcReceiverNativeCreditLimit = SaiAttribute<
        EnumType,
        SAI_VIRTUAL_CHANNEL_ATTR_CBFC_RECEIVER_NATIVE_CREDIT_LIMIT,
        sai_uint32_t>;
    using CbfcSenderCreditProfile = SaiAttribute<
        EnumType,
        SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_CREDIT_PROFILE,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using CbfcReceiverEnable = SaiAttribute<
        EnumType,
        SAI_VIRTUAL_CHANNEL_ATTR_CBFC_RECEIVER_ENABLE,
        bool,
        SaiBoolDefaultFalse>;
    using CbfcSenderEnable = SaiAttribute<
        EnumType,
        SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_ENABLE,
        bool,
        SaiBoolDefaultFalse>;
  };
  using AdapterKey = VirtualChannelSaiId;
  using AdapterHostKey = std::tuple<Attributes::Port, Attributes::Index>;
  using CreateAttributes = std::tuple<
      Attributes::Port,
      Attributes::Index,
      std::optional<Attributes::CbfcSenderCreditProfile>,
      std::optional<Attributes::CbfcReceiverEnable>,
      std::optional<Attributes::CbfcSenderEnable>>;

  static constexpr std::array<sai_stat_id_t, 0> CounterIdsToReadAndClear = {};
  static constexpr std::array<sai_stat_id_t, 4> CounterIdsToRead = {
      SAI_VIRTUAL_CHANNEL_STAT_SENDER_CREDITS_CONSUMED,
      SAI_VIRTUAL_CHANNEL_STAT_SENDER_CREDITS_FREED,
      SAI_VIRTUAL_CHANNEL_STAT_RECEIVER_CREDITS_CONSUMED,
      SAI_VIRTUAL_CHANNEL_STAT_RECEIVER_CREDITS_FREED,
  };

  // The subset of CounterIdsToRead that a real switch can read, the analogue
  // of SaiPortTraits::cbfcStats(). get_virtual_channel_stats is
  // all-or-nothing, so asking for a counter the stepping does not serve loses
  // every virtual channel counter on the port, not just that one.
  //
  // Left out because they are Tomahawk Ultra 1 B0 stepping only and return
  // NOT_SUPPORTED on A0:
  //   SENDER_CREDITS_CONSUMED    bcmCosqStatCbfcTxPortVcCreditConsumed
  //   SENDER_CREDITS_FREED       bcmCosqStatCbfcTxPortVcCRCyclicCount
  //   RECEIVER_CREDITS_FREED     bcmCosqStatCbfcRxPortVcCFCyclicCount
  //
  // CounterIdsToRead above keeps all four: fake serves them regardless of
  // stepping, and it is the set the object declares rather than the set a
  // given chip can answer.
  static const std::vector<sai_stat_id_t>& cbfcVcStats() {
    static const std::vector<sai_stat_id_t> ids = {
        SAI_VIRTUAL_CHANNEL_STAT_RECEIVER_CREDITS_CONSUMED,
    };
    return ids;
  }
};

SAI_ATTRIBUTE_NAME(VirtualChannel, Port);
SAI_ATTRIBUTE_NAME(VirtualChannel, Index);
SAI_ATTRIBUTE_NAME(VirtualChannel, CbfcReceiverNativeCreditLimit);
SAI_ATTRIBUTE_NAME(VirtualChannel, CbfcSenderCreditProfile);
SAI_ATTRIBUTE_NAME(VirtualChannel, CbfcReceiverEnable);
SAI_ATTRIBUTE_NAME(VirtualChannel, CbfcSenderEnable);

template <>
struct SaiObjectHasStats<SaiVirtualChannelTraits> : public std::true_type {};

/*
 * brcm-sai 16.0_ea_odp rejects THRESHOLD_MODE, SHARED_DYNAMIC_TH and
 * SHARED_STATIC_TH on presence rather than on value, so they cannot be
 * modelled even as optionals. PoolId is MANDATORY_ON_CREATE and must be
 * SAI_NULL_OBJECT_ID: CBFC_CREDIT_POOL is unsupported, so no pool object can
 * exist to reference.
 */
struct SaiCbfcCreditProfileTraits {
  static constexpr sai_api_t ApiType = SAI_API_VIRTUAL_CHANNEL;
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_CBFC_CREDIT_PROFILE;
  using SaiApiT = VirtualChannelApi;
  struct Attributes {
    using EnumType = sai_cbfc_credit_profile_attr_t;
    using PoolId = SaiAttribute<
        EnumType,
        SAI_CBFC_CREDIT_PROFILE_ATTR_POOL_ID,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using ReservedCreditSize = SaiAttribute<
        EnumType,
        SAI_CBFC_CREDIT_PROFILE_ATTR_RESERVED_CREDIT_SIZE,
        sai_uint64_t>;
  };
  using AdapterKey = CbfcCreditProfileSaiId;
  using CreateAttributes =
      std::tuple<Attributes::PoolId, Attributes::ReservedCreditSize>;
  using AdapterHostKey = CreateAttributes;
};

SAI_ATTRIBUTE_NAME(CbfcCreditProfile, PoolId);
SAI_ATTRIBUTE_NAME(CbfcCreditProfile, ReservedCreditSize);

class VirtualChannelApi : public SaiApi<VirtualChannelApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_VIRTUAL_CHANNEL;
  VirtualChannelApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(
        status, ApiType, "Failed to query for virtual channel api");
  }
  VirtualChannelApi(const VirtualChannelApi& other) = delete;
  VirtualChannelApi& operator=(const VirtualChannelApi& other) = delete;

 private:
  sai_status_t _create(
      VirtualChannelSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_virtual_channel(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(VirtualChannelSaiId id) const {
    return api_->remove_virtual_channel(id);
  }
  sai_status_t _getAttribute(VirtualChannelSaiId key, sai_attribute_t* attr)
      const {
    return api_->get_virtual_channel_attribute(key, 1, attr);
  }
  sai_status_t _setAttribute(
      VirtualChannelSaiId key,
      const sai_attribute_t* attr) const {
    return api_->set_virtual_channel_attribute(key, attr);
  }

  sai_status_t _getStats(
      VirtualChannelSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids,
      sai_stats_mode_t mode,
      uint64_t* counters) const {
    return mode == SAI_STATS_MODE_READ
        ? api_->get_virtual_channel_stats(
              key, num_of_counters, counter_ids, counters)
        : api_->get_virtual_channel_stats_ext(
              key, num_of_counters, counter_ids, mode, counters);
  }

  sai_status_t _clearStats(
      VirtualChannelSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids) const {
    return api_->clear_virtual_channel_stats(key, num_of_counters, counter_ids);
  }

  sai_status_t _create(
      CbfcCreditProfileSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_cbfc_credit_profile(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(CbfcCreditProfileSaiId id) const {
    return api_->remove_cbfc_credit_profile(id);
  }
  sai_status_t _getAttribute(CbfcCreditProfileSaiId key, sai_attribute_t* attr)
      const {
    return api_->get_cbfc_credit_profile_attribute(key, 1, attr);
  }
  sai_status_t _setAttribute(
      CbfcCreditProfileSaiId key,
      const sai_attribute_t* attr) const {
    return api_->set_cbfc_credit_profile_attribute(key, attr);
  }

  sai_virtual_channel_api_t* api_;
  friend class SaiApi<VirtualChannelApi>;
};

} // namespace facebook::fboss

#endif
