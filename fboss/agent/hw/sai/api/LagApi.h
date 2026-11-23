// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include "fboss/agent/hw/sai/api/PortApi.h"

#include <folly/logging/xlog.h>

#include <any>
#include <optional>
#include <tuple>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class LagApi;

struct SaiLagTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_LAG;
  using SaiApiT = LagApi;
  struct Attributes {
    using EnumType = sai_lag_attr_t;
    using Label =
        SaiAttribute<EnumType, SAI_LAG_ATTR_LABEL, std::array<char, 32>>;
    using PortVlanId =
        SaiAttribute<EnumType, SAI_LAG_ATTR_PORT_VLAN_ID, uint16_t>;
    using PortList = SaiAttribute<
        EnumType,
        SAI_LAG_ATTR_PORT_LIST,
        std::vector<sai_object_id_t>>;
    struct AttributeDisableTtlDecrement {
      std::optional<sai_attr_id_t> operator()();
    };
    using DisableTtlDecrement = SaiExtensionAttribute<
        bool,
        AttributeDisableTtlDecrement,
        SaiBoolDefaultFalse>;
  };
  using AdapterKey = LagSaiId;
  using AdapterHostKey = Attributes::Label;
  using CreateAttributes =
      std::tuple<Attributes::Label, std::optional<Attributes::PortVlanId>>;
};

SAI_ATTRIBUTE_NAME(Lag, PortList);
SAI_ATTRIBUTE_NAME(Lag, Label);
SAI_ATTRIBUTE_NAME(Lag, PortVlanId);
SAI_ATTRIBUTE_NAME(Lag, DisableTtlDecrement);

struct SaiLagMemberTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_LAG_MEMBER;
  using SaiApiT = LagApi;
  struct Attributes {
    using EnumType = sai_lag_member_attr_t;
    using LagId =
        SaiAttribute<EnumType, SAI_LAG_MEMBER_ATTR_LAG_ID, sai_object_id_t>;
    using PortId =
        SaiAttribute<EnumType, SAI_LAG_MEMBER_ATTR_PORT_ID, sai_object_id_t>;
    using EgressDisable =
        SaiAttribute<EnumType, SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE, bool>;
  };
  using AdapterKey = LagMemberSaiId;
  using AdapterHostKey = std::tuple<Attributes::LagId, Attributes::PortId>;
  using CreateAttributes = std::
      tuple<Attributes::LagId, Attributes::PortId, Attributes::EgressDisable>;
};

SAI_ATTRIBUTE_NAME(LagMember, LagId);
SAI_ATTRIBUTE_NAME(LagMember, PortId);
SAI_ATTRIBUTE_NAME(LagMember, EgressDisable);

class LagApi : public SaiApi<LagApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_LAG;
  LagApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for lag api");
  }

 private:
  sai_status_t _create(
      LagSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_lag(rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _create(
      LagMemberSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_lag_member(rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(LagSaiId id) const {
    return api_->remove_lag(id);
  }
  sai_status_t _remove(LagMemberSaiId id) const {
    return api_->remove_lag_member(id);
  }

  sai_status_t _getAttribute(LagSaiId id, sai_attribute_t* attr) const {
    return api_->get_lag_attribute(id, 1, attr);
  }
  sai_status_t _getAttribute(LagMemberSaiId id, sai_attribute_t* attr) const {
    return api_->get_lag_member_attribute(id, 1, attr);
  }

  sai_status_t _setAttribute(LagSaiId id, const sai_attribute_t* attr) const {
    return api_->set_lag_attribute(id, attr);
  }
  sai_status_t _setAttribute(LagMemberSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_lag_member_attribute(id, attr);
  }

  sai_lag_api_t* api_;
  friend class SaiApi<LagApi>;
};

} // namespace facebook::fboss
