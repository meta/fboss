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
#include <folly/String.h>
#include "common/network/if/gen-cpp2/Address_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/protocol/static/CmdConfigProtocolStatic.h"

namespace facebook::fboss {

enum class StaticRouteType {
  NEXTHOP,
  NULL_ROUTE,
  CPU_ROUTE,
};

/**
 * Template base class for static route arguments.
 * Handles both IPv4 and IPv6 routes with compile-time IP version checking.
 *
 * Parses command line arguments in the format:
 *   <prefix> <nexthop|null0|cpu> [nexthop2] [nexthop3] ...
 *
 * For example (IPv4):
 *   10.0.0.0/8 192.168.1.1               # Single nexthop
 *   10.0.0.0/8 192.168.1.1 192.168.1.2   # ECMP with two nexthops
 *   10.0.0.0/8 null0                     # Null route (drop)
 *   10.0.0.0/8 cpu                       # CPU route
 *
 * For example (IPv6):
 *   2001:db8::/32 2001:db8::1            # Single nexthop
 *   2001:db8::/32 2001:db8::1 2001:db8::2 # ECMP with two nexthops
 *   2001:db8::/32 null0                  # Null route (drop)
 *   2001:db8::/32 cpu                    # CPU route
 */
template <network::thrift::AddressType IPVersion>
class StaticRouteAddArgBase : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ StaticRouteAddArgBase( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v) {
    if (v.size() < 2) {
      throw std::invalid_argument(
          "Expected <prefix> <nexthop|null0|cpu> [nexthop2] ..., got " +
          std::to_string(v.size()) + " arguments");
    }

    // Parse and validate the prefix (CIDR network)
    try {
      auto [ip, prefixLen] = folly::IPAddress::createNetwork(v[0]);
      validateIpVersion(ip, v[0], "prefix");
      prefix_ = v[0];
    } catch (const folly::IPAddressFormatException& e) {
      throw std::invalid_argument(e.what());
    }

    // Check the route type from the first nexthop argument
    std::string firstNexthop = v[1];
    folly::toLowerAscii(firstNexthop);

    if (firstNexthop == "null0") {
      if (v.size() > 2) {
        throw std::invalid_argument(
            "null0 routes cannot have multiple nexthops");
      }
      routeType_ = StaticRouteType::NULL_ROUTE;
    } else if (firstNexthop == "cpu") {
      if (v.size() > 2) {
        throw std::invalid_argument("CPU routes cannot have multiple nexthops");
      }
      routeType_ = StaticRouteType::CPU_ROUTE;
    } else {
      // Parse all nexthops for ECMP
      for (size_t i = 1; i < v.size(); ++i) {
        auto addr = folly::IPAddress::tryFromString(v[i]);
        if (!addr.hasValue()) {
          throw std::invalid_argument(
              "Invalid nexthop IP address: " + v[i] + ". Expected valid " +
              ipVersionName() + " address or 'null0' or 'cpu'");
        }
        validateIpVersion(addr.value(), v[i], "nexthop");
        nexthops_.emplace_back(v[i]);
      }
      routeType_ = StaticRouteType::NEXTHOP;
    }
  }

  const std::string& getPrefix() const {
    return prefix_;
  }

  StaticRouteType getRouteType() const {
    return routeType_;
  }

  const std::vector<std::string>& getNexthops() const {
    return nexthops_;
  }

 private:
  static constexpr const char* ipVersionName() {
    if constexpr (IPVersion == network::thrift::AddressType::V4) {
      return "IPv4";
    } else {
      return "IPv6";
    }
  }

  static constexpr const char* otherVersionCommand() {
    if constexpr (IPVersion == network::thrift::AddressType::V4) {
      return "config protocol static ipv6 route add";
    } else {
      return "config protocol static ip route add";
    }
  }

  void validateIpVersion(
      const folly::IPAddress& ip,
      const std::string& value,
      const char* fieldName) {
    bool isCorrectVersion =
        (IPVersion == network::thrift::AddressType::V4) ? ip.isV4() : ip.isV6();
    if (!isCorrectVersion) {
      std::string otherVersion =
          (IPVersion == network::thrift::AddressType::V4) ? "IPv6" : "IPv4";
      throw std::invalid_argument(
          "Expected " + std::string(ipVersionName()) + " " + fieldName +
          ", got " + otherVersion + ": " + value + ". Use '" +
          otherVersionCommand() + "' for " + otherVersion + " routes");
    }
  }

  std::string prefix_;
  StaticRouteType routeType_;
  std::vector<std::string> nexthops_;
};

class StaticRouteAddIpArg
    : public StaticRouteAddArgBase<network::thrift::AddressType::V4> {
 public:
  using StaticRouteAddArgBase::StaticRouteAddArgBase;
};

class StaticRouteAddIpv6Arg
    : public StaticRouteAddArgBase<network::thrift::AddressType::V6> {
 public:
  using StaticRouteAddArgBase::StaticRouteAddArgBase;
};

template <typename RouteArgType>
std::string addStaticRouteImpl(const RouteArgType& routeArg);

struct CmdConfigProtocolStaticIpRouteAddTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigProtocolStatic;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "static_route",
        args,
        "<prefix> <nexthop|null0|cpu> - Add an IPv4 static route");
  }
  static constexpr network::thrift::AddressType ipVersion =
      network::thrift::AddressType::V4;
  using ObjectArgType = StaticRouteAddIpArg;
  using RetType = std::string;
};

class CmdConfigProtocolStaticIpRouteAdd
    : public CmdHandler<
          CmdConfigProtocolStaticIpRouteAdd,
          CmdConfigProtocolStaticIpRouteAddTraits> {
 public:
  using ObjectArgType = CmdConfigProtocolStaticIpRouteAddTraits::ObjectArgType;
  using RetType = CmdConfigProtocolStaticIpRouteAddTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& routeArg);

  void printOutput(const RetType& logMsg);
};

struct CmdConfigProtocolStaticIpv6RouteAddTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigProtocolStatic;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "static_route",
        args,
        "<prefix> <nexthop|null0|cpu> - Add an IPv6 static route");
  }
  static constexpr network::thrift::AddressType ipVersion =
      network::thrift::AddressType::V6;
  using ObjectArgType = StaticRouteAddIpv6Arg;
  using RetType = std::string;
};

class CmdConfigProtocolStaticIpv6RouteAdd
    : public CmdHandler<
          CmdConfigProtocolStaticIpv6RouteAdd,
          CmdConfigProtocolStaticIpv6RouteAddTraits> {
 public:
  using ObjectArgType =
      CmdConfigProtocolStaticIpv6RouteAddTraits::ObjectArgType;
  using RetType = CmdConfigProtocolStaticIpv6RouteAddTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& routeArg);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
