// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspPimContainer.h"

#include <fmt/format.h>
#include "fboss/agent/FbossError.h"
#include "fboss/lib/bsp/BspLedContainer.h"
#include "fboss/lib/bsp/BspPhyContainer.h"
#include "fboss/lib/bsp/BspTransceiverContainer.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"

namespace facebook {
namespace fboss {

BspPimContainer::BspPimContainer(BspPimMapping& bspPimMapping)
    : bspPimMapping_(bspPimMapping) {
  for (auto tcvrMapping : *bspPimMapping.tcvrMapping()) {
    auto ioControllerId = *tcvrMapping.second.io()->controllerId();
    // Create an event base and thread if not already created for this IO
    // controller
    if (ioToEvbThread_.find(ioControllerId) == ioToEvbThread_.end()) {
      ioToEvbThread_[ioControllerId] = {};
      ioToEvbThread_[ioControllerId].first =
          std::make_unique<folly::EventBase>();
      auto evb = ioToEvbThread_[ioControllerId].first.get();
      ioToEvbThread_[ioControllerId].second =
          std::make_unique<std::thread>([evb] { evb->loopForever(); });
      XLOG(DBG3) << "Created EVB for " << ioControllerId;
    }
    tcvrToIOEvb_[tcvrMapping.first] =
        ioToEvbThread_[ioControllerId].first.get();
    tcvrContainers_.emplace(
        tcvrMapping.first,
        std::make_unique<BspTransceiverContainer>(tcvrMapping.second));
  }
  // Create PhyIO controllers
  for (auto controller : *bspPimMapping.phyIOControllers()) {
    phyIOControllers_[controller.first] =
        std::make_unique<BspPhyIO>(*bspPimMapping.pimID(), controller.second);
  }
  // Create PHY containers, 1 per PHY in a PIM
  for (auto phyMapping : *bspPimMapping.phyMapping()) {
    auto phyIOControllerId = *phyMapping.second.phyIOControllerId();
    CHECK(phyIOControllers_.find(phyIOControllerId) != phyIOControllers_.end());

    // Create an event base and thread if not already created for this PHY I/O
    // controller.
    if (phyIOToEvbThread_.find(phyIOControllerId) == phyIOToEvbThread_.end()) {
      phyIOToEvbThread_[phyIOControllerId] = {};
      phyIOToEvbThread_[phyIOControllerId].first =
          std::make_unique<folly::EventBase>();
      auto evb = phyIOToEvbThread_[phyIOControllerId].first.get();
      phyIOToEvbThread_[phyIOControllerId].second =
          std::make_unique<std::thread>([evb] { evb->loopForever(); });
      XLOG(DBG3) << "Created PHY EventBase for PHY I/O controller "
                 << phyIOControllerId;
    }

    // Map this PHY ID to the EventBase of its controller
    phyToIOEvb_[phyMapping.first] =
        phyIOToEvbThread_[phyIOControllerId].first.get();

    phyContainers_.emplace(
        phyMapping.first,
        std::make_unique<BspPhyContainer>(
            *bspPimMapping.pimID(),
            phyMapping.second,
            phyIOControllers_[phyIOControllerId].get()));
  }
}

void BspPimContainer::createBspLedContainers() {
  for (auto ledMapping : *bspPimMapping_.ledMapping()) {
    ledContainers_.emplace(
        ledMapping.first, std::make_unique<BspLedContainer>(ledMapping.second));
  }
}

const BspTransceiverContainer* BspPimContainer::getTransceiverContainer(
    int tcvrID) const {
  return tcvrContainers_.at(tcvrID).get();
}

const BspPhyContainer* BspPimContainer::getPhyContainerFromPhyID(
    int phyID) const {
  return phyContainers_.at(phyID).get();
}

const BspPhyContainer* BspPimContainer::getPhyContainerFromMdioID(
    int mdioControllerID) const {
  for (auto phy : *bspPimMapping_.phyMapping()) {
    if (mdioControllerID == *phy.second.phyIOControllerId()) {
      return phyContainers_.at(phy.first).get();
    }
  }
  throw FbossError(
      fmt::format(
          "Couldn't find phy container for mdioID {:d}, PimID {:d}",
          mdioControllerID,
          *bspPimMapping_.pimID()));
}

// Here, we also need the ledcontainer and the lanes it controls.
const std::map<uint32_t, std::pair<const BspLedContainer*, std::set<int>>>
BspPimContainer::getLedContainer(int tcvrID) const {
  std::map<uint32_t, std::pair<const BspLedContainer*, std::set<int>>>
      ledContainers;

  if (bspPimMapping_.tcvrMapping().value().find(tcvrID) ==
      bspPimMapping_.tcvrMapping().value().end()) {
    XLOG(ERR) << "Transceiver mapping could not be found for " << tcvrID;
    return {};
  }

  for (auto tcvrLaneToLed : bspPimMapping_.tcvrMapping()
                                .value()
                                .at(tcvrID)
                                .tcvrLaneToLedId()
                                .value()) {
    uint32_t ledId = tcvrLaneToLed.second;
    if (ledContainers.find(ledId) == ledContainers.end()) {
      ledContainers[ledId] = std::make_pair(
          ledContainers_.at(ledId).get(), std::set<int>{tcvrLaneToLed.first});
    } else {
      ledContainers[ledId].second.insert(tcvrLaneToLed.first);
    }
  }
  return ledContainers;
}

void BspPimContainer::initAllTransceivers() const {
  for (auto tcvrContainerIt = tcvrContainers_.begin();
       tcvrContainerIt != tcvrContainers_.end();
       tcvrContainerIt++) {
    tcvrContainerIt->second->initTransceiver();
  }
}

void BspPimContainer::clearAllTransceiverReset() const {
  for (auto tcvrContainerIt = tcvrContainers_.begin();
       tcvrContainerIt != tcvrContainers_.end();
       tcvrContainerIt++) {
    tcvrContainerIt->second->releaseTransceiverReset();
  }
}

void BspPimContainer::initTransceiver(int tcvrID) const {
  getTransceiverContainer(tcvrID)->initTcvr();
}

void BspPimContainer::holdTransceiverReset(int tcvrID) const {
  getTransceiverContainer(tcvrID)->holdTransceiverReset();
}

void BspPimContainer::releaseTransceiverReset(int tcvrID) const {
  getTransceiverContainer(tcvrID)->releaseTransceiverReset();
}

void BspPimContainer::initAllPhyIOControllers(bool forceReset) const {
  XLOG(INFO) << fmt::format(
      "BspPimContainerTrace: {} initializing all PHY IO controllers for PIM {:d}",
      __func__,
      *bspPimMapping_.pimID());
  for (const auto& [controllerId, phyIO] : phyIOControllers_) {
    phyIO->init(forceReset);
  }
}

void BspPimContainer::initAllPhys() const {
  XLOG(INFO) << fmt::format(
      "BspPimContainerTrace: {} initializing all PHYs for PIM {:d}",
      __func__,
      *bspPimMapping_.pimID());
  for (const auto& [phyID, phyContainer] : phyContainers_) {
    phyContainer->initPhy(true);
  }
}

bool BspPimContainer::isTcvrPresent(int tcvrID) const {
  return getTransceiverContainer(tcvrID)->isTcvrPresent();
}

void BspPimContainer::tcvrRead(
    unsigned int tcvrID,
    const TransceiverAccessParameter& param,
    uint8_t* buf) const {
  return getTransceiverContainer(tcvrID)->tcvrRead(param, buf);
}

void BspPimContainer::tcvrWrite(
    unsigned int tcvrID,
    const TransceiverAccessParameter& param,
    const uint8_t* buf) const {
  return getTransceiverContainer(tcvrID)->tcvrWrite(param, buf);
}

const I2cControllerStats BspPimContainer::getI2cControllerStats(
    int tcvrID) const {
  return getTransceiverContainer(tcvrID)->getI2cControllerStats();
}

void BspPimContainer::i2cTimeProfilingStart(unsigned int tcvrID) const {
  getTransceiverContainer(tcvrID)->i2cTimeProfilingStart();
}

void BspPimContainer::i2cTimeProfilingEnd(unsigned int tcvrID) const {
  getTransceiverContainer(tcvrID)->i2cTimeProfilingEnd();
}

std::pair<uint64_t, uint64_t> BspPimContainer::getI2cTimeProfileMsec(
    unsigned int tcvrID) const {
  return getTransceiverContainer(tcvrID)->getI2cTimeProfileMsec();
}

BspPimContainer::~BspPimContainer() {
  // Gracefully terminate transceiver I/O EventBase threads
  for (auto& evb : ioToEvbThread_) {
    evb.second.first->terminateLoopSoon();
    evb.second.second->join();
  }

  // Gracefully terminate PHY I/O EventBase threads
  for (auto& evb : phyIOToEvbThread_) {
    evb.second.first->terminateLoopSoon();
    evb.second.second->join();
  }
}

} // namespace fboss
} // namespace facebook
