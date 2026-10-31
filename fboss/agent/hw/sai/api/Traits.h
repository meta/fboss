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

#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>

#include <fmt/core.h>
#include <array>
#include <type_traits>
#include <variant>

extern "C" {
#include <sai.h>
}

#include <boost/functional/hash.hpp>
#include "fboss/lib/TupleUtils.h"

namespace facebook::fboss {

/*
 * Helper metafunctions for C++ wrappers of non-primitive SAI types.
 *
 * e.g., folly::MacAddress for sai_mac_t, folly::IPAddress for
 * sai_ip_address_t, std::vector<uint64_t> for sai_object_list_t
 */
template <typename T>
struct WrappedSaiType {
  using value = T;
};

template <typename T>
struct IsVector : std::false_type {};

template <typename T>
struct IsVector<std::vector<T>> : std::true_type {};

template <>
struct WrappedSaiType<folly::MacAddress> {
  using value = sai_mac_t;
};

template <>
struct WrappedSaiType<folly::IPAddressV4> {
  using value = sai_ip4_t;
};

template <>
struct WrappedSaiType<folly::IPAddressV6> {
  using value = sai_ip6_t;
};

template <>
struct WrappedSaiType<folly::IPAddress> {
  using value = sai_ip_address_t;
};

template <>
struct WrappedSaiType<folly::CIDRNetwork> {
  using value = sai_ip_prefix_t;
};

#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
template <>
struct WrappedSaiType<sai_prbs_rx_state_t> {
  using value = sai_prbs_rx_state_t;
};
#endif

template <>
struct WrappedSaiType<std::vector<sai_object_id_t>> {
  using value = sai_object_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_int8_t>> {
  using value = sai_s8_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_uint8_t>> {
  using value = sai_u8_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_int16_t>> {
  using value = sai_s16_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_uint16_t>> {
  using value = sai_u16_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_int32_t>> {
  using value = sai_s32_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_uint32_t>> {
  using value = sai_u32_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_qos_map_t>> {
  using value = sai_qos_map_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_map_t>> {
  using value = sai_map_list_t;
};

#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
template <>
struct WrappedSaiType<std::vector<sai_u16_range_t>> {
  using value = sai_u16_range_list_t;
};
#endif

template <>
struct WrappedSaiType<std::vector<sai_port_lane_eye_values_t>> {
  using value = sai_port_eye_values_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_port_err_status_t>> {
  using value = sai_port_err_status_list_t;
};

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
template <>
struct WrappedSaiType<std::vector<sai_port_lane_latch_status_t>> {
  using value = sai_port_lane_latch_status_list_t;
};

template <>
struct WrappedSaiType<sai_latch_status_t> {
  using value = sai_latch_status_t;
};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
template <>
struct WrappedSaiType<std::vector<sai_port_frequency_offset_ppm_values_t>> {
  using value = sai_port_frequency_offset_ppm_list_t;
};

template <>
struct WrappedSaiType<std::vector<sai_port_snr_values_t>> {
  using value = sai_port_snr_list_t;
};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
struct SaiJsonString {
  std::string value;

  SaiJsonString() = default;
  SaiJsonString(std::string s) : value(std::move(s)) {}
  SaiJsonString(const char* s) : value(s) {}

  bool operator==(const SaiJsonString& other) const {
    return value == other.value;
  }
  bool operator!=(const SaiJsonString& other) const {
    return !(*this == other);
  }

  // Returns true if the JSON string is empty
  // or contains only empty attributes (Cisco SDK for SerDes Custom Collection)
  // This is used to treat SDK-returned empty JSON as nullopt to avoid
  // warmboot state mismatch.
  bool empty() const {
    std::string_view effective = value;
    if (!effective.empty() && effective.back() == '\0') {
      effective.remove_suffix(1);
    }
    // Todo: Cisco returns "{\"attributes\":[]} by default, will ask to change
    // to general "{}"
    return effective.empty() || effective == "{}" ||
        effective == "{\"attributes\":[]}";
  }

  std::string str() const {
    return value;
  }
};

template <>
struct WrappedSaiType<SaiJsonString> {
  using value = sai_json_t;
};
#endif

template <>
struct WrappedSaiType<std::array<char, 32>> {
  using value = char[32];
};

template <size_t size>
struct WrappedSaiType<std::array<uint8_t, size>> {
  using value = uint8_t[size];
};

template <>
struct WrappedSaiType<std::vector<sai_system_port_config_t>> {
  using value = sai_system_port_config_list_t;
};

template <>
struct WrappedSaiType<std::vector<std::array<uint8_t, 16>>> {
  using value = sai_segment_list_t;
};

template <typename T>
class AclEntryField {
 public:
  AclEntryField() {};
  AclEntryField(T dataAndMask) : dataAndMask_(dataAndMask) {}
  const T& getDataAndMask() const {
    return dataAndMask_;
  }

  void setDataAndMask(T dataAndMask) {
    dataAndMask_ = std::move(dataAndMask);
  }

  std::string str() const {
    if constexpr (
        std::is_same_v<
            T,
            std::pair<std::vector<sai_uint8_t>, std::vector<sai_uint8_t>>>) {
      std::ostringstream retStr;

      if (!dataAndMask_.first.empty()) {
        retStr << "data: ";
        std::copy(
            dataAndMask_.first.begin(),
            dataAndMask_.first.end(),
            std::ostream_iterator<int>(retStr, ", "));
        retStr << "mask: ";
        std::copy(
            dataAndMask_.second.begin(),
            dataAndMask_.second.end(),
            std::ostream_iterator<int>(retStr, ", "));
      }

      return retStr.str();
    } else if constexpr (std::is_same_v<
                             T,
                             std::pair<
                                 std::vector<sai_object_id_t>,
                                 std::vector<sai_object_id_t>>>) {
      std::ostringstream retStr;

      if (!dataAndMask_.first.empty()) {
        retStr << "data: ";
        std::copy(
            dataAndMask_.first.begin(),
            dataAndMask_.first.end(),
            std::ostream_iterator<sai_object_id_t>(retStr, ", "));
      }

      return retStr.str();
    } else {
      return folly::to<std::string>(
          "data: ", dataAndMask_.first, " mask: ", dataAndMask_.second);
    }
  }

 private:
  T dataAndMask_;
};

template <typename T>
struct WrappedSaiType<AclEntryField<T>> {
  using value = sai_acl_field_data_t;
};

template <typename T>
bool operator==(const AclEntryField<T>& lhs, const AclEntryField<T>& rhs) {
  return lhs.getDataAndMask() == rhs.getDataAndMask();
}

template <typename T>
std::size_t hash_value(const AclEntryField<T>& key) {
  std::size_t seed = 0;

  if constexpr (std::is_same_v<
                    T,
                    std::pair<folly::MacAddress, folly::MacAddress>>) {
    boost::hash_combine(
        seed, std::hash<folly::MacAddress>()(key.getDataAndMask().first));
  } else {
    boost::hash_combine(seed, boost::hash_value(key.getDataAndMask()));
  }

  return seed;
}

/*
 * AclEntryField's data and mask always have the same data type:
 * Packet matching takes into account only the bits in the data which have a
 * corresponding '1' in the same position of the mask. Bits in the data which
 * have corresponding '0' bit in the same position of the mask are ignored.
 */
using AclEntryFieldU8 = AclEntryField<std::pair<sai_uint8_t, sai_uint8_t>>;
using AclEntryFieldU16 = AclEntryField<std::pair<sai_uint16_t, sai_uint16_t>>;
using AclEntryFieldU32 = AclEntryField<std::pair<sai_uint32_t, sai_uint32_t>>;
using AclEntryFieldIpV6 =
    AclEntryField<std::pair<folly::IPAddressV6, folly::IPAddressV6>>;
using AclEntryFieldIpV4 =
    AclEntryField<std::pair<folly::IPAddressV4, folly::IPAddressV4>>;
using AclEntryFieldMac =
    AclEntryField<std::pair<folly::MacAddress, folly::MacAddress>>;
using AclEntryFieldU8List = AclEntryField<
    std::pair<std::vector<sai_uint8_t>, std::vector<sai_uint8_t>>>;

/*
 * Mask is not needed for sai_object_id_t Acl Entry field.
 * Thus, there is no oid field in sai_acl_field_data_mask_t.
 * oid is u64, but unfortunately, u64 was not added to
 * sai_acl_field_data_mask_t till SAI 1.6, so use u32.
 * This will be ignored by the implementation anyway.
 */
using AclEntryFieldSaiObjectIdT =
    AclEntryField<std::pair<sai_object_id_t, sai_uint32_t>>;

/*
 * sai_acl_field_data_mask_t has no objlist member — mask is not applicable
 * for object list fields (see SAI spec saitypes.h). The mask vector is
 * always empty.
 */
using AclEntryFieldSaiObjectIdList = AclEntryField<
    std::pair<std::vector<sai_object_id_t>, std::vector<sai_object_id_t>>>;

template <typename T>
class AclEntryAction {
 public:
  AclEntryAction() {};
  AclEntryAction(T data) : data_(data) {}
  const T& getData() const {
    return data_;
  }

  void setData(T data) {
    data_ = std::move(data);
  }

  std::string str() const {
    if constexpr (std::is_same_v<T, std::vector<sai_object_id_t>>) {
      std::ostringstream retStr;

      if (!data_.empty()) {
        std::copy(
            data_.begin(),
            data_.end() - 1,
            std::ostream_iterator<int>(retStr, ","));
        retStr << data_.back();
      }

      return retStr.str();
    } else {
      return folly::to<std::string>("data: ", data_);
    }
  }

 private:
  T data_;
};

template <typename T>
struct WrappedSaiType<AclEntryAction<T>> {
  using value = sai_acl_action_data_t;
};

template <typename T>
bool operator==(const AclEntryAction<T>& lhs, const AclEntryAction<T>& rhs) {
  return lhs.getData() == rhs.getData();
}

template <typename T>
std::size_t hash_value(const AclEntryAction<T>& key) {
  std::size_t seed = 0;
  boost::hash_combine(seed, boost::hash_value(key.getData()));
  return seed;
}

using AclEntryActionBool = AclEntryAction<bool>;
using AclEntryActionU8 = AclEntryAction<sai_uint8_t>;
using AclEntryActionU32 = AclEntryAction<sai_uint32_t>;
using AclEntryActionSaiObjectIdT = AclEntryAction<sai_object_id_t>;
using AclEntryActionSaiObjectIdList =
    AclEntryAction<std::vector<sai_object_id_t>>;

template <typename T>
struct IsSaiTypeWrapper
    : std::negation<std::is_same<typename WrappedSaiType<T>::value, T>> {};

template <typename T>
concept SaiTypeWrapper = IsSaiTypeWrapper<T>::value;

/*
 * Helper metafunctions for resolving two types in the SAI
 * sai_attribute_value_t union being aliases. This results in SaiAttribute
 * not being able to select the correct union member from sai_attribute_t
 * from the SAI type alone. i.e., how could it choose between attr.value.oid
 * and attr.value.u64 based on just the fact that it is extracting a 64 bit
 * unsigned integer?
 *
 * e.g., sai_object_id_t vs uint64_t and sai_ip4_t vs uint32_t;
 */
struct SaiObjectIdT {};

template <typename T>
struct DuplicateTypeFixer {
  using value = T;
};

template <>
struct DuplicateTypeFixer<SaiObjectIdT> {
  using value = sai_object_id_t;
};

template <>
struct DuplicateTypeFixer<folly::IPAddressV4> {
  using value = sai_ip4_t;
};

template <typename T>
struct IsDuplicateSaiType
    : std::negation<std::is_same<typename DuplicateTypeFixer<T>::value, T>> {};

template <typename T>
struct IsSaiAttribute : public std::false_type {};

template <typename AttrT>
struct IsSaiAttribute<std::optional<AttrT>> : public IsSaiAttribute<AttrT> {};

template <typename AttrT>
concept SaiAttributeType =
    IsSaiAttribute<std::remove_reference_t<AttrT>>::value;

template <typename T>
struct IsSaiEntryStruct : public std::false_type {};

template <typename SaiObjectTraits>
struct AdapterKeyIsEntryStruct
    : public IsSaiEntryStruct<typename SaiObjectTraits::AdapterKey> {};

template <typename SaiObjectTraits>
struct AdapterKeyIsObjectId
    : std::negation<AdapterKeyIsEntryStruct<SaiObjectTraits>> {};

template <typename SaiObjectTraits>
concept ObjectIdSaiObject = AdapterKeyIsObjectId<SaiObjectTraits>::value;

template <typename SaiObjectTraits>
concept EntryStructSaiObject = AdapterKeyIsEntryStruct<SaiObjectTraits>::value;

template <typename SaiObjectTraits, typename ApiT>
concept SaiObjectForApi = requires { typename SaiObjectTraits::SaiApiT; } &&
    std::is_same_v<typename SaiObjectTraits::SaiApiT, ApiT>;

template <typename T>
struct IsTupleOfSaiAttributes : public std::false_type {};

template <typename... AttrTs>
struct IsTupleOfSaiAttributes<std::tuple<AttrTs...>>
    : public std::conjunction<IsSaiAttribute<AttrTs>...> {};

template <typename TupleT>
concept SaiAttributeTuple =
    IsTupleOfSaiAttributes<std::remove_cvref_t<TupleT>>::value;

template <typename SaiObjectTraits>
struct IsSaiObjectOwnedByAdapter : public std::false_type {};

/*
 * Hardware can delete objects of some types on its own, without the agent
 * asking. For those, an object FBOSS still tracks may already be gone from
 * hardware, so ITEM_NOT_FOUND is an expected outcome rather than a fatal one.
 */
template <typename SaiObjectTraits>
struct SaiObjectMayBeMissingInHw : public std::false_type {};

template <typename SaiObjectTraits>
struct SaiObjectHasStats : public std::false_type {};

template <typename SaiObjectTraits>
concept SaiObjectWithStats = SaiObjectHasStats<SaiObjectTraits>::value;

template <typename SaiObjectTraits>
struct SaiObjectHasConditionalAttributes : public std::false_type {};

template <typename ObjectTrait>
using AdapterHostKeyTrait = typename ObjectTrait::AdapterHostKey;

/*
 * For a condition object trait, define adapter host key type
 * Adapter host key type for condition object trait is a variant of all object
 * traits forming a condition object trait
 */
template <typename... ConditionObjectTraits>
struct ConditionAdapterHostKeyTraits {
  static_assert(
      (... && SaiObjectHasConditionalAttributes<ConditionObjectTraits>::value),
      "non condition object trait can not use ConditionAdapterHostKeyTraits");
  using AdapterHostKey =
      typename std::variant<AdapterHostKeyTrait<ConditionObjectTraits>...>;
};

/*
 * For a condition object trait, define adapter key type
 * Adapter key type for condition object trait is will remain of one type since
 * each object trait member of condition object trait uses same SAI API.
 */
template <typename AdapterKeyType, typename... ConditionObjectTraits>
struct ConditionAdapterKeyTraits {
  static_assert(
      (... && SaiObjectHasConditionalAttributes<ConditionObjectTraits>::value),
      "non condition object trait can not use ConditionAdapterHostKeyTraits");
  using AdapterKey = AdapterKeyType;
};

/*
 * Condition object trait is a trait for an object which uses condition
 * attribute. a condition attribute leads to more than one object traits for
 * same object api. a typical example is next hop api, which needs condition
 * object trait. since each condition object trait is composed of more than one
 * object traits below traits gives a definition of this composition.
 */
template <typename... ObjectTraits>
struct ConditionObjectTraits {
  static_assert(
      (... && SaiObjectHasConditionalAttributes<ObjectTraits>::value),
      "non condition object trait can not use can not use  on ConditionObjectTraits");
  using ObjectTrait = std::tuple<ObjectTraits...>;
  static auto constexpr ObjectTraitCount = sizeof...(ObjectTraits);
  using ConditionAttributes = std::remove_const_t<
      typename std::tuple_element_t<0, ObjectTrait>::ConditionAttributes>;
  using AdapterHostKey =
      typename ConditionAdapterHostKeyTraits<ObjectTraits...>::AdapterHostKey;
  template <typename AdapterKeyType>
  using AdapterKey =
      typename ConditionAdapterKeyTraits<AdapterKeyType, ObjectTraits...>::
          AdapterKey;
};

template <typename ObjectTraits>
struct GetObjectKeySupported : std::true_type {};

template <typename T>
struct IsSaiExtensionAttribute : std::false_type {};
} // namespace facebook::fboss
