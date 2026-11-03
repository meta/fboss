// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once
#include <folly/logging/xlog.h>
#include <memory>
#include "fboss/lib/bsp/BspLedContainer.h"
#include "fboss/lib/bsp/BspPhyContainer.h"
#include "fboss/lib/bsp/BspPlatformMapping.h"
#include "fboss/lib/bsp/BspTransceiverContainer.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"
#include "fboss/lib/fpga/MultiPimPlatformPimContainer.h"

namespace facebook {
namespace fboss {

class BspPimContainer : public MultiPimPlatformPimContainer {
 public:
  explicit BspPimContainer(BspPimMapping& bspPimMapping);
  void createBspLedContainers();
  const BspTransceiverContainer* getTransceiverContainer(int tcvrID) const;
  const BspPhyContainer* getPhyContainerFromPhyID(int phyID) const;
  const BspPhyContainer* getPhyContainerFromMdioID(int mdioControllerID) const;
  const std::map<uint32_t, std::pair<const BspLedContainer*, std::set<int>>>
  getLedContainer(int tcvrID) const;

  bool isTcvrPresent(int tcvrID) const;
  void initAllTransceivers() const;
  void clearAllTransceiverReset() const;
  void initTransceiver(int tcvrID) const;
  void holdTransceiverReset(int tcvrID) const;
  void releaseTransceiverReset(int tcvrID) const;

  // PHY reset methods
  void initAllPhyIOControllers(bool forceReset = true) const;
  void initAllPhys() const;

  void tcvrRead(
      unsigned int tcvrID,
      const TransceiverAccessParameter& param,
      uint8_t* buf) const;
  void tcvrWrite(
      unsigned int tcvrID,
      const TransceiverAccessParameter& param,
      const uint8_t* buf) const;
  const I2cControllerStats getI2cControllerStats(int tcvrID) const;
  void i2cTimeProfilingStart(unsigned int tcvrID) const;
  void i2cTimeProfilingEnd(unsigned int tcvrID) const;
  std::pair<uint64_t, uint64_t> getI2cTimeProfileMsec(
      unsigned int tcvrID) const;

  virtual bool isPimPresent() const override {
    return true;
  };
  virtual void initHW(bool forceReset = false) override {};
  folly::EventBase* FOLLY_NULLABLE getIOEventBase(unsigned int tcvrID) const {
    if (tcvrToIOEvb_.find(tcvrID) == tcvrToIOEvb_.end()) {
      return nullptr;
    }
    return tcvrToIOEvb_.at(tcvrID);
  }
  folly::EventBase* FOLLY_NULLABLE getPhyIOEventBase(unsigned int phyID) const {
    if (phyToIOEvb_.find(phyID) == phyToIOEvb_.end()) {
      return nullptr;
    }
    return phyToIOEvb_.at(phyID);
  }
  virtual ~BspPimContainer() override;

 private:
  std::unordered_map<int, std::unique_ptr<BspTransceiverContainer>>
      tcvrContainers_;
  std::unordered_map<int, std::unique_ptr<BspPhyContainer>> phyContainers_;
  // map of io controller id to BspPhyIO
  std::unordered_map<int, std::unique_ptr<BspPhyIO>> phyIOControllers_;
  // tcvrId to LED mapping
  std::unordered_map<int, std::unique_ptr<BspLedContainer>> ledContainers_;
  BspPimMapping bspPimMapping_;
  std::unordered_map<
      std::string /* ioControllerId*/,
      std::
          pair<std::unique_ptr<folly::EventBase>, std::unique_ptr<std::thread>>>
      ioToEvbThread_;
  std::unordered_map<unsigned int, folly::EventBase*> tcvrToIOEvb_;

  // Map of PHY I/O controller ID to EventBase and thread for PHY operations
  std::unordered_map<
      int /* phyIOControllerId*/,
      std::
          pair<std::unique_ptr<folly::EventBase>, std::unique_ptr<std::thread>>>
      phyIOToEvbThread_;
  // Map of PHY ID to EventBase for quick lookup
  std::unordered_map<unsigned int, folly::EventBase*> phyToIOEvb_;
};

} // namespace fboss
} // namespace facebook
