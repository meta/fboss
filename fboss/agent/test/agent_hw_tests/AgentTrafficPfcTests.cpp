// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <folly/MapUtil.h>

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/PfcTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <fmt/ranges.h>

DEFINE_bool(
    skip_stop_pfc_test_traffic,
    false,
    "Skip stopping traffic after traffic test!");
DEFINE_int32(
    num_packets_to_trigger_pfc,
    0,
    "Overrides the number of packets to send");

namespace {

using facebook::fboss::utility::PfcBufferParams;

static constexpr auto kGlobalIngressEgressBufferPoolSize{
    5064760}; // Keep a high pool size for DNX
// P200's egress create also sets a 20MB reserve. 50MB holds that reserve.
static constexpr uint64_t kP200EgressBufferPoolBytes = 50 * 1024 * 1024;

bool isP200Asic() {
  auto agentConfig = facebook::fboss::AgentConfig::fromFile(FLAGS_config);
  return facebook::fboss::checkSameAndGetAsicType(*agentConfig->thrift.sw()) ==
      facebook::fboss::cfg::AsicType::ASIC_TYPE_P200;
}
static constexpr auto kLosslessTrafficClass{2};
static constexpr auto kLosslessPriority{2};
static const std::vector<int> kLosslessPgIds{2, 3};
static const std::vector<int> kLossyPgIds{0};

// Non-identity (non-1:1) PFC mapping under test: priority-tagged traffic is
// classified by its 802.1p PCP to a traffic class, which maps to a lossless
// priority group; the PFC priority maps to that priority group and queue.
static constexpr auto kNonIdentityPcp{0};
static constexpr auto kNonIdentityTrafficClass{2};
static constexpr auto kNonIdentityPgId{2};
static constexpr auto kNonIdentityPfcPriority{0};
static const auto kTxForVlanForChenab = facebook::fboss::VlanID(4094);

// On DNX, PFC deadlock cannot be triggered by our test, instead we have to
// force constant PFC generation by setting the N-th bit of FRC_NIF_ETH_PFC,
// where N = (port_first_phy - core_first_phy)*8 + priority
//
// port_first_phy is from "port management dump full port=<>"
// core_first_phy is from "dnx data dump nif.phys.nof_phys_per_core"
//
// This hardcoded map needs to be updated when a different port is chosen.
// The map stores a string because the register won't fit in any integer type.
// See CS00012321021 for details.
// TODO (maxgg): Use HwLogicalPortId insetad of PortIds, as that is being used
// for computing register's value.
static const std::map<std::tuple<int, int>, std::string>
    kRegValToForcePfcTxForPriorityOnPortDnx = {
        // Single-stage: portID=8, port_first_phy=0, core_first_phy=0
        {std::make_tuple(8, 2), "0x4"},
        // Dual-stage: portID=1, port_first_phy=8, core_first_phy=0
        {std::make_tuple(1, 2), "0x40000000000000000"},
        // Janga: portID=3, port_first_phy=8, core_first_phy=0 P1842843423
        {std::make_tuple(3, 2), "0x40000000000000000"},
        // Janga ASIC 1: portID=32779 (eth1/63/1), port_first_phy=8,
        // core_first_phy=0
        {std::make_tuple(32779, 2), "0x40000000000000000"},
};

struct TrafficTestParams {
  PfcBufferParams buffer = PfcBufferParams{};
  bool expectDrop = false;
  bool scale = false;
};

std::tuple<int, int, int> getPfcTxRxXonHwPortStats(
    facebook::fboss::AgentEnsemble* ensemble,
    const facebook::fboss::HwPortStats& portStats,
    const int pfcPriority) {
  return {
      folly::get_default(portStats.outPfc_().value(), pfcPriority, 0),
      folly::get_default(portStats.inPfc_().value(), pfcPriority, 0),
      ensemble->getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
          facebook::fboss::HwAsic::Feature::PFC_XON_TO_XOFF_COUNTER)
          ? folly::get_default(portStats.inPfcXon_().value(), pfcPriority, 0)
          : 0};
}

void waitPfcCounterIncrease(
    facebook::fboss::AgentEnsemble* ensemble,
    const facebook::fboss::PortID& portId,
    const int pfcPriority) {
  int txPfcCtr = 0, rxPfcCtr = 0, rxPfcXonCtr = 0;

  WITH_RETRIES({
    auto portStats = ensemble->getLatestPortStats(portId);
    std::tie(txPfcCtr, rxPfcCtr, rxPfcXonCtr) =
        getPfcTxRxXonHwPortStats(ensemble, portStats, pfcPriority);
    XLOG(DBG0) << " Port: " << portId << " PFC TX/RX PFC/RX_PFC_XON "
               << txPfcCtr << "/" << rxPfcCtr << "/" << rxPfcXonCtr
               << ", priority: " << pfcPriority;
    // Also log in/out packet/byte counts to make sure packets are flowing.
    XLOG(DBG0) << facebook::fboss::utility::pfcStatsString(portStats);

    EXPECT_EVENTUALLY_GT(txPfcCtr, 0);

    // inDiscards is tracked in software, so inDiscards + sum(inPfc) isn't
    // necessarily equal to inDiscardsRaw after warmboot. Just check for <=.
    EXPECT_EVENTUALLY_LE(*portStats.inDiscards_(), *portStats.inDiscardsRaw_());

    // TODO(maxgg): CS00012381334 - Rx counters not incrementing on TH5/TH6
    // However we know PFC is working as long as TX PFC is being generated, so
    // skip validating RX PFC counters on TH5 for now.
    auto asicType =
        checkSameAndGetAsicForTesting(ensemble->getL3Asics())->getAsicType();
    if (asicType != facebook::fboss::cfg::AsicType::ASIC_TYPE_TOMAHAWK5 &&
        asicType != facebook::fboss::cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
      EXPECT_EVENTUALLY_GT(rxPfcCtr, 0);
      if (ensemble->getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
              facebook::fboss::HwAsic::Feature::PFC_XON_TO_XOFF_COUNTER)) {
        EXPECT_EVENTUALLY_GT(rxPfcXonCtr, 0);
      }
    }
  });
}

void validatePfcCountersIncreased(
    facebook::fboss::AgentEnsemble* ensemble,
    const int pri,
    const std::vector<facebook::fboss::PortID>& portIds) {
  for (const auto& portId : portIds) {
    waitPfcCounterIncrease(ensemble, portId, pri);
  }
}

void validateBufferPoolWatermarkCounters(
    facebook::fboss::AgentEnsemble* ensemble,
    const int /* pri */,
    const std::vector<facebook::fboss::PortID>& portIds) {
  WITH_RETRIES({
    uint64_t globalHeadroomWatermark{};
    uint64_t globalSharedWatermark{};
    ensemble->getSw()->updateStats();
    // Watermarks may be cleared on read by the stats thread, so read the
    // cached p100 value from fb303 instead.
    for (const auto& asic : ensemble->getL3Asics()) {
      facebook::fboss::SwitchID switchId(*asic->getSwitchId());
      auto sharedCounters = ensemble->getFb303RegexCounters(
          "buffer_watermark_global_shared(.itm.*)?.p100.60", switchId);
      for (const auto& [_, val] : sharedCounters) {
        globalSharedWatermark += val;
      }
      auto headroomCounters = ensemble->getFb303RegexCounters(
          "buffer_watermark_global_headroom(.itm.*)?.p100.60", switchId);
      for (const auto& [_, val] : headroomCounters) {
        globalHeadroomWatermark += val;
      }
    }

    for (const auto& portId : portIds) {
      XLOG(INFO) << "validateBufferPoolWatermarkCounters: Port " << portId
                 << ": "
                 << facebook::fboss::utility::pfcStatsString(
                        ensemble->getLatestPortStats(portId));
    }
    XLOG(DBG0) << "Global headroom watermark: " << globalHeadroomWatermark
               << ", Global shared watermark: " << globalSharedWatermark;
    if (ensemble->getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
            facebook::fboss::HwAsic::Feature::BUFFER_POOL_HEADROOM_WATERMARK)) {
      EXPECT_EVENTUALLY_GT(globalHeadroomWatermark, 0);
    }
    EXPECT_EVENTUALLY_GT(globalSharedWatermark, 0);
  });
}

void validateIngressPriorityGroupWatermarkCounters(
    facebook::fboss::AgentEnsemble* ensemble,
    const int pri,
    const std::vector<facebook::fboss::PortID>& portIds) {
  std::string watermarkKeys = "shared";
  int numKeys = 1;
  if (ensemble->getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
          facebook::fboss::HwAsic::Feature::
              INGRESS_PRIORITY_GROUP_HEADROOM_WATERMARK)) {
    watermarkKeys.append("|headroom");
    numKeys++;
  }
  WITH_RETRIES({
    for (const auto& portId : portIds) {
      XLOG(INFO) << "validateIngressPriorityGroupWatermarkCounters: Port "
                 << portId << ": "
                 << facebook::fboss::utility::pfcStatsString(
                        ensemble->getLatestPortStats(portId));
      const auto& portName = ensemble->getProgrammedState()
                                 ->getPorts()
                                 ->getNodeIf(portId)
                                 ->getName();
      std::string pg = ensemble->isSai() ? fmt::format(".pg{}", pri) : "";
      auto regex = fmt::format(
          "buffer_watermark_pg_({}).{}{}.p100.60", watermarkKeys, portName, pg);
      auto counters = ensemble->getFb303CountersByRegex(portId, regex);
      EXPECT_EVENTUALLY_EQ(counters.size(), numKeys);
      uint64_t totalWatermark = 0;
      for (const auto& ctr : counters) {
        XLOG(DBG0) << ctr.first << " : " << ctr.second;
        totalWatermark += ctr.second;
      }
      EXPECT_EVENTUALLY_GT(totalWatermark, 0);
    }
  });
}

void validatePfcDurationCounters(
    facebook::fboss::AgentEnsemble* ensemble,
    const int pfcPriority,
    const std::vector<facebook::fboss::PortID>& portIds) {
  facebook::fboss::cfg::SwitchConfig config = ensemble->getCurrentConfig();
  WITH_RETRIES({
    for (const auto& portId : portIds) {
      auto portCfg = facebook::fboss::utility::findCfgPort(config, portId);
      CHECK(portCfg->pfc().has_value());
      bool rxDurationEnabled =
          portCfg->pfc()->rxPfcDurationEnable().value_or(false);
      bool txDurationEnabled =
          portCfg->pfc()->txPfcDurationEnable().value_or(false);
      auto portStats = ensemble->getLatestPortStats(portId);
      auto commonLog =
          "validatePfcDurationCounters: Port: " + std::to_string(portId);
      if (rxDurationEnabled) {
        auto rxPfcDurationUsec = folly::get_default(
            portStats.rxPfcDurationUsec_().value(), pfcPriority, 0);
        XLOG(DBG0) << commonLog << ", RX PFC duration: " << rxPfcDurationUsec
                   << " usec";
        EXPECT_EVENTUALLY_GT(rxPfcDurationUsec, 0);
      }
      if (txDurationEnabled) {
        auto txPfcDurationUsec = folly::get_default(
            portStats.txPfcDurationUsec_().value(), pfcPriority, 0);
        XLOG(DBG0) << commonLog << ", TX PFC duration: " << txPfcDurationUsec
                   << " usec";
        EXPECT_EVENTUALLY_GT(txPfcDurationUsec, 0);
      }
    }
  });
}

std::optional<std::string> extractPortIdsFromYaml(const std::string& yaml) {
  using namespace std::literals;
  size_t start = yaml.find("PORT_ID: [[");
  if (start == std::string::npos) {
    return std::nullopt;
  }
  start = yaml.find("[[", start); // skip to the [[
  size_t end = yaml.find("]]", start);
  if (end == std::string::npos) {
    return std::nullopt;
  }
  return yaml.substr(start, end - start + 2); // +2 to include the ]]
}

void addLosslessYamlConfig(std::string& yamlCfg) {
  auto portIds = extractPortIdsFromYaml(yamlCfg);
  if (portIds.has_value()) {
    yamlCfg += fmt::format(
        R"(
---
device:
  0:
    TM_ING_PORT_PRI_GRP:
      ?
        PORT_ID: {}
        TM_PRI_GRP_ID: [{}]
      :
        LOSSLESS: 1
...
)",
        *portIds,
        fmt::join(kLosslessPgIds, ","));
  }
}

} // namespace

namespace facebook::fboss {

class AgentTrafficPfcTest : public AgentHwTest {
 public:
  void TearDown() override {
    // If SetUp() threw before the ensemble was created (e.g. the switch
    // never came up), getAgentEnsemble() is null. gtest still invokes
    // TearDown(), so guard the ensemble-dependent teardown to avoid a null
    // deref in getL3Asics(); AgentHwTest::TearDown() is already null-safe.
    if (!FLAGS_list_production_feature && getAgentEnsemble() != nullptr) {
      auto asic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
        auto ports = masterLogicalInterfacePortIds();
        getAgentEnsemble()->bringDownPorts(ports);
      }
    }
    AgentHwTest::TearDown();
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    /*
     * Makes this flag available so that it can be used in early
     * stages of init to setup common buffer pool for specific
     * asics like Jericho2.
     */
    FLAGS_ingress_egress_buffer_pool_size = kGlobalIngressEgressBufferPoolSize;
    // TH4 and TH5 need the egress pool to match the 1.5MB ingress pool.
    // P200 uses 50MB so the 20MB reserve fits in the same create.
    if (isP200Asic()) {
      FLAGS_egress_buffer_pool_size = kP200EgressBufferPoolBytes;
    } else {
      FLAGS_egress_buffer_pool_size = PfcBufferParams::kGlobalSharedBytes +
          PfcBufferParams::kGlobalHeadroomBytes;
    }
    FLAGS_skip_buffer_reservation = true;
    FLAGS_qgroup_guarantee_enable = true;
  }

  void applyPlatformConfigOverrides(
      const cfg::SwitchConfig& sw,
      cfg::PlatformConfig& config) const override {
    if (checkSameAndGetAsicType(sw) == cfg::AsicType::ASIC_TYPE_CHENAB ||
        checkSameAndGetAsicType(sw) == cfg::AsicType::ASIC_TYPE_CHENAB2) {
      return;
    }

    // Apply common backend ASIC configuration
    utility::applyBackendAsicConfig(sw, config);

    // Workaround for CS00012395772 before 13.3
    utility::modifyPlatformConfig(
        config,
        [](std::string& yamlCfg) { addLosslessYamlConfig(yamlCfg); },
        [](std::map<std::string, std::string>&) {});
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), config);

    if (checkSameAndGetAsicType(config) == cfg::AsicType::ASIC_TYPE_CHENAB ||
        checkSameAndGetAsicType(config) == cfg::AsicType::ASIC_TYPE_CHENAB2) {
      FLAGS_num_packets_to_trigger_pfc = 2000;
      FLAGS_setup_for_warmboot = false;
    }
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PFC};
  }

  std::string portDesc(const PortID& portId) {
    const auto& cfg = getAgentEnsemble()->getCurrentConfig();
    for (const auto& port : *cfg.ports()) {
      if (PortID(*port.logicalID()) == portId) {
        return fmt::format(
            "portId={} name={}", *port.logicalID(), *port.name());
      }
    }
    return "";
  }

  std::vector<PortID> portIdsForTest(bool scaleTest = false) {
    auto allPorts = FLAGS_hyper_port ? masterLogicalHyperPortIds()
                                     : masterLogicalInterfacePortIds();
    int numPorts = scaleTest ? allPorts.size() : 2;
    return std::vector<PortID>(allPorts.begin(), allPorts.begin() + numPorts);
  }

  std::vector<folly::IPAddressV6> getDestinationIps(int count = 2) const {
    std::vector<folly::IPAddressV6> ips;
    for (int i = 4; i < count + 4; i++) {
      ips.emplace_back(fmt::format("2620:0:1cfe:face:b00c::{}", i));
    }
    return ips;
  }

  folly::MacAddress getIntfMac() const {
    return getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  }

 protected:
  PfcBufferParams defaultPfcBufferParams() const {
    return PfcBufferParams::getPfcBufferParams(
        checkSameAndGetAsicType(getAgentEnsemble()->getCurrentConfig()));
  }

  void setupEcmpTraffic(const PortID& portId, const folly::IPAddressV6& ip) {
    utility::EcmpSetupTargetedPorts6 ecmpHelper{
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), getIntfMac()};

    const PortDescriptor port(portId);
    RoutePrefixV6 route{ip, 128};

    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, {port});
    });

    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &routeUpdater, {port}, {route}, {}, std::nullopt, true);
    utility::disablePortTTLDecrementIfSupported(
        getAgentEnsemble(), ecmpHelper.getRouterId(), ecmpHelper.nhop(port));
  }

  void setupEcmpTraffic(const std::vector<PortID>& portIds) {
    auto ips = getDestinationIps(portIds.size());
    CHECK_EQ(portIds.size(), ips.size());
    for (int i = 0; i < portIds.size(); ++i) {
      setupEcmpTraffic(portIds[i], ips[i]);
    }
  }

  void setupPfcDurationCounters(
      cfg::SwitchConfig& cfg,
      const std::vector<PortID>& portIds,
      bool rxPfcDurationEnabled,
      bool txPfcDurationEnabled) {
    auto getPfcEnabledPortCfg = [&](const PortID& portId) {
      auto portCfg = std::find_if(
          cfg.ports()->begin(), cfg.ports()->end(), [&portId](auto& port) {
            return PortID(*port.logicalID()) == portId;
          });
      CHECK(portCfg->pfc().has_value())
          << "PFC not configured for port ID " << portId;
      return portCfg;
    };
    for (const PortID& portId : portIds) {
      auto portCfg = getPfcEnabledPortCfg(portId);
      if (rxPfcDurationEnabled) {
        portCfg->pfc()->rxPfcDurationEnable() = true;
        XLOG(DBG0) << "Enabled PFC RX duration counter for port ID " << portId;
      }
      if (txPfcDurationEnabled) {
        portCfg->pfc()->txPfcDurationEnable() = true;
        XLOG(DBG0) << "Enabled PFC TX duration counter for port ID " << portId;
      }
    }
  }

  void validateInitPfcCounters(
      const std::vector<PortID>& portIds,
      const int pfcPriority) {
    int txPfcCtr = 0, rxPfcCtr = 0, rxPfcXonCtr = 0;
    // no need to retry if looking for baseline counter
    for (const auto& portId : portIds) {
      auto portStats = getLatestPortStats(portId);
      XLOG(INFO) << "validateInitPfcCounters: Port " << portId << ": "
                 << facebook::fboss::utility::pfcStatsString(portStats);
      auto ingressDropRaw = *portStats.inDiscardsRaw_();
      EXPECT_EQ(ingressDropRaw, 0);
      std::tie(txPfcCtr, rxPfcCtr, rxPfcXonCtr) =
          getPfcTxRxXonHwPortStats(getAgentEnsemble(), portStats, pfcPriority);
      EXPECT_EQ(txPfcCtr, 0);
      EXPECT_EQ(rxPfcCtr, 0);
      EXPECT_EQ(rxPfcXonCtr, 0);
    }
  }

  void validateIngressDropCounters(const std::vector<PortID>& portIds) {
    WITH_RETRIES({
      for (const auto& portId : portIds) {
        auto portStats = getLatestPortStats(portId);
        auto ingressDropRaw = *portStats.inDiscardsRaw_();
        uint64_t ingressCongestionDiscards = 0;
        std::string ingressCongestionDiscardLog{};
        // In congestion discard stats is always supported on native. On SAI
        // it requires either the SAI_PORT_IN_CONGESTION_DISCARDS or
        // INGRESS_PRIORITY_GROUP_DROPPED_PACKETS feature.
        bool isIngressCongestionDiscardsSupported =
            isSupportedOnAllAsics(
                HwAsic::Feature::INGRESS_PRIORITY_GROUP_DROPPED_PACKETS) ||
            isSupportedOnAllAsics(
                HwAsic::Feature::SAI_PORT_IN_CONGESTION_DISCARDS) ||
            !getAgentEnsemble()->isSai();
        if (isIngressCongestionDiscardsSupported) {
          ingressCongestionDiscards = *portStats.inCongestionDiscards_();
          ingressCongestionDiscardLog = " IngressCongestionDiscards: " +
              std::to_string(ingressCongestionDiscards);
        }
        XLOG(DBG0) << " validateIngressDropCounters: Port: " << portId
                   << " IngressDropRaw: " << ingressDropRaw
                   << ingressCongestionDiscardLog;
        EXPECT_EVENTUALLY_GT(ingressDropRaw, 0);
        if (isIngressCongestionDiscardsSupported) {
          EXPECT_EVENTUALLY_GT(ingressCongestionDiscards, 0);

          // In packet counters not supported in EDB loopback on TH5 and TH6.
          auto asicType =
              checkSameAndGetAsicType(getAgentEnsemble()->getCurrentConfig());
          if (asicType != facebook::fboss::cfg::AsicType::ASIC_TYPE_TOMAHAWK5 &&
              asicType != facebook::fboss::cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
            // Ingress congestion discards should be less than
            // the total packets received on this port.
            uint64_t inPackets = *portStats.inUnicastPkts_() +
                *portStats.inMulticastPkts_() + *portStats.inBroadcastPkts_();
            EXPECT_EVENTUALLY_LT(ingressCongestionDiscards, inPackets);
          }
        }
      }
      // Only validate VSQ drops on the ASIC under test. In multi-switch
      // mode, traffic flows through one ASIC only, so the other ASIC
      // will have 0 drops.
      auto testSwitchId = scopeResolver().scope(portIds[0]).switchId();
      auto asics = getAsics();
      auto asicIt = asics.find(testSwitchId);
      if (asicIt != asics.end() &&
          asicIt->second->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
        // Jericho3 has additional VSQ drops counters which accounts for
        // ingress buffer drops.
        getSw()->updateStats();
        fb303::ThreadCachedServiceData::get()->publishStats();
        auto switchIndex =
            getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(
                testSwitchId);
        auto vsqResourcesExhautionDrops =
            *getSw()
                 ->getHwSwitchStatsExpensive()[switchIndex]
                 .fb303GlobalStats()
                 ->vsq_resource_exhaustion_drops();
        XLOG(DBG0)
            << " validateIngressDropCounters: vsqResourceExhaustionDrops: "
            << vsqResourcesExhautionDrops;
        EXPECT_EVENTUALLY_GT(vsqResourcesExhautionDrops, 0);
      }
    });
  }

  void pumpTraffic(
      const int priority,
      const std::optional<VlanID>& vlan,
      const std::optional<uint8_t> queue,
      const std::vector<PortID>& portIds,
      const std::vector<folly::IPAddressV6>& ips) {
    auto intfMac = getIntfMac();
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    // pri = 7 => dscp 56
    int dscp = priority * 8;
    int numPacketsPerFlow =
        getAgentEnsemble()->getMinPktsForLineRate(portIdsForTest()[0]);
    if (FLAGS_num_packets_to_trigger_pfc > 0) {
      numPacketsPerFlow = FLAGS_num_packets_to_trigger_pfc;
    }

    XLOG(INFO) << "Sending " << numPacketsPerFlow << " packets per flow";
    // Some asics (e.g. Yuba) won't let traffic build up evenly if all packets
    // were sent to one port before another. It's better to alternate the ports.
    for (int i = 0; i < numPacketsPerFlow; i++) {
      XLOG_EVERY_N(INFO, 100) << "Sending packet " << i;
      for (int j = 0; j < ips.size(); j++) {
        auto txPacket = utility::makeUDPTxPacket(
            getSw(),
            vlan,
            srcMac,
            intfMac,
            folly::IPAddressV6(kTestSrcIpV6),
            ips[j],
            kTestSrcPort,
            kTestDstPort,
            dscp << 2, // dscp is last 6 bits in TC
            255,
            std::vector<uint8_t>(2000, 0xff));

        // TODO(maxgg): Investigate TH3/4 WithScaleCfg failure when queue is
        // set to losslessPriority (2).

        auto asic =
            checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
        if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB &&
            vlan.has_value()) {
          // If we want to use a provided vlan ID, we need to send packets out
          // switched, so that they egress out of the correct traffic class.
          getAgentEnsemble()->sendPacketAsync(
              std::move(txPacket), std::nullopt, queue);
        } else {
          getAgentEnsemble()->sendPacketAsync(
              std::move(txPacket), PortDescriptor(portIds[j]), queue);
        }
      }
    }
  }

  // Find the strict-priority queue ID with the highest priority from
  // the config. Injecting on such a queue avoids low-priority queue
  // tail drops so to unblock lossless TC from crossing the PFC XOFF
  // threshold.
  std::optional<uint8_t> getInjectionQueueId() {
    auto cfg = getAgentEnsemble()->getCurrentConfig();
    std::optional<uint8_t> queueId = std::nullopt;
    for (const auto& [name, queues] : *cfg.portQueueConfigs()) {
      for (const auto& queueCfg : queues) {
        if (*queueCfg.scheduling() == cfg::QueueScheduling::STRICT_PRIORITY &&
            (!queueId || *queueCfg.id() > *queueId)) {
          queueId = static_cast<uint8_t>(*queueCfg.id());
        }
      }
    }
    return queueId;
  }

  void pumpTraffic(const int priority, bool scaleTest) {
    std::vector<PortID> portIds = portIdsForTest(scaleTest);
    auto queue = getInjectionQueueId();
    pumpTraffic(
        priority,
        std::nullopt /* vlanId */,
        queue,
        portIds,
        getDestinationIps(static_cast<int>(portIds.size())));
  }

  void stopTraffic(const std::vector<PortID>& portIds) {
    // Toggle the link to break looping traffic
    for (const auto& portId : portIds) {
      bringDownPort(portId);
      bringUpPort(portId);
    }
  }

  cfg::SwitchConfig getPfcTestConfig(
      const int pfcPriority,
      const std::map<int, int>& tcToPgOverride,
      const TrafficTestParams& testParams) {
    // Setup PFC
    auto cfg = getAgentEnsemble()->getCurrentConfig();
    // Apply PFC config to all ports of interest
    auto lossyPgIds = kLossyPgIds;
    if (FLAGS_allow_zero_headroom_for_lossless_pg) {
      // If the flag is set, we already have lossless PGs being created
      // with headroom as 0 and there is no way to differentiate lossy
      // and lossless PGs now that headroom is set to zero for lossless.
      // So, avoid creating lossy PGs as this will result in PFC being
      // enabled for 3 priorities, which is not supported for TAJO.
      lossyPgIds.clear();
    }
    std::vector<PortID> portIds = portIdsForTest(testParams.scale);
    utility::setupPfcBuffers(
        getAgentEnsemble(),
        cfg,
        portIds,
        kLosslessPgIds,
        lossyPgIds,
        testParams.buffer,
        utility::PfcQosMapParams{.tcToPg = tcToPgOverride});
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    if (isSupportedOnAllAsics(HwAsic::Feature::MULTIPLE_EGRESS_BUFFER_POOL)) {
      utility::setupMultipleEgressPoolAndQueueConfigs(
          cfg, kLosslessPgIds, asic->getMMUSizeBytes());
    }
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_YUBA ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_G202X) {
      // For YUBA, lossless queues needs to be configured with static
      // queue limit equal to the MMU size to ensure its lossless.
      for (auto& queueConfigs : *cfg.portQueueConfigs()) {
        for (auto& queueCfg : queueConfigs.second) {
          if (std::find(
                  kLosslessPgIds.begin(),
                  kLosslessPgIds.end(),
                  *queueCfg.id()) != kLosslessPgIds.end()) {
            // Given the 1:1 mapping for queueID to PG ID,
            // this is a lossless queue.
            queueCfg.sharedBytes() = asic->getMMUSizeBytes();
          }
        }
      }
    }
    return cfg;
  }

  void runPfcTestWithCfg(
      const cfg::SwitchConfig& cfg,
      const int trafficClass,
      const int pfcPriority,
      const TrafficTestParams& testParams,
      const std::function<void(
          AgentEnsemble* ensemble,
          const int pri,
          const std::vector<PortID>& portIdsToValidate)>& validateCounterFn =
          validatePfcCountersIncreased) {
    std::vector<PortID> portIds = portIdsForTest(testParams.scale);
    auto setup = [&]() {
      applyNewConfig(cfg);
      setupEcmpTraffic(portIds);
      // ensure counter is 0 before we start traffic
      validateInitPfcCounters(portIds, pfcPriority);
    };

    auto verifyCommon = [&](bool postWb) {
      pumpTraffic(trafficClass, testParams.scale);
      // Sleep for a bit before validation, so that the test will fail if
      // traffic doesn't actually build up, instead of passing by luck.
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep)
      sleep(2);
      // check counters are as expected
      validateCounterFn(getAgentEnsemble(), pfcPriority, portIds);
      if (testParams.expectDrop) {
        validateIngressDropCounters(portIds);
      }
      if (!FLAGS_skip_stop_pfc_test_traffic && postWb) {
        // stop traffic so that unconfiguration can happen without issues
        stopTraffic(portIds);
      }
    };
    auto verify = [&]() { verifyCommon(false /* postWb */); };
    auto verifyPostWb = [&]() { verifyCommon(true /* postWb */); };
    verifyAcrossWarmBoots(setup, verify, []() {}, verifyPostWb);
  }

  void runTestWithCfg(
      const int trafficClass,
      const int pfcPriority,
      const std::map<int, int>& tcToPgOverride,
      const TrafficTestParams& testParams,
      const std::function<void(
          AgentEnsemble* ensemble,
          const int pri,
          const std::vector<PortID>& portIdsToValidate)>& validateCounterFn =
          validatePfcCountersIncreased) {
    std::vector<PortID> portIds = portIdsForTest(testParams.scale);
    for (const auto& portId : portIds) {
      XLOG(INFO) << "Testing port: " << portDesc(portId);
    }
    auto cfg = getPfcTestConfig(pfcPriority, tcToPgOverride, testParams);
    runPfcTestWithCfg(
        cfg,
        trafficClass,
        pfcPriority,
        testParams,
        std::move(validateCounterFn));
  }
};

class AgentTrafficPfcGenTest : public AgentTrafficPfcTest {
 protected:
  // Set up ECMP routing to direct traffic to the specified port without
  // creating a MAC loopback. We use a MAC address different from the
  // interface MAC so the resolved neighbor has a distinct destination,
  // preventing the switch from looping the packet back to the CPU.
  void setupEcmpTrafficNoLoop(
      const PortID& portId,
      const folly::IPAddressV6& ip) {
    auto noLoopMac = folly::MacAddress::fromHBO(
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState())
            .u64HBO() +
        1);
    utility::EcmpSetupTargetedPorts6 ecmpHelper{
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), noLoopMac};

    const PortDescriptor port(portId);
    RoutePrefixV6 route{ip, 128};

    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, {port});
    });

    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&routeUpdater, {port}, {route});
  }

  void setupTxVlanForChenab(
      cfg::SwitchConfig& cfg,
      const PortID& portId,
      const VlanID& vlanId) {
    if (checkSameAndGetAsicType(cfg) != cfg::AsicType::ASIC_TYPE_CHENAB &&
        checkSameAndGetAsicType(cfg) != cfg::AsicType::ASIC_TYPE_CHENAB2) {
      return;
    }
    // For Chenab, we need to create a VLAN so that packets that are switched
    // hit the VLAN before hitting the route, egressing out of the first port.

    cfg::Vlan vlan;
    vlan.id() = vlanId;
    vlan.name() = fmt::format("vlan{}", static_cast<int>(vlanId));
    vlan.routable() = false; // No routing needed, just isolation
    // Set the interface ID for the VLAN
    vlan.intfID() = kTxForVlanForChenab;
    cfg.vlans()->push_back(vlan);

    // Max: we need to add an interface, otherwise warmboot will fail.
    cfg::Interface interface;
    interface.name() = folly::to<std::string>(vlanId);
    interface.type() = cfg::InterfaceType::VLAN;
    interface.scope() = cfg::Scope::LOCAL;
    interface.intfID() = kTxForVlanForChenab;
    interface.vlanID() = vlanId;
    interface.routerID() = 0;
    // Deliberately choose MAC that doesn't match local CPU MAC used for other
    // Router Interfaces (utility::kLocalCpuMac().toString()) This ensures
    // switched packet from CPU gets forwarded by VLAN out of front panel port
    // and not to this RIF.
    interface.mac() = "00:11:22:33:44:55";
    interface.mtu() = 9000;
    interface.ipAddresses().ensure().emplace_back("200.0.0.1/24");
    interface.ipAddresses().ensure().emplace_back("200::1/64");
    cfg.interfaces()->push_back(interface);

    // Change vlan port mapping so the portId is an untagged member
    // of the test VLAN.
    auto vlanPort = std::find_if(
        cfg.vlanPorts()->begin(),
        cfg.vlanPorts()->end(),
        [portId](auto vlanPort) {
          return vlanPort.logicalPort() == static_cast<int>(portId);
        });

    if (vlanPort != cfg.vlanPorts()->end()) {
      XLOG(INFO) << "Found existing VLAN port entry for portId " << portId
                 << ", changing from VLAN " << *vlanPort->vlanID()
                 << " to VLAN " << vlanId;
      vlanPort->vlanID() = vlanId;
      vlanPort->spanningTreeState() = cfg::SpanningTreeState::FORWARDING;
      vlanPort->emitTags() = false; // Untagged port
    } else {
      XLOG(INFO) << "No existing VLAN port entry found for portId " << portId
                 << ", creating new entry for VLAN " << vlanId;
      cfg::VlanPort testVlanPort;
      testVlanPort.vlanID() = vlanId;
      testVlanPort.logicalPort() = static_cast<int>(portId);
      testVlanPort.spanningTreeState() = cfg::SpanningTreeState::FORWARDING;
      testVlanPort.emitTags() = false; // Untagged port
      cfg.vlanPorts()->push_back(testVlanPort);
    }
  }

  void setupConfigAndEcmpTraffic(
      const PortID& portId,
      const PortID& txOffPortId,
      const folly::IPAddressV6& ip) {
    cfg::SwitchConfig cfg = getAgentEnsemble()->getCurrentConfig();
    PfcBufferParams buffer = defaultPfcBufferParams();
    buffer.scalingFactor = cfg::MMUScalingFactor::ONE_128TH;
    utility::setupPfcBuffers(
        getAgentEnsemble(), cfg, {portId}, kLosslessPgIds, kLossyPgIds, buffer);

    setupTxVlanForChenab(cfg, portId, kTxForVlanForChenab);

    applyNewConfig(cfg);
    setupEcmpTrafficNoLoop(txOffPortId, ip);
  }
};

TEST_F(AgentTrafficPfcTest, verifyPfcWithDefaultCfg) {
  TrafficTestParams param{
      .buffer = defaultPfcBufferParams(),
  };
  runTestWithCfg(kLosslessTrafficClass, kLosslessPriority, {}, param);
}

TEST_F(AgentTrafficPfcTest, verifyPfcWithScaleCfg) {
  TrafficTestParams param{
      .buffer = defaultPfcBufferParams(),
      .scale = true,
  };
  runTestWithCfg(kLosslessTrafficClass, kLosslessPriority, {}, param);
}

// intent of this test is to send traffic so that it maps to
// tc 2, now map tc 2 to PG 3. Mapping from PG to pfc priority
// is 1:1, which means PG 3 is mapped to pfc priority 3.
// Generate traffic to fire off PFC with smaller shared buffer
TEST_F(AgentTrafficPfcTest, verifyPfcWithMapChanges_0) {
  const int trafficClass = kLosslessTrafficClass;
  const int pfcPriority = 3;
  runTestWithCfg(
      trafficClass,
      pfcPriority,
      {{trafficClass, pfcPriority}},
      TrafficTestParams{.buffer = defaultPfcBufferParams()});
}

// intent of this test is to send traffic so that it maps to
// tc 7. Now we map tc 7 -> PG 2. Mapping from PG to pfc
// priority is 1:1, which means PG 2 is mapped to pfc priority 2.
// Generate traffic to fire off PFC with smaller shared buffer
TEST_F(AgentTrafficPfcTest, verifyPfcWithMapChanges_1) {
  const int trafficClass = 7;
  const int pfcPriority = kLosslessPriority;
  runTestWithCfg(
      trafficClass,
      pfcPriority,
      {{trafficClass, pfcPriority}},
      TrafficTestParams{.buffer = defaultPfcBufferParams()});
}

TEST_F(AgentTrafficPfcGenTest, verifyBufferPoolWatermarks) {
  std::vector<PortID> portIds = portIdsForTest();
  PortID portId = portIds[0];
  PortID txOffPortId = portIds[1];
  auto ip = getDestinationIps()[0];

  auto setup = [&]() {
    auto cfg = getPfcTestConfig(
        kLosslessPriority,
        {},
        TrafficTestParams{.buffer = defaultPfcBufferParams()});
    // Disable PFC RX on the injection port for VOQ platforms
    // (e.g. Jericho3) where queues fill up and cause test failures.
    // Only PFC TX is needed; RX can interfere with headroom watermark
    // behavior. Skip for Chenab as it does not support independent
    // PFC RX disable.
    auto asicType = checkSameAndGetAsicType(cfg);
    if (asicType != cfg::AsicType::ASIC_TYPE_CHENAB &&
        asicType != cfg::AsicType::ASIC_TYPE_CHENAB2) {
      for (auto& port : *cfg.ports()) {
        if (PortID(*port.logicalID()) == portId && port.pfc().has_value()) {
          port.pfc()->rx() = false;
        }
      }
    }
    setupTxVlanForChenab(cfg, portId, kTxForVlanForChenab);
    applyNewConfig(cfg);
    setupEcmpTrafficNoLoop(txOffPortId, ip);
  };

  auto verify = [&]() {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    std::optional<VlanID> vlanId;
    if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
      vlanId = kTxForVlanForChenab;
    }
    utility::triggerPfcGeneration(
        getAgentEnsemble(),
        portId,
        txOffPortId,
        ip,
        kLosslessTrafficClass,
        kLosslessPriority,
        vlanId);

    validateBufferPoolWatermarkCounters(
        getAgentEnsemble(), kLosslessPriority, {portId});
    utility::cleanupPfcGeneration(getAgentEnsemble(), txOffPortId);

    // Force a HW stats read so that watermark registers (clear-on-read) are
    // drained before warm boot, guaranteeing the post-warm-boot iteration
    // observes zeroed counters.
    getAgentEnsemble()->getSw()->updateStats();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTrafficPfcGenTest, verifyIngressPriorityGroupWatermarks) {
  std::vector<PortID> portIds = portIdsForTest();
  PortID portId = portIds[0];
  PortID txOffPortId = portIds[1];
  auto ip = getDestinationIps()[0];

  auto setup = [&]() {
    auto cfg = getPfcTestConfig(
        kLosslessPriority,
        {},
        TrafficTestParams{.buffer = defaultPfcBufferParams()});
    setupTxVlanForChenab(cfg, portId, kTxForVlanForChenab);
    applyNewConfig(cfg);
    setupEcmpTrafficNoLoop(txOffPortId, ip);
  };

  auto verify = [&]() {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    std::optional<VlanID> vlanId;
    if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
      vlanId = kTxForVlanForChenab;
    }
    utility::triggerPfcGeneration(
        getAgentEnsemble(),
        portId,
        txOffPortId,
        ip,
        kLosslessTrafficClass,
        kLosslessPriority,
        vlanId);

    validateIngressPriorityGroupWatermarkCounters(
        getAgentEnsemble(), kLosslessPriority, {portId});
    utility::cleanupPfcGeneration(getAgentEnsemble(), txOffPortId);

    // Force a HW stats read so that PG watermark registers (clear-on-read)
    // are drained before warm boot, guaranteeing the post-warm-boot iteration
    // observes zeroed counters.
    getAgentEnsemble()->getSw()->updateStats();
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentTrafficPfcZeroPgHeadroomTest : public AgentTrafficPfcTest {
  void setCmdLineFlagOverrides() const override {
    AgentTrafficPfcTest::setCmdLineFlagOverrides();
    FLAGS_allow_zero_headroom_for_lossless_pg = true;
  }
};

TEST_F(AgentTrafficPfcZeroPgHeadroomTest, verifyPfcWithZeroPgHeadRoomCfg) {
  TrafficTestParams param{
      .buffer = defaultPfcBufferParams(),
      .expectDrop = true,
  };
  param.buffer.pgHeadroom = 0;
  runTestWithCfg(kLosslessTrafficClass, kLosslessPriority, {}, param);
}

TEST_F(AgentTrafficPfcZeroPgHeadroomTest, verifyWithScaleCfgInCongestionDrops) {
  TrafficTestParams param{
      .buffer = defaultPfcBufferParams(),
      .expectDrop = true,
      .scale = true,
  };
  param.buffer.pgHeadroom = 0;
  runTestWithCfg(kLosslessTrafficClass, kLosslessPriority, {}, param);
}

class AgentTrafficPfcZeroGlobalHeadroomTest : public AgentTrafficPfcTest {
  void setCmdLineFlagOverrides() const override {
    AgentTrafficPfcTest::setCmdLineFlagOverrides();
    // Zero global headroom still uses the small egress pool on TH4 and TH5.
    // P200 keeps the 50MB pool from the base override.
    if (!isP200Asic()) {
      FLAGS_egress_buffer_pool_size = PfcBufferParams::kGlobalSharedBytes;
    }
  }
};

TEST_F(AgentTrafficPfcTest, verifyPfcWithZeroGlobalHeadRoomCfg) {
  auto asicType =
      checkSameAndGetAsicType(getAgentEnsemble()->getCurrentConfig());
  TrafficTestParams param{
      .buffer = PfcBufferParams::getPfcBufferParams(
          asicType, PfcBufferParams::kGlobalSharedBytes, 0 /*globalHeadroom*/),
      .expectDrop = true,
  };
  runTestWithCfg(kLosslessTrafficClass, kLosslessPriority, {}, param);
}

class AgentTrafficPfcTxDurationTest : public AgentTrafficPfcTest {
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PFC_TX_DURATION};
  }
};

TEST_F(AgentTrafficPfcTxDurationTest, verifyPfcTxDuration) {
  TrafficTestParams param{
      .buffer = defaultPfcBufferParams(),
  };
  auto cfg = getPfcTestConfig(kLosslessPriority, {}, param);
  std::vector<PortID> portIds = portIdsForTest(false /*scale*/);
  setupPfcDurationCounters(
      cfg,
      portIds,
      false /*rxPfcDurationEnabled*/,
      true /*txPfcDurationEnabled*/);
  runPfcTestWithCfg(
      cfg,
      kLosslessTrafficClass,
      kLosslessPriority,
      param,
      validatePfcDurationCounters);
}

class AgentTrafficPfcRxTxDurationTest : public AgentTrafficPfcTest {
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PFC_RX_TX_DURATION};
  }
};

TEST_F(AgentTrafficPfcRxTxDurationTest, verifyPfcRxTxDuration) {
  TrafficTestParams param{
      .buffer = defaultPfcBufferParams(),
  };
  auto cfg = getPfcTestConfig(kLosslessPriority, {}, param);
  std::vector<PortID> portIds = portIdsForTest(false /*scale*/);
  setupPfcDurationCounters(
      cfg,
      portIds,
      true /*rxPfcDurationEnabled*/,
      true /*txPfcDurationEnabled*/);
  runPfcTestWithCfg(
      cfg,
      kLosslessTrafficClass,
      kLosslessPriority,
      param,
      validatePfcDurationCounters);
}

class AgentTrafficPfcWatchdogTest : public AgentTrafficPfcGenTest {
 protected:
  void setupWatchdog(const std::vector<PortID>& portIds, bool enable) {
    cfg::PfcWatchdog pfcWatchdog;
    if (enable) {
      pfcWatchdog.recoveryAction() = cfg::PfcWatchdogRecoveryAction::NO_DROP;
      // Configure values that works for specific HW, include ASIC type checks
      // if needed, given some HW has limitations on the specific values that
      // can be programmed and we need to ensure that the configured value here
      // is in sync with what is in SAI/SDK to avoid a reprogramming attempt
      // during warmboot.
      auto asic =
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
      switch (asic->getAsicType()) {
        case cfg::AsicType::ASIC_TYPE_JERICHO2:
        case cfg::AsicType::ASIC_TYPE_JERICHO3:
          pfcWatchdog.recoveryTimeMsecs() = 1000;
          pfcWatchdog.detectionTimeMsecs() = 198;
          break;
        case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
        case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
        case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
        case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
        case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1:
          pfcWatchdog.recoveryTimeMsecs() = 100;
          pfcWatchdog.detectionTimeMsecs() = 10;
          break;
        case cfg::AsicType::ASIC_TYPE_CHENAB:
        case cfg::AsicType::ASIC_TYPE_CHENAB2:
          // The Chenab ASIC requires recovery time>=200 and detection
          // time>=200.
          pfcWatchdog.recoveryTimeMsecs() = 1000;
          pfcWatchdog.detectionTimeMsecs() = 200;
          break;
        case cfg::AsicType::ASIC_TYPE_YUBA:
        case cfg::AsicType::ASIC_TYPE_G202X:
          pfcWatchdog.recoveryTimeMsecs() = 100;
          pfcWatchdog.detectionTimeMsecs() = 25;
          break;
        default:
          pfcWatchdog.recoveryTimeMsecs() = 10;
          pfcWatchdog.detectionTimeMsecs() = 1;
          break;
      }
    }

    auto cfg = getAgentEnsemble()->getCurrentConfig();
    for (const auto& portID : portIds) {
      auto portCfg = utility::findCfgPort(cfg, portID);
      if (portCfg->pfc().has_value()) {
        if (enable) {
          portCfg->pfc()->watchdog() = pfcWatchdog;
        } else {
          portCfg->pfc()->watchdog().reset();
        }
      }
    }
    applyNewConfig(cfg);
  }

  void triggerPfcDeadlockDetection(
      const PortID& port,
      const PortID& txOffPortId,
      const folly::IPAddressV6& ip) {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
      // As traffic cannot trigger deadlock for DNX, force back
      // to back PFC frame generation which causes a deadlock!
      XLOG(DBG0) << "Triggering PFC deadlock detection on port ID : "
                 << static_cast<int>(port);
      auto iter = kRegValToForcePfcTxForPriorityOnPortDnx.find(
          std::make_tuple(static_cast<int>(port), kLosslessPriority));
      ASSERT_FALSE(iter == kRegValToForcePfcTxForPriorityOnPortDnx.end());
      std::string out;
      auto switchID = scopeResolver().scope(port).switchId();
      // Start with a blank command first and then force the PFC frame
      // generation. This is because, sometimes first diag command doesn't work
      // in some SDK versions and in FBOSS OSS
      getAgentEnsemble()->runDiagCommand("\n", out, switchID);
      getAgentEnsemble()->runDiagCommand(
          fmt::format(
              "modreg CFC_FRC_NIF_ETH_PFC FRC_NIF_ETH_PFC={}\n", iter->second),
          out,
          switchID);
    } else {
      // Disable Tx on the outbound port so that queues will build up.
      utility::setCreditWatchdogAndPortTx(
          getAgentEnsemble(), txOffPortId, false);

      // CS00012381334 - MAC loopback doesn't work on TH5 and Rx PFC counters
      // doesn't increase. Tx counters should work for all platforms.
      auto txPfcCtrOld = folly::get_default(
          *getLatestPortStats(port).outPfc_(), kLosslessPriority, 0);
      // pass vlan id for chenab to trigger deadlock detection
      auto vlanId =
          asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB
          ? std::make_optional<VlanID>(kTxForVlanForChenab)
          : getVlanIDForTx();
      pumpTraffic(
          kLosslessTrafficClass, vlanId, kLosslessPriority, {port}, {ip});
      WITH_RETRIES_N_TIMED(5, std::chrono::milliseconds(1000), {
        auto txPfcCtrNew = folly::get_default(
            *getLatestPortStats(port).outPfc_(), kLosslessPriority, 0);
        EXPECT_EVENTUALLY_GT(txPfcCtrNew, txPfcCtrOld);
      });
    }
  }

  std::tuple<int, int> getPfcDeadlockCounters(const PortID& portId) {
#ifdef IS_OSS
    auto portStats = getLatestPortStats(portId);
    auto pfcDeadlockDetection = 0;
    auto pfcDeadlockRecovery = 0;
    if (portStats.pfcDeadlockDetection_().has_value()) {
      pfcDeadlockDetection = *portStats.pfcDeadlockDetection_();
    }
    if (portStats.pfcDeadlockRecovery_().has_value()) {
      pfcDeadlockRecovery = *portStats.pfcDeadlockRecovery_();
    }

    XLOG(INFO) << "Port " << portId
               << " pfcDeadlockDetection: " << pfcDeadlockDetection
               << " pfcDeadlockRecovery: " << pfcDeadlockRecovery;

    return {pfcDeadlockDetection, pfcDeadlockRecovery};
#else
    const auto& portName = getAgentEnsemble()
                               ->getProgrammedState()
                               ->getPorts()
                               ->getNodeIf(portId)
                               ->getName();
    return {
        getAgentEnsemble()
            ->getFb303CounterIfExists(
                portId, portName + ".pfc_deadlock_detection.sum")
            .value_or(0),
        getAgentEnsemble()
            ->getFb303CounterIfExists(
                portId, portName + ".pfc_deadlock_recovery.sum")
            .value_or(0),
    };
#endif
  }

  std::tuple<int, int> getSwitchPfcDeadlockCounters() {
    auto detectionCtrBase = getAgentEnsemble()->isSai()
        ? "pfc_deadlock_detection_count.sum"
        : "pfc_deadlock_detection.sum";
    auto recoveryCtrBase = getAgentEnsemble()->isSai()
        ? "pfc_deadlock_recovery_count.sum"
        : "pfc_deadlock_recovery.sum";
    // Only query the ASIC under test. PFC deadlock is only triggered
    // on this ASIC, so other ASICs always have 0 counters.
    auto testSwitchId = SwitchID(FLAGS_switch_id_for_testing);
    // In multi-switch mode, hw_agent registers counters with a
    // "switch.<switchIndex>." prefix. Prepend it when querying.
    std::string prefix;
    if (getAsics().size() > 1) {
      auto switchIndex =
          getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(
              testSwitchId);
      prefix = folly::to<std::string>("switch.", switchIndex, ".");
    }
    int deadlockCtr = getAgentEnsemble()->getFb303Counter(
        prefix + detectionCtrBase, testSwitchId);
    int recoveryCtr = getAgentEnsemble()->getFb303Counter(
        prefix + recoveryCtrBase, testSwitchId);
    return {deadlockCtr, recoveryCtr};
  }

  void validatePfcWatchdogCountersIncrement(
      const PortID& portId,
      const uint64_t& deadlockCtrBefore,
      const uint64_t& recoveryCtrBefore) {
    WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1000), {
      auto [deadlockCtr, recoveryCtr] = getPfcDeadlockCounters(portId);
      XLOG(DBG0) << "For port: " << portId << " deadlockCtr = " << deadlockCtr
                 << " recoveryCtr = " << recoveryCtr;
      EXPECT_EVENTUALLY_GT(deadlockCtr, deadlockCtrBefore);
      EXPECT_EVENTUALLY_GT(recoveryCtr, recoveryCtrBefore);
    });
  }

  void validateGlobalPfcWatchdogCountersIncrement(
      const uint64_t& globalDeadlockCtrBefore,
      const uint64_t& globalRecoveryCtrBefore) {
    WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1000), {
      auto [globalDeadlockCtr, globalRecoveryCtr] =
          getSwitchPfcDeadlockCounters();
      XLOG(DBG0) << "Global deadlockCtr = " << globalDeadlockCtr
                 << " recoveryCtr = " << globalRecoveryCtr;
      EXPECT_EVENTUALLY_GT(globalDeadlockCtr, globalDeadlockCtrBefore);
      EXPECT_EVENTUALLY_GT(globalRecoveryCtr, globalRecoveryCtrBefore);
    });
  }

  void validateNoPfcWatchdogCountersIncrement(
      const PortID& portId,
      const uint64_t& deadlockCtrBefore,
      const uint64_t& recoveryCtrBefore) {
    int noIncrementIterations = 0;
    WITH_RETRIES({
      auto [deadlockCtr, recoveryCtr] = getPfcDeadlockCounters(portId);
      XLOG(DBG0) << "For port: " << portId << " deadlockCtr = " << deadlockCtr
                 << " recoveryCtr = " << recoveryCtr;
      EXPECT_EQ(deadlockCtr, deadlockCtrBefore);
      EXPECT_EQ(recoveryCtr, recoveryCtrBefore);
      noIncrementIterations++;
      // No increment seen in 5 iterations
      EXPECT_EVENTUALLY_EQ(noIncrementIterations, 5);
    });
  }

  void waitForPfcDeadlocksToSettle(const PortID& portId) {
    int noIncrementIterations = 0;
    auto [deadlockCtrBefore, recoveryCtrBefore] =
        getPfcDeadlockCounters(portId);
    WITH_RETRIES({
      auto [deadlockCtr, recoveryCtr] = getPfcDeadlockCounters(portId);
      XLOG(DBG0) << "For port: " << portId << " deadlockCtr = " << deadlockCtr
                 << " recoveryCtr = " << recoveryCtr;
      if (deadlockCtr == deadlockCtrBefore &&
          recoveryCtr == recoveryCtrBefore) {
        noIncrementIterations++;
      } else {
        noIncrementIterations = 0;
        deadlockCtrBefore = deadlockCtr;
        recoveryCtrBefore = recoveryCtr;
      }
      // No counter increment for 5 consecutive iterations,
      // assume no more PFC deadlocks!
      EXPECT_EVENTUALLY_EQ(noIncrementIterations, 5);
    });
  }

  void cleanupPfcDeadlockDetectionTrigger(const PortID& portId) {
    // Enable credit WD and TX on port
    utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), portId, true);
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
      std::string out;
      auto switchID = scopeResolver().scope(portId).switchId();
      // TODO: When PFC WD is continuously being triggered with this register
      // config, getAttr for queue PFC WD enabled returns wrong value and is
      // tracked in CS00012388717. Until that is fixed, work around the issue.
      // For that, stop PFC WD being triggered continuously and wait to ensure
      // that the PFC DL generation settles.
      // Start with a blank command first and then stop the PFC frame
      // generation. This is because, sometimes first diag command doesn't work
      // in some SDK versions and in FBOSS OSS
      getAgentEnsemble()->runDiagCommand("\n", out, switchID);
      getAgentEnsemble()->runDiagCommand(
          "modreg CFC_FRC_NIF_ETH_PFC FRC_NIF_ETH_PFC=0\n", out, switchID);
    }
  }
};

// Intent of this test is to setup PFC watchdog, trigger PFC, and observe that
// watchdog counters increase as deadlock/recovery callbacks are called. To
// trigger PFC, we add a route to an IP on a port (txPortPortId), then set it to
// Tx disabled. Then we send packets to that IP on a different port (portId),
// which will eventually cause queues to build up and PFC to trigger.
TEST_F(AgentTrafficPfcWatchdogTest, PfcWatchdogDetection) {
  PortID portId = portIdsForTest()[0];
  PortID txOffPortId = portIdsForTest()[1];
  XLOG(DBG3) << "Injection port: " << portDesc(portId);
  XLOG(DBG3) << "Tx off port: " << portDesc(txOffPortId);
  auto ip = getDestinationIps()[0];

  auto setup = [&]() {
    setupConfigAndEcmpTraffic(portId, txOffPortId, ip);
    setupWatchdog({portId}, true /* enable watchdog */);
  };
  auto verify = [&]() {
    auto [deadlockCtrBefore, recoveryCtrBefore] =
        getPfcDeadlockCounters(portId);
    auto [globalDeadlockBefore, globalRecoveryBefore] =
        getSwitchPfcDeadlockCounters();
    triggerPfcDeadlockDetection(portId, txOffPortId, ip);
    validatePfcWatchdogCountersIncrement(
        portId, deadlockCtrBefore, recoveryCtrBefore);
    validateGlobalPfcWatchdogCountersIncrement(
        globalDeadlockBefore, globalRecoveryBefore);
    cleanupPfcDeadlockDetectionTrigger(txOffPortId);
    waitForPfcDeadlocksToSettle(portId);
  };
  verifyAcrossWarmBoots(setup, verify);
}

// intent of this test is to setup watchdog for PFC
// and remove it. Observe that counters should stop incrementing
// Clear them and check they stay the same
// Since the watchdog counters are sw based, upon warm boot
// we don't expect these counters to be incremented either
TEST_F(AgentTrafficPfcWatchdogTest, PfcWatchdogReset) {
  PortID portId = portIdsForTest()[0];
  PortID txOffPortId = portIdsForTest()[1];
  XLOG(DBG3) << "Injection port: " << portDesc(portId);
  XLOG(DBG3) << "Tx off port: " << portDesc(txOffPortId);
  auto ip = getDestinationIps()[0];

  int deadlockCtrBefore = 0;
  int recoveryCtrBefore = 0;

  auto setup = [&]() {
    setupConfigAndEcmpTraffic(portId, txOffPortId, ip);
    setupWatchdog({portId}, true /* enable watchdog */);
    std::tie(deadlockCtrBefore, recoveryCtrBefore) =
        getPfcDeadlockCounters(portId);
    triggerPfcDeadlockDetection(portId, txOffPortId, ip);
    // Lets wait for the watchdog counters to be populated
    validatePfcWatchdogCountersIncrement(
        portId, deadlockCtrBefore, recoveryCtrBefore);
    // Stop PFC trigger
    cleanupPfcDeadlockDetectionTrigger(txOffPortId);
    waitForPfcDeadlocksToSettle(portId);
    // Reset watchdog
    setupWatchdog({portId}, false /* disable */);
  };

  auto verify = [&]() {
    std::tie(deadlockCtrBefore, recoveryCtrBefore) =
        getPfcDeadlockCounters(portId);
    // Retrigger PFC WD detection/recovery
    triggerPfcDeadlockDetection(portId, txOffPortId, ip);
    validateNoPfcWatchdogCountersIncrement(
        portId, deadlockCtrBefore, recoveryCtrBefore);
    // Stop PFC trigger
    cleanupPfcDeadlockDetectionTrigger(txOffPortId);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Validates the non-1:1 PFC priority -> PG mapping using L2-switched, 802.1p
// priority (PCP) tagged traffic classified via a PCP -> traffic class map (no
// DSCP map). Follows the AgentTrafficPfcGenTest pattern: the two test ports
// share one VLAN -- an inject port and a forwarding port whose TX is disabled
// so its queue/PG builds up and PFC is generated on the inject port. The
// injected PCP is classified to a traffic class that maps to a lossless PG, and
// PFC is expected to fire on the PFC priority that maps to that PG.
//
// Coverage: validates PCP classification, the TC -> PG mapping, PFC generation
// on the mapped priority, and that traffic lands in the expected egress queue.
// The PFC priority -> queue map is not validated -- egress queue placement
// follows TC -> queue, so this test cannot isolate it.
//
// Gated on PFC_NON_IDENTITY_PRIORITY_MAP; the PFC priority -> PG QoS map is
// programmed only when the feature flag is enabled.
class AgentTrafficPfcNonIdentityMapTest : public AgentTrafficPfcGenTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::PFC,
        ProductionFeature::PFC_NON_IDENTITY_PRIORITY_MAP};
  }

  void setCmdLineFlagOverrides() const override {
    AgentTrafficPfcGenTest::setCmdLineFlagOverrides();
    // This test exercises only the feature-enabled path.
    FLAGS_enable_pfc_priority_to_pg_map = true;
  }

  // Put the two test ports in a single VLAN so traffic is L2-switched between
  // them (no L3 routing).
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    auto ports = ensemble.masterLogicalPortIds({cfg::PortType::INTERFACE_PORT});
    auto config = utility::oneL3IntfTwoPortConfig(
        ensemble.getSw()->getPlatformMapping(),
        asic,
        ports[0],
        ports[1],
        ensemble.getSw()->getPlatformSupportsAddRemovePort(),
        asic->desiredLoopbackModes(),
        ensemble.getSw()->getPlatformType());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), config);
    // Disable L2 learning so forwarding relies solely on the configured static
    // MAC entry. Mirrors AgentDot1qMappingTest.
    config.switchSettings()->l2LearningMode() = cfg::L2LearningMode::DISABLED;
    return config;
  }

 protected:
  // Destination MAC of the injected L2 frames; statically resolved to the
  // forwarding (TX-disabled) port so frames are switched to it.
  static folly::MacAddress kL2ForwardMac() {
    return folly::MacAddress("02:00:00:00:00:02");
  }

  cfg::SwitchConfig getNonIdentityPfcConfig(
      const PortID& injectPort,
      const PortID& txOffPort) {
    auto cfg = getAgentEnsemble()->getCurrentConfig();
    // Non-1:1 PFC priority mapping (a full permutation), used for both the
    // priority->PG and priority->queue maps. Only the configured lossless
    // priority maps to the lossless PG, so PFC is expected to fire on that
    // priority.
    const std::map<int, int> kPfcPriorityMap = {
        {0, 2}, {1, 6}, {2, 0}, {3, 3}, {4, 4}, {5, 5}, {6, 1}, {7, 7}};
    utility::setupPfcBuffers(
        getAgentEnsemble(),
        cfg,
        {injectPort, txOffPort},
        {kNonIdentityPgId} /* losslessPgIds */,
        kLossyPgIds,
        defaultPfcBufferParams(),
        utility::PfcQosMapParams{
            .tcToPg = {{kNonIdentityTrafficClass, kNonIdentityPgId}},
            .pfcPriToPg = kPfcPriorityMap,
            .pfcPriToQueue = kPfcPriorityMap,
            .classification = utility::PfcIngressClassification::Pcp,
            .pcpToTc = {{kNonIdentityPcp, kNonIdentityTrafficClass}}});
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    if (isSupportedOnAllAsics(HwAsic::Feature::MULTIPLE_EGRESS_BUFFER_POOL)) {
      utility::setupMultipleEgressPoolAndQueueConfigs(
          cfg, {kNonIdentityPgId}, asic->getMMUSizeBytes());
    }
    // Configure the ports for 802.1p priority tagging (VLAN id 0 carrying the
    // PCP, no regular VLAN tag) so the PCP is preserved and used for ingress
    // classification. Mirrors AgentDot1qMappingTest.
    for (auto& vlanPort : *cfg.vlanPorts()) {
      vlanPort.emitPriorityTags() = true;
      vlanPort.emitTags() = false;
    }
    // Statically resolve the destination MAC to the forwarding port so injected
    // frames are L2-switched to it.
    cfg::StaticMacEntry staticMac;
    staticMac.macAddress() = kL2ForwardMac().toString();
    CHECK(!cfg.vlanPorts()->empty());
    staticMac.vlanID() = cfg.vlanPorts()[0].vlanID().value();
    staticMac.egressLogicalPortID() = static_cast<int32_t>(txOffPort);
    cfg.staticMacAddrs() = {staticMac};
    return cfg;
  }

  // Validate that PFC was generated (TX) on the inject port for the given PFC
  // priority and that the given lossless priority group's shared buffer
  // watermark built up on the inject port. RX PFC is not validated: RX PFC
  // counters are known not to increment on TH5/TH6 (CS00012381334).
  void validateNonIdentityPfc(
      const PortID& injectPort,
      const int pfcPriority,
      const int pgId) {
    int txPfcCtr = 0, rxPfcCtr = 0, rxPfcXonCtr = 0;
    WITH_RETRIES({
      auto portStats = getLatestPortStats(injectPort);
      std::tie(txPfcCtr, rxPfcCtr, rxPfcXonCtr) =
          getPfcTxRxXonHwPortStats(getAgentEnsemble(), portStats, pfcPriority);
      XLOG(DBG0) << "validateNonIdentityPfc: Port " << injectPort
                 << " PFC TX/RX/RX_XON " << txPfcCtr << "/" << rxPfcCtr << "/"
                 << rxPfcXonCtr << ", priority " << pfcPriority;
      XLOG(DBG0) << facebook::fboss::utility::pfcStatsString(portStats);
      // PFC must be generated on the given priority.
      EXPECT_EVENTUALLY_GT(txPfcCtr, 0);
    });
    // Lossless traffic must build up the given priority group's shared buffer.
    validateIngressPriorityGroupWatermarkCounters(
        getAgentEnsemble(), pgId, {injectPort});
  }

  // Confirm traffic was forwarded through the mapped egress queue (per the
  // TC -> queue map) on the forwarding port by checking its per-queue out-byte
  // counter increased. Call after re-enabling TX so the queued traffic drains.
  void validateEgressQueueTraffic(const PortID& txOffPort, const int queueId) {
    WITH_RETRIES({
      auto portStats = getLatestPortStats(txOffPort);
      auto queueOutBytes =
          folly::get_default(*portStats.queueOutBytes_(), queueId, 0);
      XLOG(DBG0) << "validateEgressQueueTraffic: Port " << txOffPort
                 << " queue " << queueId << " out bytes " << queueOutBytes;
      EXPECT_EVENTUALLY_GT(queueOutBytes, 0);
    });
  }

  // Inject L2-switched, 802.1p priority (PCP) tagged Ethernet frames on
  // injectPort, destined to the forwarding port's MAC, so they are L2-switched
  // (no L3 routing) to the forwarding port and build up its queue / ingress PG.
  //
  // The frame uses a non-control ethertype so it is bridged rather than trapped
  // to the CPU (a control protocol like LLDP would be punted and never reach
  // the forwarding port). ETHERTYPE_IPV6 is chosen because L2 bridging keys
  // only on {VLAN, dst MAC} and never parses the L3 payload here (dst MAC is a
  // static L2 entry, not the router MAC), so the payload need not be a
  // well-formed IPv6 packet.
  void pumpL2PriorityTaggedTraffic(uint8_t pcp, const PortID& injectPort) {
    auto srcMac =
        utility::MacAddressGenerator().get(kL2ForwardMac().u64HBO() + 1);
    const auto numPackets =
        getAgentEnsemble()->getMinPktsForLineRate(injectPort);
    for (size_t i = 0; i < numPackets; i++) {
      auto txPacket = utility::makeEthTxPacket(
          getSw(),
          VlanID(0) /* priority tag, VLAN id 0 */,
          srcMac,
          kL2ForwardMac() /* dst MAC -> L2-switched to txOff port */,
          ETHERTYPE::ETHERTYPE_IPV6,
          std::vector<uint8_t>(2000, 0xff),
          pcp /* 802.1p priority */);
      getSw()->sendPacketOutOfPortAsync(std::move(txPacket), injectPort);
    }
  }
};

TEST_F(AgentTrafficPfcNonIdentityMapTest, verifyPfcWithNonIdentityMap) {
  auto ports = masterLogicalInterfacePortIds();
  const PortID& injectPort = ports[0];
  const PortID& txOffPort = ports[1];
  auto cfg = getNonIdentityPfcConfig(injectPort, txOffPort);
  auto setup = [&]() {
    applyNewConfig(cfg);
    // Ensure counters are 0 before we start traffic.
    validateInitPfcCounters({injectPort}, kNonIdentityPfcPriority);
  };
  auto verify = [&]() {
    // Disable TX on the forwarding port so its queue/PG builds up.
    utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), txOffPort, false);
    pumpL2PriorityTaggedTraffic(kNonIdentityPcp, injectPort);
    // PFC must be generated on the mapped priority (not the traffic class) and
    // the mapped lossless PG shared buffer must fill on the inject port.
    validateNonIdentityPfc(
        injectPort, kNonIdentityPfcPriority, kNonIdentityPgId);
    // Re-enable TX so the queued traffic drains, then confirm it egressed the
    // mapped queue. The egress queue equals the traffic class under the default
    // 1:1 TC -> queue map.
    utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), txOffPort, true);
    validateEgressQueueTraffic(txOffPort, kNonIdentityTrafficClass);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
