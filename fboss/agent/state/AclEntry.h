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

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/MatchAction.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace facebook::fboss {

class AclTtl {
 public:
  AclTtl(const AclTtl& ttl) {
    setValue(ttl.value_);
    setMask(ttl.mask_);
  }

  AclTtl(const uint16_t value, const uint16_t mask) {
    setValue(value);
    setMask(mask);
  }

  uint16_t getValue() const {
    return value_;
  }

  void setValue(uint16_t value) {
    if (value > 255) {
      throw FbossError("ttl value is invalid (must be [0,255])");
    }
    value_ = value;
  }

  uint16_t getMask() const {
    return mask_;
  }

  void setMask(uint16_t mask) {
    if (mask > 255) {
      throw FbossError("ttl mask is invalid (must be [0,255])");
    }
    mask_ = mask;
  }

  bool operator<(const AclTtl& ttl) const {
    return std::tie(value_, mask_) < std::tie(ttl.value_, ttl.mask_);
  }

  bool operator==(const AclTtl& ttl) const {
    return std::tie(value_, mask_) == std::tie(ttl.value_, ttl.mask_);
  }

  AclTtl& operator=(const AclTtl& ttl) = default;

  state::AclTtl toThrift() const;
  static AclTtl fromThrift(state::AclTtl const& entry);

 private:
  uint16_t value_;
  uint16_t mask_;
};

USE_THRIFT_COW(AclEntry);

/*
 * AclEntry stores state about one of the access control entries on
 * the switch.
 */
class AclEntry : public ThriftStructNode<AclEntry, state::AclEntryFields> {
 public:
  using BaseT = ThriftStructNode<AclEntry, state::AclEntryFields>;
  using BaseT::modify;
  using UdfGroups = std::vector<std::string>;
  using RoceBytes = std::vector<signed char>;
  using RoceMask = std::vector<signed char>;
  using UdfTable = std::vector<cfg::AclUdfEntry>;
  static const uint8_t kProtoIcmp = 1;
  static const uint8_t kProtoIcmpv6 = 58;
  static const uint8_t kMaxIcmpType = 0xFF;
  static const uint8_t kMaxIcmpCode = 0xFF;
  static const uint8_t kMaxTcpFlags = 0xFF;
  static const uint8_t kMaxTcpFlagsMask = 0x3F;
  static const uint16_t kMaxL4Port = 65535;

  explicit AclEntry(int priority, const std::string& name);
  explicit AclEntry(int priority, std::string&& name);

  int getPriority() const {
    return cref<switch_state_tags::priority>()->cref();
  }

  const std::string& getID() const {
    return cref<switch_state_tags::name>()->cref();
  }

  std::optional<bool> isEnabled() const {
    if (auto enabled = cref<switch_state_tags::enabled>()) {
      return enabled->cref();
    }
    return std::nullopt;
  }

  void setEnabled(std::optional<bool> enabled) {
    if (!enabled) {
      ref<switch_state_tags::enabled>().reset();
      return;
    }
    set<switch_state_tags::enabled>(enabled.value());
  }

  const auto& getAclAction() const {
    return cref<switch_state_tags::aclAction>();
  }

  // THRIFT_COPY
  void setAclAction(const MatchAction& action) {
    set<switch_state_tags::aclAction>(action.toThrift());
  }

  const cfg::AclActionType& getActionType() const {
    return cref<switch_state_tags::actionType>()->cref();
  }

  void setActionType(const cfg::AclActionType& actionType) {
    set<switch_state_tags::actionType>(actionType);
  }

  folly::CIDRNetwork getSrcIp() const {
    auto ref = cref<switch_state_tags::srcIp>();
    if (!ref) {
      return folly::CIDRNetwork{folly::IPAddress(), 0};
    }
    return folly::IPAddress::createNetwork(ref->cref());
  }

  void setSrcIp(const folly::CIDRNetwork& ip) {
    set<switch_state_tags::srcIp>(folly::IPAddress::networkToString(ip));
  }

  folly::CIDRNetwork getDstIp() const {
    auto ref = cref<switch_state_tags::dstIp>();
    if (!ref) {
      return folly::CIDRNetwork{folly::IPAddress(), 0};
    }
    return folly::IPAddress::createNetwork(ref->cref());
  }

  void setDstIp(const folly::CIDRNetwork& ip) {
    set<switch_state_tags::dstIp>(folly::IPAddress::networkToString(ip));
  }

  std::optional<uint32_t> getDstIpV6Word3() const {
    return getIpV6Word<switch_state_tags::dstIpV6Word3>();
  }

  void setDstIpV6Word3(int64_t word) {
    setIpV6Word<switch_state_tags::dstIpV6Word3>(word);
  }

  std::optional<uint32_t> getDstIpV6Word2() const {
    return getIpV6Word<switch_state_tags::dstIpV6Word2>();
  }

  void setDstIpV6Word2(int64_t word) {
    setIpV6Word<switch_state_tags::dstIpV6Word2>(word);
  }

  std::optional<uint8_t> getProto() const {
    if (auto proto = cref<switch_state_tags::proto>()) {
      return proto->cref();
    }
    return std::nullopt;
  }

  void setProto(const uint8_t proto) {
    set<switch_state_tags::proto>(proto);
  }

  std::optional<uint8_t> getTcpFlagsBitMap() const {
    if (auto tcpFlagsBitMap = cref<switch_state_tags::tcpFlagsBitMap>()) {
      return tcpFlagsBitMap->cref();
    }
    return std::nullopt;
  }

  void setTcpFlagsBitMap(const uint8_t flagsBitMap) {
    set<switch_state_tags::tcpFlagsBitMap>(flagsBitMap);
  }

  std::optional<uint8_t> getTcpFlagsMask() const {
    if (auto tcpFlagsMask = cref<switch_state_tags::tcpFlagsMask>()) {
      return tcpFlagsMask->cref();
    }
    return std::nullopt;
  }

  void setTcpFlagsMask(const uint8_t flagsMask) {
    set<switch_state_tags::tcpFlagsMask>(flagsMask);
  }

  std::optional<uint16_t> getSrcPort() const {
    if (auto srcPort = cref<switch_state_tags::srcPort>()) {
      return srcPort->cref();
    }
    return std::nullopt;
  }

  void setSrcPort(const uint16_t port) {
    set<switch_state_tags::srcPort>(port);
  }

  std::optional<uint16_t> getDstPort() const {
    if (auto dstPort = cref<switch_state_tags::dstPort>()) {
      return dstPort->cref();
    }
    return std::nullopt;
  }

  void setDstPort(const uint16_t port) {
    set<switch_state_tags::dstPort>(port);
  }

  std::optional<cfg::IpFragMatch> getIpFrag() const {
    if (auto ipFrag = cref<switch_state_tags::ipFrag>()) {
      return ipFrag->cref();
    }
    return std::nullopt;
  }

  void setIpFrag(const cfg::IpFragMatch& frag) {
    set<switch_state_tags::ipFrag>(frag);
  }

  std::optional<uint8_t> getIcmpCode() const {
    if (auto icmpCode = cref<switch_state_tags::icmpCode>()) {
      return icmpCode->cref();
    }
    return std::nullopt;
  }

  void setIcmpCode(const uint8_t code) {
    set<switch_state_tags::icmpCode>(code);
  }

  std::optional<uint8_t> getIcmpType() const {
    if (auto icmpType = cref<switch_state_tags::icmpType>()) {
      return icmpType->cref();
    }
    return std::nullopt;
  }

  void setIcmpType(const uint8_t type) {
    set<switch_state_tags::icmpType>(type);
  }

  std::optional<uint8_t> getDscp() const {
    if (auto dscp = cref<switch_state_tags::dscp>()) {
      return dscp->cref();
    }
    return std::nullopt;
  }

  void setDscp(uint8_t dscp) {
    set<switch_state_tags::dscp>(dscp);
  }

  std::optional<cfg::IpType> getIpType() const {
    if (auto ipType = cref<switch_state_tags::ipType>()) {
      return ipType->cref();
    }
    return std::nullopt;
  }

  void setIpType(const cfg::IpType& ipType) {
    set<switch_state_tags::ipType>(ipType);
  }

  // THRIFT_COPY
  std::optional<AclTtl> getTtl() const {
    if (auto ttl = cref<switch_state_tags::ttl>()) {
      return AclTtl::fromThrift(ttl->toThrift());
    }
    return std::nullopt;
  }

  void setTtl(const AclTtl& ttl) {
    set<switch_state_tags::ttl>(ttl.toThrift());
  }

  // THRIFT_COPY
  std::optional<AclTtl> getMplsLabel0Ttl() const {
    if (auto ttl = cref<switch_state_tags::mplsLabel0Ttl>()) {
      return AclTtl::fromThrift(ttl->toThrift());
    }
    return std::nullopt;
  }

  void setMplsLabel0Ttl(const AclTtl& ttl) {
    set<switch_state_tags::mplsLabel0Ttl>(ttl.toThrift());
  }

  std::optional<cfg::EtherType> getEtherType() const {
    if (auto etherType = cref<switch_state_tags::etherType>()) {
      return etherType->cref();
    }
    return std::nullopt;
  }

  void setEtherType(cfg::EtherType etherType) {
    set<switch_state_tags::etherType>(etherType);
  }

  std::optional<folly::MacAddress> getDstMac() const {
    if (auto dstMac = cref<switch_state_tags::dstMac>()) {
      return folly::MacAddress(dstMac->cref());
    }
    return std::nullopt;
  }

  void setDstMac(const folly::MacAddress& dstMac) {
    set<switch_state_tags::dstMac>(dstMac.toString());
  }

  std::optional<uint16_t> getL4SrcPort() const {
    if (auto l4SrcPort = cref<switch_state_tags::l4SrcPort>()) {
      return l4SrcPort->cref();
    }
    return std::nullopt;
  }

  void setL4SrcPort(const uint16_t port) {
    set<switch_state_tags::l4SrcPort>(port);
  }

  std::optional<uint16_t> getL4DstPort() const {
    if (auto l4DstPort = cref<switch_state_tags::l4DstPort>()) {
      return l4DstPort->cref();
    }
    return std::nullopt;
  }

  void setL4DstPort(const uint16_t port) {
    set<switch_state_tags::l4DstPort>(port);
  }

  std::optional<cfg::Range> getL4DstPortRange() const {
    if (auto l4DstPortRange = cref<switch_state_tags::l4DstPortRange>()) {
      return l4DstPortRange->toThrift();
    }
    return std::nullopt;
  }

  void setL4DstPortRange(const cfg::Range& range) {
    set<switch_state_tags::l4DstPortRange>(range);
  }

  std::optional<cfg::AclLookupClassPort> getLookupClassPort() const {
    if (auto lookupClassPort = cref<switch_state_tags::lookupClassPort>()) {
      return lookupClassPort->cref();
    }
    return std::nullopt;
  }

  void setLookupClassPort(const cfg::AclLookupClassPort& lookupClassPort) {
    set<switch_state_tags::lookupClassPort>(lookupClassPort);
  }

  std::optional<uint8_t> getTrafficClass() const {
    if (auto trafficClass = cref<switch_state_tags::trafficClass>()) {
      return trafficClass->cref();
    }
    return std::nullopt;
  }

  void setTrafficClass(uint8_t trafficClass) {
    set<switch_state_tags::trafficClass>(trafficClass);
  }

  std::optional<int64_t> getNextHopGroupId() const {
    if (auto id = cref<switch_state_tags::nextHopGroupId>()) {
      return id->cref();
    }
    return std::nullopt;
  }

  void setNextHopGroupId(int64_t id) {
    set<switch_state_tags::nextHopGroupId>(id);
  }

  std::optional<cfg::AclLookupClass> getLookupClassL2() const {
    if (auto lookupClassL2 = cref<switch_state_tags::lookupClassL2>()) {
      return lookupClassL2->cref();
    }
    return std::nullopt;
  }
  void setLookupClassL2(const cfg::AclLookupClass& lookupClassL2) {
    set<switch_state_tags::lookupClassL2>(lookupClassL2);
  }

  std::optional<cfg::AclLookupClass> getLookupClassNeighbor() const {
    if (auto lookupClassNeighbor =
            cref<switch_state_tags::lookupClassNeighbor>()) {
      return lookupClassNeighbor->cref();
    }
    return std::nullopt;
  }
  void setLookupClassNeighbor(const cfg::AclLookupClass& lookupClassNeighbor) {
    set<switch_state_tags::lookupClassNeighbor>(lookupClassNeighbor);
  }

  void setUdfGroups(const UdfGroups& udfGroups) {
    set<switch_state_tags::udfGroups>(udfGroups);
  }

  std::optional<UdfGroups> getUdfGroups() const {
    if (auto udf = cref<switch_state_tags::udfGroups>()) {
      return udf->toThrift();
    }
    return std::nullopt;
  }

  std::optional<uint8_t> getRoceOpcode() const {
    if (auto opcode = cref<switch_state_tags::roceOpcode>()) {
      return opcode->cref();
    }
    return std::nullopt;
  }

  void setRoceOpcode(const uint8_t opcode) {
    set<switch_state_tags::roceOpcode>(opcode);
  }

  std::optional<RoceBytes> getRoceBytes() const {
    if (auto roceBytes = cref<switch_state_tags::roceBytes>()) {
      return roceBytes->toThrift();
    }
    return std::nullopt;
  }

  void setRoceBytes(RoceBytes roceBytes) {
    set<switch_state_tags::roceBytes>(roceBytes);
  }

  std::optional<RoceMask> getRoceMask() const {
    if (auto roceMask = cref<switch_state_tags::roceMask>()) {
      return roceMask->toThrift();
    }
    return std::nullopt;
  }

  void setRoceMask(RoceMask roceMask) {
    set<switch_state_tags::roceMask>(roceMask);
  }

  std::optional<UdfTable> getUdfTable() const {
    if (auto udfData = cref<switch_state_tags::udfTable>()) {
      return udfData->toThrift();
    }
    return std::nullopt;
  }

  void setUdfTable(const UdfTable& aclUdfEntryList) {
    set<switch_state_tags::udfTable>(aclUdfEntryList);
  }

  std::optional<cfg::AclLookupClass> getLookupClassRoute() const {
    if (auto lookupClassRoute = cref<switch_state_tags::lookupClassRoute>()) {
      return lookupClassRoute->cref();
    }
    return std::nullopt;
  }
  void setLookupClassRoute(const cfg::AclLookupClass& lookupClassRoute) {
    set<switch_state_tags::lookupClassRoute>(lookupClassRoute);
  }

  std::optional<cfg::PacketLookupResultType> getPacketLookupResult() const {
    if (auto packetLookupResult =
            cref<switch_state_tags::packetLookupResult>()) {
      return packetLookupResult->cref();
    }
    return std::nullopt;
  }

  void setPacketLookupResult(
      const cfg::PacketLookupResultType packetLookupResult) {
    set<switch_state_tags::packetLookupResult>(packetLookupResult);
  }

  std::optional<uint32_t> getVlanID() const {
    if (auto vlanID = cref<switch_state_tags::vlanID>()) {
      return vlanID->cref();
    }
    return std::nullopt;
  }

  void setVlanID(uint32_t vlanID) {
    set<switch_state_tags::vlanID>(vlanID);
  }

  static std::shared_ptr<AclEntry> fromFollyDynamic(
      const folly::dynamic& /*dyn*/) {
    // for PrioAclMap
    XLOG(FATAL) << "folly dynamic method not supported for acl entry";
    return nullptr;
  }

  bool hasMatcher() const {
    // at least one qualifier must be specified
    return getSrcIp().first || getDstIp().first || getProto() ||
        getTcpFlagsBitMap() || getSrcPort() || getDstPort() || getIpFrag() ||
        getIcmpType() || getDscp() || getIpType() || getTtl() || getDstMac() ||
        getL4SrcPort() || getL4DstPort() || getL4DstPortRange() ||
        getLookupClassL2() || getLookupClassNeighbor() ||
        getLookupClassRoute() || getPacketLookupResult() || getEtherType() ||
        getVlanID() || getUdfGroups() || getRoceOpcode() || getRoceBytes() ||
        getRoceMask() || getUdfTable() || getTrafficClass() ||
        getNextHopGroupId() || getDstIpV6Word3() || getDstIpV6Word2() ||
        getLookupClassPort() || getMplsLabel0Ttl();
  }

  std::set<cfg::AclTableQualifier> getRequiredAclTableQualifiers() const;

  AclEntry* modify(
      std::shared_ptr<SwitchState>* state,
      const HwSwitchMatcher& matcher);

 private:
  // IPv6 word qualifiers are 32-bit values, but thrift stores them as signed
  // i64 because it has no unsigned 32-bit integer type.
  static constexpr int64_t kMaxIpV6Word = 0xFFFFFFFF;

  static uint32_t validateIpV6Word(int64_t word) {
    if (word < 0 || word > kMaxIpV6Word) {
      throw FbossError("IPv6 ACL word must be a 32-bit value");
    }
    return static_cast<uint32_t>(word);
  }

  template <typename Tag>
  std::optional<uint32_t> getIpV6Word() const {
    if (auto word = cref<Tag>()) {
      return validateIpV6Word(word->cref());
    }
    return std::nullopt;
  }

  template <typename Tag>
  void setIpV6Word(int64_t word) {
    set<Tag>(static_cast<int64_t>(validateIpV6Word(word)));
  }

  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

/*
 * True when the two entries differ only in their counter action. An entry
 * whose remaining action fields are all unset is treated the same as one with
 * no action at all, so attaching a counter to a previously action-less entry
 * also qualifies. Returns false when the counters are identical -- "nothing
 * changed" is not "only the counter changed".
 */
bool onlyCounterChanged(
    const std::shared_ptr<AclEntry>& oldAclEntry,
    const std::shared_ptr<AclEntry>& newAclEntry);

} // namespace facebook::fboss
