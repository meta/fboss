// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/utils/PfcTestUtils.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <folly/MapUtil.h>
#include "folly/MacAddress.h"

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestEnsembleIf.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss::utility {

namespace {

struct PfcPriorityMaps {
  std::map<int16_t, int16_t> tcToPg;
  std::map<int16_t, int16_t> pfcPriToPg;
  std::map<int16_t, int16_t> pfcPriToQueue;
};

// Identity maps with any PfcQosMapParams overrides merged on top.
PfcPriorityMaps makePfcPriorityMaps(const PfcQosMapParams& params) {
  PfcPriorityMaps maps;
  for (auto i = 0; i <= cfg::switch_config_constants::PORT_PG_VALUE_MAX();
       i++) {
    maps.tcToPg.emplace(i, i);
    maps.pfcPriToPg.emplace(i, i);
    maps.pfcPriToQueue.emplace(i, i);
  }
  for (const auto& [tc, pg] : params.tcToPg) {
    maps.tcToPg[tc] = pg;
  }
  for (const auto& [pri, pg] : params.pfcPriToPg) {
    maps.pfcPriToPg[pri] = pg;
  }
  for (const auto& [pri, queue] : params.pfcPriToQueue) {
    maps.pfcPriToQueue[pri] = queue;
  }
  return maps;
}

void setupQosMapForPfc(
    cfg::QosMap& qosMap,
    bool isCpuQosMap,
    const PfcQosMapParams& qosMapParams = {}) {
  auto priorityMaps = makePfcPriorityMaps(qosMapParams);
  auto tc2PgId = std::move(priorityMaps.tcToPg);
  auto pfcPri2PgId = std::move(priorityMaps.pfcPriToPg);
  auto pfcPri2QueueId = std::move(priorityMaps.pfcPriToQueue);

  std::map<int16_t, int16_t> tc2QueueId;
  for (auto i = 0; i <= cfg::switch_config_constants::PORT_PG_VALUE_MAX();
       i++) {
    // Jericho3 cpu/recycle port only has 2 egress queues. Tomahawk has more
    // queues, but we stick to the lowest common denominator here.
    // See https://fburl.com/gdoc/nyyg1cve and https://fburl.com/code/mhdeuiky
    tc2QueueId.emplace(i, isCpuQosMap ? (i < 7 ? 0 : 1) : i);
  }

  // Build the DSCP -> TC map only when not classifying by PCP. When PCP
  // classification is used, a DSCP -> TC map must not be programmed: on
  // platforms with global QoS maps it binds switch-wide and its (identity) DSCP
  // classification takes precedence over the 802.1p priority, pulling traffic
  // into the wrong PG/queue.
  if (qosMapParams.classification == PfcIngressClassification::Dscp) {
    qosMap.dscpMaps()->resize(8);
    for (auto i = 0; i < 8; i++) {
      qosMap.dscpMaps()[i].internalTrafficClass() = i;
      for (auto j = 0; j < 8; j++) {
        qosMap.dscpMaps()[i].fromDscpToTrafficClass()->push_back(8 * i + j);
      }
    }
  }

  // Optionally classify ingress traffic by 802.1p priority (PCP). The PCP -> TC
  // map defaults to identity, with any override merged on top, and stays fully
  // populated.
  if (qosMapParams.classification == PfcIngressClassification::Pcp) {
    std::map<int16_t, int16_t> pcp2Tc;
    for (auto i = 0; i < 8; i++) {
      pcp2Tc.emplace(i, i);
    }
    for (const auto& [pcp, tc] : qosMapParams.pcpToTc) {
      pcp2Tc[pcp] = tc;
    }
    // PcpQosMap is keyed by traffic class, with the list of PCPs that map to
    // it.
    std::map<int16_t, std::vector<int16_t>> tc2Pcps;
    for (const auto& [pcp, tc] : pcp2Tc) {
      tc2Pcps[tc].push_back(pcp);
    }
    qosMap.pcpMaps() = std::vector<cfg::PcpQosMap>();
    for (const auto& [tc, pcps] : tc2Pcps) {
      cfg::PcpQosMap pcpMap;
      pcpMap.internalTrafficClass() = tc;
      for (auto pcp : pcps) {
        pcpMap.fromPcpToTrafficClass()->push_back(pcp);
      }
      pcpMap.fromTrafficClassToPcp() = pcps[0];
      qosMap.pcpMaps()->push_back(pcpMap);
    }
  }

  qosMap.trafficClassToPgId() = std::move(tc2PgId);
  qosMap.trafficClassToQueueId() = std::move(tc2QueueId);
  qosMap.pfcPriorityToPgId() = std::move(pfcPri2PgId);
  qosMap.pfcPriorityToQueueId() = std::move(pfcPri2QueueId);
}

void setupPfc(
    const TestEnsembleIf* ensemble,
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    const PfcQosMapParams& qosMapParams) {
  cfg::PortPfc pfc;
  pfc.tx() = true;
  pfc.rx() = true;
  pfc.portPgConfigName() = "foo";

  for (const auto& portID : ports) {
    auto portCfg = std::find_if(
        cfg.ports()->begin(), cfg.ports()->end(), [&portID](auto& port) {
          return PortID(*port.logicalID()) == portID;
        });
    portCfg->pfc() = pfc;
  }

  // setup qosPolicy
  auto setupQosPolicy = [&](bool isCpuQosMap, const std::string& name) {
    cfg::QosMap qosMap;
    setupQosMapForPfc(qosMap, isCpuQosMap, qosMapParams);
    auto qosPolicy = cfg::QosPolicy();
    *qosPolicy.name() = name;
    qosPolicy.qosMap() = std::move(qosMap);

    // add or replace existing policy (don't add the same policy twice!)
    bool found = false;
    for (auto& existing : *cfg.qosPolicies()) {
      if (existing.name() == name) {
        existing = qosPolicy;
        found = true;
        break;
      }
    }
    if (!found) {
      cfg.qosPolicies()->push_back(qosPolicy);
    }

    cfg::TrafficPolicyConfig trafficPolicy;
    trafficPolicy.defaultQosPolicy() = name;
    return trafficPolicy;
  };
  auto dataTrafficPolicy = setupQosPolicy(false /*isCpuQosMap*/, "qp");
  if (ensemble->getHwAsicTable()
          ->getHwAsics()
          .cbegin()
          ->second->getSwitchType() == cfg::SwitchType::VOQ) {
    // Start with the current CPU traffic policy, overwrite whats
    // needed here, leave the rest as is!
    cfg::CPUTrafficPolicyConfig cpuPolicy = cfg.cpuTrafficPolicy()
        ? *cfg.cpuTrafficPolicy()
        : cfg::CPUTrafficPolicyConfig();
    const std::string kCpuQueueingPolicy{"cpuQp"};
    cpuPolicy.trafficPolicy() =
        setupQosPolicy(true /*isCpuQosMap*/, kCpuQueueingPolicy);
    cfg.cpuTrafficPolicy() = std::move(cpuPolicy);
    std::map<int, std::string> portIdToQosPolicy{};
    // Iterate over all ports in config to find CPU/recycle ports across
    // all NPUs. masterLogicalPortIds() only returns ports for the NPU
    // under test (FLAGS_switch_id_for_testing), which misses recycle
    // ports on other NPUs in multi-switch mode.
    for (const auto& port : *cfg.ports()) {
      if (*port.portType() == cfg::PortType::CPU_PORT ||
          *port.portType() == cfg::PortType::RECYCLE_PORT) {
        portIdToQosPolicy[*port.logicalID()] = kCpuQueueingPolicy;
      }
    }
    if (portIdToQosPolicy.size()) {
      dataTrafficPolicy.portIdToQosPolicy() = std::move(portIdToQosPolicy);
    }
  }
  cfg.dataPlaneTrafficPolicy() = dataTrafficPolicy;
}

void setupBufferPoolConfig(
    const HwAsic* asic,
    std::map<std::string, cfg::BufferPoolConfig>& bufferPoolCfgMap,
    int globalSharedBytes,
    int globalHeadroomBytes) {
  cfg::BufferPoolConfig poolConfig;
  // provide small shared buffer size
  // idea is to hit the limit and trigger XOFF (PFC)
  if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    // Round up the configured buffer size to the nearest multiple of unit size
    auto unit = asic->getPacketBufferUnitSize();
    auto roundUp = [unit](int size) {
      return std::ceil(static_cast<double>(size) / unit) * unit;
    };
    poolConfig.sharedBytes() = roundUp(globalSharedBytes);
    poolConfig.headroomBytes() = roundUp(globalHeadroomBytes);
  } else if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
    // TH6 XGS SDK subtracts an internal reserved size (~5.99MB) from the pool
    // when computing the shared limit. Add reservedBytes to compensate, so the
    // HW shared limit equals our intended sharedBytes. Using a larger reserved
    // value to leave room for PG min limit allocation.
    poolConfig.sharedBytes() = globalSharedBytes;
    poolConfig.headroomBytes() = globalHeadroomBytes;
    poolConfig.reservedBytes() = 4600000;
  } else {
    poolConfig.sharedBytes() = globalSharedBytes;
    poolConfig.headroomBytes() = globalHeadroomBytes;
  }
  bufferPoolCfgMap.insert(std::make_pair("bufferNew", poolConfig));
}

void setupPortPgConfig(
    const TestEnsembleIf* ensemble,
    std::map<std::string, std::vector<cfg::PortPgConfig>>& portPgConfigMap,
    const std::vector<int>& losslessPgIds,
    const std::vector<int>& lossyPgIds,
    const PfcBufferParams& buffer,
    const std::string& pgConfigName = "foo") {
  std::vector<cfg::PortPgConfig> portPgConfigs;

  for (auto pgId : losslessPgIds) {
    cfg::PortPgConfig pgConfig;
    pgConfig.id() = pgId;
    pgConfig.bufferPoolName() = "bufferNew";
    pgConfig.minLimitBytes() = buffer.minLimit;
    // set large enough headroom to avoid drop
    pgConfig.headroomLimitBytes() = buffer.pgHeadroom;
    // resume threshold/offset
    if (buffer.resumeThreshold.has_value()) {
      pgConfig.resumeBytes() = *buffer.resumeThreshold;
    }
    if (buffer.resumeOffset.has_value()) {
      pgConfig.resumeOffsetBytes() = *buffer.resumeOffset;
    }
    if (ensemble->getHwAsicTable()
            ->getHwAsics()
            .cbegin()
            ->second->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
      // Need to translate global config to shared thresholds
      pgConfig.maxSharedXoffThresholdBytes() = buffer.globalShared;
      pgConfig.minSharedXoffThresholdBytes() = buffer.globalShared;
      // Set some default values for SRAM thresholds
      pgConfig.maxSramXoffThresholdBytes() = 2048 * 16 * 256;
      pgConfig.minSramXoffThresholdBytes() = 256 * 16 * 256;
      pgConfig.sramResumeOffsetBytes() = 128 * 16 * 256;
      pgConfig.sramScalingFactor() = cfg::MMUScalingFactor::ONE_HALF;
    }
    // set static/dynamic thresholds
    if (buffer.pgShared.has_value()) {
      pgConfig.staticLimitBytes() = *buffer.pgShared;
    } else {
      pgConfig.scalingFactor() = buffer.scalingFactor;
    }

    portPgConfigs.emplace_back(pgConfig);
  }

  for (auto pgId : lossyPgIds) {
    cfg::PortPgConfig pgConfig;
    pgConfig.id() = pgId;
    pgConfig.bufferPoolName() = "bufferNew";
    pgConfig.minLimitBytes() = buffer.minLimit;
    // headroom set 0 identifies lossy pgs
    pgConfig.headroomLimitBytes() = 0;
    // set static/dynamic thresholds
    if (buffer.pgShared.has_value()) {
      pgConfig.staticLimitBytes() = *buffer.pgShared;
    } else {
      pgConfig.scalingFactor() = buffer.scalingFactor;
    }

    portPgConfigs.emplace_back(pgConfig);
  }

  portPgConfigMap[pgConfigName] = std::move(portPgConfigs);
}

} // namespace

PfcBufferParams PfcBufferParams::getPfcBufferParams(
    cfg::AsicType asicType,
    int globalShared,
    int globalHeadroom) {
  PfcBufferParams buffer;
  buffer.globalShared = globalShared;
  buffer.globalHeadroom = globalHeadroom;

  if (asicType == cfg::AsicType::ASIC_TYPE_CHENAB ||
      asicType == cfg::AsicType::ASIC_TYPE_CHENAB2) {
    // For CHENAB:
    // - XON represents the "min guarantee", must be at least 2xMTU (20480).
    // - RESERVED represents the total amount of buffer exclusively reserved
    //   for the PG.
    //   - In shared headroom pool mode (SAI_BUFFER_POOL_XOFF_SIZE > 0), this
    //     must be the same as XON.
    //   - In non-shared headroom pool mode (SAI_BUFFER_POOL_XOFF_SIZE == 0),
    //     this is the sum of XON and headroom size. Note that XOFF can be
    //     less than the headroom size, in which case there will be hystersis.
    if (globalHeadroom > 0) {
      buffer.resumeThreshold = 20480;
      buffer.pgHeadroom = 4400;
      buffer.minLimit = *buffer.resumeThreshold;
    } else {
      buffer.resumeThreshold = 20480;
      // TODO(maxgg): Understand why PFC won't trigger if this is < ~8000.
      buffer.pgHeadroom = 8000;
      buffer.minLimit = *buffer.resumeThreshold + buffer.pgHeadroom;
    }
  } else if (asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
    // TH6 has 420-byte cells and limited shared space after SDK reserves
    // ~5.99MB. Use small cell-aligned values that fit within available shared
    // buffer.
    buffer.minLimit = 4200; // 10 cells * 420 bytes
    buffer.pgHeadroom = 4200;
    buffer.resumeOffset = 2100; // 5 cells * 420 bytes
  } else if (asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
    // TU1 low-latency MMU admits lossless packets against worst-case MTU
    // (~169 cells); PG headroom must clear it or packets statically reject
    // instead of triggering XOFF. Pool carries SDK defaults (5486 shared +
    // 39516 headroom per buffer x33), which program single-step inside the
    // [40540, 46026] window; smaller pools need two-step.
    buffer.globalShared = 10138128; // 5486 cells * 33 * 56
    buffer.globalHeadroom = 73025568; // 39516 cells * 33 * 56
    buffer.minLimit = 2240; // 40 cells * 56 bytes
    buffer.pgHeadroom = 11200; // 200 cells * 56 bytes
    buffer.resumeOffset = 1800; // less than pgHeadroom
  } else {
    buffer.minLimit = 2200;
    buffer.pgHeadroom = 2200; // keep this lower than globalShared (why?)
    buffer.resumeOffset = 1800; // less than pgHeadroom
  }

  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_JERICHO2:
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
    case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
      buffer.globalShared = kSmallGlobalSharedBytes;
      break;
    default:
      break;
  }

  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      buffer.scalingFactor = cfg::MMUScalingFactor::ONE_HALF;
      break;
    default:
      buffer.scalingFactor = cfg::MMUScalingFactor::ONE_128TH;
      break;
  }

  return buffer;
}

void setupPfcBuffers(
    const TestEnsembleIf* ensemble,
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    const std::vector<int>& losslessPgIds,
    const std::vector<int>& lossyPgIds,
    const PfcQosMapParams& qosMapParams) {
  auto asicType = checkSameAndGetAsicType(cfg);
  setupPfcBuffers(
      ensemble,
      cfg,
      ports,
      losslessPgIds,
      lossyPgIds,
      PfcBufferParams::getPfcBufferParams(asicType),
      qosMapParams);
}

void setupPfcBuffers(
    const TestEnsembleIf* ensemble,
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    const std::vector<int>& losslessPgIds,
    const std::vector<int>& lossyPgIds,
    PfcBufferParams buffer,
    const PfcQosMapParams& qosMapParams) {
  setupPfc(ensemble, cfg, ports, qosMapParams);

  std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
  setupPortPgConfig(
      ensemble, portPgConfigMap, losslessPgIds, lossyPgIds, buffer);
  cfg.portPgConfigs() = std::move(portPgConfigMap);

  // create buffer pool
  std::map<std::string, cfg::BufferPoolConfig> bufferPoolCfgMap =
      cfg.bufferPoolConfigs().ensure();
  auto asic =
      checkSameAndGetAsic(ensemble->getL3Asics(), FLAGS_switch_id_for_testing);
  setupBufferPoolConfig(
      asic, bufferPoolCfgMap, buffer.globalShared, buffer.globalHeadroom);
  cfg.bufferPoolConfigs() = std::move(bufferPoolCfgMap);
  if (ensemble->getHwAsicTable()
          ->getHwAsics()
          .cbegin()
          ->second->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
    // For J3, set the SRAM global PFC thresholds as well
    cfg.switchSettings()->sramGlobalFreePercentXoffThreshold() = 10;
    cfg.switchSettings()->sramGlobalFreePercentXonThreshold() = 20;
    cfg.switchSettings()->linkFlowControlCreditThreshold() = 99;
  }
}

void setupUplinkDownlinkPfc(
    const TestEnsembleIf* ensemble,
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& uplinkPorts,
    const std::vector<PortID>& downlinkPorts,
    const std::vector<int>& losslessPgIds,
    const std::vector<int>& lossyPgIds,
    const PfcQosMapParams& qosMapParams) {
  auto buffer =
      PfcBufferParams::getPfcBufferParams(checkSameAndGetAsicType(cfg));

  // The "uplink"/"downlink" substrings in the pg config name are load-bearing
  // for getRtswUplinkDownlinkPorts(), so a port in both lists would be
  // silently misclassified by whichever call runs last.
  for (const auto& portID : uplinkPorts) {
    if (std::find(downlinkPorts.begin(), downlinkPorts.end(), portID) !=
        downlinkPorts.end()) {
      throw FbossError("Port ", portID, " is both an uplink and a downlink");
    }
  }

  auto tagPorts = [&cfg](
                      const std::vector<PortID>& ports,
                      const std::string& pgConfigName) {
    cfg::PortPfc pfc;
    pfc.tx() = true;
    pfc.rx() = true;
    pfc.portPgConfigName() = pgConfigName;
    for (const auto& portID : ports) {
      auto portCfg = std::find_if(
          cfg.ports()->begin(), cfg.ports()->end(), [&portID](auto& port) {
            return PortID(*port.logicalID()) == portID;
          });
      if (portCfg == cfg.ports()->end()) {
        throw FbossError("No port ", portID, " in config to apply PFC to");
      }
      portCfg->pfc() = pfc;
    }
  };
  tagPorts(uplinkPorts, kUplinkPgConfigName);
  tagPorts(downlinkPorts, kDownlinkPgConfigName);

  auto portPgConfigMap = cfg.portPgConfigs().ensure();
  for (const auto& pgConfigName :
       {kUplinkPgConfigName, kDownlinkPgConfigName}) {
    setupPortPgConfig(
        ensemble,
        portPgConfigMap,
        losslessPgIds,
        lossyPgIds,
        buffer,
        pgConfigName);
  }
  cfg.portPgConfigs() = std::move(portPgConfigMap);

  auto bufferPoolCfgMap = cfg.bufferPoolConfigs().ensure();
  setupBufferPoolConfig(
      checkSameAndGetAsic(ensemble->getL3Asics(), FLAGS_switch_id_for_testing),
      bufferPoolCfgMap,
      buffer.globalShared,
      buffer.globalHeadroom);
  cfg.bufferPoolConfigs() = std::move(bufferPoolCfgMap);

  // Merge into the existing QoS policies rather than adding one. A second
  // policy would give getOlympicQosMaps() a competing DSCP -> traffic class
  // mapping for the same traffic classes.
  auto priorityMaps = makePfcPriorityMaps(qosMapParams);
  bool merged = false;
  for (auto& qosPolicy : *cfg.qosPolicies()) {
    if (!qosPolicy.qosMap().has_value()) {
      continue;
    }
    qosPolicy.qosMap()->trafficClassToPgId() = priorityMaps.tcToPg;
    qosPolicy.qosMap()->pfcPriorityToPgId() = priorityMaps.pfcPriToPg;
    qosPolicy.qosMap()->pfcPriorityToQueueId() = priorityMaps.pfcPriToQueue;
    merged = true;
  }
  if (!merged) {
    throw FbossError(
        "No QoS policy with a qosMap to merge PFC priority maps into; "
        "install the QoS policy before calling setupUplinkDownlinkPfc()");
  }
}

void addPuntPfcPacketAcl(cfg::SwitchConfig& cfg, uint16_t queueId) {
  cfg::AclEntry entry;
  entry.name() = "pfcMacEntry";
  entry.actionType() = cfg::AclActionType::PERMIT;
  entry.dstMac() = "01:80:C2:00:00:01";
  utility::addAclEntry(&cfg, entry, utility::kDefaultAclTable());

  cfg::MatchToAction matchToAction;
  matchToAction.matcher() = "pfcMacEntry";
  cfg::MatchAction& action = matchToAction.action().ensure();
  action.toCpuAction() = cfg::ToCpuAction::TRAP;
  action.sendToQueue().ensure().queueId() = queueId;
  action.setTc().ensure().tcValue() = queueId;
  cfg.cpuTrafficPolicy()
      .ensure()
      .trafficPolicy()
      .ensure()
      .matchToAction()
      .ensure()
      .push_back(matchToAction);
}

std::string pfcStatsString(const HwPortStats& stats) {
  std::stringstream ss;
  ss << "outBytes=" << *stats.outBytes_() << " inBytes=" << *stats.inBytes_()
     << " outUnicastPkts=" << *stats.outUnicastPkts_()
     << " inUnicastPkts=" << *stats.inUnicastPkts_()
     << " inDiscards=" << *stats.inDiscards_()
     << " inDiscardsRaw=" << *stats.inDiscardsRaw_()
     << " inCongestionDiscards=" << *stats.inCongestionDiscards_()
     << " inErrors=" << *stats.inErrors_();
  for (auto [qos, value] : *stats.inPfc_()) {
    ss << " inPfc." << qos << "=" << value;
  }
  for (auto [qos, value] : *stats.outPfc_()) {
    ss << " outPfc." << qos << "=" << value;
  }
  return ss.str();
}

std::unique_ptr<TxPacket> makePfcFramePacket(
    const AgentEnsemble& ensemble,
    uint8_t classVector) {
  // Construct PFC payload with fixed quanta 0x00F0.
  // See https://github.com/archjeb/pfctest for frame structure.
  std::vector<uint8_t> payload{
      0x01, 0x01, 0x00, classVector, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0,
      0x00, 0xF0, 0x00, 0xF0,        0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0,
  };
  std::vector<uint8_t> padding(26, 0);
  payload.insert(payload.end(), padding.begin(), padding.end());

  // Construct PFC frame packet
  std::optional<VlanID> vlanId = ensemble.getVlanIDForTx();
  folly::MacAddress intfMac =
      getMacForFirstInterfaceWithPortsForTesting(ensemble.getProgrammedState());
  MacAddressGenerator::ResourceT srcMac =
      utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
  return utility::makeEthTxPacket(
      ensemble.getSw(),
      vlanId,
      srcMac,
      folly::MacAddress("01:80:C2:00:00:01"), // MAC control address
      ETHERTYPE::ETHERTYPE_EPON, // Ethertype for PFC frames
      std::move(payload));
}

void triggerPfcGeneration(
    AgentEnsemble* ensemble,
    const PortID& port,
    const PortID& txDisablePort,
    const folly::IPAddressV6& destIp,
    int trafficClass,
    int pfcPriority,
    const std::optional<VlanID>& vlanId) {
  // Disable Tx on the outbound port so that queues will build up.
  setCreditWatchdogAndPortTx(ensemble, txDisablePort, false);

  auto txPfcCtrOld = folly::get_default(
      *ensemble->getLatestPortStats(port).outPfc_(), pfcPriority, 0);

  // Send traffic in chunks of 1000 packets at a time, checking if PFC
  // has been triggered after each chunk. Continue until PFC counter
  // increments, indicating congestion and PFC frame generation.
  auto intfMac = getMacForFirstInterfaceWithPortsForTesting(
      ensemble->getProgrammedState());
  auto srcMac = MacAddressGenerator().get(intfMac.u64HBO() + 1);
  int dscp = trafficClass * 8;
  constexpr int kChunkSize = 1000;

  checkWithRetry(
      [&]() {
        for (int i = 0; i < kChunkSize; i++) {
          auto txPacket = makeUDPTxPacket(
              ensemble->getSw(),
              vlanId.has_value() ? vlanId : ensemble->getVlanIDForTx(),
              srcMac,
              intfMac,
              folly::IPAddressV6("2620:0:1cfe:face:b00c::1"),
              destIp,
              8000,
              8001,
              dscp << 2,
              255,
              std::vector<uint8_t>(2000, 0xff));

          if (vlanId.has_value()) {
            ensemble->getSw()->sendPacketSwitchedAsync(
                std::move(txPacket),
                {ensemble->scopeResolver().scope(port).switchId()});
          } else {
            ensemble->sendPacketAsync(
                std::move(txPacket), PortDescriptor(port), pfcPriority);
          }
        }
        auto txPfcCtrNew = folly::get_default(
            *ensemble->getLatestPortStats(port).outPfc_(), pfcPriority, 0);
        return txPfcCtrNew > txPfcCtrOld;
      },
      60 /* retries */,
      std::chrono::milliseconds(1000),
      "PFC TX counter did not increase");
}

void cleanupPfcGeneration(
    AgentEnsemble* ensemble,
    const PortID& txDisablePort) {
  // Enable credit WD and TX on port
  setCreditWatchdogAndPortTx(ensemble, txDisablePort, true);
}

} // namespace facebook::fboss::utility
