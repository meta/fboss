/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/tests/BcmSwitchEnsemble.h"

#include <folly/logging/xlog.h>
#include <memory>
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/hw/bcm/BcmAPI.h"
#include "fboss/agent/hw/bcm/BcmConfig.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPort.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmTrunkTable.h"
#include "fboss/agent/hw/bcm/tests/BcmLinkStateToggler.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/platforms/tests/utils/CreateTestPlatform.h"
#include "fboss/agent/test/LinkStateToggler.h"

DECLARE_bool(setup_thrift);

using apache::thrift::can_throw;
using namespace facebook::fboss;

namespace {
void addPort(
    facebook::fboss::BcmConfig::ConfigMap& cfg,
    int port,
    int speed,
    bool enabled = true) {
  auto key = folly::to<std::string>("portmap_", port);
  auto value = folly::to<std::string>(port, ":", speed);
  if (!enabled) {
    value = folly::to<std::string>(value, ":i");
  }
  cfg[key] = value;
}

void addFlexPort(
    facebook::fboss::BcmConfig::ConfigMap& cfg,
    int start,
    int speed) {
  addPort(cfg, start, speed);
  addPort(cfg, start + 1, speed / 4, false);
  addPort(cfg, start + 2, speed / 2, false);
  addPort(cfg, start + 3, speed / 4, false);
}

std::unique_ptr<AgentConfig> loadCfgFromLocalFile(
    std::unique_ptr<Platform>& platform,
    BcmTestPlatform* bcmTestPlatform,
    std::string& yamlCfg,
    BcmConfig::ConfigMap& cfg) {
  std::unique_ptr<AgentConfig> agentConfig;
  if (!FLAGS_bcm_config.empty()) {
    XLOG(DBG2) << "Loading bcm config from " << FLAGS_bcm_config;
    if (bcmTestPlatform->usesYamlConfig()) {
      yamlCfg = BcmYamlConfig::loadFromFile(FLAGS_bcm_config);
    } else {
      cfg = BcmConfig::loadFromFile(FLAGS_bcm_config);
    }
    agentConfig = createEmptyAgentConfig();
  } else if (!FLAGS_config.empty()) {
    XLOG(DBG2) << "Loading config from " << FLAGS_config;
    agentConfig = AgentConfig::fromFile(FLAGS_config);
    if (bcmTestPlatform->usesYamlConfig()) {
      yamlCfg = can_throw(*(platform->config()->thrift)
                               .platform()
                               ->chip()
                               ->bcm()
                               ->yamlConfig());
    } else {
      cfg = *(platform->config()->thrift).platform()->chip()->bcm()->config();
    }
  } else {
    throw FbossError("No config file to load!");
  }
  return agentConfig;
}

void modifyCfgForPfcTests(
    BcmTestPlatform* bcmTestPlatform,
    std::string& yamlCfg,
    BcmConfig::ConfigMap& cfg,
    bool skipBufferReservation) {
  if (bcmTestPlatform->usesYamlConfig()) {
    std::string toReplace("LOSSY");
    std::size_t pos = yamlCfg.find(toReplace);
    if (pos != std::string::npos) {
      // for TH4 we skip buffer reservation in prod
      // but it doesn't seem to work for pfc tests which
      // play around with other variables. For unblocking
      // skip it for now
      if (skipBufferReservation) {
        yamlCfg.replace(
            pos,
            toReplace.length(),
            "LOSSY_AND_LOSSLESS\n      SKIP_BUFFER_RESERVATION: 1");
      } else {
        yamlCfg.replace(pos, toReplace.length(), "LOSSY_AND_LOSSLESS");
      }
    }
  } else {
    cfg["mmu_lossless"] = "0x2";
    cfg["buf.mqueue.guarantee.0"] = "0C";
    cfg["mmu_config_override"] = "0";
    cfg["buf.prigroup7.guarantee"] = "0C";
    if (FLAGS_qgroup_guarantee_enable) {
      cfg["buf.qgroup.guarantee_mc"] = "0";
      cfg["buf.qgroup.guarantee"] = "0";
    }
  }
}

void modifyCfgForQcmTests(facebook::fboss::BcmConfig::ConfigMap& cfg) {
  if (cfg.find("l2_mem_entries") != cfg.end()) {
    // remove
    cfg.erase("l2_mem_entries");
  }
  cfg["flowtracker_enable"] = "2";
  cfg["qcm_flow_enable"] = "1";
  cfg["qcm_max_flows"] = "1000";
  cfg["num_queues_pci"] = "46";
  cfg["num_queues_uc0"] = "1";
  cfg["num_queues_uc1"] = "1";
  cfg["flowtracker_max_export_pkt_length"] = "1500";
  cfg["flowtracker_enterprise_number"] = "0xAABBCCDD";
  cfg["fpem_mem_entries"] = "0x2000";
  cfg["ctr_evict_enable"] = "0";
}

void modifyCfgForEMTests(std::string& yamlCfg) {
  facebook::fboss::enableExactMatch(yamlCfg);
}

} // namespace

namespace facebook::fboss {

BcmSwitchEnsemble::BcmSwitchEnsemble(
    const HwSwitchEnsemble::Features& featuresDesired)
    : HwSwitchEnsemble(featuresDesired) {}

std::vector<PortID> BcmSwitchEnsemble::getAllMasterLogicalPortIds() const {
  return getPlatform()->masterLogicalPortIds();
}

std::vector<PortID> BcmSwitchEnsemble::getAllPortsInGroup(PortID portID) const {
  return getPlatform()->getAllPortsInGroup(portID);
}

std::vector<FlexPortMode> BcmSwitchEnsemble::getSupportedFlexPortModes() const {
  return getPlatform()->getSupportedFlexPortModes();
}

void BcmSwitchEnsemble::dumpHwCounters() const {
  getHwSwitch()->printDiagCmd("show c");
}

std::map<PortID, HwPortStats> BcmSwitchEnsemble::getLatestPortStats(
    const std::vector<PortID>& ports) {
  // sync the stats
  auto rv = bcm_stat_sync(getSdkSwitchId());
  bcmCheckError(rv, "Unable to sync stats ");
  return HwSwitchEnsemble::getLatestPortStats(ports);
}

std::map<AggregatePortID, HwTrunkStats>
BcmSwitchEnsemble::getLatestAggregatePortStats(
    const std::vector<AggregatePortID>& aggregatePorts) {
  std::map<AggregatePortID, HwTrunkStats> stats{};
  for (auto aggregatePort : aggregatePorts) {
    auto trunkStats =
        getHwSwitch()->getTrunkTable()->getHwTrunkStats(aggregatePort);
    stats.emplace(
        aggregatePort, (trunkStats.has_value() ? *trunkStats : HwTrunkStats{}));
  }
  return stats;
}

bool BcmSwitchEnsemble::isRouteScaleEnabled() const {
  return BcmAPI::isAlpmEnabled();
}

std::unique_ptr<LinkStateToggler> BcmSwitchEnsemble::createLinkToggler() {
  return std::make_unique<BcmLinkStateToggler>(this);
}

uint64_t BcmSwitchEnsemble::getSdkSwitchId() const {
  return getHwSwitch()->getUnit();
}

void BcmSwitchEnsemble::runDiagCommand(
    const std::string& input,
    std::string& /*output*/,
    std::optional<SwitchID> /*switchId*/) {
  getHwSwitch()->printDiagCmd(input);
}

void BcmSwitchEnsemble::init(const TestEnsembleInitInfo& info) {
  CHECK(!info.overrideDsfNodes.has_value())
      << " Dsf nodes not supported in BCM tests";
  auto platform = createTestPlatform();
  auto bcmTestPlatform = static_cast<BcmTestPlatform*>(platform.get());
  std::unique_ptr<AgentConfig> agentConfig;
  BcmConfig::ConfigMap cfg;
  std::string yamlCfg;
  auto platformType = getPlatformType();
  if (platformType == PlatformType::PLATFORM_FAKE_WEDGE) {
    FLAGS_flexports = true;
    for (int n = 1; n <= 125; n += 4) {
      addFlexPort(cfg, n, 40);
    }
    cfg["pbmp_xport_xe"] = "0x1fffffffffffffffffffffffe";
    // Create an empty AgentConfig for now, once we fully roll out the new
    // platform config, we should generate a mock platform config for the fake
    // bcm test
    auto emptyAgentConfig = createEmptyAgentConfig()->thrift;
    cfg::SwitchInfo switchInfo{};
    switchInfo.switchType() = cfg::SwitchType::NPU;
    switchInfo.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
    fboss::cfg::Range64 portIdRange;
    portIdRange.minimum() = 0;
    portIdRange.maximum() = 0;
    switchInfo.portIdRange() = portIdRange;
    switchInfo.switchIndex() = 0;
    emptyAgentConfig.sw()->switchSettings()->switchIdToSwitchInfo()->emplace(
        0, switchInfo);
    agentConfig = std::make_unique<AgentConfig>(emptyAgentConfig);
  } else {
    // Load from a local file
    agentConfig = loadCfgFromLocalFile(platform, bcmTestPlatform, yamlCfg, cfg);
    for (auto item : *agentConfig->thrift.defaultCommandLineArgs()) {
      gflags::SetCommandLineOptionWithMode(
          item.first.c_str(), item.second.c_str(), gflags::SET_FLAG_IF_DEFAULT);
    }
  }
  // Augment bcm configs based on capabilities.
  // We should really move this functionality to generated bcm_test configs for
  // features we deploy widely (say EM).
  // Unfortunately we can't use ASIC for querying this capabilities, since
  // ASIC construction requires inputs from AgentConfig (switchType) during
  // construction.
  std::unordered_set<PlatformType> th3BcmPlatforms = {
      PlatformType::PLATFORM_MINIPACK,
      PlatformType::PLATFORM_YAMP,
      PlatformType::PLATFORM_WEDGE400,
      PlatformType::PLATFORM_DARWIN,
      PlatformType::PLATFORM_DARWIN48V};
  std::unordered_set<PlatformType> th4BcmPlatforms = {
      PlatformType::PLATFORM_FUJI, PlatformType::PLATFORM_ELBERT};
  bool th3Platform = false;
  bool th4Platform = false;
  if (th3BcmPlatforms.find(platformType) != th3BcmPlatforms.end()) {
    th3Platform = true;
  } else if (th4BcmPlatforms.find(platformType) != th4BcmPlatforms.end()) {
    th4Platform = true;
  }

  // when in lossless mode on support platforms, use a different BCM knob
  if (FLAGS_mmu_lossless_mode && (th3Platform || th4Platform)) {
    bool skipBufferReservation = false;
    if (FLAGS_skip_buffer_reservation && th4Platform) {
      // onlt skip for TH4 for now
      skipBufferReservation = true;
    }
    XLOG(DBG2)
        << "Modify the bcm cfg as mmu_lossless mode is enabled and skip buffer reservation is: "
        << skipBufferReservation;
    modifyCfgForPfcTests(bcmTestPlatform, yamlCfg, cfg, skipBufferReservation);
  }
  if (FLAGS_enable_exact_match) {
    XLOG(DBG2) << "Modify bcm cfg as enable_exact_match is enabled";
    if (bcmTestPlatform->usesYamlConfig()) {
      modifyCfgForEMTests(yamlCfg);
    } else {
      cfg["fpem_mem_entries"] = "0x10000";
    }
  }
  std::unordered_set<PlatformType> thBcmPlatforms = {
      PlatformType::PLATFORM_WEDGE100,
      PlatformType::PLATFORM_GALAXY_LC,
      PlatformType::PLATFORM_GALAXY_FC};
  if (FLAGS_load_qcm_fw &&
      thBcmPlatforms.find(platformType) != thBcmPlatforms.end()) {
    XLOG(DBG2) << "Modify bcm cfg as load_qcm_fw is enabled";
    modifyCfgForQcmTests(cfg);
  }
  if (bcmTestPlatform->usesYamlConfig()) {
    BcmAPI::initHSDK(yamlCfg);
  } else {
    BcmAPI::init(cfg);
  }
  // TODO pass agent config to platform init
  platform->init(std::move(agentConfig), getHwSwitchFeatures(), 0);
  if (auto tcvr = info.overrideTransceiverInfo) {
    platform->setOverrideTransceiverInfo(*tcvr);
  }

  std::unique_ptr<LinkStateToggler> linkToggler;
  if (haveFeature(HwSwitchEnsemble::LINKSCAN)) {
    linkToggler = createLinkToggler();
  }
  std::unique_ptr<std::thread> thriftThread;
  if (FLAGS_setup_thrift) {
    thriftThread =
        createThriftThread(static_cast<BcmSwitch*>(platform->getHwSwitch()));
  }
  setupEnsemble(
      std::make_unique<HwAgent>(std::move(platform)),
      std::move(linkToggler),
      std::move(thriftThread),
      info);
  getPlatform()->initLEDs(getHwSwitch()->getUnit());
}

// intent of this routine is to pick uplinks, downlinks from
// 2 BCM ITMs equally. Since this configuraiton validates
// production configuration values, we want to pick ports equally
// from 2 MMU buffers
void BcmSwitchEnsemble::createEqualDistributedUplinkDownlinks(
    const std::vector<PortID>& ports,
    std::vector<PortID>& uplinks,
    std::vector<PortID>& downlinks,
    std::vector<PortID>& disabled,
    const int totalLinkCount) {
  std::map<int, std::vector<PortID>> pipeToPortMap;
  for (const auto& mp : ports) {
    auto bcmPort = getHwSwitch()->getPortTable()->getBcmPort(mp);
    auto pipe = bcmPort->getPipe();
    auto pipeToPortMapIter = pipeToPortMap.find(pipe);
    if (pipeToPortMapIter != pipeToPortMap.end()) {
      pipeToPortMapIter->second.emplace_back(mp);
    } else {
      pipeToPortMap[pipe] = {mp};
    }
  }

  // in zionex case, we have totalLinkCount = 32
  // there are 8 pipes, we should pick 4 per pipe
  // and 2 donwlinks, 2 uplinks
  const int perPipeCount =
      static_cast<int>(totalLinkCount / pipeToPortMap.size());
  const int upOrDownlinkCount = perPipeCount / 2;

  // assign ports to uplink, downlink
  for (const auto& pipeToPortEntry : pipeToPortMap) {
    // for every pipe, pick first 2 uplinks, next 2 downlinks .. rest all
    // disabled
    int uplinkCount = 0;
    int downlinkCount = 0;
    for (const auto& port : pipeToPortEntry.second) {
      if (uplinkCount < upOrDownlinkCount) {
        uplinks.emplace_back(static_cast<PortID>(port));
        uplinkCount++;
      } else {
        if (downlinkCount >= upOrDownlinkCount) {
          disabled.emplace_back(static_cast<PortID>(port));
        } else {
          downlinks.emplace_back(static_cast<PortID>(port));
        }
        downlinkCount++;
      }
    }
  }
}

} // namespace facebook::fboss
