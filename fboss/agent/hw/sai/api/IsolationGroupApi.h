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
#include "fboss/agent/hw/sai/api/Types.h"

#include <folly/logging/xlog.h>

#include <string>
#include <tuple>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class IsolationGroupApi;

struct SaiIsolationGroupTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_ISOLATION_GROUP;
  using SaiApiT = IsolationGroupApi;
  struct Attributes {
    using EnumType = sai_isolation_group_attr_t;
    /*
     * Carries a default getter, and is held as an optional in CreateAttributes,
     * so that warm boot survives an adapter that does not implement the GET.
     */
    using Type = SaiAttribute<
        EnumType,
        SAI_ISOLATION_GROUP_ATTR_TYPE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using IsolationMemberList = SaiAttribute<
        EnumType,
        SAI_ISOLATION_GROUP_ATTR_ISOLATION_MEMBER_LIST,
        std::vector<sai_object_id_t>>;
  };

  using AdapterKey = IsolationGroupSaiId;
  /*
   * Keyed on the FBOSS isolation group name, as SaiUdfGroupTraits is.
   *
   * SAI defines no LABEL-like attribute on an isolation group, and any number
   * of groups may share the same TYPE, so nothing on the object distinguishes
   * one group from another. The member list would, but it is READ_ONLY and
   * populated only after the group exists -- and keying on membership would
   * mean adding or removing one member recreates the group and re-points every
   * port bound to it.
   *
   * FBOSS therefore carries the name itself, and
   * AdapterHostKeyWarmbootRecoverable is false: SaiStore persists an oid to
   * name map in warm boot state and looks up each surviving oid on restart to
   * re-adopt the group under the same name.
   */
  using AdapterHostKey = std::string;
  using CreateAttributes = std::tuple<std::optional<Attributes::Type>>;
};

SAI_ATTRIBUTE_NAME(IsolationGroup, Type);
SAI_ATTRIBUTE_NAME(IsolationGroup, IsolationMemberList);

struct SaiIsolationGroupMemberTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER;
  using SaiApiT = IsolationGroupApi;
  struct Attributes {
    using EnumType = sai_isolation_group_member_attr_t;
    using IsolationGroupId = SaiAttribute<
        EnumType,
        SAI_ISOLATION_GROUP_MEMBER_ATTR_ISOLATION_GROUP_ID,
        SaiObjectIdT>;
    using IsolationObject = SaiAttribute<
        EnumType,
        SAI_ISOLATION_GROUP_MEMBER_ATTR_ISOLATION_OBJECT,
        SaiObjectIdT>;
  };

  using AdapterKey = IsolationGroupMemberSaiId;
  using AdapterHostKey =
      std::tuple<Attributes::IsolationGroupId, Attributes::IsolationObject>;
  using CreateAttributes =
      std::tuple<Attributes::IsolationGroupId, Attributes::IsolationObject>;
};

SAI_ATTRIBUTE_NAME(IsolationGroupMember, IsolationGroupId);
SAI_ATTRIBUTE_NAME(IsolationGroupMember, IsolationObject);

class IsolationGroupApi : public SaiApi<IsolationGroupApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_ISOLATION_GROUP;
  IsolationGroupApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(
        status, ApiType, "Failed to query for isolation group api");
  }
  IsolationGroupApi(const IsolationGroupApi& other) = delete;

 private:
  sai_status_t _create(
      IsolationGroupSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_isolation_group(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _create(
      IsolationGroupMemberSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_isolation_group_member(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(IsolationGroupSaiId id) const {
    return api_->remove_isolation_group(id);
  }
  sai_status_t _remove(IsolationGroupMemberSaiId id) const {
    return api_->remove_isolation_group_member(id);
  }

  sai_status_t _getAttribute(IsolationGroupSaiId id, sai_attribute_t* attr)
      const {
    return api_->get_isolation_group_attribute(id, 1, attr);
  }
  sai_status_t _getAttribute(
      IsolationGroupMemberSaiId id,
      sai_attribute_t* attr) const {
    return api_->get_isolation_group_member_attribute(id, 1, attr);
  }

  sai_status_t _setAttribute(
      IsolationGroupSaiId id,
      const sai_attribute_t* attr) const {
    return api_->set_isolation_group_attribute(id, attr);
  }
  sai_status_t _setAttribute(
      IsolationGroupMemberSaiId id,
      const sai_attribute_t* attr) const {
    return api_->set_isolation_group_member_attribute(id, attr);
  }

  sai_isolation_group_api_t* api_;
  friend class SaiApi<IsolationGroupApi>;
};

} // namespace facebook::fboss
