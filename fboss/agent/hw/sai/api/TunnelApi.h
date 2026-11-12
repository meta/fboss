// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiDefaultAttributeValues.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class TunnelApi;

namespace detail {

template <sai_tunnel_type_t type>
struct TunnelAttributesTypes;

template <>
struct TunnelAttributesTypes<SAI_TUNNEL_TYPE_IPINIP> {
  using EnumType = sai_tunnel_attr_t;
  using OverlayInterface =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_OVERLAY_INTERFACE, SaiObjectIdT>;
  using EncapSrcIp =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_SRC_IP, folly::IPAddress>;
  using DecapTtlMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_DECAP_TTL_MODE, sai_int32_t>;
  using DecapDscpMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_DECAP_DSCP_MODE, sai_int32_t>;
  using DecapEcnMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_DECAP_ECN_MODE, sai_int32_t>;
  // Only SRv6 decap tunnels bind a DSCP->TC map, and
  // SAI_TUNNEL_ATTR_DECAP_QOS_DSCP_TO_TC_MAP does not exist in SAI specs older
  // than the SRv6 tunnel support below.
  using DecapQosDscpToTcMap = void;
  using EncapTtlMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_TTL_MODE, sai_int32_t>;
  using EncapDscpMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_DSCP_MODE, sai_int32_t>;
  using EncapEcnMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_ECN_MODE, sai_int32_t>;
  // VXLAN-only attributes (tunnel maps).
  using EncapDstIp = void;
  using EncapMappers = void;
  using DecapMappers = void;
};

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
// Encap-only and decap-only attributes are unset on the opposite tunnel kind
// (e.g. UnderlayInterface/EncapSrcIp on a decap tunnel), and the SDK returns
// NOT_SUPPORTED when GET-ing them during warm boot store reload. Default
// getters make that GET fall back to a benign default instead of throwing. (The
// AdapterHostKey itself is serialized and restored verbatim; see
// AdapterHostKeyWarmbootRecoverable.)
template <>
struct TunnelAttributesTypes<SAI_TUNNEL_TYPE_SRV6> {
  using EnumType = sai_tunnel_attr_t;
  using UnderlayInterface = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE,
      SaiObjectIdT,
      SaiObjectIdDefault>;
  using OverlayInterface = void;
  using EncapSrcIp = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_ENCAP_SRC_IP,
      folly::IPAddress,
      SaiIpAddressDefault>;
  using DecapTtlMode = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_DECAP_TTL_MODE,
      sai_int32_t,
      SaiIntDefault<sai_int32_t>>;
  using DecapDscpMode = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_DECAP_DSCP_MODE,
      sai_int32_t,
      SaiIntDefault<sai_int32_t>>;
  using DecapEcnMode = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_DECAP_ECN_MODE,
      sai_int32_t,
      SaiIntDefault<sai_int32_t>>;
  // Unset on tunnels that do not classify on the inner DSCP, so it too needs a
  // default getter.
  using DecapQosDscpToTcMap = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_DECAP_QOS_DSCP_TO_TC_MAP,
      SaiObjectIdT,
      SaiObjectIdDefault>;
  using EncapTtlMode = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_ENCAP_TTL_MODE,
      sai_int32_t,
      SaiIntDefault<sai_int32_t>>;
  using EncapDscpMode = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_ENCAP_DSCP_MODE,
      sai_int32_t,
      SaiIntDefault<sai_int32_t>>;
  using EncapEcnMode = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_ENCAP_ECN_MODE,
      sai_int32_t,
      SaiIntDefault<sai_int32_t>>;
  // VXLAN-only attributes (tunnel maps).
  using EncapDstIp = void;
  using EncapMappers = void;
  using DecapMappers = void;
};
#endif

// VXLAN tunnels (SAI_TUNNEL_TYPE_VXLAN) do overlay routing via tunnel maps:
// EncapMappers carries the VIRTUAL_ROUTER_ID_TO_VNI map and DecapMappers the
// VNI_TO_VIRTUAL_ROUTER_ID map. Our tunnels use P2MP peer mode and have no
// overlay interface. UDP sport attributes are intentionally omitted (defaults
// apply).
template <>
struct TunnelAttributesTypes<SAI_TUNNEL_TYPE_VXLAN> {
  using EnumType = sai_tunnel_attr_t;
  using OverlayInterface = void;
  using EncapSrcIp =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_SRC_IP, folly::IPAddress>;
  // EncapDstIp is only valid with P2P peer mode; our tunnels are P2MP, which
  // is also the SAI default, so peer mode needs no attribute.
  using EncapDstIp = void;
  using DecapTtlMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_DECAP_TTL_MODE, sai_int32_t>;
  using DecapDscpMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_DECAP_DSCP_MODE, sai_int32_t>;
  using DecapEcnMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_DECAP_ECN_MODE, sai_int32_t>;
  using DecapQosDscpToTcMap = void;
  using EncapTtlMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_TTL_MODE, sai_int32_t>;
  using EncapDscpMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_DSCP_MODE, sai_int32_t>;
  using EncapEcnMode =
      SaiAttribute<EnumType, SAI_TUNNEL_ATTR_ENCAP_ECN_MODE, sai_int32_t>;
  using EncapMappers = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_ENCAP_MAPPERS,
      std::vector<sai_object_id_t>>;
  using DecapMappers = SaiAttribute<
      EnumType,
      SAI_TUNNEL_ATTR_DECAP_MAPPERS,
      std::vector<sai_object_id_t>>;
};

template <typename Attributes, sai_tunnel_type_t type>
struct TunnelTraitsAttributes;

template <typename Attributes>
struct TunnelTraitsAttributes<Attributes, SAI_TUNNEL_TYPE_IPINIP> {
  using CreateAttributes = std::tuple<
      typename Attributes::Type,
      typename Attributes::UnderlayInterface,
      typename Attributes::OverlayInterface,
      std::optional<typename Attributes::DecapTtlMode>,
      std::optional<typename Attributes::DecapDscpMode>,
      std::optional<typename Attributes::DecapEcnMode>,
      std::optional<typename Attributes::EncapSrcIp>,
      std::optional<typename Attributes::EncapTtlMode>,
      std::optional<typename Attributes::EncapDscpMode>>;
  using AdapterHostKey = CreateAttributes;
};

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
// SRv6 tunnels (SAI_TUNNEL_TYPE_SRV6) cover both encap and decap: an encap
// tunnel sets EncapSrcIp/UnderlayInterface/Encap* modes; a decap tunnel sets
// only the Decap* modes. All are optional so a single store/traits holds both
// kinds without a same-object-type/same-Type warmboot collision.
template <typename Attributes>
struct TunnelTraitsAttributes<Attributes, SAI_TUNNEL_TYPE_SRV6> {
  using CreateAttributes = std::tuple<
      typename Attributes::Type,
      std::optional<typename Attributes::UnderlayInterface>,
      std::optional<typename Attributes::EncapSrcIp>,
      std::optional<typename Attributes::EncapTtlMode>,
      std::optional<typename Attributes::EncapEcnMode>,
      std::optional<typename Attributes::EncapDscpMode>,
      std::optional<typename Attributes::DecapTtlMode>,
      std::optional<typename Attributes::DecapDscpMode>,
      std::optional<typename Attributes::DecapEcnMode>,
      std::optional<typename Attributes::DecapQosDscpToTcMap>>;
  using AdapterHostKey = CreateAttributes;
};
#endif

// VXLAN tunnels cover both encap and decap. Only Type and UnderlayInterface
// are mandatory on create; the rest (src IP, TTL/DSCP/ECN modes, encap/decap
// tunnel maps) are optional with SAI defaults.
template <typename Attributes>
struct TunnelTraitsAttributes<Attributes, SAI_TUNNEL_TYPE_VXLAN> {
  using CreateAttributes = std::tuple<
      typename Attributes::Type,
      typename Attributes::UnderlayInterface,
      std::optional<typename Attributes::EncapSrcIp>,
      std::optional<typename Attributes::EncapTtlMode>,
      std::optional<typename Attributes::EncapDscpMode>,
      std::optional<typename Attributes::EncapEcnMode>,
      std::optional<typename Attributes::EncapMappers>,
      std::optional<typename Attributes::DecapTtlMode>,
      std::optional<typename Attributes::DecapDscpMode>,
      std::optional<typename Attributes::DecapEcnMode>,
      std::optional<typename Attributes::DecapMappers>>;
  using AdapterHostKey = CreateAttributes;
};

} // namespace detail

template <sai_tunnel_type_t type>
struct SaiTunnelTraitsT {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_TUNNEL;
  using SaiApiT = TunnelApi;
  struct Attributes {
    using EnumType = sai_tunnel_attr_t;
    using Type = SaiAttribute<EnumType, SAI_TUNNEL_ATTR_TYPE, sai_int32_t>;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    // UnderlayInterface is the same base attribute for both tunnel kinds; only
    // SRv6 decorates it with a default getter (unset on a decap tunnel), so it
    // is defined per-type only for SRv6 and defaults to the plain form here.
    using UnderlayInterface = std::conditional_t<
        type == SAI_TUNNEL_TYPE_SRV6,
        typename detail::TunnelAttributesTypes<
            SAI_TUNNEL_TYPE_SRV6>::UnderlayInterface,
        SaiAttribute<
            EnumType,
            SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE,
            SaiObjectIdT>>;
#else
    using UnderlayInterface = SaiAttribute<
        EnumType,
        SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE,
        SaiObjectIdT>;
#endif
    using OverlayInterface =
        typename detail::TunnelAttributesTypes<type>::OverlayInterface;
    using EncapSrcIp = typename detail::TunnelAttributesTypes<type>::EncapSrcIp;
    using DecapTtlMode =
        typename detail::TunnelAttributesTypes<type>::DecapTtlMode;
    using DecapDscpMode =
        typename detail::TunnelAttributesTypes<type>::DecapDscpMode;
    using DecapEcnMode =
        typename detail::TunnelAttributesTypes<type>::DecapEcnMode;
    using DecapQosDscpToTcMap =
        typename detail::TunnelAttributesTypes<type>::DecapQosDscpToTcMap;
    using EncapTtlMode =
        typename detail::TunnelAttributesTypes<type>::EncapTtlMode;
    using EncapDscpMode =
        typename detail::TunnelAttributesTypes<type>::EncapDscpMode;
    using EncapEcnMode =
        typename detail::TunnelAttributesTypes<type>::EncapEcnMode;
    using EncapDstIp = typename detail::TunnelAttributesTypes<type>::EncapDstIp;
    using EncapMappers =
        typename detail::TunnelAttributesTypes<type>::EncapMappers;
    using DecapMappers =
        typename detail::TunnelAttributesTypes<type>::DecapMappers;
  };
  using AdapterKey = TunnelSaiId;
  using AdapterHostKey =
      typename detail::TunnelTraitsAttributes<Attributes, type>::AdapterHostKey;
  using CreateAttributes = typename detail::
      TunnelTraitsAttributes<Attributes, type>::CreateAttributes;
  using ConditionAttributes = std::tuple<typename Attributes::Type>;
  inline const static ConditionAttributes kConditionAttributes{type};
};

using SaiIpInIpTunnelTraits = SaiTunnelTraitsT<SAI_TUNNEL_TYPE_IPINIP>;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
using SaiSrv6TunnelTraits = SaiTunnelTraitsT<SAI_TUNNEL_TYPE_SRV6>;
#endif
using SaiVxlanTunnelTraits = SaiTunnelTraitsT<SAI_TUNNEL_TYPE_VXLAN>;

template <>
struct SaiObjectHasConditionalAttributes<SaiIpInIpTunnelTraits>
    : public std::true_type {};
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
template <>
struct SaiObjectHasConditionalAttributes<SaiSrv6TunnelTraits>
    : public std::true_type {};
#endif
template <>
struct SaiObjectHasConditionalAttributes<SaiVxlanTunnelTraits>
    : public std::true_type {};

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
using SaiTunnelTraits = ConditionObjectTraits<
    SaiIpInIpTunnelTraits,
    SaiSrv6TunnelTraits,
    SaiVxlanTunnelTraits>;
#else
using SaiTunnelTraits =
    ConditionObjectTraits<SaiIpInIpTunnelTraits, SaiVxlanTunnelTraits>;
#endif
using SaiTunnelAdapterHostKey = typename SaiTunnelTraits::AdapterHostKey;
using SaiTunnelAdapterKey = typename SaiTunnelTraits::AdapterKey<TunnelSaiId>;

SAI_ATTRIBUTE_NAME(IpInIpTunnel, Type);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, UnderlayInterface);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, OverlayInterface);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, DecapTtlMode);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, DecapDscpMode);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, DecapEcnMode);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, EncapSrcIp);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, EncapTtlMode);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, EncapDscpMode);
SAI_ATTRIBUTE_NAME(IpInIpTunnel, EncapEcnMode);

// Only the VXLAN-only attributes need names here; the rest are identical
// types to their IP-in-IP counterparts, already named above.
SAI_ATTRIBUTE_NAME(VxlanTunnel, EncapMappers);
SAI_ATTRIBUTE_NAME(VxlanTunnel, DecapMappers);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
// SRv6 tunnel encap/decap attributes carry default getters (see above), making
// them distinct types from their IPINIP counterparts, so they need their own
// AttributeName specializations.
SAI_ATTRIBUTE_NAME(Srv6Tunnel, UnderlayInterface);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, EncapSrcIp);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, DecapTtlMode);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, DecapDscpMode);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, DecapEcnMode);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, DecapQosDscpToTcMap);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, EncapTtlMode);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, EncapDscpMode);
SAI_ATTRIBUTE_NAME(Srv6Tunnel, EncapEcnMode);
#endif

namespace detail {

template <sai_tunnel_term_table_entry_type_t type>
struct SaiTunnelTermTraits;

template <>
struct SaiTunnelTermTraits<SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP> {
  using EnumType = sai_tunnel_term_table_entry_attr_t;
  struct Attributes {
    using Type = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE,
        sai_int32_t>;
    using VrId = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_VR_ID,
        SaiObjectIdT>;
    using DstIp = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP,
        folly::IPAddress>;
    using DstIpMask = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP_MASK,
        folly::IPAddress>;
    using SrcIp = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_SRC_IP,
        folly::IPAddress>;
    using SrcIpMask = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_SRC_IP_MASK,
        folly::IPAddress>;
    using TunnelType = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TUNNEL_TYPE,
        sai_int32_t>;
    using ActionTunnelId = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_ACTION_TUNNEL_ID,
        SaiObjectIdT>;
  };
  using AdapterKey = TunnelTermSaiId;
  using CreateAttributes = std::tuple<
      Attributes::Type,
      Attributes::VrId,
      Attributes::DstIp,
      Attributes::TunnelType,
      Attributes::ActionTunnelId>;
  using AdapterHostKey = CreateAttributes;
};

template <>
struct SaiTunnelTermTraits<SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2P> {
  using EnumType = sai_tunnel_term_table_entry_attr_t;
  struct Attributes {
    using Type = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE,
        sai_int32_t>;
    using VrId = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_VR_ID,
        SaiObjectIdT>;
    using DstIp = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP,
        folly::IPAddress>;
    using DstIpMask = void;
    using SrcIp = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_SRC_IP,
        folly::IPAddress>;
    using SrcIpMask = void;
    using TunnelType = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TUNNEL_TYPE,
        sai_int32_t>;
    using ActionTunnelId = SaiAttribute<
        EnumType,
        SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_ACTION_TUNNEL_ID,
        SaiObjectIdT>;
  };
  using AdapterKey = TunnelTermSaiId;
  using CreateAttributes = std::tuple<
      Attributes::Type,
      Attributes::VrId,
      Attributes::DstIp,
      Attributes::SrcIp,
      Attributes::TunnelType,
      Attributes::ActionTunnelId>;
  using AdapterHostKey = CreateAttributes;
};

} // namespace detail

template <sai_tunnel_term_table_entry_type_t type>
struct SaiTunnelTermTraitsT {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY;
  using SaiApiT = TunnelApi;
  using AdapterKey = TunnelTermSaiId;
  using Attributes = typename detail::SaiTunnelTermTraits<type>::Attributes;
  using CreateAttributes =
      typename detail::SaiTunnelTermTraits<type>::CreateAttributes;
  using AdapterHostKey =
      typename detail::SaiTunnelTermTraits<type>::AdapterHostKey;
  using ConditionAttributes =
      std::tuple<typename detail::SaiTunnelTermTraits<type>::Attributes::Type>;
  inline const static ConditionAttributes kConditionAttributes{type};
};

using SaiP2MPTunnelTermTraits =
    SaiTunnelTermTraitsT<SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP>;
using SaiP2PTunnelTermTraits =
    SaiTunnelTermTraitsT<SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2P>;

template <>
struct SaiObjectHasConditionalAttributes<SaiP2MPTunnelTermTraits>
    : public std::true_type {};

template <>
struct SaiObjectHasConditionalAttributes<SaiP2PTunnelTermTraits>
    : public std::true_type {};

using SaiTunnelTermTraits =
    ConditionObjectTraits<SaiP2MPTunnelTermTraits, SaiP2PTunnelTermTraits>;
using SaiTunnelTermAdapterHostKey =
    typename SaiTunnelTermTraits::AdapterHostKey;
using SaiTunnelTermAdaptertKey =
    typename SaiTunnelTermTraits::AdapterKey<TunnelTermSaiId>;

SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, Type);
SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, VrId);
SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, DstIp);
SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, DstIpMask);
SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, SrcIp);
SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, SrcIpMask);
SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, TunnelType);
SAI_ATTRIBUTE_NAME(P2MPTunnelTerm, ActionTunnelId);

class TunnelApi : public SaiApi<TunnelApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_TUNNEL;
  TunnelApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for tunnel api");
  }

 private:
  sai_status_t _create(
      TunnelSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_tunnel(rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(TunnelSaiId key) const {
    return api_->remove_tunnel(key);
  }
  sai_status_t _getAttribute(TunnelSaiId key, sai_attribute_t* attr) const {
    return api_->get_tunnel_attribute(key, 1, attr);
  }
  sai_status_t _setAttribute(TunnelSaiId key, const sai_attribute_t* attr)
      const {
    return api_->set_tunnel_attribute(key, attr);
  }

  sai_status_t _getStats(
      TunnelSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids,
      sai_stats_mode_t mode,
      uint64_t* counters) const {
    return api_->get_tunnel_stats(key, num_of_counters, counter_ids, counters);
    // #TODO: get_tunnel_stats will be deprecated
    // use get_tunnel_stats_ext(key, num_of_counters, counter_ids, mode,
    // counters) if supported later
  }

  sai_status_t _clearStats(
      TunnelSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids) const {
    return api_->clear_tunnel_stats(key, num_of_counters, counter_ids);
  }

  sai_status_t _create(
      TunnelTermSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_tunnel_term_table_entry(
        rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _remove(TunnelTermSaiId id) const {
    return api_->remove_tunnel_term_table_entry(id);
  }

  sai_status_t _getAttribute(TunnelTermSaiId key, sai_attribute_t* attr) const {
    return api_->get_tunnel_term_table_entry_attribute(key, 1, attr);
  }

  sai_status_t _setAttribute(TunnelTermSaiId key, const sai_attribute_t* attr)
      const {
    return api_->set_tunnel_term_table_entry_attribute(key, attr);
  }

  sai_status_t _create(
      TunnelMapSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_tunnel_map(rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _remove(TunnelMapSaiId id) const {
    return api_->remove_tunnel_map(id);
  }

  sai_status_t _getAttribute(TunnelMapSaiId key, sai_attribute_t* attr) const {
    return api_->get_tunnel_map_attribute(key, 1, attr);
  }

  sai_status_t _setAttribute(TunnelMapSaiId key, const sai_attribute_t* attr)
      const {
    return api_->set_tunnel_map_attribute(key, attr);
  }

  sai_status_t _create(
      TunnelMapEntrySaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_tunnel_map_entry(
        rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _remove(TunnelMapEntrySaiId id) const {
    return api_->remove_tunnel_map_entry(id);
  }

  sai_status_t _getAttribute(TunnelMapEntrySaiId key, sai_attribute_t* attr)
      const {
    return api_->get_tunnel_map_entry_attribute(key, 1, attr);
  }

  sai_status_t _setAttribute(
      TunnelMapEntrySaiId key,
      const sai_attribute_t* attr) const {
    return api_->set_tunnel_map_entry_attribute(key, attr);
  }

  sai_tunnel_api_t* api_;
  friend class SaiApi<TunnelApi>;
};

} // namespace facebook::fboss
