/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 * *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/lib/TupleUtils.h"

#include <fmt/format.h>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

folly::StringPiece saiApiTypeToString(sai_api_t apiType);
folly::StringPiece saiObjectTypeToString(sai_object_type_t objectType);
folly::StringPiece saiStatusToString(sai_status_t status);
sai_log_level_t saiLogLevelFromString(const std::string& logLevel);
folly::StringPiece packetRxReasonToString(cfg::PacketRxReason rxReason);

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
std::string saiSwitchSdkHealthSeverityToString(
    const sai_switch_asic_sdk_health_severity_t& severity);
std::string saiSwitchSdkHealthCategoryToString(
    const sai_switch_asic_sdk_health_category_t& category);
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 15, 0)
std::string saiSerTypeToString(const sai_ser_type_t& sai_ser_type);
std::string saiSerCorrectionTypeToString(
    const sai_ser_correction_type_t& sai_ser_correction_type);
std::string saiSerLogTypeToString(sai_ser_log_type_t sai_ser_log_type);
#endif
} // namespace facebook::fboss

/*
 * fmt specializations for the types that we use in SaiApi
 * specifically:
 * any c++ value types used in attributes without one (e.g., folly::MacAddress)
 * sai attribute id enums
 * SaiAttribute itself
 * std::tuple (of attributes, ostensibly)
 */
namespace fmt {

// Formatting for folly::MacAddress
template <>
struct formatter<folly::MacAddress> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const folly::MacAddress& mac, FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", mac.toString());
  }
};

// Formatting for folly::IpAddress
template <>
struct formatter<folly::IPAddress> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const folly::IPAddress& ip, FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", ip.str());
  }
};

// Formatting for folly::IPAddressV6
template <>
struct formatter<folly::IPAddressV6> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const folly::IPAddressV6& ip, FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", ip.str());
  }
};

// Formatting for AdapterKeys which are SAI entry structs
template <typename AdapterKeyType>
  requires facebook::fboss::IsSaiEntryStruct<AdapterKeyType>::value
struct formatter<AdapterKeyType, char> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const AdapterKeyType& key, FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", key.toString());
  }
};

// Formatting for std::variant
template <typename... Ts>
struct formatter<std::variant<Ts...>> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const std::variant<Ts...>& var, FormatContext& ctx) const {
    auto formatVariant = [&ctx](auto&& val) {
      return format_to(ctx.out(), "{}", val);
    };
    return std::visit(formatVariant, var);
  }
};

// Formatting for SaiAttributes
template <
    typename AttrEnumT,
    AttrEnumT AttrEnum,
    typename DataT,
    typename DefaultGetterT>
struct formatter<
    facebook::fboss::SaiAttribute<AttrEnumT, AttrEnum, DataT, DefaultGetterT>> {
  using AttrT =
      facebook::fboss::SaiAttribute<AttrEnumT, AttrEnum, DataT, DefaultGetterT>;

  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const AttrT& attr, FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "{}: {}",
        facebook::fboss::AttributeName<AttrT>::value,
        attr.value());
  }
};

// Formatting for std::monostate
template <>
struct formatter<std::monostate> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const std::monostate& unit, FormatContext& ctx) const {
    return format_to(ctx.out(), "(monostate)");
  }
};

// Formatting for empty std::tuple
template <>
struct formatter<std::tuple<>> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const std::tuple<>& tup, FormatContext& ctx) const {
    return format_to(ctx.out(), "()");
  }
};

// Formatting for std::optional<SaiAttribute>
template <typename T>
struct formatter<std::optional<T>> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const std::optional<T>& opt, FormatContext& ctx) const {
    static_assert(
        facebook::fboss::IsSaiAttribute<T>::value,
        "format(std::optional) only valid for SaiAttributes");
    if (opt) {
      return format_to(ctx.out(), "{}", opt.value());
    } else {
      return format_to(ctx.out(), "nullopt");
    }
  }
};

// Formatting for sai_qos_map_t
template <>
struct formatter<sai_qos_map_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_qos_map_t& qosMap, FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "(qos_mapping: key.dscp: {}, key.tc: {}, "
        "value.tc: {}, value.queue_index: {})",
        qosMap.key.dscp,
        qosMap.key.tc,
        qosMap.value.tc,
        qosMap.value.queue_index);
  }
};

// Formatting for sai_map_t
template <>
struct formatter<sai_map_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_map_t& map, FormatContext& ctx) const {
    return format_to(
        ctx.out(), "(mapping: key: {}, value: {})", map.key, map.value);
  }
};

#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
// Formatting for sai_u16_range_t
template <>
struct formatter<sai_u16_range_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_u16_range_t& range, FormatContext& ctx) const {
    return format_to(
        ctx.out(), "(u16_range: min: {}, max: {})", range.min, range.max);
  }
};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1)
template <>
struct formatter<sai_u64_range_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_u64_range_t& range, FormatContext& ctx) const {
    return format_to(
        ctx.out(), "(u64_range: min: {}, max: {})", range.min, range.max);
  }
};
#endif

// Formatting for sai_port_eye_values_list_t
template <>
struct formatter<sai_port_lane_eye_values_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_port_lane_eye_values_t& eyeVal, FormatContext& ctx)
      const {
    return format_to(
        ctx.out(),
        "(eye_value: eyeVal.lane: {}, "
        "eyeVal.left: {}, eyeVal.right: {}, "
        "eyeVal.up: {}, eyeVal.down: {})",
        eyeVal.lane,
        eyeVal.left,
        eyeVal.right,
        eyeVal.up,
        eyeVal.down);
  }
};

// Formatting for sai_object_type_t
template <>
struct formatter<sai_object_type_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_object_type_t& type, FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", static_cast<int>(type));
  }
};

#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
// Formatting for sai_port_prbs_rx_status_t
template <>
struct formatter<sai_port_prbs_rx_status_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_port_prbs_rx_status_t& status, FormatContext& ctx)
      const {
    return format_to(ctx.out(), "{}", static_cast<int>(status));
  }
};
// Formatting for sai_prbs_rx_state_t
template <>
struct formatter<sai_prbs_rx_state_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_prbs_rx_state_t& prbsStats, FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "(PRBS Rx stats:{}, "
        "PRBS error count: {})",
        prbsStats.rx_status,
        prbsStats.error_count);
  }
};
#endif

// Formatting for sai_port_err_status_list_t
template <>
struct formatter<sai_port_err_status_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_port_err_status_t& errStatus, FormatContext& ctx)
      const {
    return format_to(ctx.out(), "{}", static_cast<int>(errStatus));
  }
};

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
// Formatting for sai_port_lane_latch_status_list_t
template <>
struct formatter<sai_port_lane_latch_status_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
      const sai_port_lane_latch_status_t& latchStatus,
      FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "(lane_latch_status: lane_latch_status.lane: {}, "
        "lane_latch_status.value.current_status: {}, lane_latch_status.value.changed: {}",
        latchStatus.lane,
        latchStatus.value.current_status,
        latchStatus.value.changed);
  }
};

// Formatting for sai_latch_status_t
template <>
struct formatter<sai_latch_status_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_latch_status_t& latchStatus, FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "latch_status.current_status: {}, latch_status.changed: {}",
        latchStatus.current_status,
        latchStatus.changed);
  }
};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
// Formatting for sai_port_frequency_offset_ppm_list_t
template <>
struct formatter<sai_port_frequency_offset_ppm_values_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
      const sai_port_frequency_offset_ppm_values_t& ppmValues,
      FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "(rx_ppm: rx_ppm.lane: {}, "
        "rx_ppm.value: {}",
        ppmValues.lane,
        ppmValues.ppm);
  }
};

template <>
struct formatter<sai_port_snr_values_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_port_snr_values_t& snrValues, FormatContext& ctx)
      const {
    return format_to(
        ctx.out(),
        "(rx_snr: rx_snr.lane: {}, "
        "rx_snr.value: {}",
        snrValues.lane,
        snrValues.snr);
  }
};
#endif

// Formatting for AclEntryField<T>
template <typename T>
struct formatter<facebook::fboss::AclEntryField<T>> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>

  auto format(
      const facebook::fboss::AclEntryField<T>& aclEntryField,
      FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", aclEntryField.str());
  }
};

// Formatting for AclEntryAction<T>
template <typename T>
struct formatter<facebook::fboss::AclEntryAction<T>> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>

  auto format(
      const facebook::fboss::AclEntryAction<T>& aclEntryAction,
      FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", aclEntryAction.str());
  }
};

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
// Formatting for SaiJsonString
template <>
struct formatter<facebook::fboss::SaiJsonString> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
      const facebook::fboss::SaiJsonString& jsonString,
      FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", jsonString.str());
  }
};
#endif

// Formatting for sai_u32_range_t
template <>
struct formatter<sai_u32_range_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>

  auto format(const sai_u32_range_t& u32Range, FormatContext& ctx) const {
    return format_to(
        ctx.out(), "u32 range: min: {}, max: {}", u32Range.min, u32Range.max);
  }
};

// Formatting for sai_s32_range_t
template <>
struct formatter<sai_s32_range_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>

  auto format(const sai_s32_range_t& s32Range, FormatContext& ctx) const {
    return format_to(
        ctx.out(), "s32 range: min: {}, max: {}", s32Range.min, s32Range.max);
  }
};

// formatter for extension attributes
template <typename T>
  requires facebook::fboss::IsSaiExtensionAttribute<T>::value
struct formatter<T, char> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const T& attr, FormatContext& ctx) const {
    // TODO: implement this
    return format_to(
        ctx.out(),
        "{}: {}",
        facebook::fboss::AttributeName<T>::value,
        attr.value());
  }
};

// Formatting for char[32]
template <>
struct formatter<SaiCharArray32> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>

  auto format(const SaiCharArray32& data, FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", std::string(data.begin(), data.end()));
  }
};

template <>
struct formatter<facebook::fboss::SaiPortDescriptor> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
      const facebook::fboss::SaiPortDescriptor& port,
      FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", port.str());
  }
};

// Formatting for sai_system_port_config_t
template <>
struct formatter<sai_system_port_config_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_system_port_config_t& sysPortConf, FormatContext& ctx)
      const {
    return format_to(
        ctx.out(),
        "(port_id: {}, switch_id: {}, "
        " attached_core_index: {}, attached_core_port_index: {}, "
        " speed: {}, num_voqs: {})",
        sysPortConf.port_id,
        sysPortConf.attached_switch_id,
        sysPortConf.attached_core_index,
        sysPortConf.attached_core_port_index,
        sysPortConf.speed,
        sysPortConf.num_voq);
  }
};

template <>
struct formatter<sai_fabric_port_reachability_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_fabric_port_reachability_t& reach, FormatContext& ctx)
      const {
    return format_to(
        ctx.out(),
        "(SwitchId: {}, "
        " Reachable: {})",
        reach.switch_id,
        reach.reachable);
  }
};

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)

template <>
struct formatter<sai_switch_asic_sdk_health_severity_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
      const sai_switch_asic_sdk_health_severity_t& arg,
      FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "{}",
        facebook::fboss::saiSwitchSdkHealthSeverityToString(arg));
  }
};

template <>
struct formatter<sai_switch_asic_sdk_health_category_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
      const sai_switch_asic_sdk_health_category_t& arg,
      FormatContext& ctx) const {
    return format_to(
        ctx.out(),
        "{}",
        facebook::fboss::saiSwitchSdkHealthCategoryToString(arg));
  }
};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 15, 0)

template <>
struct formatter<sai_ser_type_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_ser_type_t& arg, FormatContext& ctx) const {
    return format_to(ctx.out(), "{}", facebook::fboss::saiSerTypeToString(arg));
  }
};

template <>
struct formatter<sai_ser_correction_type_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_ser_correction_type_t& arg, FormatContext& ctx) const {
    return format_to(
        ctx.out(), "{}", facebook::fboss::saiSerCorrectionTypeToString(arg));
  }
};

template <>
struct formatter<sai_ser_log_type_t> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const sai_ser_log_type_t& arg, FormatContext& ctx) const {
    return format_to(
        ctx.out(), "{}", facebook::fboss::saiSerLogTypeToString(arg));
  }

  template <typename FormatContext>
  auto format(const std::vector<sai_ser_log_type_t>& arg, FormatContext& ctx)
      const {
    return format_to(ctx.out(), "[{}]", fmt::join(arg, ", "));
  }
};
#endif

} // namespace fmt
