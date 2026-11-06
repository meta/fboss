// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/route/utils.h"

#include <folly/Conv.h>
#include <folly/String.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include "fboss/agent/AddressUtil.h"

namespace facebook::fboss::show::route::utils {

using facebook::fboss::NextHopThrift;
using facebook::fboss::utils::getAddrStr;

bool isFpfEncoding(
    const std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>&
        encoding) {
  return encoding.has_value() &&
      encoding->getType() ==
      facebook::bgp::nsf_policy::NsfTeWeightEncoding::Type::fpf_l2_encoding;
}

ClientID parseClientId(const std::string& str) {
  auto upper = str;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  ClientID clientId;
  if (apache::thrift::util::tryParseEnum(upper, &clientId)) {
    return clientId;
  }
  if (!str.empty() && std::all_of(str.begin(), str.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
      })) {
    return static_cast<ClientID>(folly::to<int16_t>(str));
  }
  std::vector<std::string> names;
  for (auto id : apache::thrift::TEnumTraits<ClientID>::names) {
    names.emplace_back(id);
  }
  throw std::invalid_argument(
      fmt::format(
          "Invalid --clientID '{}'. Accepted values are: {} or a numeric ClientID",
          str,
          folly::join(", ", names)));
}

std::string getAddressFamilyStr(const IpPrefix& dest) {
  return facebook::network::toIPAddress(*dest.ip()).isV4() ? "ipv4" : "ipv6";
}

std::string getMplsActionCodeStr(MplsActionCode mplsActionCode) {
  switch (mplsActionCode) {
    case MplsActionCode::PUSH:
      return "PUSH";
    case MplsActionCode::SWAP:
      return "SWAP";
    case MplsActionCode::PHP:
      return "PHP";
    case MplsActionCode::POP_AND_LOOKUP:
      return "POP_AND_LOOKUP";
    case MplsActionCode::NOOP:
      return "NOOP";
  }
  throw std::runtime_error(
      "Unsupported MplsActionCode: " +
      std::to_string(static_cast<int>(mplsActionCode)));
}

std::string getMplsActionInfoStr(const cli::MplsActionInfo& mplsActionInfo) {
  const auto& action = mplsActionInfo.action().value();
  auto swapLabelPtr = apache::thrift::get_pointer(mplsActionInfo.swapLabel());
  auto pushLabelsPtr = apache::thrift::get_pointer(mplsActionInfo.pushLabels());
  std::string labels;

  if (action == "SWAP" && swapLabelPtr != nullptr) {
    labels = fmt::format(": {}", *swapLabelPtr);
  } else if (action == "PUSH" && pushLabelsPtr != nullptr) {
    auto stackStr = folly::join(",", *pushLabelsPtr);
    labels = fmt::format(": {{{}}}", stackStr);
  }
  return fmt::format(" MPLS -> {} {}", mplsActionInfo.action().value(), labels);
}

void getNextHopInfoAddr(
    const network::thrift::BinaryAddress& addr,
    cli::NextHopInfo& nextHopInfo) {
  nextHopInfo.addr() = getAddrStr(addr);
  auto ifNamePtr = apache::thrift::get_pointer(addr.ifName());
  if (ifNamePtr != nullptr) {
    nextHopInfo.ifName() = *ifNamePtr;
  }
}

void getNextHopInfoThrift(
    const NextHopThrift& nextHop,
    cli::NextHopInfo& nextHopInfo) {
  getNextHopInfoAddr(nextHop.address().value(), nextHopInfo);
  nextHopInfo.weight() = folly::copy(nextHop.weight().value());
  nextHopInfo.isBackup() =
      folly::copy(nextHop.role().value()) == NextHopRole::BACKUP;

  if (nextHop.cost().has_value()) {
    nextHopInfo.cost() = folly::copy(nextHop.cost().value());
  }

  auto mplsActionPtr = apache::thrift::get_pointer(nextHop.mplsAction());
  if (mplsActionPtr != nullptr) {
    cli::MplsActionInfo mplsActionInfo;
    mplsActionInfo.action() =
        getMplsActionCodeStr(folly::copy(mplsActionPtr->action().value()));
    auto swapLabelPtr = apache::thrift::get_pointer(mplsActionPtr->swapLabel());
    auto pushLabelsPtr =
        apache::thrift::get_pointer(mplsActionPtr->pushLabels());
    if (swapLabelPtr != nullptr) {
      mplsActionInfo.swapLabel() = *swapLabelPtr;
    }
    if (pushLabelsPtr != nullptr) {
      mplsActionInfo.pushLabels() = *pushLabelsPtr;
    }
    nextHopInfo.mplsAction() = mplsActionInfo;
  }

  auto topologyInfoPtr = apache::thrift::get_pointer(nextHop.topologyInfo());
  if (topologyInfoPtr != nullptr) {
    nextHopInfo.topologyInfo() = *topologyInfoPtr;
  }

  if (!nextHop.srv6SegmentList()->empty()) {
    std::vector<std::string> sidStrs;
    for (const auto& sid : *nextHop.srv6SegmentList()) {
      sidStrs.push_back(getAddrStr(sid));
    }
    nextHopInfo.srv6SegmentList() = std::move(sidStrs);
  }
}

std::string getMplsLabelStr(const cli::NextHopInfo& nextHopInfo) {
  std::string labelStr;
  auto mplsActionInfoPtr =
      apache::thrift::get_pointer(nextHopInfo.mplsAction());
  if (mplsActionInfoPtr != nullptr) {
    labelStr = getMplsActionInfoStr(*mplsActionInfoPtr);
  }
  return labelStr;
}
std::string getTopologyInfoStr(
    const cli::NextHopInfo& nextHopInfo,
    const std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>&
        encoding) {
  auto topoInfoPtr = apache::thrift::get_pointer(nextHopInfo.topologyInfo());
  if (topoInfoPtr == nullptr) {
    return "";
  }
  // The two encodings populate disjoint topology fields, so gate each field on
  // the encoding: FPF carries spine_id, while the non-FPF (L2) encoding carries
  // plane_id together with the spine_capacity/local_rack_capacity weights.
  // rack_id and remote_rack_capacity are common to both.
  const bool isFpf = isFpfEncoding(encoding);
  std::string topoStr;
  if (topoInfoPtr->rack_id().has_value()) {
    topoStr += fmt::format(" rack {}", topoInfoPtr->rack_id().value());
  }
  // Group topology identifiers (spine id / plane) right after rack and before
  // the weight fields, so both encodings order identifiers first. The explicit
  // "spine id" label also disambiguates it from the non-FPF "spine weight".
  if (isFpf && topoInfoPtr->spine_id().has_value()) {
    topoStr += fmt::format(" spine id {}", topoInfoPtr->spine_id().value());
  }
  if (!isFpf && topoInfoPtr->plane_id().has_value()) {
    topoStr += fmt::format(" plane {}", topoInfoPtr->plane_id().value());
  }
  if (topoInfoPtr->remote_rack_capacity().has_value()) {
    topoStr += fmt::format(
        " remote weight {}", topoInfoPtr->remote_rack_capacity().value());
  }
  if (!isFpf && topoInfoPtr->spine_capacity().has_value()) {
    topoStr +=
        fmt::format(" spine weight {}", topoInfoPtr->spine_capacity().value());
  }
  if (!isFpf && topoInfoPtr->local_rack_capacity().has_value()) {
    topoStr += fmt::format(
        " local weight {}", topoInfoPtr->local_rack_capacity().value());
  }
  return topoStr;
}

std::string getInterfaceIDStr(const cli::NextHopInfo& nextHopInfo) {
  std::string interfaceIDStr;
  auto interfaceIDPtr = apache::thrift::get_pointer(nextHopInfo.interfaceID());
  if (interfaceIDPtr != nullptr) {
    interfaceIDStr = fmt::format("(i/f {}) ", *interfaceIDPtr);
  }
  return interfaceIDStr;
}
std::string getWeightStr(const cli::NextHopInfo& nextHopInfo) {
  const auto weight = folly::copy(nextHopInfo.weight().value());
  std::string weightStr = fmt::format(" weight {}", weight == 0 ? 1 : weight);
  if (nextHopInfo.preNormalizationWeight().has_value()) {
    const auto preNormalizationWeight = *nextHopInfo.preNormalizationWeight();
    weightStr += fmt::format(
        " (pre-normalization weight: {})",
        preNormalizationWeight == 0 ? 1 : preNormalizationWeight);
  }
  return weightStr;
}

std::string getCostStr(const cli::NextHopInfo& nextHopInfo) {
  std::string costStr;
  if (nextHopInfo.cost().has_value()) {
    costStr = fmt::format(" cost {}", nextHopInfo.cost().value());
  }
  return costStr;
}

std::string getRoleStr(const cli::NextHopInfo& nextHopInfo) {
  return folly::copy(nextHopInfo.isBackup().value()) ? " (BACKUP)" : "";
}

std::string getSrv6SidListStr(const cli::NextHopInfo& nextHopInfo) {
  auto sidListPtr = apache::thrift::get_pointer(nextHopInfo.srv6SegmentList());
  if (sidListPtr == nullptr || sidListPtr->empty()) {
    return "";
  }
  return fmt::format(" SRv6 SID List [{}]", folly::join(",", *sidListPtr));
}

std::string getNextHopInfoStr(
    const cli::NextHopInfo& nextHopInfo,
    const std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>&
        encoding) {
  auto ifNamePtr = apache::thrift::get_pointer(nextHopInfo.ifName());
  std::string viaStr;
  if (ifNamePtr != nullptr) {
    viaStr = fmt::format(" dev {}", *ifNamePtr);
  }
  std::string labelStr = getMplsLabelStr(nextHopInfo);
  std::string interfaceIDStr = getInterfaceIDStr(nextHopInfo);
  std::string weightStr = getWeightStr(nextHopInfo);
  std::string costStr = getCostStr(nextHopInfo);
  std::string topologyStr = getTopologyInfoStr(nextHopInfo, encoding);
  std::string srv6SidStr = getSrv6SidListStr(nextHopInfo);
  std::string roleStr = getRoleStr(nextHopInfo);
  auto ret = fmt::format(
      "{}{}{}{}{}{}{}{}{}",
      interfaceIDStr,
      nextHopInfo.addr().value(),
      viaStr,
      weightStr,
      costStr,
      labelStr,
      topologyStr,
      srv6SidStr,
      roleStr);
  return ret;
}

std::string getViaStr(
    const std::basic_string<char>* ifNamePtr,
    const std::map<std::string, std::string>& vlanAggregatePortMap,
    const std::map<
        std::string,
        std::map<std::string, std::vector<std::string>>>& vlanPortMap) {
  std::string fbossStr = "fboss";
  std::string viaStr;
  if (ifNamePtr != nullptr) {
    std::string vlanId = *ifNamePtr;
    if (vlanId.rfind(fbossStr, 0) == 0) {
      vlanId = vlanId.substr(fbossStr.length());
    }
    if (vlanAggregatePortMap.contains(vlanId)) {
      viaStr = fmt::format(" dev {}", vlanAggregatePortMap.at(vlanId));
    } else if (vlanPortMap.contains(vlanId)) {
      std::vector<std::string> port_names;
      for (const auto& ports : vlanPortMap.at(vlanId)) {
        for (const auto& port : ports.second) {
          port_names.push_back(port);
        }
      }
      if (!port_names.empty()) {
        viaStr = fmt::format(" dev {}", folly::join(", ", port_names));
      }
    } else {
      viaStr = fmt::format(" dev {}", *ifNamePtr);
    }
  }
  return viaStr;
}

std::string getNextHopInfoStr(
    const cli::NextHopInfo& nextHopInfo,
    const std::map<std::string, std::string>& vlanAggregatePortMap,
    const std::map<
        std::string,
        std::map<std::string, std::vector<std::string>>>& vlanPortMap,
    const std::optional<facebook::bgp::nsf_policy::NsfTeWeightEncoding>&
        encoding) {
  auto ifNamePtr = apache::thrift::get_pointer(nextHopInfo.ifName());
  auto viaStr = getViaStr(ifNamePtr, vlanAggregatePortMap, vlanPortMap);
  std::string labelStr = getMplsLabelStr(nextHopInfo);
  std::string interfaceIDStr = getInterfaceIDStr(nextHopInfo);
  std::string weightStr = getWeightStr(nextHopInfo);
  std::string costStr = getCostStr(nextHopInfo);
  std::string topologyStr = getTopologyInfoStr(nextHopInfo, encoding);
  std::string srv6SidStr = getSrv6SidListStr(nextHopInfo);
  std::string roleStr = getRoleStr(nextHopInfo);
  auto ret = fmt::format(
      "{}{}{}{}{}{}{}{}{}",
      interfaceIDStr,
      nextHopInfo.addr().value(),
      viaStr,
      weightStr,
      costStr,
      labelStr,
      topologyStr,
      srv6SidStr,
      roleStr);
  return ret;
}

} // namespace facebook::fboss::show::route::utils
