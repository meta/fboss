/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/acl/rule/AclRuleAttrs.h"

#include "fboss/agent/packet/IPProto.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/Range.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/LookupClassUtils.h"

namespace facebook::fboss {

namespace {

// Parse an integer that may be hex (0x...) or decimal. Used for tcp-flags,
// ethertype, and packet-lookup-result. Range-checks against [min, max].
int32_t parseIntInRange(
    std::string_view attr,
    const std::string& s,
    int64_t min,
    int64_t max) {
  int64_t parsed = 0;
  try {
    if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
      parsed = std::stoll(s, nullptr, 16);
    } else {
      parsed = folly::to<int64_t>(s);
    }
  } catch (const std::exception&) {
    throw std::invalid_argument(
        fmt::format("Value for '{}' must be an integer, got '{}'", attr, s));
  }
  if (parsed < min || parsed > max) {
    throw std::invalid_argument(
        fmt::format(
            "Value for '{}' out of range [{}, {}], got {}",
            attr,
            min,
            max,
            parsed));
  }
  return static_cast<int32_t>(parsed);
}

// Range-checked overload taking a named AclRuleRange.
int32_t parseIntInRange(
    std::string_view attr,
    const std::string& s,
    AclRuleRange range) {
  return parseIntInRange(attr, s, range.min, range.max);
}

// protocol: tcp/udp/icmp/icmpv6 keyword or numeric 0-255.
int32_t parseProtocol(const std::string& s) {
  if (s == "tcp") {
    return static_cast<int32_t>(IP_PROTO::IP_PROTO_TCP);
  }
  if (s == "udp") {
    return static_cast<int32_t>(IP_PROTO::IP_PROTO_UDP);
  }
  if (s == "icmp") {
    return static_cast<int32_t>(IP_PROTO::IP_PROTO_ICMP);
  }
  if (s == "icmpv6") {
    return static_cast<int32_t>(IP_PROTO::IP_PROTO_IPV6_ICMP);
  }
  return parseIntInRange(kAclRuleAttrProtocol, s, kProtocolRange);
}

cfg::IpFragMatch parseIpFragment(const std::string& s) {
  if (s == "not-fragmented") {
    return cfg::IpFragMatch::MATCH_NOT_FRAGMENTED;
  }
  if (s == "first-fragment") {
    return cfg::IpFragMatch::MATCH_FIRST_FRAGMENT;
  }
  if (s == "non-first-fragment") {
    return cfg::IpFragMatch::MATCH_NOT_FIRST_FRAGMENT;
  }
  if (s == "any-fragment") {
    return cfg::IpFragMatch::MATCH_ANY_FRAGMENT;
  }
  throw std::invalid_argument(
      fmt::format(
          "Value for 'ip-fragment' must be one of: not-fragmented, "
          "first-fragment, non-first-fragment, any-fragment; got '{}'",
          s));
}

cfg::IpType parseIpType(const std::string& s) {
  if (s == "ipv4") {
    return cfg::IpType::IP4;
  }
  if (s == "ipv6") {
    return cfg::IpType::IP6;
  }
  if (s == "any") {
    return cfg::IpType::ANY;
  }
  throw std::invalid_argument(
      fmt::format(
          "Value for 'ip-type' must be one of: ipv4, ipv6, any; got '{}'", s));
}

cfg::EtherType parseEtherType(const std::string& s) {
  if (s == "ipv4") {
    return cfg::EtherType::IPv4;
  }
  if (s == "ipv6") {
    return cfg::EtherType::IPv6;
  }
  if (s == "arp") {
    return cfg::EtherType::ARP;
  }
  if (s == "lldp") {
    return cfg::EtherType::LLDP;
  }
  if (s == "lacp") {
    return cfg::EtherType::LACP;
  }
  if (s == "macsec") {
    return cfg::EtherType::MACSEC;
  }
  if (s == "eapol") {
    return cfg::EtherType::EAPOL;
  }
  if (s == "any") {
    return cfg::EtherType::ANY;
  }
  // Numeric form (decimal or 0x-hex). EtherType is 16 bits.
  auto numeric = parseIntInRange(kAclRuleAttrEtherType, s, kU16Range);
  return static_cast<cfg::EtherType>(numeric);
}

cfg::PacketLookupResultType parsePacketLookupResult(const std::string& s) {
  if (s == "mpls-no-match") {
    return cfg::PacketLookupResultType::PACKET_LOOKUP_RESULT_MPLS_NO_MATCH;
  }
  // Numeric form. PacketLookupResultType currently has only one defined
  // value (= 1), but we accept any positive int and let the agent reject
  // unknown codes.
  auto numeric = parseIntInRange(kAclRuleAttrPacketLookupResult, s, kU16Range);
  return static_cast<cfg::PacketLookupResultType>(numeric);
}

AclRuleMutation onEntry(std::function<void(cfg::AclEntry&)> fn) {
  return {std::move(fn)};
}

// The value tokens following the fixed prefix (<table> <rule> <attr>, or
// ... <sub> for actions).
using Values = folly::Range<const std::string*>;

// Value-token arities used by the grammar rows below.
constexpr std::size_t kNoValue = 0;
constexpr std::size_t kOneValue = 1;
constexpr std::size_t kTwoValues = 2;

// Positions within a row's value span.
constexpr std::size_t kValue0 = 0; // single value / ttl value
constexpr std::size_t kValue1 = 1; // ttl mask

// Validate `value` parses as a CIDR network (bare IP → /32 or /128).
void validateCidrNetwork(std::string_view attr, const std::string& value) {
  try {
    folly::IPAddress::createNetwork(value);
  } catch (const std::exception& e) {
    throw std::invalid_argument(
        fmt::format(
            "Value for '{}' must be a CIDR network (A.B.C.D/N or "
            "A:B::C/N), got '{}': {}",
            attr,
            value,
            e.what()));
  }
}

// One row of the grammar: a keyword, its value-token arity [minVals, maxVals],
// a form string for --help/errors ("" = no value), and a builder turning the
// value tokens into a mutation. build is a non-capturing function pointer, so
// the table is plain data; only the returned mutation captures parsed values.
struct AclRuleRow {
  std::string_view key;
  std::size_t minVals;
  std::size_t maxVals;
  std::string_view form;
  AclRuleMutation (*build)(std::string_view key, Values vals);
};

const std::vector<AclRuleRow>& matchFieldRows() {
  static const std::vector<AclRuleRow> kRows = {
      {kAclRuleAttrSourceIp,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         validateCidrNetwork(key, v[kValue0]);
         const std::string& ip = v[kValue0];
         return onEntry([ip](cfg::AclEntry& r) { r.srcIp() = ip; });
       }},
      {kAclRuleAttrDestinationIp,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         validateCidrNetwork(key, v[kValue0]);
         const std::string& ip = v[kValue0];
         return onEntry([ip](cfg::AclEntry& r) { r.dstIp() = ip; });
       }},
      {kAclRuleAttrProtocol,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view, Values v) {
         auto p = static_cast<int16_t>(parseProtocol(v[kValue0]));
         return onEntry([p](cfg::AclEntry& r) { r.proto() = p; });
       }},
      {kAclRuleAttrSourcePort,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         auto p = parseIntInRange(key, v[kValue0], kL4PortRange);
         return onEntry([p](cfg::AclEntry& r) { r.l4SrcPort() = p; });
       }},
      {kAclRuleAttrDestinationPort,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         auto p = parseIntInRange(key, v[kValue0], kL4PortRange);
         return onEntry([p](cfg::AclEntry& r) { r.l4DstPort() = p; });
       }},
      {kAclRuleAttrDscp,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         auto d =
             static_cast<int8_t>(parseIntInRange(key, v[kValue0], kDscpRange));
         return onEntry([d](cfg::AclEntry& r) { r.dscp() = d; });
       }},
      {kAclRuleAttrTcpFlags,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         auto f = static_cast<int16_t>(
             parseIntInRange(key, v[kValue0], kTcpFlagsRange));
         return onEntry([f](cfg::AclEntry& r) { r.tcpFlagsBitMap() = f; });
       }},
      {kAclRuleAttrIcmpType,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         auto t = static_cast<int16_t>(
             parseIntInRange(key, v[kValue0], kIcmpByteRange));
         return onEntry([t](cfg::AclEntry& r) { r.icmpType() = t; });
       }},
      {kAclRuleAttrIcmpCode,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         auto c = static_cast<int16_t>(
             parseIntInRange(key, v[kValue0], kIcmpByteRange));
         return onEntry([c](cfg::AclEntry& r) { r.icmpCode() = c; });
       }},
      {kAclRuleAttrIpFragment,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view, Values v) {
         auto frag = parseIpFragment(v[kValue0]);
         return onEntry([frag](cfg::AclEntry& r) { r.ipFrag() = frag; });
       }},
      {kAclRuleAttrTtl,
       kOneValue,
       kTwoValues,
       "<value> [<mask>]",
       [](std::string_view key, Values v) {
         auto val =
             static_cast<int16_t>(parseIntInRange(key, v[kValue0], kTtlRange));
         int16_t mask = v.size() == kTwoValues
             ? static_cast<int16_t>(
                   parseIntInRange("ttl-mask", v[kValue1], kTtlRange))
             : kTtlMaskDefault;
         return onEntry([val, mask](cfg::AclEntry& r) {
           cfg::Ttl ttl;
           ttl.value() = val;
           ttl.mask() = mask;
           r.ttl() = ttl;
         });
       }},
      {kAclRuleAttrDestinationMac,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view, Values v) {
         std::string mac = v[kValue0];
         try {
           (void)folly::MacAddress{mac};
         } catch (const std::exception& e) {
           throw std::invalid_argument(
               fmt::format(
                   "Value for 'destination-mac' must be a MAC address, "
                   "got '{}': {}",
                   mac,
                   e.what()));
         }
         return onEntry([mac](cfg::AclEntry& r) { r.dstMac() = mac; });
       }},
      {kAclRuleAttrEtherType,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view, Values v) {
         auto et = parseEtherType(v[kValue0]);
         return onEntry([et](cfg::AclEntry& r) { r.etherType() = et; });
       }},
      {kAclRuleAttrVlan,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view key, Values v) {
         auto vlan = parseIntInRange(
             key, v[kValue0], {utils::kVlanIdMin, utils::kVlanIdMax});
         return onEntry([vlan](cfg::AclEntry& r) { r.vlanID() = vlan; });
       }},
      {kAclRuleAttrIpType,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view, Values v) {
         auto it = parseIpType(v[kValue0]);
         return onEntry([it](cfg::AclEntry& r) { r.ipType() = it; });
       }},
      {kAclRuleAttrPacketLookupResult,
       kOneValue,
       kOneValue,
       "<value>",
       [](std::string_view, Values v) {
         auto plr = parsePacketLookupResult(v[kValue0]);
         return onEntry(
             [plr](cfg::AclEntry& r) { r.packetLookupResult() = plr; });
       }},
      // Same spelling as `config interface <intf> lookup-class`: a numeric id
      // or a class name, parsed and range-checked by the shared helper.
      {kAclRuleAttrLookupClassL2,
       kOneValue,
       kOneValue,
       "<class-id>",
       [](std::string_view, Values v) {
         auto lc = lookup_class::parseLookupClassId(v[kValue0]);
         return onEntry([lc](cfg::AclEntry& r) { r.lookupClassL2() = lc; });
       }},
      {kAclRuleAttrLookupClassNeighbor,
       kOneValue,
       kOneValue,
       "<class-id>",
       [](std::string_view, Values v) {
         auto lc = lookup_class::parseLookupClassId(v[kValue0]);
         return onEntry(
             [lc](cfg::AclEntry& r) { r.lookupClassNeighbor() = lc; });
       }},
      {kAclRuleAttrLookupClassRoute,
       kOneValue,
       kOneValue,
       "<class-id>",
       [](std::string_view, Values v) {
         auto lc = lookup_class::parseLookupClassId(v[kValue0]);
         return onEntry([lc](cfg::AclEntry& r) { r.lookupClassRoute() = lc; });
       }},
  };
  return kRows;
}

const std::vector<AclRuleRow>& actionRows() {
  static const std::vector<AclRuleRow> kRows = {
      {kAclRuleActionPermit,
       kNoValue,
       kNoValue,
       "",
       [](std::string_view, Values) {
         return onEntry([](cfg::AclEntry& r) {
           r.actionType() = cfg::AclActionType::PERMIT;
         });
       }},
      {kAclRuleActionDeny,
       kNoValue,
       kNoValue,
       "",
       [](std::string_view, Values) {
         return onEntry([](cfg::AclEntry& r) {
           r.actionType() = cfg::AclActionType::DENY;
         });
       }},
      {kAclRuleActionDenyDataAndControlPlane,
       kNoValue,
       kNoValue,
       "",
       [](std::string_view, Values) {
         return onEntry([](cfg::AclEntry& r) {
           r.actionType() = cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE;
         });
       }},
  };
  return kRows;
}

const AclRuleRow* FOLLY_NULLABLE
findRow(const std::vector<AclRuleRow>& rows, std::string_view key) {
  for (const auto& row : rows) {
    if (row.key == key) {
      return &row;
    }
  }
  return nullptr;
}

std::string rowKeysCsv(const std::vector<AclRuleRow>& rows) {
  std::string out;
  for (const auto& row : rows) {
    if (!out.empty()) {
      out += ", ";
    }
    out += row.key;
  }
  return out;
}

// Attr-level keywords are the match fields plus `action`.
std::string attrKeysCsv() {
  return rowKeysCsv(matchFieldRows()) + ", " + std::string(kAclRuleAttrAction);
}

void checkArity(std::string_view kind, const AclRuleRow& row, Values vals) {
  if (vals.size() >= row.minVals && vals.size() <= row.maxVals) {
    return;
  }
  // Order the branches by which bound was crossed so the subtraction is always
  // guarded (no size_t underflow regardless of the row's arity).
  if (vals.size() < row.minVals) {
    throw std::invalid_argument(
        fmt::format(
            "{} '{}' expects '{}'; missing {} token(s)",
            kind,
            row.key,
            row.form,
            row.minVals - vals.size()));
  }
  const auto extra = vals.size() - row.maxVals;
  if (row.form.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "{} '{}' takes no value; got {} extra token(s)",
            kind,
            row.key,
            extra));
  }
  throw std::invalid_argument(
      fmt::format(
          "{} '{}' expects '{}'; got {} extra token(s)",
          kind,
          row.key,
          row.form,
          extra));
}

AclRuleMutation buildMatchField(
    const std::string& attr,
    const std::vector<std::string>& v) {
  const auto* row = findRow(matchFieldRows(), attr);
  if (!row) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown attribute '{}' for acl rule. Valid attrs: {}",
            attr,
            attrKeysCsv()));
  }
  Values vals(v.data() + kAclRuleMatchPrefix, v.size() - kAclRuleMatchPrefix);
  checkArity("Attribute", *row, vals);
  return row->build(row->key, vals);
}

AclRuleMutation buildAction(const std::vector<std::string>& v) {
  if (v.size() <= kAclRuleIdxActionSub) {
    throw std::invalid_argument(
        fmt::format(
            "Action requires a sub-attribute. Valid actions: {}",
            rowKeysCsv(actionRows())));
  }
  const auto* row = findRow(actionRows(), v[kAclRuleIdxActionSub]);
  if (!row) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown action '{}' for acl rule. Valid actions: {}",
            v[kAclRuleIdxActionSub],
            rowKeysCsv(actionRows())));
  }
  Values vals(v.data() + kAclRuleActionPrefix, v.size() - kAclRuleActionPrefix);
  checkArity("Action", *row, vals);
  return row->build(row->key, vals);
}

} // namespace

// Parse a `config acl rule` token vector (<table> <rule> <attr> <value>...)
// into a mutation. Forks match-field vs action and validates the keyword and
// its arity; throws std::invalid_argument on any problem.
AclRuleMutation parseAclRuleSpec(const std::vector<std::string>& tokens) {
  if (tokens.size() < kAclRuleMatchPrefix) {
    throw std::invalid_argument(
        fmt::format(
            "Expected <table-name> <rule-name> <attr> <value> [<extra>], "
            "got {} argument(s)",
            tokens.size()));
  }
  const std::string& attr = tokens[kAclRuleIdxAttr];
  return attr == kAclRuleAttrAction ? buildAction(tokens)
                                    : buildMatchField(attr, tokens);
}

std::string aclRuleAttrKeysCsv() {
  return attrKeysCsv();
}

std::string aclRuleActionKeysCsv() {
  return rowKeysCsv(actionRows());
}

} // namespace facebook::fboss
