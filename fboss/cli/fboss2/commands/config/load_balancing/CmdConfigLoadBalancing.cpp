/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/load_balancing/CmdConfigLoadBalancing.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/lang/Assume.h>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

// Attribute names accepted after `config load-balancing <ecmp|lag>`.
constexpr std::string_view kAttrHashFieldsIpv4 = "hash-fields-ipv4";
constexpr std::string_view kAttrHashFieldsIpv6 = "hash-fields-ipv6";
constexpr std::string_view kAttrHashFieldsTransport = "hash-fields-transport";
constexpr std::string_view kAttrHashFieldsMpls = "hash-fields-mpls";
constexpr std::string_view kAttrHashAlgorithm = "hash-algorithm";
constexpr std::string_view kAttrHashSeed = "hash-seed";

const std::set<std::string_view> kLoadBalancingValidAttrs = {
    kAttrHashFieldsIpv4,
    kAttrHashFieldsIpv6,
    kAttrHashFieldsTransport,
    kAttrHashFieldsMpls,
    kAttrHashAlgorithm,
    kAttrHashSeed,
};

// Sentinel value for an empty set (hash-fields-*) or unset optional
// (hash-seed).
constexpr std::string_view kValueNone = "none";
constexpr std::string_view kValueDefault = "default";

// LoadBalancerID display names used in CLI output messages.
constexpr std::string_view kLbIdEcmp = "ecmp";
constexpr std::string_view kLbIdLag = "lag";

// Field name tokens accepted inside hash-fields-* value lists.
constexpr std::string_view kFieldSrcIp = "src-ip";
constexpr std::string_view kFieldDstIp = "dst-ip";
constexpr std::string_view kFieldFlowLabel = "flow-label";
constexpr std::string_view kFieldSrcPort = "src-port";
constexpr std::string_view kFieldDstPort = "dst-port";
constexpr std::string_view kFieldMplsTop = "top";
constexpr std::string_view kFieldMplsSecond = "second";
constexpr std::string_view kFieldMplsThird = "third";

// Hashing-algorithm tokens accepted as the value for hash-algorithm.
constexpr std::string_view kAlgoCrc16Ccitt = "crc16-ccitt";
constexpr std::string_view kAlgoCrc32Lo = "crc32-lo";
constexpr std::string_view kAlgoCrc32Hi = "crc32-hi";
constexpr std::string_view kAlgoCrc32EthernetLo = "crc32-ethernet-lo";
constexpr std::string_view kAlgoCrc32EthernetHi = "crc32-ethernet-hi";
constexpr std::string_view kAlgoCrc32KoopmanLo = "crc32-koopman-lo";
constexpr std::string_view kAlgoCrc32KoopmanHi = "crc32-koopman-hi";
constexpr std::string_view kAlgoCrc = "crc";

const std::set<std::string_view> kValidAlgorithms = {
    kAlgoCrc16Ccitt,
    kAlgoCrc32Lo,
    kAlgoCrc32Hi,
    kAlgoCrc32EthernetLo,
    kAlgoCrc32EthernetHi,
    kAlgoCrc32KoopmanLo,
    kAlgoCrc32KoopmanHi,
    kAlgoCrc,
};

std::string algorithmToString(cfg::HashingAlgorithm a) {
  switch (a) {
    case cfg::HashingAlgorithm::CRC16_CCITT:
      return std::string(kAlgoCrc16Ccitt);
    case cfg::HashingAlgorithm::CRC32_LO:
      return std::string(kAlgoCrc32Lo);
    case cfg::HashingAlgorithm::CRC32_HI:
      return std::string(kAlgoCrc32Hi);
    case cfg::HashingAlgorithm::CRC32_ETHERNET_LO:
      return std::string(kAlgoCrc32EthernetLo);
    case cfg::HashingAlgorithm::CRC32_ETHERNET_HI:
      return std::string(kAlgoCrc32EthernetHi);
    case cfg::HashingAlgorithm::CRC32_KOOPMAN_LO:
      return std::string(kAlgoCrc32KoopmanLo);
    case cfg::HashingAlgorithm::CRC32_KOOPMAN_HI:
      return std::string(kAlgoCrc32KoopmanHi);
    case cfg::HashingAlgorithm::CRC:
      return std::string(kAlgoCrc);
  }
  folly::assume_unreachable();
}

cfg::HashingAlgorithm parseAlgorithm(const std::string& v) {
  if (v == kAlgoCrc16Ccitt) {
    return cfg::HashingAlgorithm::CRC16_CCITT;
  } else if (v == kAlgoCrc32Lo) {
    return cfg::HashingAlgorithm::CRC32_LO;
  } else if (v == kAlgoCrc32Hi) {
    return cfg::HashingAlgorithm::CRC32_HI;
  } else if (v == kAlgoCrc32EthernetLo) {
    return cfg::HashingAlgorithm::CRC32_ETHERNET_LO;
  } else if (v == kAlgoCrc32EthernetHi) {
    return cfg::HashingAlgorithm::CRC32_ETHERNET_HI;
  } else if (v == kAlgoCrc32KoopmanLo) {
    return cfg::HashingAlgorithm::CRC32_KOOPMAN_LO;
  } else if (v == kAlgoCrc32KoopmanHi) {
    return cfg::HashingAlgorithm::CRC32_KOOPMAN_HI;
  } else if (v == kAlgoCrc) {
    return cfg::HashingAlgorithm::CRC;
  }
  throw std::invalid_argument(
      fmt::format(
          "Unknown hash algorithm '{}'. Valid: {}",
          v,
          folly::join(", ", kValidAlgorithms)));
}

std::vector<std::string> splitFieldList(const std::string& v) {
  std::vector<std::string> tokens;
  if (v == kValueNone) {
    return tokens;
  }
  folly::split(',', v, tokens);
  for (auto& t : tokens) {
    t = folly::trimWhitespace(t).str();
  }
  // Reject empty tokens (e.g. "src-ip,,dst-ip" or trailing comma).
  for (const auto& t : tokens) {
    if (t.empty()) {
      throw std::invalid_argument(
          fmt::format("Empty field name in list '{}'", v));
    }
  }
  return tokens;
}

std::set<cfg::IPv4Field> parseIpv4Fields(const std::string& v) {
  std::set<cfg::IPv4Field> out;
  for (const auto& t : splitFieldList(v)) {
    if (t == kFieldSrcIp) {
      out.insert(cfg::IPv4Field::SOURCE_ADDRESS);
    } else if (t == kFieldDstIp) {
      out.insert(cfg::IPv4Field::DESTINATION_ADDRESS);
    } else {
      throw std::invalid_argument(
          fmt::format(
              "Unknown IPv4 hash field '{}'. Valid: {}, {} or 'none'",
              t,
              kFieldSrcIp,
              kFieldDstIp));
    }
  }
  return out;
}

std::set<cfg::IPv6Field> parseIpv6Fields(const std::string& v) {
  std::set<cfg::IPv6Field> out;
  for (const auto& t : splitFieldList(v)) {
    if (t == kFieldSrcIp) {
      out.insert(cfg::IPv6Field::SOURCE_ADDRESS);
    } else if (t == kFieldDstIp) {
      out.insert(cfg::IPv6Field::DESTINATION_ADDRESS);
    } else if (t == kFieldFlowLabel) {
      out.insert(cfg::IPv6Field::FLOW_LABEL);
    } else {
      throw std::invalid_argument(
          fmt::format(
              "Unknown IPv6 hash field '{}'. Valid: {}, {}, {} or 'none'",
              t,
              kFieldSrcIp,
              kFieldDstIp,
              kFieldFlowLabel));
    }
  }
  return out;
}

std::set<cfg::TransportField> parseTransportFields(const std::string& v) {
  std::set<cfg::TransportField> out;
  for (const auto& t : splitFieldList(v)) {
    if (t == kFieldSrcPort) {
      out.insert(cfg::TransportField::SOURCE_PORT);
    } else if (t == kFieldDstPort) {
      out.insert(cfg::TransportField::DESTINATION_PORT);
    } else {
      throw std::invalid_argument(
          fmt::format(
              "Unknown transport hash field '{}'. Valid: {}, {} or 'none'",
              t,
              kFieldSrcPort,
              kFieldDstPort));
    }
  }
  return out;
}

std::set<cfg::MPLSField> parseMplsFields(const std::string& v) {
  std::set<cfg::MPLSField> out;
  for (const auto& t : splitFieldList(v)) {
    if (t == kFieldMplsTop) {
      out.insert(cfg::MPLSField::TOP_LABEL);
    } else if (t == kFieldMplsSecond) {
      out.insert(cfg::MPLSField::SECOND_LABEL);
    } else if (t == kFieldMplsThird) {
      out.insert(cfg::MPLSField::THIRD_LABEL);
    } else {
      throw std::invalid_argument(
          fmt::format(
              "Unknown MPLS hash field '{}'. Valid: {}, {}, {} or 'none'",
              t,
              kFieldMplsTop,
              kFieldMplsSecond,
              kFieldMplsThird));
    }
  }
  return out;
}

// Find the LoadBalancer entry with the given id, or append a new one with a
// default algorithm and empty field sets.
cfg::LoadBalancer& findOrCreateLoadBalancer(
    cfg::SwitchConfig& swConfig,
    cfg::LoadBalancerID id) {
  auto& list = *swConfig.loadBalancers();
  for (auto& lb : list) {
    if (*lb.id() == id) {
      return lb;
    }
  }
  cfg::LoadBalancer newLb;
  newLb.id() = id;
  // CRC16_CCITT is the lowest-numbered HashingAlgorithm enum value and a
  // reasonable default when the user hasn't yet set one.
  newLb.algorithm() = cfg::HashingAlgorithm::CRC16_CCITT;
  list.push_back(std::move(newLb));
  return list.back();
}

} // namespace

std::string lbIdToString(cfg::LoadBalancerID id) {
  switch (id) {
    case cfg::LoadBalancerID::ECMP:
      return std::string(kLbIdEcmp);
    case cfg::LoadBalancerID::AGGREGATE_PORT:
      return std::string(kLbIdLag);
  }
  folly::assume_unreachable();
}

LoadBalancingConfigArgs::LoadBalancingConfigArgs(std::vector<std::string> v) {
  if (v.size() != 2) {
    throw std::invalid_argument(
        fmt::format(
            "Expected <attr> <value>, got {} argument(s). Valid attrs: {}",
            v.size(),
            folly::join(", ", kLoadBalancingValidAttrs)));
  }
  if (kLoadBalancingValidAttrs.find(v[0]) == kLoadBalancingValidAttrs.end()) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown load-balancing attribute '{}'. Valid attrs: {}",
            v[0],
            folly::join(", ", kLoadBalancingValidAttrs)));
  }
  attribute_ = v[0];
  value_ = v[1];
  data_ = std::move(v);
}

std::string applyLoadBalancerConfig(
    cfg::SwitchConfig& swConfig,
    cfg::LoadBalancerID id,
    const std::string& attr,
    const std::string& value) {
  auto& lb = findOrCreateLoadBalancer(swConfig, id);
  auto idStr = lbIdToString(id);

  if (attr == kAttrHashFieldsIpv4) {
    lb.fieldSelection()->ipv4Fields() = parseIpv4Fields(value);
    return fmt::format("Set {} {} to {}", idStr, attr, value);
  }
  if (attr == kAttrHashFieldsIpv6) {
    lb.fieldSelection()->ipv6Fields() = parseIpv6Fields(value);
    return fmt::format("Set {} {} to {}", idStr, attr, value);
  }
  if (attr == kAttrHashFieldsTransport) {
    lb.fieldSelection()->transportFields() = parseTransportFields(value);
    return fmt::format("Set {} {} to {}", idStr, attr, value);
  }
  if (attr == kAttrHashFieldsMpls) {
    lb.fieldSelection()->mplsFields() = parseMplsFields(value);
    return fmt::format("Set {} {} to {}", idStr, attr, value);
  }
  if (attr == kAttrHashAlgorithm) {
    auto algo = parseAlgorithm(value);
    lb.algorithm() = algo;
    return fmt::format("Set {} {} to {}", idStr, attr, algorithmToString(algo));
  }
  if (attr == kAttrHashSeed) {
    if (value == kValueDefault) {
      lb.seed().reset();
      return fmt::format(
          "Cleared {} {} (using deterministic default)", idStr, attr);
    }
    int32_t parsed = 0;
    try {
      parsed = folly::to<int32_t>(value);
    } catch (const folly::ConversionError&) {
      throw std::invalid_argument(
          fmt::format(
              "Value for '{}' must be an integer or 'default', got '{}'",
              attr,
              value));
    }
    lb.seed() = parsed;
    return fmt::format("Set {} {} to {}", idStr, attr, parsed);
  }

  // LoadBalancingConfigArgs validates this; defensive guard in case
  // kLoadBalancingValidAttrs drifts from the dispatch here.
  throw std::runtime_error(
      fmt::format("Unhandled load-balancing attribute '{}'", attr));
}

namespace {

// Shared implementation for both ECMP and LAG handlers. LoadBalancer edits
// are applied hitlessly by the SAI layer: SaiSwitch dispatches
// LoadBalancersDelta changes to SaiSwitchManager::changeLoadBalancer without
// any *ChangeProhibited guard.
std::string runLoadBalancerConfig(
    cfg::LoadBalancerID id,
    const LoadBalancingConfigArgs& args) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();
  auto msg = applyLoadBalancerConfig(
      *config.sw(), id, args.getAttribute(), args.getValue());
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

} // namespace

CmdConfigLoadBalancingEcmpTraits::RetType
CmdConfigLoadBalancingEcmp::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  return runLoadBalancerConfig(cfg::LoadBalancerID::ECMP, args);
}

void CmdConfigLoadBalancingEcmp::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

CmdConfigLoadBalancingLagTraits::RetType CmdConfigLoadBalancingLag::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  return runLoadBalancerConfig(cfg::LoadBalancerID::AGGREGATE_PORT, args);
}

void CmdConfigLoadBalancingLag::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiations
template void
CmdHandler<CmdConfigLoadBalancingEcmp, CmdConfigLoadBalancingEcmpTraits>::run();
template void
CmdHandler<CmdConfigLoadBalancingLag, CmdConfigLoadBalancingLagTraits>::run();

} // namespace facebook::fboss
