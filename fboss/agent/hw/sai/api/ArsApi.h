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
class ArsApi;

struct SaiArsTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_ARS;
  using SaiApiT = ArsApi;
  struct Attributes {
    using EnumType = sai_ars_attr_t;
    using Mode = SaiAttribute<EnumType, SAI_ARS_ATTR_MODE, sai_int32_t>;
    using IdleTime = SaiAttribute<
        EnumType,
        SAI_ARS_ATTR_IDLE_TIME,
        sai_uint32_t,
        StdNullOptDefault<sai_uint32_t>>;
    using MaxFlows = SaiAttribute<
        EnumType,
        SAI_ARS_ATTR_MAX_FLOWS,
        sai_uint32_t,
        StdNullOptDefault<sai_uint32_t>>;
    using PrimaryPathQualityThreshold = SaiAttribute<
        EnumType,
        SAI_ARS_ATTR_PRIMARY_PATH_QUALITY_THRESHOLD,
        sai_uint32_t,
        StdNullOptDefault<sai_uint32_t>>;
    using AlternatePathCost = SaiAttribute<
        EnumType,
        SAI_ARS_ATTR_ALTERNATE_PATH_COST,
        sai_uint32_t,
        StdNullOptDefault<sai_uint32_t>>;
    using AlternatePathBias = SaiAttribute<
        EnumType,
        SAI_ARS_ATTR_ALTERNATE_PATH_BIAS,
        sai_uint32_t,
        StdNullOptDefault<sai_uint32_t>>;
    struct AttributeNextHopGroupType {
      std::optional<sai_attr_id_t> operator()();
    };
    using NextHopGroupType = SaiExtensionAttribute<
        sai_int32_t,
        AttributeNextHopGroupType,
        StdNullOptDefault<sai_int32_t>>;
    struct AttributeSourcePortPrune {
      std::optional<sai_attr_id_t> operator()();
    };
    // Prevents an ECMP group from load balancing a packet back out the port it
    // was received on.
    using SourcePortPrune = SaiExtensionAttribute<
        bool,
        AttributeSourcePortPrune,
        StdNullOptDefault<bool>>;
    struct AttributeEcmpMemberCount {
      std::optional<sai_attr_id_t> operator()();
    };
    // Number of members in the DLB super group backing a virtual ARS group.
    // CREATE_ONLY, and only meaningful when NextHopGroupType is VIRTUAL.
    using EcmpMemberCount = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeEcmpMemberCount,
        StdNullOptDefault<sai_uint32_t>>;
    struct AttributeMaxAltMembersPerGroup {
      std::optional<sai_attr_id_t> operator()();
    };
    // Slots of the group reserved for alternate members. The id is in every
    // saiars.h, but only 15.4 implements it, so it is modelled as an extension
    // attribute to keep the version gating in the per SDK ArsApi.cpp.
    // Defaulting to nullopt rather than 0 keeps an unset slot round tripping as
    // unset where the adapter rejects the get, as NextHopGroupType does above.
    using MaxAltMembersPerGroup = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeMaxAltMembersPerGroup,
        StdNullOptDefault<sai_uint32_t>>;
    struct AttributeMaxPrimaryMembersPerGroup {
      std::optional<sai_attr_id_t> operator()();
    };
    // Slots of the group available to primary members. Gated the same way as
    // MaxAltMembersPerGroup.
    using MaxPrimaryMembersPerGroup = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeMaxPrimaryMembersPerGroup,
        StdNullOptDefault<sai_uint32_t>>;
    struct AttributeCommonMembersThresholdCount {
      std::optional<sai_attr_id_t> operator()();
    };
    // How many members shared by every virtual group in the super group there
    // have to be before the adapter starts promoting them to alternate
    // members.
    using CommonMembersThresholdCount = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeCommonMembersThresholdCount,
        StdNullOptDefault<sai_uint32_t>>;
  };

  using AdapterKey = ArsSaiId;
  using CreateAttributes = std::tuple<
      Attributes::Mode,
      std::optional<Attributes::IdleTime>,
      std::optional<Attributes::MaxFlows>,
      std::optional<Attributes::PrimaryPathQualityThreshold>,
      std::optional<Attributes::AlternatePathCost>,
      std::optional<Attributes::AlternatePathBias>,
      std::optional<Attributes::NextHopGroupType>,
      std::optional<Attributes::SourcePortPrune>,
      std::optional<Attributes::EcmpMemberCount>,
      std::optional<Attributes::MaxAltMembersPerGroup>,
      std::optional<Attributes::MaxPrimaryMembersPerGroup>,
      std::optional<Attributes::CommonMembersThresholdCount>>;
#if defined(CHENAB_SAI_SDK)
  using AdapterHostKey = std::tuple<Attributes::Mode>;
#else
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  using AdapterHostKey = std::tuple<
      Attributes::Mode,
      std::optional<Attributes::IdleTime>,
      std::optional<Attributes::MaxFlows>,
      std::optional<Attributes::AlternatePathCost>,
#if defined(BRCM_SAI_SDK_GTE_15_4)
      std::optional<Attributes::AlternatePathBias>,
      std::optional<Attributes::NextHopGroupType>,
      // EcmpMemberCount is CREATE_ONLY, so it has to take part in the key:
      // a width change must create a new object rather than be set on the
      // existing one.
      std::optional<Attributes::EcmpMemberCount>>;
#elif defined(BRCM_SAI_SDK_GTE_14_0)
      std::optional<Attributes::AlternatePathBias>,
      std::optional<Attributes::NextHopGroupType>>;
#else
      std::optional<Attributes::AlternatePathBias>>;
#endif
#else
  using AdapterHostKey = std::monostate;
#endif
#endif
};

SAI_ATTRIBUTE_NAME(Ars, Mode)
SAI_ATTRIBUTE_NAME(Ars, IdleTime)
SAI_ATTRIBUTE_NAME(Ars, MaxFlows)
SAI_ATTRIBUTE_NAME(Ars, PrimaryPathQualityThreshold)
SAI_ATTRIBUTE_NAME(Ars, AlternatePathCost)
SAI_ATTRIBUTE_NAME(Ars, AlternatePathBias)
SAI_ATTRIBUTE_NAME(Ars, NextHopGroupType)
SAI_ATTRIBUTE_NAME(Ars, SourcePortPrune)
SAI_ATTRIBUTE_NAME(Ars, EcmpMemberCount)
SAI_ATTRIBUTE_NAME(Ars, MaxAltMembersPerGroup)
SAI_ATTRIBUTE_NAME(Ars, MaxPrimaryMembersPerGroup)
SAI_ATTRIBUTE_NAME(Ars, CommonMembersThresholdCount)

inline SaiArsTraits::AdapterHostKey getAdapterHostKey(
    const SaiArsTraits::CreateAttributes& createAttributes) {
#if defined(CHENAB_SAI_SDK)
  return SaiArsTraits::AdapterHostKey{
      std::get<SaiArsTraits::Attributes::Mode>(createAttributes)};
#else
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
#if defined(BRCM_SAI_SDK_GTE_15_4)
  return SaiArsTraits::AdapterHostKey{
      std::get<SaiArsTraits::Attributes::Mode>(createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::IdleTime>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::MaxFlows>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::AlternatePathCost>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::AlternatePathBias>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::NextHopGroupType>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::EcmpMemberCount>>(
          createAttributes)};
#elif defined(BRCM_SAI_SDK_GTE_14_0)
  return SaiArsTraits::AdapterHostKey{
      std::get<SaiArsTraits::Attributes::Mode>(createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::IdleTime>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::MaxFlows>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::AlternatePathCost>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::AlternatePathBias>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::NextHopGroupType>>(
          createAttributes)

  };
#else
  return SaiArsTraits::AdapterHostKey{
      std::get<SaiArsTraits::Attributes::Mode>(createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::IdleTime>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::MaxFlows>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::AlternatePathCost>>(
          createAttributes),
      std::get<std::optional<SaiArsTraits::Attributes::AlternatePathBias>>(
          createAttributes)};
#endif
#else
  return SaiArsTraits::AdapterHostKey{};
#endif
#endif
}

class ArsApi : public SaiApi<ArsApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_ARS;
  ArsApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for ars api");
  }
  ArsApi(const ArsApi& other) = delete;
  ArsApi& operator=(const ArsApi& other) = delete;

 private:
  sai_status_t _create(
      ArsSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_ars(rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(ArsSaiId id) const {
    return api_->remove_ars(id);
  }
  sai_status_t _getAttribute(ArsSaiId id, sai_attribute_t* attr) const {
    return api_->get_ars_attribute(id, 1, attr);
  }
  sai_status_t _setAttribute(ArsSaiId id, const sai_attribute_t* attr) const {
    return api_->set_ars_attribute(id, attr);
  }

  sai_ars_api_t* api_;
  friend class SaiApi<ArsApi>;
};
#endif

} // namespace facebook::fboss
