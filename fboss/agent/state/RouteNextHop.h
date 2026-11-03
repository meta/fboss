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
#include <folly/MacAddress.h>
#include <folly/Poly.h>
#include <folly/Range.h>
#include <folly/json/dynamic.h>
#include <folly/poly/Regular.h>
#include <optional>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/Constants.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/LabelForwardingAction.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

inline folly::StringPiece constexpr kInterface() {
  return "interface";
}

inline folly::StringPiece constexpr kNexthop() {
  return "nexthop";
}

inline folly::StringPiece constexpr kLabelForwardingAction() {
  return "label_forwarding_action";
}

using NextHopWeight = uint64_t;
constexpr NextHopWeight ECMP_WEIGHT = 0;
constexpr NextHopWeight UCMP_DEFAULT_WEIGHT = 1;

struct INextHop {
  // In this context "Interface" does not refer to network interfaces
  // but is rather an implementation detail of folly::Poly. This is
  // the "well-known" name you must use for your interface definition
  // for things like poly_call to work properly.
  template <class Base>
  struct Interface : Base {
    std::optional<InterfaceID> intfID() const {
      return folly::poly_call<0>(*this);
    }

    folly::IPAddress addr() const {
      return folly::poly_call<1>(*this);
    }

    NextHopWeight weight() const {
      return folly::poly_call<2>(*this);
    }

    std::optional<LabelForwardingAction> labelForwardingAction() const {
      return folly::poly_call<3>(*this);
    }

    std::optional<bool> disableTTLDecrement() const {
      return folly::poly_call<4>(*this);
    }

    std::optional<NextHopWeight> adjustedWeight() const {
      return folly::poly_call<5>(*this);
    }

    std::optional<NetworkTopologyInformation> topologyInfo() const {
      return folly::poly_call<6>(*this);
    }

    std::vector<folly::IPAddressV6> srv6SegmentList() const {
      return folly::poly_call<7>(*this);
    }

    std::optional<TunnelType> tunnelType() const {
      return folly::poly_call<8>(*this);
    }

    std::optional<std::string> tunnelId() const {
      return folly::poly_call<9>(*this);
    }

    std::optional<int64_t> cost() const {
      return folly::poly_call<10>(*this);
    }

    NextHopRole role() const {
      return folly::poly_call<11>(*this);
    }

    std::optional<folly::MacAddress> overlayDstMac() const {
      return folly::poly_call<12>(*this);
    }

    bool isResolved() const {
      return intfID().has_value();
    }

    InterfaceID intf() const {
      return intfID().value();
    }

    bool isPopAndLookup() const {
      return labelForwardingAction().has_value() &&
          labelForwardingAction()->type() ==
          LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP;
    }

    folly::dynamic toFollyDynamic() const {
      folly::dynamic nh = folly::dynamic::object;
      nh[kNexthop()] = addr().str();
      nh[kWeight] = folly::to<std::string>(weight());
      if (isResolved()) {
        nh[kInterface()] = static_cast<uint32_t>(intf());
      }
      if (labelForwardingAction()) {
        nh[kLabelForwardingAction()] =
            labelForwardingAction()->toFollyDynamic();
      }
      return nh;
    }

    NextHopThrift toThrift() const {
      NextHopThrift nht;
      *nht.address() = network::toBinaryAddress(addr());
      *nht.weight() = weight();
      if (isResolved()) {
        nht.address()->ifName() = utility::createTunIntfName(intf());
      }
      if (labelForwardingAction()) {
        nht.mplsAction() = labelForwardingAction()->toThrift();
      }
      if (auto value = disableTTLDecrement()) {
        nht.disableTTLDecrement() = value.value();
      }
      if (auto value = adjustedWeight()) {
        nht.adjustedWeight() = value.value();
      }
      if (auto value = topologyInfo()) {
        nht.topologyInfo() = value.value();
      }
      if (auto segList = srv6SegmentList(); !segList.empty()) {
        std::vector<network::thrift::BinaryAddress> binarySegList;
        binarySegList.reserve(segList.size());
        for (const auto& ip : segList) {
          binarySegList.push_back(network::toBinaryAddress(ip));
        }
        *nht.srv6SegmentList() = std::move(binarySegList);
      }
      if (auto value = tunnelType()) {
        nht.tunnelType() = value.value();
      }
      if (auto value = tunnelId()) {
        nht.tunnelId() = value.value();
      }
      if (auto value = overlayDstMac()) {
        nht.overlayDstMac() = value->toString();
      }
      if (auto value = cost()) {
        nht.cost() = value.value();
      }
      *nht.role() = role();
      return nht;
    }

    std::string str() const {
      std::string intfStr =
          isResolved() ? folly::to<std::string>("@I", intf()) : "";
      std::string labelActionStr{};
      if (auto action = labelForwardingAction()) {
        labelActionStr = folly::to<std::string>(" ", action->str());
      }
      return folly::to<std::string>(
          addr(), intfStr, "x", weight(), labelActionStr);
    }
  };

  template <class T>
  using Members = FOLLY_POLY_MEMBERS(
      &T::intfID,
      &T::addr,
      &T::weight,
      &T::labelForwardingAction,
      &T::disableTTLDecrement,
      &T::adjustedWeight,
      &T::topologyInfo,
      &T::srv6SegmentList,
      &T::tunnelType,
      &T::tunnelId,
      &T::cost,
      &T::role,
      &T::overlayDstMac);
};

using NextHop = folly::Poly<INextHop>;

void toAppend(const NextHop& nhop, std::string* result);
std::ostream& operator<<(std::ostream& os, const NextHop& nhop);

bool operator<(const NextHop& a, const NextHop& b);
bool operator>(const NextHop& a, const NextHop& b);
bool operator<=(const NextHop& a, const NextHop& b);
bool operator>=(const NextHop& a, const NextHop& b);
bool operator==(const NextHop& a, const NextHop& b);
bool operator!=(const NextHop& a, const NextHop& b);

class ResolvedNextHop {
 public:
  ResolvedNextHop(
      const folly::IPAddress& addr,
      InterfaceID intfID,
      const NextHopWeight& weight,
      const std::optional<LabelForwardingAction>& action = std::nullopt,
      const std::optional<bool>& disableTTLDecrement = std::nullopt,
      const std::optional<NetworkTopologyInformation>& topologyInfo =
          std::nullopt,
      const std::optional<NextHopWeight>& adjustedWeight = std::nullopt,
      const std::vector<folly::IPAddressV6>& srv6SegmentList = {},
      const std::optional<TunnelType>& tunnelType = std::nullopt,
      const std::optional<std::string>& tunnelId = std::nullopt,
      const std::optional<int64_t>& cost = std::nullopt,
      NextHopRole role = NextHopRole::PRIMARY,
      const std::optional<folly::MacAddress>& overlayDstMac = std::nullopt);
  ResolvedNextHop(
      folly::IPAddress&& addr,
      InterfaceID intfID,
      const NextHopWeight& weight,
      std::optional<LabelForwardingAction>&& action = std::nullopt,
      std::optional<bool>&& disableTTLDecrement = std::nullopt,
      const std::optional<NetworkTopologyInformation>&& topologyInfo =
          std::nullopt,
      const std::optional<NextHopWeight>& adjustedWeight = std::nullopt,
      std::vector<folly::IPAddressV6>&& srv6SegmentList = {},
      std::optional<TunnelType>&& tunnelType = std::nullopt,
      std::optional<std::string>&& tunnelId = std::nullopt,
      std::optional<int64_t>&& cost = std::nullopt,
      NextHopRole role = NextHopRole::PRIMARY,
      std::optional<folly::MacAddress>&& overlayDstMac = std::nullopt);
  std::optional<InterfaceID> intfID() const {
    return intfID_;
  }
  folly::IPAddress addr() const {
    return addr_;
  }
  NextHopWeight weight() const {
    return weight_;
  }
  std::optional<LabelForwardingAction> labelForwardingAction() const {
    return labelForwardingAction_;
  }
  std::optional<bool> disableTTLDecrement() const {
    return disableTTLDecrement_;
  }

  void setDisableTTLDecrement(std::optional<bool> disableTTLDecrement) {
    disableTTLDecrement_ = disableTTLDecrement;
  }

  std::optional<NextHopWeight> adjustedWeight() const {
    return adjustedWeight_;
  }

  void setAdjustedWeight(std::optional<NextHopWeight> weight) {
    adjustedWeight_ = weight;
  }

  std::optional<NetworkTopologyInformation> topologyInfo() const {
    return topologyInfo_;
  }

  void setTopologyInfo(std::optional<NetworkTopologyInformation> topologyInfo) {
    topologyInfo_ = topologyInfo;
  }

  std::vector<folly::IPAddressV6> srv6SegmentList() const {
    return srv6SegmentList_;
  }

  void setSrv6SegmentList(std::vector<folly::IPAddressV6> srv6SegmentList) {
    srv6SegmentList_ = std::move(srv6SegmentList);
  }

  std::optional<TunnelType> tunnelType() const {
    return tunnelType_;
  }

  void setTunnelType(std::optional<TunnelType> tunnelType) {
    tunnelType_ = tunnelType;
  }

  std::optional<std::string> tunnelId() const {
    return tunnelId_;
  }

  void setTunnelId(std::optional<std::string> tunnelId) {
    tunnelId_ = tunnelId;
  }

  std::optional<folly::MacAddress> overlayDstMac() const {
    return overlayDstMac_;
  }

  void setOverlayDstMac(std::optional<folly::MacAddress> overlayDstMac) {
    overlayDstMac_ = overlayDstMac;
  }

  std::optional<int64_t> cost() const {
    return cost_;
  }

  void setCost(std::optional<int64_t> cost) {
    cost_ = cost;
  }

  NextHopRole role() const {
    return role_;
  }

 private:
  folly::IPAddress addr_;
  InterfaceID intfID_;
  NextHopWeight weight_;
  std::optional<LabelForwardingAction> labelForwardingAction_;
  std::optional<bool> disableTTLDecrement_{};
  std::optional<NetworkTopologyInformation> topologyInfo_;
  std::optional<NextHopWeight> adjustedWeight_{};
  std::vector<folly::IPAddressV6> srv6SegmentList_;
  std::optional<TunnelType> tunnelType_;
  std::optional<std::string> tunnelId_;
  std::optional<int64_t> cost_;
  NextHopRole role_;
  std::optional<folly::MacAddress> overlayDstMac_;
};

bool operator==(const ResolvedNextHop& a, const ResolvedNextHop& b);

class UnresolvedNextHop {
 public:
  UnresolvedNextHop(
      const folly::IPAddress& addr,
      const NextHopWeight& weight,
      const std::optional<LabelForwardingAction>& action = std::nullopt,
      const std::optional<bool>& disableTTLDecrement = std::nullopt,
      const std::optional<NetworkTopologyInformation>& topologyInfo =
          std::nullopt,
      const std::optional<NextHopWeight>& adjustedWeight = std::nullopt,
      const std::vector<folly::IPAddressV6>& srv6SegmentList = {},
      const std::optional<TunnelType>& tunnelType = std::nullopt,
      const std::optional<std::string>& tunnelId = std::nullopt,
      const std::optional<int64_t>& cost = std::nullopt,
      NextHopRole role = NextHopRole::PRIMARY,
      const std::optional<folly::MacAddress>& overlayDstMac = std::nullopt);
  UnresolvedNextHop(
      folly::IPAddress&& addr,
      const NextHopWeight& weight,
      std::optional<LabelForwardingAction>&& action = std::nullopt,
      std::optional<bool>&& disableTTLDecrement = std::nullopt,
      const std::optional<NetworkTopologyInformation>&& topologyInfo =
          std::nullopt,
      const std::optional<NextHopWeight>& adjustedWeight = std::nullopt,
      std::vector<folly::IPAddressV6>&& srv6SegmentList = {},
      std::optional<TunnelType>&& tunnelType = std::nullopt,
      std::optional<std::string>&& tunnelId = std::nullopt,
      std::optional<int64_t>&& cost = std::nullopt,
      NextHopRole role = NextHopRole::PRIMARY,
      std::optional<folly::MacAddress>&& overlayDstMac = std::nullopt);
  std::optional<InterfaceID> intfID() const {
    return std::nullopt;
  }
  folly::IPAddress addr() const {
    return addr_;
  }
  NextHopWeight weight() const {
    return weight_;
  }
  std::optional<LabelForwardingAction> labelForwardingAction() const {
    return labelForwardingAction_;
  }
  std::optional<bool> disableTTLDecrement() const {
    return disableTTLDecrement_;
  }
  std::optional<NetworkTopologyInformation> topologyInfo() const {
    return topologyInfo_;
  }
  std::optional<NextHopWeight> adjustedWeight() const {
    return adjustedWeight_;
  }

  std::vector<folly::IPAddressV6> srv6SegmentList() const {
    return srv6SegmentList_;
  }

  std::optional<TunnelType> tunnelType() const {
    return tunnelType_;
  }

  std::optional<std::string> tunnelId() const {
    return tunnelId_;
  }

  std::optional<folly::MacAddress> overlayDstMac() const {
    return overlayDstMac_;
  }

  std::optional<int64_t> cost() const {
    return cost_;
  }

  NextHopRole role() const {
    return role_;
  }

 private:
  folly::IPAddress addr_;
  NextHopWeight weight_;
  std::optional<LabelForwardingAction> labelForwardingAction_;
  std::optional<bool> disableTTLDecrement_{};
  std::optional<NetworkTopologyInformation> topologyInfo_{};
  std::optional<NextHopWeight> adjustedWeight_{};
  std::vector<folly::IPAddressV6> srv6SegmentList_;
  std::optional<TunnelType> tunnelType_;
  std::optional<std::string> tunnelId_;
  std::optional<int64_t> cost_;
  NextHopRole role_;
  std::optional<folly::MacAddress> overlayDstMac_;
};

bool operator==(const UnresolvedNextHop& a, const UnresolvedNextHop& b);

namespace util {
NextHop fromThrift(const NextHopThrift& nht, bool allowV6NonLinkLocal = false);
NextHop nextHopFromFollyDynamic(const folly::dynamic& nhopJson);
} // namespace util

} // namespace facebook::fboss

namespace std {

template <>
struct hash<facebook::fboss::NetworkTopologyInformation> {
  size_t operator()(
      const facebook::fboss::NetworkTopologyInformation& info) const {
    size_t rack_id = info.rack_id() ? std::hash<int32_t>{}(*info.rack_id()) : 0;
    size_t plane_id =
        info.plane_id() ? std::hash<int32_t>{}(*info.plane_id()) : 0;
    size_t remote_rak_capacity = info.remote_rack_capacity()
        ? std::hash<int32_t>{}(*info.remote_rack_capacity())
        : 0;
    size_t spine_capacity = info.spine_capacity()
        ? std::hash<int32_t>{}(*info.spine_capacity())
        : 0;
    size_t local_rack_capacity = info.local_rack_capacity()
        ? std::hash<int32_t>{}(*info.local_rack_capacity())
        : 0;
    size_t spine_id =
        info.spine_id() ? std::hash<int32_t>{}(*info.spine_id()) : 0;
    return folly::hash::hash_combine(
        rack_id,
        plane_id,
        remote_rak_capacity,
        spine_capacity,
        local_rack_capacity,
        spine_id);
  }
};

template <>
struct hash<facebook::fboss::NextHop> {
  size_t operator()(const facebook::fboss::NextHop& nexthop) const {
    size_t seed = folly::hash::hash_combine(
        nexthop.intfID(),
        nexthop.addr(),
        nexthop.weight(),
        nexthop.labelForwardingAction(),
        nexthop.disableTTLDecrement(),
        nexthop.adjustedWeight(),
        nexthop.topologyInfo(),
        nexthop.tunnelType(),
        nexthop.tunnelId(),
        nexthop.cost(),
        nexthop.role(),
        nexthop.overlayDstMac());
    for (const auto& seg : nexthop.srv6SegmentList()) {
      seed = folly::hash::hash_combine(seed, seg);
    }
    return seed;
  }
};

template <>
struct hash<facebook::fboss::ResolvedNextHop> {
  size_t operator()(const facebook::fboss::ResolvedNextHop& nexthop) const {
    size_t seed = folly::hash::hash_combine(
        nexthop.intfID(),
        nexthop.addr(),
        nexthop.weight(),
        nexthop.labelForwardingAction(),
        nexthop.disableTTLDecrement(),
        nexthop.adjustedWeight(),
        nexthop.topologyInfo(),
        nexthop.tunnelType(),
        nexthop.tunnelId(),
        nexthop.cost(),
        nexthop.role(),
        nexthop.overlayDstMac());
    for (const auto& seg : nexthop.srv6SegmentList()) {
      seed = folly::hash::hash_combine(seed, seg);
    }
    return seed;
  }
};

} // namespace std
