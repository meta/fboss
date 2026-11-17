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

#include <tuple>
#include <variant>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

namespace detail {

template <sai_mirror_session_type_t type>
struct SaiMirrorTraits;

template <>
struct SaiMirrorTraits<SAI_MIRROR_SESSION_TYPE_LOCAL> {
  using EnumType = sai_mirror_session_attr_t;
  struct Attributes {
    using Type =
        SaiAttribute<EnumType, SAI_MIRROR_SESSION_ATTR_TYPE, sai_int32_t>;
    using MonitorPort = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_MONITOR_PORT,
        SaiObjectIdT>;
    struct AttributeTcBufferLimit {
      std::optional<sai_attr_id_t> operator()();
    };
    using TcBufferLimit =
        SaiExtensionAttribute<sai_uint32_t, AttributeTcBufferLimit>;
  };
  using CreateAttributes = std::tuple<
      Attributes::Type,
      Attributes::MonitorPort,
      std::optional<typename Attributes::TcBufferLimit>>;
  using AdapterHostKey = CreateAttributes;
};

template <>
struct SaiMirrorTraits<SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE> {
  using EnumType = sai_mirror_session_attr_t;
  struct Attributes {
    using Type =
        SaiAttribute<EnumType, SAI_MIRROR_SESSION_ATTR_TYPE, sai_int32_t>;
    using MonitorPort = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_MONITOR_PORT,
        SaiObjectIdT>;
    using TruncateSize = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_TRUNCATE_SIZE,
        sai_uint16_t>;
    using ErspanEncapsulationType = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_ERSPAN_ENCAPSULATION_TYPE,
        sai_int32_t>;
    using Tos =
        SaiAttribute<EnumType, SAI_MIRROR_SESSION_ATTR_TOS, sai_uint8_t>;
    using Ttl =
        SaiAttribute<EnumType, SAI_MIRROR_SESSION_ATTR_TTL, sai_uint8_t>;
    using SrcIpAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS,
        folly::IPAddress>;
    using DstIpAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS,
        folly::IPAddress>;
    using SrcMacAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS,
        folly::MacAddress>;
    using DstMacAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS,
        folly::MacAddress>;
    using GreProtocolType = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_GRE_PROTOCOL_TYPE,
        sai_uint16_t>;
    using IpHeaderVersion = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION,
        sai_uint8_t>;
    using SampleRate = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_SAMPLE_RATE,
        sai_uint32_t>;
    using Policer = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_POLICER,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    struct AttributeTcBufferLimit {
      std::optional<sai_attr_id_t> operator()();
    };
    using TcBufferLimit =
        SaiExtensionAttribute<sai_uint32_t, AttributeTcBufferLimit>;
  };
  using CreateAttributes = std::tuple<
      Attributes::Type,
      Attributes::MonitorPort,
      Attributes::ErspanEncapsulationType,
      Attributes::Tos,
      Attributes::SrcIpAddress,
      Attributes::DstIpAddress,
      Attributes::SrcMacAddress,
      Attributes::DstMacAddress,
      Attributes::GreProtocolType,
      Attributes::IpHeaderVersion,
      std::optional<Attributes::Ttl>,
      std::optional<Attributes::TruncateSize>,
      std::optional<Attributes::SampleRate>,
      std::optional<typename Attributes::TcBufferLimit>,
      std::optional<Attributes::Policer>>;
  using AdapterHostKey = std::tuple<
      Attributes::Type,
      Attributes::MonitorPort,
      Attributes::SrcIpAddress,
      Attributes::DstIpAddress>;
};

template <>
struct SaiMirrorTraits<SAI_MIRROR_SESSION_TYPE_SFLOW> {
  using EnumType = sai_mirror_session_attr_t;
  struct Attributes {
    using Type =
        SaiAttribute<EnumType, SAI_MIRROR_SESSION_ATTR_TYPE, sai_int32_t>;
    using MonitorPort = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_MONITOR_PORT,
        SaiObjectIdT>;
    using TruncateSize = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_TRUNCATE_SIZE,
        sai_uint16_t>;
    using Tos =
        SaiAttribute<EnumType, SAI_MIRROR_SESSION_ATTR_TOS, sai_uint8_t>;
    using ErspanEncapsulationType = void;
    using Ttl =
        SaiAttribute<EnumType, SAI_MIRROR_SESSION_ATTR_TTL, sai_uint8_t>;
    using SrcIpAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS,
        folly::IPAddress>;
    using DstIpAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS,
        folly::IPAddress>;
    using SrcMacAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS,
        folly::MacAddress>;
    using DstMacAddress = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS,
        folly::MacAddress>;
    using GreProtocolType = void;
    using UdpSrcPort = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_UDP_SRC_PORT,
        sai_uint16_t>;
    using UdpDstPort = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_UDP_DST_PORT,
        sai_uint16_t>;
    using IpHeaderVersion = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION,
        sai_uint8_t>;
    using SampleRate = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_SAMPLE_RATE,
        sai_uint32_t>;
    using Policer = SaiAttribute<
        EnumType,
        SAI_MIRROR_SESSION_ATTR_POLICER,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    struct AttributeTcBufferLimit {
      std::optional<sai_attr_id_t> operator()();
    };
    using TcBufferLimit =
        SaiExtensionAttribute<sai_uint32_t, AttributeTcBufferLimit>;
  };
  using CreateAttributes = std::tuple<
      typename Attributes::Type,
      typename Attributes::MonitorPort,
      typename Attributes::Tos,
      typename Attributes::SrcIpAddress,
      typename Attributes::DstIpAddress,
      typename Attributes::SrcMacAddress,
      typename Attributes::DstMacAddress,
      typename Attributes::UdpSrcPort,
      typename Attributes::UdpDstPort,
      typename Attributes::IpHeaderVersion,
      std::optional<typename Attributes::Ttl>,
      std::optional<typename Attributes::TruncateSize>,
      std::optional<Attributes::SampleRate>,
      std::optional<typename Attributes::TcBufferLimit>,
      std::optional<Attributes::Policer>>;
  using AdapterHostKey = std::tuple<
      typename Attributes::Type,
      typename Attributes::MonitorPort,
      typename Attributes::SrcIpAddress,
      typename Attributes::DstIpAddress,
      typename Attributes::UdpSrcPort,
      typename Attributes::UdpDstPort>;
};
} // namespace detail

class MirrorApi;

template <sai_mirror_session_type_t type>
struct SaiMirrorTraitsT {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_MIRROR_SESSION;
  using SaiApiT = MirrorApi;
  using AdapterKey = MirrorSaiId;
  using Attributes = typename detail::SaiMirrorTraits<type>::Attributes;
  using CreateAttributes =
      typename detail::SaiMirrorTraits<type>::CreateAttributes;
  using AdapterHostKey = typename detail::SaiMirrorTraits<type>::AdapterHostKey;
  using ConditionAttributes =
      std::tuple<typename detail::SaiMirrorTraits<type>::Attributes::Type>;
  inline const static ConditionAttributes kConditionAttributes{type};
};

using SaiLocalMirrorTraits = SaiMirrorTraitsT<SAI_MIRROR_SESSION_TYPE_LOCAL>;
using SaiEnhancedRemoteMirrorTraits =
    SaiMirrorTraitsT<SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE>;
using SaiSflowMirrorTraits = SaiMirrorTraitsT<SAI_MIRROR_SESSION_TYPE_SFLOW>;

template <>
struct SaiObjectHasConditionalAttributes<SaiLocalMirrorTraits>
    : public std::true_type {};
template <>
struct SaiObjectHasConditionalAttributes<SaiEnhancedRemoteMirrorTraits>
    : public std::true_type {};
template <>
struct SaiObjectHasConditionalAttributes<SaiSflowMirrorTraits>
    : public std::true_type {};

// TODO: Add SaiSflowMirrorTraits here.
using SaiMirrorTraits = ConditionObjectTraits<
    SaiLocalMirrorTraits,
    SaiEnhancedRemoteMirrorTraits,
    SaiSflowMirrorTraits>;
using SaiMirrorAdapterHostKey = typename SaiMirrorTraits::AdapterHostKey;
using SaiMirrorAdaptertKey = typename SaiMirrorTraits::AdapterKey<MirrorSaiId>;

SAI_ATTRIBUTE_NAME(LocalMirror, TcBufferLimit)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, Policer)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, Type)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, MonitorPort)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, TruncateSize)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, Tos)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, ErspanEncapsulationType)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, GreProtocolType)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, Ttl)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, SrcIpAddress)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, DstIpAddress)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, SrcMacAddress)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, DstMacAddress)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, IpHeaderVersion)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, SampleRate)
SAI_ATTRIBUTE_NAME(EnhancedRemoteMirror, TcBufferLimit)
SAI_ATTRIBUTE_NAME(SflowMirror, UdpSrcPort)
SAI_ATTRIBUTE_NAME(SflowMirror, UdpDstPort)
SAI_ATTRIBUTE_NAME(SflowMirror, TcBufferLimit)

class MirrorApi : public SaiApi<MirrorApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_MIRROR;
  MirrorApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for mirror session api");
  }

 private:
  sai_status_t _create(
      MirrorSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_mirror_session(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(MirrorSaiId id) const {
    return api_->remove_mirror_session(id);
  }

  sai_status_t _getAttribute(MirrorSaiId id, sai_attribute_t* attr) const {
    return api_->get_mirror_session_attribute(id, 1, attr);
  }
  sai_status_t _setAttribute(MirrorSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_mirror_session_attribute(id, attr);
  }
  sai_mirror_api_t* api_;
  friend class SaiApi<MirrorApi>;
};

} // namespace facebook::fboss
