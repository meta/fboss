/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/HwSwitchWarmBootHelper.h"
#include "fboss/agent/hw/sai/api/AdapterKeySerializers.h"
#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"
#include "fboss/agent/hw/switch_asics/FakeAsic.h"
#include "fboss/agent/platforms/sai/SaiFakePlatform.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/LoadBalancer.h"

#include <folly/Synchronized.h>
#include <folly/logging/LogHandler.h>
#include <folly/logging/LogHandlerConfig.h>
#include <folly/logging/LogMessage.h>
#include <folly/logging/LoggerDB.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace facebook::fboss;

class SwitchManagerTest : public ManagerTestBase {};

namespace {

class HashDetachFakeAsic : public FakeAsic {
 public:
  HashDetachFakeAsic(
      std::optional<int64_t> switchId,
      const cfg::SwitchInfo& switchInfo)
      : FakeAsic(switchId, switchInfo) {}

  bool isSupported(Feature feature) const override {
    if (feature == Feature::SAI_HASH_FIELDS_CLEAR_BEFORE_SET) {
      return false;
    }
    return FakeAsic::isSupported(feature);
  }
};

class HashDetachFakePlatform : public SaiFakePlatform {
 public:
  explicit HashDetachFakePlatform(
      std::unique_ptr<PlatformProductInfo> productInfo)
      : SaiFakePlatform(std::move(productInfo)) {}

  HwAsic* getAsic() const override {
    return asic_.get();
  }

 private:
  void setupAsic(
      std::optional<int64_t> switchId,
      const cfg::SwitchInfo& switchInfo,
      std::optional<HwAsic::FabricNodeRole> /*fabricNodeRole*/) override {
    asic_ = std::make_unique<HashDetachFakeAsic>(switchId, switchInfo);
  }

  std::unique_ptr<HashDetachFakeAsic> asic_;
};

class SaiHashDetachTest : public ManagerTestBase {
 protected:
  std::unique_ptr<SaiPlatform> createSaiPlatformForTest(
      std::unique_ptr<PlatformProductInfo> productInfo) override {
    return std::make_unique<HashDetachFakePlatform>(std::move(productInfo));
  }
};

std::shared_ptr<LoadBalancer> makeLoadBalancer(
    LoadBalancerID id,
    LoadBalancer::IPv4Fields ipv4Fields,
    LoadBalancer::TransportFields transportFields) {
  return std::make_shared<LoadBalancer>(
      id,
      cfg::HashingAlgorithm::CRC,
      65515,
      std::move(ipv4Fields),
      LoadBalancer::IPv6Fields{},
      std::move(transportFields),
      LoadBalancer::MPLSFields{},
      LoadBalancer::UdfGroupIds{});
}

} // namespace

TEST_F(SaiHashDetachTest, removeLoadBalancerDetachesHashesBeforeRelease) {
  auto ecmpLoadBalancer = makeLoadBalancer(
      LoadBalancerID::ECMP,
      {cfg::IPv4Field::SOURCE_ADDRESS},
      {cfg::TransportField::SOURCE_PORT});
  auto lagLoadBalancer = makeLoadBalancer(
      LoadBalancerID::AGGREGATE_PORT,
      {cfg::IPv4Field::DESTINATION_ADDRESS},
      {cfg::TransportField::DESTINATION_PORT});

  auto& switchManager = saiManagerTable->switchManager();
  switchManager.addOrUpdateLoadBalancer(ecmpLoadBalancer);
  switchManager.addOrUpdateLoadBalancer(lagLoadBalancer);

  auto& fakeSwitch =
      FakeSai::getInstance()->switchManager.get(switchManager.getSwitchSaiId());
  const auto ecmpHashId = fakeSwitch.ecmpHashV4Id();
  const auto lagHashId = fakeSwitch.lagHashV4Id();
  ASSERT_NE(ecmpHashId, SAI_NULL_OBJECT_ID);
  ASSERT_NE(lagHashId, SAI_NULL_OBJECT_ID);
  ASSERT_TRUE(FakeSai::getInstance()->hashManager.exists(ecmpHashId));
  ASSERT_TRUE(FakeSai::getInstance()->hashManager.exists(lagHashId));

  switchManager.removeLoadBalancer(ecmpLoadBalancer);
  EXPECT_EQ(fakeSwitch.ecmpHashV4Id(), SAI_NULL_OBJECT_ID);
  EXPECT_FALSE(FakeSai::getInstance()->hashManager.exists(ecmpHashId));

  switchManager.removeLoadBalancer(lagLoadBalancer);
  EXPECT_EQ(fakeSwitch.lagHashV4Id(), SAI_NULL_OBJECT_ID);
  EXPECT_FALSE(FakeSai::getInstance()->hashManager.exists(lagHashId));
}

TEST_F(SwitchManagerTest, serDeser) {
  auto saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto adapterKeys = saiSwitch->toFollyDynamic();
  std::vector<typename SaiSwitchTraits::AdapterKey> switchKeys;
  for (auto obj : adapterKeys[kAdapterKeys][saiObjectTypeToString(
           SaiSwitchTraits::ObjectType)]) {
    switchKeys.push_back(fromFollyDynamic<SaiSwitchTraits>(obj));
  }
  EXPECT_EQ(
      std::vector<SaiSwitchTraits::AdapterKey>{saiSwitch->getSaiSwitchId()},
      switchKeys);
}

namespace facebook::fboss {
namespace {

constexpr uint16_t kWarmBootLatchPortId = 42;

class PortLatchFakeAsic : public FakeAsic {
 public:
  PortLatchFakeAsic(
      std::optional<int64_t> switchId,
      const cfg::SwitchInfo& switchInfo)
      : FakeAsic(switchId, switchInfo) {}

  bool isSupported(Feature feature) const override {
    if (feature == Feature::PORT_LATCH_STATUS) {
      return true;
    }
    return FakeAsic::isSupported(feature);
  }
};

class PortLatchFakePlatform : public SaiFakePlatform {
 public:
  explicit PortLatchFakePlatform(
      std::unique_ptr<PlatformProductInfo> productInfo)
      : SaiFakePlatform(std::move(productInfo)) {}

  HwAsic* getAsic() const override {
    return asic_.get();
  }

 private:
  void setupAsic(
      std::optional<int64_t> switchId,
      const cfg::SwitchInfo& switchInfo,
      std::optional<HwAsic::FabricNodeRole> /*fabricNodeRole*/) override {
    asic_ = std::make_unique<PortLatchFakeAsic>(switchId, switchInfo);
  }

  std::unique_ptr<PortLatchFakeAsic> asic_;
};

class ThreadSafeLogCapture : public folly::LogHandler {
 public:
  void handleMessage(
      const folly::LogMessage& message,
      const folly::LogCategory* /*category*/) override {
    messages_.wlock()->push_back(message.getMessage());
  }

  void flush() override {}

  folly::LogHandlerConfig getConfig() const override {
    return folly::LogHandlerConfig{"capture"};
  }

  std::vector<std::string> messages() const {
    return messages_.copy();
  }

  bool contains(const std::string& substring) const {
    const auto snapshot = messages();
    return std::any_of(
        snapshot.begin(), snapshot.end(), [&](const auto& message) {
          return message.find(substring) != std::string::npos;
        });
  }

 private:
  folly::Synchronized<std::vector<std::string>> messages_;
};

class ScopedRootLogHandler {
 public:
  explicit ScopedRootLogHandler(std::shared_ptr<folly::LogHandler> handler)
      : rootCategory_(folly::LoggerDB::get().getCategory("")),
        originalHandlers_(rootCategory_->getHandlers()) {
    rootCategory_->addHandler(std::move(handler));
  }

  ~ScopedRootLogHandler() {
    rootCategory_->replaceHandlers(std::move(originalHandlers_));
  }

 private:
  folly::LogCategory* rootCategory_;
  std::vector<std::shared_ptr<folly::LogHandler>> originalHandlers_;
};

folly::dynamic makeLatchWarmBootState() {
  folly::dynamic portState = folly::dynamic::object;
  portState["portId"] = static_cast<int64_t>(kWarmBootLatchPortId);
  portState["currentStatus"] = false;
  portState["changed"] = true;
  return folly::dynamic::array(std::move(portState));
}

} // namespace

class PortLatchStatusTest : public ManagerTestBase {
 protected:
  std::unique_ptr<SaiPlatform> createSaiPlatformForTest(
      std::unique_ptr<PlatformProductInfo> productInfo) override {
    return std::make_unique<PortLatchFakePlatform>(std::move(productInfo));
  }

  void TearDown() override {
    if (!gracefulExitCalled_) {
      ManagerTestBase::TearDown();
      return;
    }
    pseudoWarmBootExitAndStoreReload();
    saiStore->release();
    FakeSai::clear();
    programmedState.reset();
  }

  void markGracefulExitCalled() {
    gracefulExitCalled_ = true;
  }

 private:
  bool gracefulExitCalled_{false};
};

class SaiSwitchWarmBootTest : public PortLatchStatusTest {
 public:
  void gracefulExitLocked() {
    auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
    std::lock_guard<std::mutex> lock(saiSwitch->saiSwitchMutex_);
    saiSwitch->gracefulExitLocked(lock);
    markGracefulExitCalled();
  }

  void restoreSavedLatchState(
      const std::optional<folly::dynamic>& latchWarmBootState,
      bool sdkSupportsLatchState) {
    static_cast<SaiSwitch*>(saiPlatform->getHwSwitch())
        ->restoreExtOperStatusLatchWarmBootBaseline(
            latchWarmBootState, sdkSupportsLatchState);
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  const SaiPortManager::ExtOperStatusLatchWarmBootState& latchWarmBootBaseline()
      const {
    return static_cast<SaiSwitch*>(saiPlatform->getHwSwitch())
        ->extOperStatusLatchWarmBootBaseline_;
  }
#endif
};

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
TEST_F(
    SaiSwitchWarmBootTest,
    gracefulExitSerializesExtOperStatusLatchWarmBootState) {
  const auto& testPort = testInterfaces[0].remoteHosts[0].port;
  auto iphySwPort = makePort(testPort);
  auto swPort = makePort(testPort, std::nullopt, true);
  const auto medium = iphySwPort->getProfileConfig().medium().value();
  auto systemProfileConfig = swPort->getProfileConfig();
  systemProfileConfig.medium() = medium;
  swPort->setProfileConfig(systemProfileConfig);
  auto lineProfileConfig = *swPort->getLineProfileConfig();
  lineProfileConfig.medium() = medium;
  swPort->setLineProfileConfig(lineProfileConfig);
  auto& portManager = saiManagerTable->portManager();
  portManager.addPort(swPort);

  const auto* handle = portManager.getPortHandle(swPort->getID());
  ASSERT_NE(handle, nullptr);
  auto& fakePort =
      FakeSai::getInstance()->portManager.get(handle->port->adapterKey());
  fakePort.portExtOperStatusLatch = {false, true};

  gracefulExitLocked();

  const auto savedState = saiPlatform->getWarmBootHelper()->getWarmBootState();
  const auto latchState = savedState.find(kExtOperStatusLatchWarmBootState);
  ASSERT_NE(latchState, savedState.items().end());
  ASSERT_EQ(latchState->second.size(), 1);
  const auto& savedPortState = latchState->second[0];
  EXPECT_EQ(
      savedPortState["portId"].asInt(), static_cast<uint16_t>(swPort->getID()));
  EXPECT_FALSE(savedPortState["currentStatus"].asBool());
  EXPECT_TRUE(savedPortState["changed"].asBool());
}

TEST_F(SaiSwitchWarmBootTest, initDeserializesExtOperStatusLatchWarmBootState) {
  restoreSavedLatchState(makeLatchWarmBootState(), true);
  const auto& baseline = latchWarmBootBaseline();
  ASSERT_EQ(baseline.size(), 1);
  const auto portId = PortID(kWarmBootLatchPortId);
  ASSERT_NE(baseline.find(portId), baseline.end());
  EXPECT_FALSE(baseline.at(portId).current_status);
  EXPECT_TRUE(baseline.at(portId).changed);
}
#endif

TEST_F(SaiSwitchWarmBootTest, restoreWarnsWhenSdkCannotConsumeSavedLatchState) {
  auto logCapture = std::make_shared<ThreadSafeLogCapture>();
  const std::string expectedWarning =
      "Ignoring saved extOperStatusLatchWarmBootState because this build "
      "cannot consume the latch state";
  {
    ScopedRootLogHandler captureScope(logCapture);
    restoreSavedLatchState(makeLatchWarmBootState(), false);
  }
  EXPECT_TRUE(logCapture->contains(expectedWarning));
}

} // namespace facebook::fboss
