// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/api/AclApi.h"
#include "fboss/agent/hw/sai/api/AddressUtil.h"

#include <folly/lang/Assume.h>

#include <algorithm>

namespace facebook {
namespace fboss {

namespace {
// Keys under which the extra next-hop-group adapter-host-key fields are
// persisted in warm boot state.
// SAI group type; only written for non-ECMP groups (e.g. HW_PROTECTION).
constexpr auto kGroupType = "groupType";
// Hierarchical ECMP / protection: list of child group adapter-host-keys (for a
// protection group, the backup/standby group). No per-child weight.
constexpr auto kNextHopGroupList = "nextHopGroupList";
constexpr auto kLevel = "level";

// Forward declaration: nhopGroupAhkToFollyDynamic recurses into child groups.
folly::dynamic nhopGroupAhkToFollyDynamic(
    const SaiNextHopGroupTraits::AdapterHostKey& ahk);
SaiNextHopGroupTraits::AdapterHostKey follyDynamicToNhopGroupAhk(
    const folly::dynamic& json);

folly::dynamic nhopMemberKeyToFollyDynamic(
    const detail::NextHopMemberKey& member) {
  folly::dynamic object = folly::dynamic::object;
  const auto& nhopKey = member.first;

  if (auto ipAhk = std::get_if<SaiIpNextHopTraits::AdapterHostKey>(&nhopKey)) {
    object[AttributeName<SaiIpNextHopTraits::Attributes::Type>::value] =
        folly::to<std::string>(SAI_NEXT_HOP_TYPE_IP);
    object[AttributeName<
        SaiIpNextHopTraits::Attributes::RouterInterfaceId>::value] =
        folly::to<std::string>(
            std::get<SaiIpNextHopTraits::Attributes::RouterInterfaceId>(*ipAhk)
                .value());
    object[AttributeName<SaiIpNextHopTraits::Attributes::Ip>::value] =
        std::get<SaiIpNextHopTraits::Attributes::Ip>(*ipAhk).value().str();
  } else if (
      auto mplsAhk =
          std::get_if<SaiMplsNextHopTraits::AdapterHostKey>(&nhopKey)) {
    object[AttributeName<SaiIpNextHopTraits::Attributes::Type>::value] =
        folly::to<std::string>(SAI_NEXT_HOP_TYPE_MPLS);
    object[AttributeName<
        SaiMplsNextHopTraits::Attributes::RouterInterfaceId>::value] =
        folly::to<std::string>(
            std::get<SaiMplsNextHopTraits::Attributes::RouterInterfaceId>(
                *mplsAhk)
                .value());
    object[AttributeName<SaiMplsNextHopTraits::Attributes::Ip>::value] =
        std::get<SaiMplsNextHopTraits::Attributes::Ip>(*mplsAhk).value().str();
    object[AttributeName<SaiMplsNextHopTraits::Attributes::LabelStack>::value] =
        folly::dynamic::array;
    for (auto label :
         std::get<SaiMplsNextHopTraits::Attributes::LabelStack>(*mplsAhk)
             .value()) {
      object[AttributeName<SaiMplsNextHopTraits::Attributes::LabelStack>::value]
          .push_back(folly::to<std::string>(label));
    }
    if (const auto& outsegType = std::get<
            std::optional<SaiMplsNextHopTraits::Attributes::OutsegType>>(
            *mplsAhk)) {
      object
          [AttributeName<SaiMplsNextHopTraits::Attributes::OutsegType>::value] =
              folly::to<std::string>(outsegType->value());
    }
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  else if (
      auto srv6Ahk =
          std::get_if<SaiSrv6SidlistNextHopTraits::AdapterHostKey>(&nhopKey)) {
    object[AttributeName<SaiIpNextHopTraits::Attributes::Type>::value] =
        folly::to<std::string>(SAI_NEXT_HOP_TYPE_SRV6_SIDLIST);
    object[AttributeName<
        SaiSrv6SidlistNextHopTraits::Attributes::TunnelId>::value] =
        folly::to<std::string>(
            std::get<SaiSrv6SidlistNextHopTraits::Attributes::TunnelId>(
                *srv6Ahk)
                .value());
    object[AttributeName<
        SaiSrv6SidlistNextHopTraits::Attributes::Srv6SidlistId>::value] =
        folly::to<std::string>(
            std::get<SaiSrv6SidlistNextHopTraits::Attributes::Srv6SidlistId>(
                *srv6Ahk)
                .value());
  }
#endif

  object
      [AttributeName<SaiNextHopGroupMemberTraits::Attributes::Weight>::value] =
          member.second;
  return object;
}

detail::NextHopMemberKey follyDynamicToNhopMemberKey(
    const folly::dynamic& object) {
  auto type = object[AttributeName<SaiIpNextHopTraits::Attributes::Type>::value]
                  .asInt();

  // D32229488 adds logic to write weight to switch_state's nhop group.
  // While warmbooting from pre-D32229488 to post-D32229488, default the
  // weight to 1 (default for SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT).
  // UCMP would only be enabled after D32229488 is rolled out, so OK to
  // use default weight.
  sai_uint32_t weight = 1;
  if (object.find(
          AttributeName<
              SaiNextHopGroupMemberTraits::Attributes::Weight>::value) !=
      object.items().end()) {
    weight = static_cast<sai_uint32_t>(
        object[AttributeName<
                   SaiNextHopGroupMemberTraits::Attributes::Weight>::value]
            .asInt());
  }

  switch (type) {
    case SAI_NEXT_HOP_TYPE_IP: {
      SaiIpNextHopTraits::AdapterHostKey ipAhk;
      std::get<SaiIpNextHopTraits::Attributes::RouterInterfaceId>(ipAhk) =
          folly::to<sai_object_id_t>(
              object[AttributeName<SaiIpNextHopTraits::Attributes::
                                       RouterInterfaceId>::value]
                  .asString());
      std::get<SaiIpNextHopTraits::Attributes::Ip>(ipAhk) = folly::IPAddress(
          object[AttributeName<SaiIpNextHopTraits::Attributes::Ip>::value]
              .asString());
      return detail::NextHopMemberKey(ipAhk, weight);
    }

    case SAI_NEXT_HOP_TYPE_MPLS: {
      SaiMplsNextHopTraits::AdapterHostKey mplsAhk;
      std::get<SaiMplsNextHopTraits::Attributes::RouterInterfaceId>(mplsAhk) =
          folly::to<sai_object_id_t>(
              object[AttributeName<SaiMplsNextHopTraits::Attributes::
                                       RouterInterfaceId>::value]
                  .asString());
      std::get<SaiMplsNextHopTraits::Attributes::Ip>(mplsAhk) =
          folly::IPAddress(
              object[AttributeName<SaiMplsNextHopTraits::Attributes::Ip>::value]
                  .asString());
      std::vector<sai_uint32_t> stack;
      for (const auto& label : object[AttributeName<
               SaiMplsNextHopTraits::Attributes::LabelStack>::value]) {
        stack.push_back(static_cast<sai_uint32_t>(label.asInt()));
      }
      std::get<SaiMplsNextHopTraits::Attributes::LabelStack>(mplsAhk) = stack;
      // Absent when warm booting from an agent that predates the attribute;
      // those next hops were created as the SAI default, SWAP.
      auto outsegType = object.get_ptr(
          AttributeName<SaiMplsNextHopTraits::Attributes::OutsegType>::value);
      std::get<std::optional<SaiMplsNextHopTraits::Attributes::OutsegType>>(
          mplsAhk) = outsegType ? folly::to<sai_int32_t>(outsegType->asString())
                                : SAI_OUTSEG_TYPE_SWAP;
      return detail::NextHopMemberKey(mplsAhk, weight);
    }

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    case SAI_NEXT_HOP_TYPE_SRV6_SIDLIST: {
      SaiSrv6SidlistNextHopTraits::AdapterHostKey srv6Ahk;
      std::get<SaiSrv6SidlistNextHopTraits::Attributes::TunnelId>(srv6Ahk) =
          folly::to<sai_object_id_t>(
              object[AttributeName<SaiSrv6SidlistNextHopTraits::Attributes::
                                       TunnelId>::value]
                  .asString());
      std::get<SaiSrv6SidlistNextHopTraits::Attributes::Srv6SidlistId>(
          srv6Ahk) =
          folly::to<sai_object_id_t>(
              object[AttributeName<SaiSrv6SidlistNextHopTraits::Attributes::
                                       Srv6SidlistId>::value]
                  .asString());
      return detail::NextHopMemberKey(srv6Ahk, weight);
    }
#endif

    default:
      XLOG(FATAL) << "unsupported next hop type " << type;
  }
  folly::assume_unreachable();
}
folly::dynamic nhopGroupAhkToFollyDynamic(
    const SaiNextHopGroupTraits::AdapterHostKey& ahk) {
  folly::dynamic json = folly::dynamic::object;
  folly::dynamic memberList = folly::dynamic::array;
  for (const auto& member : ahk.nhopMemberSet) {
    memberList.push_back(nhopMemberKeyToFollyDynamic(member));
  }
  json[AttributeName<
      SaiNextHopGroupTraits::Attributes::NextHopMemberList>::value] =
      memberList;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  json[AttributeName<SaiArsTraits::Attributes::Mode>::value] = ahk.mode;
#endif
  // Persist the SAI group type so a protection group reloads with the same
  // identity across warm boot instead of colliding with an ECMP group that has
  // the same members. Only written for non-ECMP groups, so flat-ECMP warm boot
  // state is unchanged.
  if (ahk.groupType != SAI_NEXT_HOP_GROUP_TYPE_ECMP) {
    json[kGroupType] = ahk.groupType;
  }
  // Hierarchical ECMP: persist the child group identities (recursively) and the
  // level. Only written for hierarchical groups, so flat-ECMP warm boot state
  // is unchanged.
  if (!ahk.childNextHopGroups.empty()) {
    folly::dynamic childList = folly::dynamic::array;
    for (const auto& child : ahk.childNextHopGroups) {
      childList.push_back(nhopGroupAhkToFollyDynamic(child));
    }
    json[kNextHopGroupList] = childList;
  }
  if (ahk.level != 0) {
    json[kLevel] = ahk.level;
  }
  return json;
}

SaiNextHopGroupTraits::AdapterHostKey follyDynamicToNhopGroupAhk(
    const folly::dynamic& json) {
  SaiNextHopGroupTraits::AdapterHostKey key;
  const auto& memberJson = json[AttributeName<
      SaiNextHopGroupTraits::Attributes::NextHopMemberList>::value];
  for (const auto& object : memberJson) {
    key.nhopMemberSet.insert(follyDynamicToNhopMemberKey(object));
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  key.mode = json[AttributeName<SaiArsTraits::Attributes::Mode>::value].asInt();
#endif
  if (auto it = json.find(kGroupType); it != json.items().end()) {
    key.groupType = folly::to<sai_int32_t>(it->second.asInt());
  }
  if (auto it = json.find(kNextHopGroupList); it != json.items().end()) {
    // std::set keeps the child list canonically ordered and unique, so the
    // reconstructed key compares/hashes identically regardless of serialized
    // order.
    for (const auto& childJson : it->second) {
      key.childNextHopGroups.insert(follyDynamicToNhopGroupAhk(childJson));
    }
  }
  if (auto it = json.find(kLevel); it != json.items().end()) {
    key.level = folly::to<sai_uint32_t>(it->second.asInt());
  }
  return key;
}
} // namespace

template <>
folly::dynamic
SaiObject<SaiNextHopGroupTraits>::adapterHostKeyToFollyDynamic() {
  return nhopGroupAhkToFollyDynamic(adapterHostKey_);
}

template <>
typename SaiNextHopGroupTraits::AdapterHostKey
SaiObject<SaiNextHopGroupTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  return follyDynamicToNhopGroupAhk(json);
}

template <>
folly::dynamic SaiObject<SaiLagTraits>::adapterHostKeyToFollyDynamic() {
  const auto& value = adapterHostKey_.value();
  std::string label{std::begin(value), std::end(value)};
  return label;
}

template <>
typename SaiLagTraits::AdapterHostKey
SaiObject<SaiLagTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  std::string label = json.asString();
  SaiLagTraits::AdapterHostKey::ValueType key{};
  std::copy(std::begin(label), std::end(label), std::begin(key));
  return key;
}

#if defined(BRCM_SAI_SDK_XGS_AND_DNX)

template <typename attrT>
void addOptionalAttrToArray(
    folly::dynamic& array,
    const SaiWredTraits::AdapterHostKey& adapterHostKey) {
  if (auto optionalAttr = std::get<std::optional<attrT>>(adapterHostKey)) {
    array.push_back(optionalAttr.value().value());
  } else {
    array.push_back("None");
  }
}

template <>
folly::dynamic SaiObject<SaiWredTraits>::adapterHostKeyToFollyDynamic() {
  folly::dynamic array = folly::dynamic::array;
  array.push_back(
      std::get<SaiWredTraits::Attributes::GreenEnable>(adapterHostKey_)
          .value());
  addOptionalAttrToArray<SaiWredTraits::Attributes::GreenMinThreshold>(
      array, adapterHostKey_);
  addOptionalAttrToArray<SaiWredTraits::Attributes::GreenMaxThreshold>(
      array, adapterHostKey_);
  addOptionalAttrToArray<SaiWredTraits::Attributes::GreenDropProbability>(
      array, adapterHostKey_);
  array.push_back(
      std::get<SaiWredTraits::Attributes::EcnMarkMode>(adapterHostKey_)
          .value());
  addOptionalAttrToArray<SaiWredTraits::Attributes::EcnGreenMinThreshold>(
      array, adapterHostKey_);
  addOptionalAttrToArray<SaiWredTraits::Attributes::EcnGreenMaxThreshold>(
      array, adapterHostKey_);
  addOptionalAttrToArray<SaiWredTraits::Attributes::EcnGreenMarkProbability>(
      array, adapterHostKey_);
  return array;
}

template <typename attrT>
void pupulateOptionalAttrtToKey(
    const folly::dynamic& array,
    SaiWredTraits::AdapterHostKey& adapterHostKey,
    int index) {
  if (!array[index].isString()) {
    std::get<std::optional<attrT>>(adapterHostKey) = array[index].asInt();
  }
}

template <>
typename SaiWredTraits::AdapterHostKey
SaiObject<SaiWredTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  SaiWredTraits::AdapterHostKey key;
  std::get<SaiWredTraits::Attributes::GreenEnable>(key) = json[0].asBool();
  pupulateOptionalAttrtToKey<SaiWredTraits::Attributes::GreenMinThreshold>(
      json, key, 1);
  pupulateOptionalAttrtToKey<SaiWredTraits::Attributes::GreenMaxThreshold>(
      json, key, 2);
  pupulateOptionalAttrtToKey<SaiWredTraits::Attributes::GreenDropProbability>(
      json, key, 3);
  std::get<SaiWredTraits::Attributes::EcnMarkMode>(key) = json[4].asInt();
  pupulateOptionalAttrtToKey<SaiWredTraits::Attributes::EcnGreenMinThreshold>(
      json, key, 5);
  pupulateOptionalAttrtToKey<SaiWredTraits::Attributes::EcnGreenMaxThreshold>(
      json, key, 6);
  // Handle upgrade: if 8th element exists, populate EcnGreenMarkProbability.
  // Otherwise, default to 100 for warmboot from old format (7 elements) to new
  // format (8 elements)
  if (json.size() > 7) {
    pupulateOptionalAttrtToKey<
        SaiWredTraits::Attributes::EcnGreenMarkProbability>(json, key, 7);
  } else {
    // Default to 100 to be same as the SAI attribute default.
    std::get<std::optional<SaiWredTraits::Attributes::EcnGreenMarkProbability>>(
        key) = 100;
  }
  return key;
}

#endif

template <>
folly::dynamic SaiObject<SaiAclTableTraits>::adapterHostKeyToFollyDynamic() {
  return adapterHostKey_;
}

template <>
typename SaiAclTableTraits::AdapterHostKey
SaiObject<SaiAclTableTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  return json.asString();
}

template <>
folly::dynamic SaiObject<SaiUdfGroupTraits>::adapterHostKeyToFollyDynamic() {
  return adapterHostKey_;
}

template <>
typename SaiUdfGroupTraits::AdapterHostKey
SaiObject<SaiUdfGroupTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  return json.asString();
}

template <>
folly::dynamic
SaiObject<SaiIsolationGroupTraits>::adapterHostKeyToFollyDynamic() {
  return adapterHostKey_;
}

template <>
typename SaiIsolationGroupTraits::AdapterHostKey
SaiObject<SaiIsolationGroupTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  return json.asString();
}

template <>
folly::dynamic
SaiObject<SaiVirtualRouterTraits>::adapterHostKeyToFollyDynamic() {
  return static_cast<uint32_t>(adapterHostKey_);
}

template <>
typename SaiVirtualRouterTraits::AdapterHostKey
SaiObject<SaiVirtualRouterTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  return RouterID(static_cast<uint32_t>(json.asInt()));
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
template <>
folly::dynamic SaiObject<SaiSrv6SidListTraits>::adapterHostKeyToFollyDynamic() {
  // Assert that NextHopId is set on the SID list
  auto nextHopIdOpt =
      std::get<std::optional<SaiSrv6SidListTraits::Attributes::NextHopId>>(
          attributes_);
  CHECK(nextHopIdOpt.has_value())
      << "SRv6 SID list must have NextHopId set for adapterHostKeyToFollyDynamic";

  folly::dynamic json = folly::dynamic::object;
  json["type"] =
      std::get<SaiSrv6SidListTraits::Attributes::Type>(adapterHostKey_).value();
  auto segmentListOpt =
      std::get<std::optional<SaiSrv6SidListTraits::Attributes::SegmentList>>(
          adapterHostKey_);
  if (segmentListOpt.has_value()) {
    folly::dynamic segments = folly::dynamic::array;
    for (const auto& ip6 : segmentListOpt.value().value()) {
      segments.push_back(
          fromSaiIpAddress(*reinterpret_cast<const sai_ip6_t*>(ip6.data()))
              .str());
    }
    json["segmentList"] = segments;
  }
  json["routerInterfaceId"] =
      folly::to<std::string>(std::get<RouterInterfaceSaiId>(adapterHostKey_));
  json["ip"] = std::get<folly::IPAddress>(adapterHostKey_).str();
  return json;
}

template <>
typename SaiSrv6SidListTraits::AdapterHostKey
SaiObject<SaiSrv6SidListTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  SaiSrv6SidListTraits::Attributes::Type type{
      static_cast<sai_int32_t>(json["type"].asInt())};
  std::optional<SaiSrv6SidListTraits::Attributes::SegmentList> segmentList;
  if (json.find("segmentList") != json.items().end()) {
    std::vector<std::array<uint8_t, 16>> segments;
    for (const auto& seg : json["segmentList"]) {
      std::array<uint8_t, 16> ip6{};
      toSaiIpAddressV6(
          folly::IPAddressV6(seg.asString()),
          reinterpret_cast<sai_ip6_t*>(ip6.data()));
      segments.push_back(ip6);
    }
    segmentList = segments;
  }
  auto rifId = RouterInterfaceSaiId(
      folly::to<sai_object_id_t>(json["routerInterfaceId"].asString()));
  auto ip = folly::IPAddress(json["ip"].asString());
  return SaiSrv6SidListTraits::AdapterHostKey{type, segmentList, rifId, ip};
}

// SRv6 encap and decap tunnels share one non-recoverable traits type; unset
// optional slots are simply omitted from the JSON so they round-trip as
// nullopt.
template <>
folly::dynamic SaiObject<SaiSrv6TunnelTraits>::adapterHostKeyToFollyDynamic() {
  using Attributes = SaiSrv6TunnelTraits::Attributes;
  folly::dynamic json = folly::dynamic::object;
  json["type"] = std::get<Attributes::Type>(adapterHostKey_).value();
  if (auto attr = std::get<std::optional<Attributes::UnderlayInterface>>(
          adapterHostKey_)) {
    json["underlayInterface"] = folly::to<std::string>(attr->value());
  }
  if (auto attr =
          std::get<std::optional<Attributes::EncapSrcIp>>(adapterHostKey_)) {
    json["encapSrcIp"] = attr->value().str();
  }
  if (auto attr =
          std::get<std::optional<Attributes::EncapTtlMode>>(adapterHostKey_)) {
    json["encapTtlMode"] = attr->value();
  }
  if (auto attr =
          std::get<std::optional<Attributes::EncapEcnMode>>(adapterHostKey_)) {
    json["encapEcnMode"] = attr->value();
  }
  if (auto attr =
          std::get<std::optional<Attributes::EncapDscpMode>>(adapterHostKey_)) {
    json["encapDscpMode"] = attr->value();
  }
  if (auto attr =
          std::get<std::optional<Attributes::DecapTtlMode>>(adapterHostKey_)) {
    json["decapTtlMode"] = attr->value();
  }
  if (auto attr =
          std::get<std::optional<Attributes::DecapDscpMode>>(adapterHostKey_)) {
    json["decapDscpMode"] = attr->value();
  }
  if (auto attr =
          std::get<std::optional<Attributes::DecapEcnMode>>(adapterHostKey_)) {
    json["decapEcnMode"] = attr->value();
  }
  if (auto attr = std::get<std::optional<Attributes::DecapQosDscpToTcMap>>(
          adapterHostKey_)) {
    json["decapQosDscpToTcMap"] = folly::to<std::string>(attr->value());
  }
  return json;
}

template <>
typename SaiSrv6TunnelTraits::AdapterHostKey
SaiObject<SaiSrv6TunnelTraits>::follyDynamicToAdapterHostKey(
    const folly::dynamic& json) {
  using Attributes = SaiSrv6TunnelTraits::Attributes;
  SaiSrv6TunnelTraits::AdapterHostKey key;
  auto has = [&](const char* k) { return json.find(k) != json.items().end(); };
  std::get<Attributes::Type>(key) =
      Attributes::Type{static_cast<sai_int32_t>(json["type"].asInt())};
  if (has("underlayInterface")) {
    std::get<std::optional<Attributes::UnderlayInterface>>(key) =
        Attributes::UnderlayInterface{
            folly::to<sai_object_id_t>(json["underlayInterface"].asString())};
  }
  if (has("encapSrcIp")) {
    std::get<std::optional<Attributes::EncapSrcIp>>(key) =
        Attributes::EncapSrcIp{folly::IPAddress(json["encapSrcIp"].asString())};
  }
  if (has("encapTtlMode")) {
    std::get<std::optional<Attributes::EncapTtlMode>>(key) =
        Attributes::EncapTtlMode{
            static_cast<sai_int32_t>(json["encapTtlMode"].asInt())};
  }
  if (has("encapEcnMode")) {
    std::get<std::optional<Attributes::EncapEcnMode>>(key) =
        Attributes::EncapEcnMode{
            static_cast<sai_int32_t>(json["encapEcnMode"].asInt())};
  }
  if (has("encapDscpMode")) {
    std::get<std::optional<Attributes::EncapDscpMode>>(key) =
        Attributes::EncapDscpMode{
            static_cast<sai_int32_t>(json["encapDscpMode"].asInt())};
  }
  if (has("decapTtlMode")) {
    std::get<std::optional<Attributes::DecapTtlMode>>(key) =
        Attributes::DecapTtlMode{
            static_cast<sai_int32_t>(json["decapTtlMode"].asInt())};
  }
  if (has("decapDscpMode")) {
    std::get<std::optional<Attributes::DecapDscpMode>>(key) =
        Attributes::DecapDscpMode{
            static_cast<sai_int32_t>(json["decapDscpMode"].asInt())};
  }
  if (has("decapEcnMode")) {
    std::get<std::optional<Attributes::DecapEcnMode>>(key) =
        Attributes::DecapEcnMode{
            static_cast<sai_int32_t>(json["decapEcnMode"].asInt())};
  }
  if (has("decapQosDscpToTcMap")) {
    std::get<std::optional<Attributes::DecapQosDscpToTcMap>>(key) =
        Attributes::DecapQosDscpToTcMap{
            folly::to<sai_object_id_t>(json["decapQosDscpToTcMap"].asString())};
  }
  return key;
}
#endif

} // namespace fboss
} // namespace facebook
