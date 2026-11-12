// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <string>
#include <tuple>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class PolicerApi;

struct SaiPolicerTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_POLICER;
  using SaiApiT = PolicerApi;
  struct Attributes {
    using EnumType = sai_policer_attr_t;
    using Type =
        SaiAttribute<EnumType, SAI_POLICER_ATTR_METER_TYPE, sai_int32_t>;
    using Mode = SaiAttribute<EnumType, SAI_POLICER_ATTR_MODE, sai_int32_t>;
    using ColorSource =
        SaiAttribute<EnumType, SAI_POLICER_ATTR_COLOR_SOURCE, sai_int32_t>;
    using Cir = SaiAttribute<EnumType, SAI_POLICER_ATTR_CIR, sai_uint64_t>;
    using Cbs = SaiAttribute<EnumType, SAI_POLICER_ATTR_CBS, sai_uint64_t>;
    using Pbs = SaiAttribute<EnumType, SAI_POLICER_ATTR_PBS, sai_uint64_t>;
    using GreenPacketAction = SaiAttribute<
        EnumType,
        SAI_POLICER_ATTR_GREEN_PACKET_ACTION,
        sai_int32_t>;
    using YellowPacketAction = SaiAttribute<
        EnumType,
        SAI_POLICER_ATTR_YELLOW_PACKET_ACTION,
        sai_int32_t>;
    using RedPacketAction =
        SaiAttribute<EnumType, SAI_POLICER_ATTR_RED_PACKET_ACTION, sai_int32_t>;
  };

  using AdapterKey = PolicerSaiId;
  using AdapterHostKey = std::string;
  using CreateAttributes = std::tuple<
      Attributes::Type,
      Attributes::Mode,
      Attributes::ColorSource,
      Attributes::Cir,
      Attributes::Cbs,
      Attributes::Pbs,
      Attributes::GreenPacketAction,
      Attributes::YellowPacketAction,
      Attributes::RedPacketAction>;
};

SAI_ATTRIBUTE_NAME(Policer, Type);
SAI_ATTRIBUTE_NAME(Policer, Mode);
SAI_ATTRIBUTE_NAME(Policer, ColorSource);
SAI_ATTRIBUTE_NAME(Policer, Cir);
SAI_ATTRIBUTE_NAME(Policer, Cbs);
SAI_ATTRIBUTE_NAME(Policer, Pbs);
SAI_ATTRIBUTE_NAME(Policer, GreenPacketAction);
SAI_ATTRIBUTE_NAME(Policer, YellowPacketAction);
SAI_ATTRIBUTE_NAME(Policer, RedPacketAction);

class PolicerApi : public SaiApi<PolicerApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_POLICER;
  PolicerApi() {
    auto status = sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for policer api");
  }

 private:
  sai_status_t _create(
      PolicerSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_policer(
        rawSaiId(id), switch_id, static_cast<uint32_t>(count), attr_list);
  }
  sai_status_t _remove(PolicerSaiId id) const {
    return api_->remove_policer(id);
  }
  sai_status_t _getAttribute(PolicerSaiId id, sai_attribute_t* attr) const {
    return api_->get_policer_attribute(id, 1, attr);
  }
  sai_status_t _setAttribute(PolicerSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_policer_attribute(id, attr);
  }

  sai_policer_api_t* api_{};
  friend class SaiApi<PolicerApi>;
};

} // namespace facebook::fboss
