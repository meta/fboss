/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/logging/xlog.h>
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/types.h"

using namespace facebook::fboss;

namespace {

// arbitrary values
constexpr int kPgResumeCells = 5;
constexpr int kPgResumeOffsetCells = 5;
constexpr int kPgMinLimitCells = 6;
constexpr int kPgHeadroomLimitCells = 2;
constexpr int kPoolHeadroomLimitCells = 10;
constexpr int kPoolSharedCells = 11000;
constexpr int kPoolReservedCells = 4000;
/*
 * SDK has the expectation that pool_total_size - delta <= shared size,
 * where delta is the difference between old and new pool limits, if
 * not, its considered an error.
 */
constexpr int kPoolSharedSizeBytesHighValue = 113091;
constexpr std::string_view kBufferPoolName = "fooBuffer";

// TU1 programs each of its 33 ingress buffers as pool/33, held within
// [40540, 46026] cells of default state; these totals land at 43707/41096
// cells per buffer.
cfg::BufferPoolConfig getIngressBufferPoolConfig(
    const HwAsic* asic,
    int mmuBytesInCell,
    bool high,
    int deltaValue = 0) {
  int headroomCells, sharedCells, reservedCells;
  if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
    headroomCells = high ? 112776 : 91;
    sharedCells = high ? 1347107 : 1406071;
    reservedCells = high ? 9071 : 36286;
  } else {
    headroomCells = high ? 12432 : kPoolHeadroomLimitCells;
    sharedCells = high ? kPoolSharedSizeBytesHighValue : kPoolSharedCells;
    reservedCells = high ? 1000 : kPoolReservedCells;
  }
  cfg::BufferPoolConfig tmpCfg;
  tmpCfg.headroomBytes() = (headroomCells + deltaValue) * mmuBytesInCell;
  tmpCfg.sharedBytes() = (sharedCells + deltaValue) * mmuBytesInCell;
  tmpCfg.reservedBytes() = (reservedCells + deltaValue) * mmuBytesInCell;
  return tmpCfg;
}

// util to construct per PG params
// pass in number of queues and use queueId, deltaValue to differ
// PG params
std::vector<cfg::PortPgConfig> getPortPgConfig(
    const HwAsic* asic,
    const std::vector<int>& queues,
    int deltaValue = 0,
    const bool enableHeadroom = true,
    const bool zeroHeadroom = false) {
  std::vector<cfg::PortPgConfig> portPgConfigs;

  uint32_t mmuCellBytes = asic->getPacketBufferUnitSize();
  for (const auto queueId : queues) {
    cfg::PortPgConfig pgConfig;
    pgConfig.id() = queueId;
    // use queueId value to assign different values for each param/queue
    if (enableHeadroom) {
      if (zeroHeadroom) {
        pgConfig.headroomLimitBytes() = 0;
      } else {
        pgConfig.headroomLimitBytes() =
            (kPgHeadroomLimitCells + queueId + deltaValue) * mmuCellBytes;
      }
    }
    pgConfig.minLimitBytes() =
        (kPgMinLimitCells + queueId + deltaValue) * mmuCellBytes;
    if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
      pgConfig.resumeBytes() =
          (kPgResumeCells + queueId + deltaValue) * mmuCellBytes;
    } else {
      pgConfig.resumeOffsetBytes() =
          (kPgResumeOffsetCells + queueId + deltaValue) * mmuCellBytes;
    }
    pgConfig.scalingFactor() = cfg::MMUScalingFactor::EIGHT;
    pgConfig.bufferPoolName() = kBufferPoolName;
    portPgConfigs.emplace_back(pgConfig);
  }
  return portPgConfigs;
}

} // unnamed namespace

namespace facebook::fboss {

class AgentIngressBufferTest : public AgentHwTest {
 protected:
  void setCmdLineFlagOverrides() const override {
    FLAGS_fix_lossless_mode_per_pg = true;
    AgentHwTest::setCmdLineFlagOverrides();
  }
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PFC};
  }

  std::vector<PortID> portIdsForTest() const {
    if (FLAGS_hyper_port) {
      return masterLogicalHyperPortIds();
    }
    return masterLogicalInterfacePortIds();
  }

  void setupGlobalBuffer(
      cfg::SwitchConfig& cfg,
      bool useLargeHwValues,
      PortID portId) {
    std::map<std::string, cfg::BufferPoolConfig> bufferPoolCfgMap;
    cfg::BufferPoolConfig bufferPoolConfig;
    auto switchId = getSw()->getScopeResolver()->scope(portId).switchId();
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);

    bufferPoolConfig = getIngressBufferPoolConfig(
        asic, asic->getPacketBufferUnitSize(), useLargeHwValues);

    bufferPoolCfgMap.insert(
        make_pair(static_cast<std::string>(kBufferPoolName), bufferPoolConfig));
    cfg.bufferPoolConfigs() = std::move(bufferPoolCfgMap);
  }

  void setupPgBuffers(
      cfg::SwitchConfig& cfg,
      const bool enableHeadroom,
      PortID portId) {
    std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
    auto switchId = getSw()->getScopeResolver()->scope(portId).switchId();
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    portPgConfigMap["foo"] =
        getPortPgConfig(asic, {0, 1}, 0 /* delta value */, enableHeadroom);
    cfg.portPgConfigs() = std::move(portPgConfigMap);
  }

  void
  setupPfc(cfg::SwitchConfig& cfg, const PortID& portId, const bool pfcEnable) {
    auto portCfg = utility::findCfgPort(cfg, portId);
    // setup pfc
    cfg::PortPfc pfc;
    pfc.portPgConfigName() = "foo";
    pfc.tx() = pfcEnable;
    pfc.rx() = pfcEnable;
    portCfg->pfc() = std::move(pfc);
  }

  void setupHelper(
      bool enableHeadroom = true,
      bool pfcEnable = true,
      bool enableHighBufferValues = false) {
    auto cfg = initialConfig(*getAgentEnsemble());

    auto portId = portIdsForTest()[0];
    // setup PFC
    setupPfc(cfg, portId, pfcEnable);
    // setup pgConfig
    setupPgBuffers(cfg, enableHeadroom, portId);
    // setup bufferPool
    setupGlobalBuffer(cfg, enableHighBufferValues, portId);
    applyNewConfig(cfg);
    cfg_ = cfg;
  }

  void setupHelperWithHighBufferValues() {
    setupHelper(true, true, true /* enable high buffer defaults */);
  }

  bool checkSwHwPgCfgMatch(PortID portId, bool pfcEnabled) {
    auto client = getAgentEnsemble()->getHwAgentTestClient(
        getSw()->getScopeResolver()->scope(portId).switchId());
    return client->sync_verifyPGSettings(portId, pfcEnabled);
  }

  cfg::SwitchConfig cfg_;
};

// Create PG config, associate with PFC config
// validate that SDK programming is as per the cfg
// Read back from HW (using SDK calls) and validate
TEST_F(AgentIngressBufferTest, validateConfig) {
  auto setup = [=, this]() { setupHelper(); };

  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/));
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Create PG config, associate with PFC config. Modify the PG
// config params and ensure that its getting re-programmed.
TEST_F(AgentIngressBufferTest, validatePGParamChange) {
  auto setup = [&]() {
    setupHelper();
    // update one PG, and see ifs reflected in the HW
    std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
    auto portId = portIdsForTest()[0];
    auto switchId = getSw()->getScopeResolver()->scope(portId).switchId();
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    portPgConfigMap["foo"] = getPortPgConfig(asic, {0, 1}, 1);
    cfg_.portPgConfigs() = portPgConfigMap;
    applyNewConfig(cfg_);
  };

  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/));
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

// For each of the below transitions, ensure headroom is programmed
TEST_F(AgentIngressBufferTest, validatePGHeadroomLimitChange) {
  auto setup = [&]() {
    // Start with PG0 and PG1 with non-zero headroom
    setupHelper();

    // Modify PG1 headroom value
    // This ensure the new value is getting programmed after config update
    // For both PGs, they will be created in lossless mode
    std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
    auto portId = portIdsForTest()[0];
    auto switchId = getSw()->getScopeResolver()->scope(portId).switchId();
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    auto portPgConfigs = getPortPgConfig(asic, {0}, 0);
    portPgConfigs.push_back(getPortPgConfig(asic, {1}, 1)[0]);
    portPgConfigMap["foo"] = portPgConfigs;
    cfg_.portPgConfigs() = portPgConfigMap;
    applyNewConfig(cfg_);
    checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/);

    // Remove PG1 headroom field and add a new PG2 with no headroom field
    // both cases, PG1 and PG2 should be created in lossy mode
    portPgConfigs = getPortPgConfig(asic, {0}, 0);
    portPgConfigs.push_back(getPortPgConfig(asic, {1}, 1, false)[0]);
    portPgConfigs.push_back(getPortPgConfig(asic, {2}, 0, false)[0]);
    portPgConfigMap["foo"] = portPgConfigs;
    cfg_.portPgConfigs() = portPgConfigMap;
    applyNewConfig(cfg_);
    checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/);

    // Remove PG1 and update PG2 headroom to non-zero
    // This ensure counters are accurately updated per PG. PFC counters are
    // are created only for non-zero headroom PGs
    // Also ensures, PG2 is updated to lossless mode.
    portPgConfigs = getPortPgConfig(asic, {0}, 0);
    portPgConfigs.push_back(getPortPgConfig(asic, {2}, 0)[0]);
    portPgConfigMap["foo"] = portPgConfigs;
    cfg_.portPgConfigs() = portPgConfigMap;
    applyNewConfig(cfg_);
    checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/);

    // Make PG2 headroom value 0. This also make PG2 lossy
    portPgConfigs = getPortPgConfig(asic, {0}, 0);
    portPgConfigs.push_back(getPortPgConfig(
        asic,
        {2},
        0,
        true, /* enableHeadroom */
        true /* zeroHeadroom */)[0]);
    portPgConfigMap["foo"] = portPgConfigs;
    cfg_.portPgConfigs() = portPgConfigMap;
    applyNewConfig(cfg_);
  };

  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/));
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Validate the Pg's pfc mode bit, by default we have been enabling
// PFC on the port and hence on every PG. Force the port to have no
// PFC. Validate that Pg's pfc mode is False now.
TEST_F(AgentIngressBufferTest, validatePgNoPfc) {
  auto setup = [&]() {
    setupHelper(true /* enable headroom */, false /* pfc */);
  };
  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], false /*pfcEnable*/));
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Validate that if we program the global buffer with high values, we
// don't throw any errors programming. We saw this issue, when we try
// programming higher values and logic has been added to program what
// ever buffer value is lower than programmed first to workaround the
// sdk error.
TEST_F(AgentIngressBufferTest, validateHighBufferValues) {
  auto setup = [&]() { setupHelperWithHighBufferValues(); };
  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/));
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Create PG config, associate with PFC config. Do not create headroom
// cfg, PGs should be in lossy mode now; validate that SDK programming
// is as per cfg.
TEST_F(AgentIngressBufferTest, validateLossyMode) {
  auto setup = [&]() {
    setupHelper(false /* enable headroom */, false /* pfcEnable */);
  };

  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], false /* pfcEnable */));
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Create PG config, associate with PFC config. Modify the PG queue
// config params and ensure that its getting re-programmed.
TEST_F(AgentIngressBufferTest, validatePGQueueChanges) {
  auto setup = [&]() {
    setupHelper();
    // update one PG, and see ifs reflected in the HW
    std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
    auto portId = portIdsForTest()[0];
    auto switchId = getSw()->getScopeResolver()->scope(portId).switchId();
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    portPgConfigMap["foo"] =
        getPortPgConfig(asic, {1}, 0, true /* enableHeadroom */);
    cfg_.portPgConfigs() = portPgConfigMap;
    applyNewConfig(cfg_);
  };

  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/));
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentIngressBufferPoolTest : public AgentIngressBufferTest {
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentIngressBufferTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::SEPARATE_INGRESS_EGRESS_BUFFER_POOL);
    return features;
  }
};

// Create PG, Ingress pool config, associate with PFC config.
// Modify the ingress pool params only and ensure that it is
// getting re-programmed
TEST_F(AgentIngressBufferPoolTest, validateIngressPoolParamChange) {
  auto setup = [&]() {
    setupHelper();
    auto portId = portIdsForTest()[0];
    auto switchId = getSw()->getScopeResolver()->scope(portId).switchId();
    auto asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    // setup bufferPool
    std::map<std::string, cfg::BufferPoolConfig> bufferPoolCfgMap;
    bufferPoolCfgMap.insert(make_pair(
        static_cast<std::string>(kBufferPoolName),
        getIngressBufferPoolConfig(
            asic, asic->getPacketBufferUnitSize(), false, 1)));
    cfg_.bufferPoolConfigs() = bufferPoolCfgMap;
    // update one PG, and see ifs reflected in the HW
    applyNewConfig(cfg_);
  };

  auto verify = [&]() {
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          checkSwHwPgCfgMatch(portIdsForTest()[0], true /*pfcEnable*/));
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
