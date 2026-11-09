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

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <folly/logging/xlog.h>

#include <tuple>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
class ArsProfileApi;

struct SaiArsProfileTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_ARS_PROFILE;
  using SaiApiT = ArsProfileApi;
  struct Attributes {
    using EnumType = sai_ars_profile_attr_t;
    using Algo = SaiAttribute<EnumType, SAI_ARS_PROFILE_ATTR_ALGO, sai_int32_t>;
    using SamplingInterval = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_SAMPLING_INTERVAL,
        sai_uint32_t>;
    using RandomSeed = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_ARS_RANDOM_SEED,
        sai_uint32_t>;
    using EnableIPv4 =
        SaiAttribute<EnumType, SAI_ARS_PROFILE_ATTR_ENABLE_IPV4, bool>;
    using EnableIPv6 =
        SaiAttribute<EnumType, SAI_ARS_PROFILE_ATTR_ENABLE_IPV6, bool>;
    // Past load
    using PortLoadPast =
        SaiAttribute<EnumType, SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST, bool>;
    using PortLoadPastWeight = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST_WEIGHT,
        sai_uint8_t>;
    using LoadPastMinVal = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_LOAD_PAST_MIN_VAL,
        sai_uint32_t>;
    using LoadPastMaxVal = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_LOAD_PAST_MAX_VAL,
        sai_uint32_t>;
    // Future load
    using PortLoadFuture = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE,
        bool,
        StdNullOptDefault<bool>>;
    using PortLoadFutureWeight = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE_WEIGHT,
        sai_uint8_t,
        StdNullOptDefault<uint8_t>>;
    using LoadFutureMinVal = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MIN_VAL,
        sai_uint32_t>;
    using LoadFutureMaxVal = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MAX_VAL,
        sai_uint32_t>;
    // Current load
    using PortLoadCurrent =
        SaiAttribute<EnumType, SAI_ARS_PROFILE_ATTR_PORT_LOAD_CURRENT, bool>;
    using PortLoadExponent = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_PORT_LOAD_EXPONENT,
        sai_uint8_t>;
    using LoadCurrentMinVal = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MIN_VAL,
        sai_uint32_t>;
    using LoadCurrentMaxVal = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MAX_VAL,
        sai_uint32_t>;
    using MaxFlows =
        SaiAttribute<EnumType, SAI_ARS_PROFILE_ATTR_MAX_FLOWS, sai_uint32_t>;
    using QuantBand0MinThreshold = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_QUANT_BAND_0_MIN_THRESHOLD,
        sai_uint32_t>;
    using QuantBand1MinThreshold = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_QUANT_BAND_1_MIN_THRESHOLD,
        sai_uint32_t>;
    using QuantBand2MinThreshold = SaiAttribute<
        EnumType,
        SAI_ARS_PROFILE_ATTR_QUANT_BAND_2_MIN_THRESHOLD,
        sai_uint32_t>;
    struct AttributeExtensionSamplingIntervalNanosec {
      std::optional<sai_attr_id_t> operator()();
    };
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
    using ExtensionSamplingIntervalNanosec = SaiExtensionAttribute<
        sai_uint64_t,
        AttributeExtensionSamplingIntervalNanosec,
        SaiIntDefault<sai_uint64_t>>;
#endif
    struct AttributeArsMaxGroups {
      std::optional<sai_attr_id_t> operator()();
    };
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
    using ArsMaxGroups = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeArsMaxGroups,
        SaiIntDefault<sai_uint32_t>>;
#endif
    struct AttributeArsBaseIndex {
      std::optional<sai_attr_id_t> operator()();
    };
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
    using ArsBaseIndex = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeArsBaseIndex,
        SaiIntDefault<sai_uint32_t>>;
#endif
    struct AttributeEcmpMemberCount {
      std::optional<sai_attr_id_t> operator()();
    };
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
    using EcmpMemberCount = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeEcmpMemberCount,
        SaiIntDefault<sai_uint32_t>>;
#endif
  };

  using AdapterKey = ArsProfileSaiId;
  using AdapterHostKey = std::monostate;
  using CreateAttributes = std::tuple<
#if defined(CHENAB_SAI_SDK)
      Attributes::QuantBand0MinThreshold,
      Attributes::QuantBand1MinThreshold,
      Attributes::QuantBand2MinThreshold>;
#else
      Attributes::Algo,
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
      Attributes::ExtensionSamplingIntervalNanosec,
#else
      Attributes::SamplingInterval,
#endif
      Attributes::RandomSeed,
      Attributes::EnableIPv4,
      Attributes::EnableIPv6,
      Attributes::PortLoadPast,
      Attributes::PortLoadPastWeight,
      Attributes::LoadPastMinVal,
      Attributes::LoadPastMaxVal,
      std::optional<Attributes::PortLoadFuture>,
      std::optional<Attributes::PortLoadFutureWeight>,
      Attributes::LoadFutureMinVal,
      Attributes::LoadFutureMaxVal,
      Attributes::PortLoadCurrent,
      Attributes::PortLoadExponent,
      Attributes::LoadCurrentMinVal,
      Attributes::LoadCurrentMaxVal
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
      ,
      std::optional<Attributes::ArsMaxGroups>,
      std::optional<Attributes::ArsBaseIndex>
#if defined(BRCM_SAI_SDK_GTE_14_0)
      ,
      std::optional<Attributes::EcmpMemberCount>
#endif
      >;
#else
      >;
#endif
#endif
};

#if defined(CHENAB_SAI_SDK)
SAI_ATTRIBUTE_NAME(ArsProfile, QuantBand0MinThreshold)
SAI_ATTRIBUTE_NAME(ArsProfile, QuantBand1MinThreshold)
SAI_ATTRIBUTE_NAME(ArsProfile, QuantBand2MinThreshold)
#else
SAI_ATTRIBUTE_NAME(ArsProfile, Algo)
SAI_ATTRIBUTE_NAME(ArsProfile, SamplingInterval)
SAI_ATTRIBUTE_NAME(ArsProfile, RandomSeed)
SAI_ATTRIBUTE_NAME(ArsProfile, EnableIPv4)
SAI_ATTRIBUTE_NAME(ArsProfile, EnableIPv6)
SAI_ATTRIBUTE_NAME(ArsProfile, PortLoadPast)
SAI_ATTRIBUTE_NAME(ArsProfile, PortLoadPastWeight)
SAI_ATTRIBUTE_NAME(ArsProfile, LoadPastMinVal)
SAI_ATTRIBUTE_NAME(ArsProfile, LoadPastMaxVal)
SAI_ATTRIBUTE_NAME(ArsProfile, PortLoadFuture)
SAI_ATTRIBUTE_NAME(ArsProfile, PortLoadFutureWeight)
SAI_ATTRIBUTE_NAME(ArsProfile, LoadFutureMinVal)
SAI_ATTRIBUTE_NAME(ArsProfile, LoadFutureMaxVal)
SAI_ATTRIBUTE_NAME(ArsProfile, PortLoadCurrent)
SAI_ATTRIBUTE_NAME(ArsProfile, PortLoadExponent)
SAI_ATTRIBUTE_NAME(ArsProfile, LoadCurrentMinVal)
SAI_ATTRIBUTE_NAME(ArsProfile, LoadCurrentMaxVal)
SAI_ATTRIBUTE_NAME(ArsProfile, MaxFlows)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
SAI_ATTRIBUTE_NAME(ArsProfile, ExtensionSamplingIntervalNanosec)
SAI_ATTRIBUTE_NAME(ArsProfile, ArsMaxGroups)
SAI_ATTRIBUTE_NAME(ArsProfile, ArsBaseIndex)
SAI_ATTRIBUTE_NAME(ArsProfile, EcmpMemberCount)
#endif

class ArsProfileApi : public SaiApi<ArsProfileApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_ARS_PROFILE;
  ArsProfileApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for ars profile api");
  }
  ArsProfileApi(const ArsProfileApi& other) = delete;
  ArsProfileApi& operator=(const ArsProfileApi& other) = delete;

 private:
  sai_status_t _create(
      ArsProfileSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_ars_profile(rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(ArsProfileSaiId id) const {
    return api_->remove_ars_profile(id);
  }
  sai_status_t _getAttribute(ArsProfileSaiId id, sai_attribute_t* attr) const {
    return api_->get_ars_profile_attribute(id, 1, attr);
  }
  sai_status_t _setAttribute(ArsProfileSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_ars_profile_attribute(id, attr);
  }

  sai_ars_profile_api_t* api_;
  friend class SaiApi<ArsProfileApi>;
};
#endif

} // namespace facebook::fboss
