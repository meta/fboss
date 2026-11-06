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

#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include <folly/IPAddress.h>
#include <optional>

namespace facebook::fboss {

enum class AclType {
  UDF_ACK,
  UDF_NAK,
  UDF_ACK_WITH_NAK,
  UDF_WR_IMM_ZERO,
  FLOWLET,
  FLOWLET_WITH_UDF_ACK,
  FLOWLET_WITH_UDF_NAK,
  UDF_FLOWLET,
  UDF_FLOWLET_WITH_UDF_ACK,
  UDF_FLOWLET_WITH_UDF_NAK,
  ECMP_HASH_CANCEL,
  ROCE_SPRAY_MISS,
};

class AgentArsBase : public AgentHwTest {
 public:
  void SetUp() override;
  void TearDown() override;
  cfg::SwitchConfig initialConfig(const AgentEnsemble& ensemble) const override;
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return kMaxEcmpWidthForTest;
  }
  std::string getAclName(AclType aclType) const;
  std::string getCounterName(AclType aclType) const;
  void setup(int ecmpWidth = 1);
  void addSamplingConfig(cfg::SwitchConfig& config);
  void addAclTableConfig(
      cfg::SwitchConfig& config,
      std::vector<std::string>& udfGroups) const;
  void resolveMirror(const std::string& mirrorName, uint8_t dstPort);
  void generateApplyConfig(AclType aclType);
  void flowletSwitchingAclHitHelper(AclType aclTypePre, AclType aclTypePost);
  void verifyUdfAddDelete(AclType aclTypePre, AclType aclTypePost);

  // pumpRoCETraffic holds the L4 source port fixed and only varies the RoCE
  // destination queue pair, so every packet shares a 5-tuple and the ECMP hash
  // sends them all to one member. Callers that need traffic to actually spread
  // pass srcIp and vary it per flow.
  size_t sendRoceTraffic(
      const PortID& frontPanelEgrPort,
      int roceOpcode = utility::kUdfRoceOpcodeAck,
      const std::optional<std::vector<uint8_t>>& nxtHdr =
          std::optional<std::vector<uint8_t>>(),
      int packetCount = 1,
      int destPort = utility::kUdfL4DstPort,
      uint8_t reserved = utility::kRoceReserved,
      const std::optional<folly::IPAddressV6>& srcIp = std::nullopt);
  auto verifyAclType(bool bumpOnHit, AclType aclType);
  void verifyAcl(AclType aclType);
  std::vector<cfg::AclUdfEntry> addUdfTable(
      const std::vector<std::string>& udfGroups,
      const std::vector<std::vector<int8_t>>& roceBytes,
      const std::vector<std::vector<int8_t>>& roceMask) const;
  void addRoceAcl(
      cfg::SwitchConfig* config,
      const std::string& aclName,
      const std::string& counterName,
      bool isSai,
      const std::optional<std::string>& udfGroups,
      const std::optional<int>& roceOpcode,
      const std::optional<int>& roceBytes,
      const std::optional<int>& roceMask,
      const std::optional<std::vector<cfg::AclUdfEntry>>& udfTable,
      bool addMirror = false) const;
  std::vector<std::string> getUdfGroupsForAcl(AclType aclType) const;
  void addAclAndStat(
      cfg::SwitchConfig* config,
      AclType aclType,
      bool isSai,
      bool addMirror = false) const;
  RoutePrefixV6 getMirrorDestRoutePrefix(const folly::IPAddress dip) const;
  virtual void generatePrefixes();
  virtual std::vector<PortID> getTestPorts() const;
  void setupEcmpGroups(int numEcmp);
  cfg::SwitchingMode getFwdSwitchingMode(const RoutePrefixV6& prefix) const;
  void verifyFwdSwitchingMode(
      const RoutePrefixV6& prefix,
      cfg::SwitchingMode switchingMode) const;
  uint32_t getMaxArsGroups() const;
  bool isChenab(const AgentEnsemble& ensemble) const;
  bool isTH3(const AgentEnsemble& ensemble) const;

 protected:
  // Port flowlet config helpers - consolidated from AgentArsFlowletTest
  cfg::PortFlowletConfig getPortFlowletConfig(
      int scalingFactor,
      int loadWeight,
      int queueWeight) const;

  void updatePortFlowletConfigs(
      cfg::SwitchConfig& cfg,
      int scalingFactor,
      int loadWeight,
      int queueWeight) const;

  void updatePortFlowletConfigName(cfg::SwitchConfig& cfg) const;

  void updateFlowletConfigs(
      cfg::SwitchConfig& cfg,
      const cfg::SwitchingMode switchingMode,
      int flowletTableSize,
      int scalingFactor,
      int loadWeight,
      int queueWeight,
      const std::optional<cfg::SwitchingMode> backupSwitchingMode =
          std::nullopt) const;

  // Verification helpers - consolidated from AgentArsFlowletTest
  bool verifyPortFlowletConfig(
      const folly::CIDRNetwork& ip,
      cfg::PortFlowletConfig& portFlowletConfig,
      const PortID& port);

  bool verifyEcmpForFlowletSwitching(
      const folly::CIDRNetwork& ip,
      const cfg::FlowletSwitchingConfig& flowletCfg,
      bool flowletEnable,
      const PortID& port);

  bool verifyEcmpForNonFlowlet(
      const folly::CIDRNetwork& ip,
      const cfg::FlowletSwitchingConfig& flowletCfg,
      bool expectFlowsetFree,
      const PortID& port);

  cfg::AclActionType aclActionType_{cfg::AclActionType::PERMIT};
  static inline constexpr auto kOutQueue = 6;
  static inline constexpr auto kDscp = 30;
  static inline constexpr auto kSflowMirrorName = "sflow_mirror";
  static inline constexpr auto kAclMirror = "acl_mirror";
  static inline constexpr auto sflowDestinationVIP = "2001::101";
  static inline constexpr auto aclDestinationVIP = "2002::101";
  // Match the addresses pumpRoCETraffic itself defaults to for IPv6.
  static inline constexpr auto kRoceSrcIp = "1001::1";
  static inline constexpr auto kRoceDstIp = "2001::1";
  static inline constexpr auto kFrontPanelPortForTest = 8;
  std::unique_ptr<utility::EcmpSetupTargetedPorts6> helper_;
  std::vector<boost::container::flat_set<PortDescriptor>> nhopSets;
  std::vector<RoutePrefixV6> prefixes;

 private:
  // Max ECMP group width generatePrefixes() builds, and the per-switch
  // interface-port count this test needs to form them.
  static constexpr size_t kMaxEcmpWidthForTest = 12;
};

} // namespace facebook::fboss
