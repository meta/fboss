/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "CmdShowAcl.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <fmt/format.h>
#include <folly/MapUtil.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "fboss/agent/AddressUtil.h"
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

using RetType = CmdShowAclTraits::RetType;

RetType CmdShowAcl::queryClient(const HostInfo& hostInfo) {
  facebook::fboss::AclTableThrift entries;
  std::vector<facebook::fboss::AclEntryThrift> aclEntries;
  auto client =
      utils::createClient<facebook::fboss::FbossCtrlAsyncClient>(hostInfo);
  client->sync_getAclTableGroup(entries);

  std::vector<std::string> counterKeys;
  for (const auto& [_, tableEntries] : *entries.aclTableEntries()) {
    for (const auto& entry : tableEntries) {
      if (entry.counterName()) {
        counterKeys.push_back(*entry.counterName() + ".packets.sum");
        counterKeys.push_back(*entry.counterName() + ".bytes.sum");
      }
    }
  }
  if (counterKeys.empty()) {
    return createModel(entries, {});
  }
  return createModel(
      entries, utils::sumAgentFb303Counters(hostInfo, counterKeys));
}

void CmdShowAcl::printOutput(const RetType& model, std::ostream& out) {
  for (const auto& aclTable : model.aclTableEntries().value()) {
    out << "ACL Table Name: " << aclTable.first << std::endl;
    for (const auto& aclEntry : aclTable.second) {
      out << "Acl: " << aclEntry.name().value() << std::endl;
      out << "   priority: " << folly::copy(aclEntry.priority().value())
          << std::endl;
      if (aclEntry.srcIp().has_value()) {
        out << "   src ip: " << aclEntry.srcIp().value();
        if (aclEntry.srcIpPrefixLength().has_value()) {
          out << "/" << folly::copy(aclEntry.srcIpPrefixLength().value());
        }
        out << std::endl;
      }
      if (aclEntry.dstIp().has_value()) {
        out << "   dst ip: " << aclEntry.dstIp().value();
        if (aclEntry.dstIpPrefixLength().has_value()) {
          out << "/" << folly::copy(aclEntry.dstIpPrefixLength().value());
        }
        out << std::endl;
      }
      if (aclEntry.proto().has_value()) {
        out << "   proto: " << folly::copy(aclEntry.proto().value())
            << std::endl;
      }
      if (aclEntry.srcPort().has_value()) {
        out << "   src port: " << folly::copy(aclEntry.srcPort().value())
            << std::endl;
      }
      if (aclEntry.dstPort().has_value()) {
        out << "   dst port: " << folly::copy(aclEntry.dstPort().value())
            << std::endl;
      }
      if (aclEntry.ipFrag().has_value()) {
        out << "   ip fragment: " << folly::copy(aclEntry.ipFrag().value())
            << std::endl;
      }
      if (aclEntry.dscp().has_value()) {
        out << "   dscp: " << folly::copy(aclEntry.dscp().value()) << std::endl;
      }
      if (aclEntry.ipType().has_value()) {
        out << "   ip type: " << folly::copy(aclEntry.ipType().value())
            << std::endl;
      }
      if (aclEntry.icmpType().has_value()) {
        out << "   icmp type: " << folly::copy(aclEntry.icmpType().value())
            << std::endl;
      }
      if (aclEntry.icmpCode().has_value()) {
        out << "   icmp code: " << folly::copy(aclEntry.icmpCode().value())
            << std::endl;
      }
      if (aclEntry.ttl().has_value()) {
        out << "   ttl: " << folly::copy(aclEntry.ttl().value()) << std::endl;
      }
      if (aclEntry.l4SrcPort().has_value()) {
        out << "   L4 src port: " << folly::copy(aclEntry.l4SrcPort().value())
            << std::endl;
      }
      if (aclEntry.l4DstPort().has_value()) {
        out << "   L4 dst port: " << folly::copy(aclEntry.l4DstPort().value())
            << std::endl;
      }
      if (aclEntry.dstMac().has_value()) {
        out << "   dst mac: " << aclEntry.dstMac().value() << std::endl;
      }
      if (aclEntry.lookupClassL2().has_value()) {
        out << "   lookup class L2: "
            << folly::copy(aclEntry.lookupClassL2().value()) << std::endl;
      }
      if (aclEntry.lookupClassPort().has_value()) {
        out << "   lookup class port: " << aclEntry.lookupClassPort().value()
            << std::endl;
      }
      if (aclEntry.etherType()) {
        out << "   ether type: " << *aclEntry.etherType() << std::endl;
      }
      if (aclEntry.dstIpV6Word3()) {
        out << fmt::format(
                   "   dst ipv6 word3: {:#010x}", *aclEntry.dstIpV6Word3())
            << std::endl;
      }
      if (aclEntry.dstIpV6Word2()) {
        out << fmt::format(
                   "   dst ipv6 word2: {:#010x}", *aclEntry.dstIpV6Word2())
            << std::endl;
      }
      if (aclEntry.trafficClass()) {
        out << "   traffic class: " << *aclEntry.trafficClass() << std::endl;
      }
      if (aclEntry.nextHopGroupId()) {
        out << "   route dst nhg id: " << *aclEntry.nextHopGroupId()
            << std::endl;
      }
      out << "   action: " << aclEntry.actionType().value() << std::endl;
      if (aclEntry.redirectNextHopGroupId()) {
        out << "   redirect nhg id: " << *aclEntry.redirectNextHopGroupId()
            << std::endl;
      }
      if (aclEntry.counterName()) {
        out << "   counter: " << *aclEntry.counterName() << std::endl;
      }
      if (aclEntry.packets()) {
        out << "   packets: " << *aclEntry.packets() << std::endl;
      }
      if (aclEntry.bytes()) {
        out << "   bytes: " << *aclEntry.bytes() << std::endl;
      }
      if (folly::copy(aclEntry.enabled().value())) {
        out << "   enabled: " << folly::copy(aclEntry.enabled().value())
            << std::endl;
      }
      out << std::endl;
    }
  }
}

RetType CmdShowAcl::createModel(
    facebook::fboss::AclTableThrift entries,
    const std::map<std::string, int64_t>& counters) {
  RetType model;
  std::map<std::string, std::vector<facebook::fboss::AclEntryThrift>>
      aclTableEntries = entries.aclTableEntries().value();

  for (auto& [aclTableName, aclEntries] : aclTableEntries) {
    for (const auto& entry : aclEntries) {
      cli::AclEntry aclDetails;

      aclDetails.name() = entry.name().value();
      aclDetails.priority() = folly::copy(entry.priority().value());
      // srcIp/dstIp are non-optional BinaryAddress on the wire; the agent
      // populates them from a default-constructed CIDRNetwork when unset,
      // yielding an empty addr and prefixLength=0. Treat prefixLength>0 as
      // the signal that the ACL actually qualifies on the address.
      if (folly::copy(entry.srcIpPrefixLength().value()) > 0 &&
          !entry.srcIp()->addr()->empty()) {
        aclDetails.srcIp() =
            facebook::network::toIPAddress(entry.srcIp().value()).str();
        aclDetails.srcIpPrefixLength() =
            folly::copy(entry.srcIpPrefixLength().value());
      }
      if (folly::copy(entry.dstIpPrefixLength().value()) > 0 &&
          !entry.dstIp()->addr()->empty()) {
        aclDetails.dstIp() =
            facebook::network::toIPAddress(entry.dstIp().value()).str();
        aclDetails.dstIpPrefixLength() =
            folly::copy(entry.dstIpPrefixLength().value());
      }
      if (apache::thrift::get_pointer(entry.proto())) {
        aclDetails.proto() =
            static_cast<int16_t>(*apache::thrift::get_pointer(entry.proto()));
      }
      if (apache::thrift::get_pointer(entry.srcPort())) {
        aclDetails.srcPort() = *apache::thrift::get_pointer(entry.srcPort());
      }
      if (apache::thrift::get_pointer(entry.dstPort())) {
        aclDetails.dstPort() = *apache::thrift::get_pointer(entry.dstPort());
      }
      if (apache::thrift::get_pointer(entry.ipFrag())) {
        aclDetails.ipFrag() =
            static_cast<int16_t>(*apache::thrift::get_pointer(entry.ipFrag()));
      }
      if (apache::thrift::get_pointer(entry.dscp())) {
        aclDetails.dscp() =
            static_cast<int16_t>(*apache::thrift::get_pointer(entry.dscp()));
      }
      if (apache::thrift::get_pointer(entry.ipType())) {
        aclDetails.ipType() =
            static_cast<int16_t>(*apache::thrift::get_pointer(entry.ipType()));
      }
      if (apache::thrift::get_pointer(entry.icmpType())) {
        aclDetails.icmpType() = static_cast<int16_t>(
            *apache::thrift::get_pointer(entry.icmpType()));
      }
      if (apache::thrift::get_pointer(entry.icmpCode())) {
        aclDetails.icmpCode() = static_cast<int16_t>(
            *apache::thrift::get_pointer(entry.icmpCode()));
      }
      if (apache::thrift::get_pointer(entry.ttl())) {
        aclDetails.ttl() = *apache::thrift::get_pointer(entry.ttl());
      }
      if (apache::thrift::get_pointer(entry.l4SrcPort())) {
        aclDetails.l4SrcPort() =
            *apache::thrift::get_pointer(entry.l4SrcPort());
      }
      if (apache::thrift::get_pointer(entry.l4DstPort())) {
        aclDetails.l4DstPort() =
            *apache::thrift::get_pointer(entry.l4DstPort());
      }
      if (apache::thrift::get_pointer(entry.dstMac())) {
        aclDetails.dstMac() = *apache::thrift::get_pointer(entry.dstMac());
      }
      if (apache::thrift::get_pointer(entry.lookupClassL2())) {
        aclDetails.lookupClassL2() = static_cast<int16_t>(
            *apache::thrift::get_pointer(entry.lookupClassL2()));
      }
      if (apache::thrift::get_pointer(entry.lookupClassPort())) {
        aclDetails.lookupClassPort() = utils::getAclLookupClassPortStr(
            entry.lookupClassPort().to_optional());
      }
      if (entry.etherType()) {
        aclDetails.etherType() =
            apache::thrift::util::enumNameSafe(*entry.etherType());
      }
      if (entry.dstIpV6Word3()) {
        aclDetails.dstIpV6Word3() = *entry.dstIpV6Word3();
      }
      if (entry.dstIpV6Word2()) {
        aclDetails.dstIpV6Word2() = *entry.dstIpV6Word2();
      }
      if (entry.trafficClass()) {
        aclDetails.trafficClass() = *entry.trafficClass();
      }
      if (entry.nextHopGroupId()) {
        aclDetails.nextHopGroupId() = *entry.nextHopGroupId();
      }
      if (entry.redirectNextHopGroupId()) {
        aclDetails.redirectNextHopGroupId() = *entry.redirectNextHopGroupId();
      }
      if (entry.counterName()) {
        aclDetails.counterName() = *entry.counterName();
        if (auto packets = folly::get_optional(
                counters, *entry.counterName() + ".packets.sum")) {
          aclDetails.packets() = *packets;
        }
        if (auto bytes = folly::get_optional(
                counters, *entry.counterName() + ".bytes.sum")) {
          aclDetails.bytes() = *bytes;
        }
      }
      aclDetails.actionType() = entry.actionType().value();
      if (apache::thrift::get_pointer(entry.enabled())) {
        aclDetails.enabled() = *apache::thrift::get_pointer(entry.enabled());
      }
      model.aclTableEntries()[aclTableName].push_back(aclDetails);
    }
  }
  return model;
}

std::string_view CmdShowAclTraits::description() {
  return "Displays the configured ACL tables and their entries: each ACL's name, priority, match fields (IP, DSCP, protocol, TTL, etc.), action, and whether it's enabled. Use it to review packet-classification and control-plane policing rules.";
}

RetType CmdShowAcl::sampleModel() {
  RetType model;

  cli::AclEntry acl1;
  acl1.name() = "cpuPolicing-high-NetworkControl-ff02::/16";
  acl1.priority() = 2;
  acl1.dstIp() = "ff02::";
  acl1.dstIpPrefixLength() = 16;
  acl1.dscp() = 48;
  acl1.actionType() = "permit";
  acl1.enabled() = 1;

  cli::AclEntry acl2;
  acl2.name() = "mpls-dest-nomatch";
  acl2.priority() = 4;
  acl2.actionType() = "permit";
  acl2.enabled() = 1;
  acl2.lookupClassPort() = "Restricted";

  cli::AclEntry acl3;
  acl3.name() = "ttld-interconnect";
  acl3.priority() = 100004;
  acl3.srcIp() = "2001:db8:1c00::";
  acl3.srcIpPrefixLength() = 40;
  acl3.proto() = 6;
  acl3.ttl() = 128;
  acl3.actionType() = "permit";
  acl3.enabled() = 1;

  model.aclTableEntries()["AclTable1"] = {acl1, acl2, acl3};

  return model;
}

// Explicit template instantiation
template void CmdHandler<CmdShowAcl, CmdShowAclTraits>::run();
template const ValidFilterMapType
CmdHandler<CmdShowAcl, CmdShowAclTraits>::getValidFilters();

} // namespace facebook::fboss
