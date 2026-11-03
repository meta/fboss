// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

// Reverse mappings from LoadBalancer Thrift enum values (as they appear in
// the running-config JSON) to the token spellings the `config load-balancing`
// CLI accepts. Shared by ConfigLoadBalancingTest and DeleteLoadBalancingTest
// so the token vocabulary lives in one place.

#pragma once

#include <fmt/format.h>
#include <stdexcept>
#include <string>
#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {

inline std::string algorithmIntToToken(int enumValue) {
  switch (static_cast<cfg::HashingAlgorithm>(enumValue)) {
    case cfg::HashingAlgorithm::CRC16_CCITT:
      return "crc16-ccitt";
    case cfg::HashingAlgorithm::CRC32_LO:
      return "crc32-lo";
    case cfg::HashingAlgorithm::CRC32_HI:
      return "crc32-hi";
    case cfg::HashingAlgorithm::CRC32_ETHERNET_LO:
      return "crc32-ethernet-lo";
    case cfg::HashingAlgorithm::CRC32_ETHERNET_HI:
      return "crc32-ethernet-hi";
    case cfg::HashingAlgorithm::CRC32_KOOPMAN_LO:
      return "crc32-koopman-lo";
    case cfg::HashingAlgorithm::CRC32_KOOPMAN_HI:
      return "crc32-koopman-hi";
    case cfg::HashingAlgorithm::CRC:
      return "crc";
  }
  throw std::runtime_error(
      fmt::format("Unknown HashingAlgorithm enum value {}", enumValue));
}

// fieldKey is the fieldSelection JSON key: "ipv4Fields", "ipv6Fields",
// "transportFields", or "mplsFields".
inline std::string fieldIntToToken(const std::string& fieldKey, int enumValue) {
  if (fieldKey == "ipv4Fields") {
    switch (static_cast<cfg::IPv4Field>(enumValue)) {
      case cfg::IPv4Field::SOURCE_ADDRESS:
        return "src-ip";
      case cfg::IPv4Field::DESTINATION_ADDRESS:
        return "dst-ip";
    }
  } else if (fieldKey == "ipv6Fields") {
    switch (static_cast<cfg::IPv6Field>(enumValue)) {
      case cfg::IPv6Field::SOURCE_ADDRESS:
        return "src-ip";
      case cfg::IPv6Field::DESTINATION_ADDRESS:
        return "dst-ip";
      case cfg::IPv6Field::FLOW_LABEL:
        return "flow-label";
    }
  } else if (fieldKey == "transportFields") {
    switch (static_cast<cfg::TransportField>(enumValue)) {
      case cfg::TransportField::SOURCE_PORT:
        return "src-port";
      case cfg::TransportField::DESTINATION_PORT:
        return "dst-port";
    }
  } else if (fieldKey == "mplsFields") {
    switch (static_cast<cfg::MPLSField>(enumValue)) {
      case cfg::MPLSField::TOP_LABEL:
        return "top";
      case cfg::MPLSField::SECOND_LABEL:
        return "second";
      case cfg::MPLSField::THIRD_LABEL:
        return "third";
    }
  }
  throw std::runtime_error(
      fmt::format("Unknown {} enum value {}", fieldKey, enumValue));
}

} // namespace facebook::fboss
