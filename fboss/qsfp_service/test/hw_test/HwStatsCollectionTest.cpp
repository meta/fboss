/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <fmt/format.h>

#include "fboss/lib/CommonUtils.h"
#include "fboss/qsfp_service/StatsPublisher.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"
#include "fboss/qsfp_service/test/hw_test/HwExternalPhyPortTest.h"
#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "fboss/qsfp_service/test/hw_test/HwTest.h"

namespace facebook::fboss {

TEST_F(HwTest, publishStats) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::STATS_COLLECTION});
  auto* phyManager = getHwQsfpEnsemble()->getPhyManager();
  auto* portManager =
      getHwQsfpEnsemble()->getQsfpServiceHandler()->getPortManager();
  StatsPublisher publisher(
      getHwQsfpEnsemble()->getWedgeManager(), phyManager, portManager);
  publisher.init();
  publisher.publishStats(nullptr, 0);
  getHwQsfpEnsemble()->getWedgeManager()->publishI2cTransactionStats();

  auto counterKeys = fb303::fbData->getCounterKeys();
#ifndef IS_OSS
  auto anyInterfaceCounter = [&counterKeys](const std::string& suffix) {
    return std::any_of(
        counterKeys.begin(), counterKeys.end(), [&suffix](const auto& key) {
          return key.starts_with("qsfp.interface.") && key.ends_with(suffix);
        });
  };

  // getTransceiversInfo stamps the state machine state onto every valid
  // transceiver, so any platform with named ports exports this.
  EXPECT_TRUE(anyInterfaceCounter(".tcvrStateMachineState"))
      << "no qsfp.interface.<portName>.tcvrStateMachineState counter published "
      << "to fb303";

  // Port state machines only exist in Port Manager mode, so the counter must
  // appear there and must not appear otherwise.
  EXPECT_EQ(
      anyInterfaceCounter(".portStateMachineState"), portManager != nullptr)
      << "qsfp.interface.<portName>.portStateMachineState counters "
      << (portManager ? "missing in" : "published outside")
      << " Port Manager mode";
#endif

  // A PhyManager is constructed for every platform of an XPHY capable family,
  // but the XPHYs themselves only exist on the PIMs that carry them, so having
  // a PhyManager does not imply having XPHYs to report counters for.
  if (!phyManager || phyManager->getNumXphys() == 0) {
    return;
  }
  // Where XPHYs do exist, publishStats must export their MDIO IO counters. The
  // PhyManager is owned by PortManager in Port Manager mode and by
  // TransceiverManager otherwise, so this also guards against the counters
  // silently disappearing in one of the two modes.
  EXPECT_TRUE(
      std::any_of(
          counterKeys.begin(),
          counterKeys.end(),
          [](const auto& key) {
            return key.starts_with("qsfp.pim") &&
                key.find(".mdioReadTotal") != std::string::npos;
          }))
      << "no qsfp.pim<N>.xphy<M>.mdioReadTotal counter published to fb303";
}

namespace {
int getSleepSeconds(PlatformType platformMode) {
  // Minipack Xphy stats are relatively slow, one port xphy stat can take
  // about 3s. And because all ports from the same pim need to wait for
  // the pim EventBase, even though we can make the stats collection across
  // all 8 pims in parallel, we might still need about 3X16=48 seconds for
  // all ports. Therefore, use 1 min for all stats to be collected.
  int sleepSeconds = 5;
  if (platformMode == PlatformType::PLATFORM_MINIPACK ||
      platformMode == PlatformType::PLATFORM_FUJI) {
    sleepSeconds = 60;
  }
  return sleepSeconds;
}

template <typename IOStatsType>
std::string ioStatsString(const IOStatsType& stats) {
  return fmt::format(
      "numReadAttempted = {}, numReadFailed = {}, numWriteAttempted = {}, numWriteFailed = {}, readDownTime = {}, writeDownTime = {}",
      stats.get_numReadAttempted(),
      stats.get_numReadFailed(),
      stats.get_numWriteAttempted(),
      stats.get_numWriteFailed(),
      stats.get_readDownTime(),
      stats.get_writeDownTime());
}
} // namespace

class HwXphyPortStatsCollectionTest : public HwExternalPhyPortTest {
 public:
  const std::vector<phy::ExternalPhy::Feature>& neededFeatures()
      const override {
    static const std::vector<phy::ExternalPhy::Feature> kNeededFeatures = {
        phy::ExternalPhy::Feature::PORT_STATS};
    return kNeededFeatures;
  }

  void runTest() {
    const auto& availableXphyPorts = findAvailableXphyPorts();
    addTestedPorts(availableXphyPorts);
    auto setup = [this, &availableXphyPorts]() {
      for (const auto& [port, profile] : availableXphyPorts) {
        // First program the xphy port
        getHwQsfpEnsemble()->getQsfpServiceHandler()->programXphyPort(
            port, profile);
      }
    };

    auto verify = [&]() {
      getHwQsfpEnsemble()->getQsfpServiceHandler()->updateAllXphyPortsStats();

      auto allStatsCollectionDone = [this, &availableXphyPorts]() {
        for (const auto& [port, _] : availableXphyPorts) {
          if (!getHwQsfpEnsemble()->getPhyManager()->isXphyStatsCollectionDone(
                  port)) {
            return false;
          }
        }
        return true;
      };
      WITH_RETRIES_N_TIMED(
          30 /* retries */,
          std::chrono::milliseconds(10000) /* msBetweenRetry */,
          { EXPECT_EVENTUALLY_TRUE(allStatsCollectionDone()); });

      auto counterKeys = fb303::fbData->getCounterKeys();
      for (const auto& [port, _] : availableXphyPorts) {
        // Verify fb303 has the XPHY FEC counters
        auto portName =
            getHwQsfpEnsemble()->getQsfpServiceHandler()->getPortNameByPortId(
                port);
        CHECK(portName.has_value());
        for (const auto side : {"system", "line"}) {
          auto counterPrefix =
              folly::to<std::string>(*portName, ".xphy.", side);
          for (const auto& counterName :
               {folly::to<std::string>(counterPrefix, ".fec_uncorrectable.sum"),
                folly::to<std::string>(
                    counterPrefix, ".fec_correctable.sum")}) {
            EXPECT_TRUE(
                std::find(
                    counterKeys.begin(), counterKeys.end(), counterName) !=
                counterKeys.end())
                << "counter " << counterName << " not present in fb303";
          }
        }
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(HwXphyPortStatsCollectionTest, checkXphyStatsCollectionDone) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::STATS_COLLECTION});
  runTest();
}

// This is a basic test that doesn't require hardware. It's here in hw_test
// because Fake SDK doesn't support XPHY APIs in BCM SDK yet.
//
// Since this is not a link test, link is not established and all counter
// values are meaningless. We only check struct sanity here.
class HwXphyPortInfoTest : public HwExternalPhyPortTest {
 public:
  const std::vector<phy::ExternalPhy::Feature>& neededFeatures()
      const override {
    static const std::vector<phy::ExternalPhy::Feature> kNeededFeatures = {
        phy::ExternalPhy::Feature::PORT_INFO};
    return kNeededFeatures;
  }

  void runTest() {
    const auto& availableXphyPorts = findAvailableXphyPorts();
    addTestedPorts(availableXphyPorts);
    auto setup = [this, &availableXphyPorts]() {
      for (const auto& [port, profile] : availableXphyPorts) {
        // First program the xphy port
        getHwQsfpEnsemble()->getQsfpServiceHandler()->programXphyPort(
            port, profile);
      }
    };

    auto verify = [&]() {
      /* sleep override */
      sleep(getSleepSeconds(
          getHwQsfpEnsemble()->getWedgeManager()->getPlatformType()));
      for (const auto& [port, _] : availableXphyPorts) {
        // Assemble our own lane list
        std::vector<LaneID> sysLanes, lineLanes;
        auto portConfig =
            getHwQsfpEnsemble()->getPhyManager()->getHwPhyPortConfig(port);
        for (const auto& item : portConfig.config.system.lanes) {
          sysLanes.push_back(item.first);
        }
        for (const auto& item : portConfig.config.line.lanes) {
          lineLanes.push_back(item.first);
        }

        phy::PhyInfo lastPhyInfo;

        auto portInfo = getHwQsfpEnsemble()
                            ->getPhyManager()
                            ->getExternalPhy(port)
                            ->getPortInfo(sysLanes, lineLanes, lastPhyInfo);

        // Sanity check the info we received
        auto chipInfo = portInfo.state()->phyChip();
        EXPECT_EQ(chipInfo->type().value(), phy::DataPlanePhyChipType::XPHY);
        EXPECT_FALSE(chipInfo->name().value().empty());
        if (auto sysState = portInfo.state()->system()) {
          EXPECT_EQ(sysState->side().value(), phy::Side::SYSTEM);
          for (auto const& [lane, laneInfo] :
               sysState->pmd().value().lanes().value()) {
            EXPECT_EQ(lane, laneInfo.lane().value());
          }
        }
        if (auto sysStats = portInfo.stats()->system()) {
          EXPECT_EQ(sysStats->side().value(), phy::Side::SYSTEM);
          for (auto const& [lane, laneInfo] :
               sysStats->pmd().value().lanes().value()) {
            EXPECT_EQ(lane, laneInfo.lane().value());
          }
        }
        auto lineState = portInfo.state()->line();
        EXPECT_EQ(lineState->side().value(), phy::Side::LINE);
        for (auto const& [lane, laneInfo] :
             lineState->pmd().value().lanes().value()) {
          EXPECT_EQ(lane, laneInfo.lane().value());
        }
        auto lineStats = portInfo.stats()->line();
        EXPECT_EQ(lineStats->side().value(), phy::Side::LINE);
        for (auto const& [lane, laneInfo] :
             lineStats->pmd().value().lanes().value()) {
          EXPECT_EQ(lane, laneInfo.lane().value());
        }
        EXPECT_GT(portInfo.state()->timeCollected().value(), 0);
        EXPECT_GT(portInfo.stats()->timeCollected().value(), 0);
      }

      // Verify that there are no PIM errors since all getPortInfo calls above
      // should have succeeded
      auto pimStates = getHwQsfpEnsemble()->getWedgeManager()->getPimStates();
      for (const auto& [pimId, pimState] : pimStates) {
        EXPECT_TRUE(pimState.errors()->empty())
            << "PIM " << pimId << " should not have any errors";
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(HwXphyPortInfoTest, getPortInfo) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::STATS_COLLECTION});
  runTest();
}

class HwXphyPrbsStatsCollectionTest : public HwExternalPhyPortTest {
 public:
  const std::vector<phy::ExternalPhy::Feature>& neededFeatures()
      const override {
    static const std::vector<phy::ExternalPhy::Feature> kNeededFeatures = {
        phy::ExternalPhy::Feature::PRBS, phy::ExternalPhy::Feature::PRBS_STATS};
    return kNeededFeatures;
  }

  void runTest(phy::Side side) {
    const auto& availableXphyPorts = findAvailableXphyPorts();
    addTestedPorts(availableXphyPorts);
    auto setup = [this, &availableXphyPorts, side]() {
      for (const auto& [port, profile] : availableXphyPorts) {
        // First program the xphy port
        getHwQsfpEnsemble()->getQsfpServiceHandler()->programXphyPort(
            port, profile);
        // Then program this xphy port with a common prbs polynominal
        phy::PortPrbsState prbs;
        prbs.enabled() = true;
        static constexpr auto kCommonPolynominal = 9;
        prbs.polynominal() = kCommonPolynominal;
        getHwQsfpEnsemble()->getQsfpServiceHandler()->programXphyPortPrbs(
            port, side, prbs);
      }
    };

    auto verify = [&]() {
      auto platformMode =
          getHwQsfpEnsemble()->getWedgeManager()->getPlatformType();
      int sleepSeconds = getSleepSeconds(platformMode);

      // The first update stats will set the prbs stats to lock and accumulated
      // errors to 0
      getHwQsfpEnsemble()->getQsfpServiceHandler()->updateAllXphyPortsStats();
      /* sleep override */
      sleep(sleepSeconds);
      // The second update stats will provide the prbs stats since the first
      // update, which should see some bers
      getHwQsfpEnsemble()->getQsfpServiceHandler()->updateAllXphyPortsStats();
      /* sleep override */
      sleep(sleepSeconds);

      // Now check the stats collection future job is done.
      for (const auto& [port, _] : availableXphyPorts) {
        EXPECT_TRUE(
            getHwQsfpEnsemble()->getPhyManager()->isPrbsStatsCollectionDone(
                port))
            << "port:" << port << " prbs stats collection is not done";

        // For Minipack family we need to enable both sides prbs for actually
        // seeing any error bits. Like asic and xphy system side.
        // Since this is only qsfp_hw_test, it won't be able to trigger
        // asic prbs programming without using wedge_agent.
        // Skip the prbs stats check for Minipack family
        if (platformMode == PlatformType::PLATFORM_MINIPACK ||
            platformMode == PlatformType::PLATFORM_FUJI) {
          continue;
        }

        // Also try to get prbs stats
        const auto& prbsStats =
            getHwQsfpEnsemble()->getPhyManager()->getPortPrbsStats(port, side);
        EXPECT_TRUE(!prbsStats.empty());
        const auto& phyPortConfig =
            getHwQsfpEnsemble()->getPhyManager()->getHwPhyPortConfig(port);
        auto modulation =
            (side == phy::Side::SYSTEM
                 ? *phyPortConfig.profile.system.modulation()
                 : *phyPortConfig.profile.line.modulation());
        auto now = std::time(nullptr);
        for (const auto& lanePrbsStats : prbsStats) {
          EXPECT_TRUE(
              now - *lanePrbsStats.timeSinceLastLocked() >= sleepSeconds);
          EXPECT_TRUE(
              now - *lanePrbsStats.timeSinceLastClear() >= sleepSeconds);
          // NRZ on short channels can have 0 errors.
          // Expecting PAM4 to be BER 0 is not reasonable.
          // xphy and optics are both on the pim so the channel is short
          // The system side link from ASIC<->XPHY goes through a connector and
          // that adds loss
          if (modulation == phy::IpModulation::PAM4 &&
              side == phy::Side::SYSTEM) {
            EXPECT_TRUE(*lanePrbsStats.ber() > 0);
          }
          EXPECT_TRUE(*lanePrbsStats.maxBer() >= *lanePrbsStats.ber());
          EXPECT_TRUE(*lanePrbsStats.locked());
          EXPECT_TRUE(*lanePrbsStats.numLossOfLock() == 0);
        }
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(HwXphyPrbsStatsCollectionTest, getSystemPrbsStats) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::XPHY_PRBS_CONTROL});
  runTest(phy::Side::SYSTEM);
}
TEST_F(HwXphyPrbsStatsCollectionTest, getLinePrbsStats) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::XPHY_PRBS_CONTROL});
  runTest(phy::Side::LINE);
}

TEST_F(HwTest, transceiverIOStats) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::STATS_COLLECTION});
  /*
   * 1. Refresh transceivers
   * 2. Get the transceiver IO stats
   * 3. Refresh transceivers again
   * 4. Get the transceiver IO stats again
   * 5. Compare the stats.
   *    The read attempted counters should increment for all transceivers.
   *    The write attempted counters should increment only for CMIS optics as we
   *    change pages during refresh on them.
   *    readFailed and writeFailed should not increment at all
   */
  auto wedgeMgr = getHwQsfpEnsemble()->getWedgeManager();
  auto qsfpServiceHandler = getHwQsfpEnsemble()->getQsfpServiceHandler();
  qsfpServiceHandler->refreshStateMachines();
  auto tcvrs = utility::legacyTransceiverIds(
      utility::getCabledPortTranceivers(getHwQsfpEnsemble()));
  addTestedTransceiverIds(tcvrs);

  std::unordered_map<int32_t, TransceiverStats> tcvrStatsBefore;

  for (auto tcvrID : tcvrs) {
    auto tcvrInfo = wedgeMgr->getTransceiverInfo(TransceiverID(tcvrID));
    auto& tcvrStats = *tcvrInfo.tcvrStats();
    CHECK(tcvrStats.stats());
    tcvrStatsBefore[tcvrID] = *tcvrStats.stats();
  }

  qsfpServiceHandler->refreshStateMachines();
  for (auto tcvrID : tcvrs) {
    auto tcvrInfo = wedgeMgr->getTransceiverInfo(TransceiverID(tcvrID));
    auto& tcvrState = *tcvrInfo.tcvrState();
    auto& tcvrStats = *tcvrInfo.tcvrStats();
    TransceiverStats& tcvrStatsAfter = tcvrStats.stats().ensure();

    XLOG(DBG3) << "tcvrID: " << tcvrID << " Stats Before Refresh: "
               << ioStatsString<TransceiverStats>(tcvrStatsBefore[tcvrID]);
    XLOG(DBG3) << "tcvrID: " << tcvrID << " Stats After Refresh: "
               << ioStatsString<TransceiverStats>(tcvrStatsAfter);
    EXPECT_EQ(tcvrStatsAfter.readDownTime().value(), 0);
    EXPECT_EQ(tcvrStatsAfter.writeDownTime().value(), 0);
    EXPECT_EQ(
        tcvrStatsAfter.numReadFailed().value(),
        tcvrStatsBefore[tcvrID].numReadFailed().value());
    EXPECT_EQ(
        tcvrStatsAfter.numWriteFailed().value(),
        tcvrStatsBefore[tcvrID].numWriteFailed().value());
    EXPECT_GT(
        tcvrStatsAfter.numReadAttempted().value(),
        tcvrStatsBefore[tcvrID].numReadAttempted().value());
    if (TransceiverManager::opticalOrActiveCmisCable(tcvrState)) {
      EXPECT_GT(
          tcvrStatsAfter.numWriteAttempted().value(),
          tcvrStatsBefore[tcvrID].numWriteAttempted().value());
    } else {
      EXPECT_GE(
          tcvrStatsAfter.numWriteAttempted().value(),
          tcvrStatsBefore[tcvrID].numWriteAttempted().value());
    }
  }
}

class PhyIOTest : public HwExternalPhyPortTest {
 public:
  const std::vector<phy::ExternalPhy::Feature>& neededFeatures()
      const override {
    static const std::vector<phy::ExternalPhy::Feature> kNeededFeatures = {
        phy::ExternalPhy::Feature::PORT_STATS};
    return kNeededFeatures;
  }
};

TEST_F(PhyIOTest, phyIOStats) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::STATS_COLLECTION});
  /*
   * 1. Update all phy stats
   * 2. Get the phy IO stats
   * 3. Update all phy stats again
   * 4. Get the phy IO stats again
   * 5. Compare the stats.
   *    The read and write attempted counters should increment
   *    readFailed and writeFailed should not increment at all
   */
  std::unordered_map<PortID, phy::PhyInfo> phyInfoBefore;
  const auto& availableXphyPorts = findAvailableXphyPorts();
  addTestedPorts(availableXphyPorts);

  auto allPhyStatsUpdated =
      [&](std::unordered_map<PortID, phy::PhyInfo>& phyInfoToCompare) {
        for (auto [portId, _] : availableXphyPorts) {
          try {
            phy::PhyInfo phyInfo;
            getHwQsfpEnsemble()->getQsfpServiceHandler()->getXphyInfo(
                phyInfo, portId);
            // Make sure the timestamp advances
            if (phyInfoToCompare.find(portId) != phyInfoToCompare.end() &&
                folly::copy(phyInfo.stats()->timeCollected().value()) <=
                    folly::copy(
                        phyInfoToCompare[portId]
                            .stats()
                            ->timeCollected()
                            .value())) {
              return false;
            }
          } catch (...) {
            return false;
          }
        }
        return true;
      };
  auto waitForAllPhyStatsToBeUpdated =
      [allPhyStatsUpdated](
          std::unordered_map<PortID, phy::PhyInfo>& phyInfoToCompare) {
        WITH_RETRIES_N_TIMED(
            20 /* retries */,
            std::chrono::milliseconds(10000) /* msBetweenRetry */,
            { ASSERT_EVENTUALLY_TRUE(allPhyStatsUpdated(phyInfoToCompare)); });
      };

  getHwQsfpEnsemble()->getQsfpServiceHandler()->updateAllXphyPortsStats();
  // updateAllXphyPortsStats asynchronously updates phy stats, so wait until
  // stats are updated
  waitForAllPhyStatsToBeUpdated(phyInfoBefore);

  for (auto [portId, _] : availableXphyPorts) {
    phy::PhyInfo phyInfo;
    getHwQsfpEnsemble()->getQsfpServiceHandler()->getXphyInfo(phyInfo, portId);
    phyInfoBefore[portId] = phyInfo;
  }

  getHwQsfpEnsemble()->getQsfpServiceHandler()->updateAllXphyPortsStats();
  // updateAllXphyPortsStats asynchronously updates phy stats, so wait until
  // stats are updated
  waitForAllPhyStatsToBeUpdated(phyInfoBefore);
  for (auto [portId, _] : availableXphyPorts) {
    phy::PhyInfo phyInfo;
    getHwQsfpEnsemble()->getQsfpServiceHandler()->getXphyInfo(phyInfo, portId);
    auto& phyStats = *phyInfo.stats();
    auto ioStatsAfter = *phyStats.ioStats();
    auto ioStatsBefore = *phyInfoBefore[portId].stats()->ioStats();

    XLOG(DBG3) << "portID: " << portId
               << " Stats Before: " << ioStatsString<IOStats>(ioStatsBefore);
    XLOG(DBG3) << "portID: " << portId
               << " Stats After: " << ioStatsString<IOStats>(ioStatsAfter);
    EXPECT_EQ(ioStatsAfter.readDownTime().value(), 0);
    EXPECT_EQ(ioStatsAfter.writeDownTime().value(), 0);
    EXPECT_EQ(
        ioStatsAfter.numReadFailed().value(),
        ioStatsBefore.numReadFailed().value());
    EXPECT_EQ(
        ioStatsAfter.numWriteFailed().value(),
        ioStatsBefore.numWriteFailed().value());
    EXPECT_GT(
        ioStatsAfter.numReadAttempted().value(),
        ioStatsBefore.numReadAttempted().value());
    EXPECT_GT(
        ioStatsAfter.numWriteAttempted().value(),
        ioStatsBefore.numWriteAttempted().value());
  }
}
} // namespace facebook::fboss
