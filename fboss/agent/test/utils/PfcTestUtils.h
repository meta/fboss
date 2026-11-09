// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <memory>

#include <folly/IPAddressV6.h>

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/TestEnsembleIf.h"

namespace facebook::fboss::utility {

struct PfcBufferParams {
  static constexpr auto kSmallGlobalSharedBytes{20000};
  // TODO(maxgg): Change this back 85344 once CS00012382848 is fixed.
  static constexpr auto kGlobalSharedBytes{1500000};
  static constexpr auto kGlobalHeadroomBytes{5000}; // keep this small

  int globalShared{0};
  int globalHeadroom{0};
  int minLimit{0};
  int pgHeadroom{0};
  facebook::fboss::cfg::MMUScalingFactor scalingFactor;
  std::optional<int> resumeOffset;
  std::optional<int> resumeThreshold;
  std::optional<int> pgShared;

  static PfcBufferParams getPfcBufferParams(
      cfg::AsicType asicType,
      int globalShared = kGlobalSharedBytes,
      int globalHeadroom = kGlobalHeadroomBytes);
};

// Selects how ingress traffic is classified into traffic classes. Pcp
// classifies by 802.1p priority and, because the two are mutually exclusive at
// the QoS-map level, also suppresses the DSCP -> TC map (which would otherwise
// bind switch-wide on platforms with global QoS maps and take precedence).
enum class PfcIngressClassification {
  Dscp,
  Pcp,
};

// Overrides for the QoS maps programmed by the PFC setup helpers. Each map is
// merged on top of the corresponding default identity mapping; specify only the
// entries that differ.
struct PfcQosMapParams {
  // Traffic class -> priority group.
  std::map<int, int> tcToPg;
  // PFC priority -> priority group.
  std::map<int, int> pfcPriToPg;
  // PFC priority -> queue.
  std::map<int, int> pfcPriToQueue;
  // How ingress traffic is classified into traffic classes.
  PfcIngressClassification classification = PfcIngressClassification::Dscp;
  // PCP (802.1p) -> traffic class (only used when classification is Pcp).
  std::map<int, int> pcpToTc;
};

void setupPfcBuffers(
    const TestEnsembleIf* ensemble,
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    const std::vector<int>& losslessPgIds,
    const std::vector<int>& lossyPgIds,
    const PfcQosMapParams& qosMapParams = {});

void setupPfcBuffers(
    const TestEnsembleIf* ensemble,
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    const std::vector<int>& losslessPgIds,
    const std::vector<int>& lossyPgIds,
    PfcBufferParams buffer,
    const PfcQosMapParams& qosMapParams = {});

// Prod PG profile names. getRtswUplinkDownlinkPorts() classifies ports by
// searching these for "uplink" / "downlink", so the substrings are
// load-bearing.
inline constexpr auto kUplinkPgConfigName = "uplink_pg_config";
inline constexpr auto kDownlinkPgConfigName = "downlink_pg_config";

// Two-profile uplink/downlink PFC, as prod RTSW-family configs carry. Unlike
// setupPfcBuffers() this adds no QoS policy of its own and leaves
// dataPlaneTrafficPolicy alone, so it composes with an existing policy.
void setupUplinkDownlinkPfc(
    const TestEnsembleIf* ensemble,
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& uplinkPorts,
    const std::vector<PortID>& downlinkPorts,
    const std::vector<int>& losslessPgIds,
    const std::vector<int>& lossyPgIds,
    const PfcQosMapParams& qosMapParams = {});

void addPuntPfcPacketAcl(cfg::SwitchConfig& cfg, uint16_t queueId);

std::string pfcStatsString(const HwPortStats& stats);

std::unique_ptr<TxPacket> makePfcFramePacket(
    const AgentEnsemble& ensemble,
    uint8_t classVector);

// Trigger PFC generation by disabling TX on txDisablePort and sending traffic
// to cause queue buildup. This is a utility version of the logic in
// triggerPfcDeadlockDetection for use in watermark tests.
void triggerPfcGeneration(
    AgentEnsemble* ensemble,
    const PortID& port,
    const PortID& txDisablePort,
    const folly::IPAddressV6& destIp,
    int trafficClass,
    int pfcPriority,
    const std::optional<VlanID>& vlanId = std::nullopt);

// Cleanup PFC generation by re-enabling TX on the port
void cleanupPfcGeneration(AgentEnsemble* ensemble, const PortID& txDisablePort);

} // namespace facebook::fboss::utility
