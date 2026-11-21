// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

/**
 * @def AGENT_TUNNEL_MGR_FRIEND_TESTS
 * @brief Macro defining friend declarations for AgentTunnelMgrTest
 *
 * This macro contains friend class declarations needed to access private
 * members of TunManager for testing.
 */
#define AGENT_TUNNEL_MGR_FRIEND_TESTS                      \
  friend class AgentTunnelMgrTest;                         \
  FRIEND_TEST(AgentTunnelMgrTest, checkProbedDataCleanup); \
  FRIEND_TEST(AgentTunnelMgrTest, checkProbedDataCleanupInterfaceDown);

#include <folly/ScopeGuard.h>
#include <string>
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/TunManager.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/TunnelMgrTestUtils.h"

namespace facebook::fboss {

class AgentTunnelMgrTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::CPU_RX_TX};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_tun_intf = true;
    FLAGS_cleanup_probed_kernel_data = true;
  }

 public:
  void SetUp() override {
    // Check for warmboot flag before any processing using the helper function
    bool isWarmbootSetup = isWarmbootSetupRequested();

    XLOG(INFO) << "Setup requested for "
               << (isWarmbootSetup ? "warmboot" : "coldboot");

    // Clear kernel entries for coldboot only, BEFORE agent initialization
    if (isWarmbootSetup) {
      XLOG(INFO)
          << "Coldboot detected: clearing all kernel entries before agent initialization";
      utility::clearAllKernelEntries();
    } else {
      XLOG(INFO) << "Warmboot detected: skipping kernel entries cleanup";
    }

    // Now call parent SetUp to initialize the agent
    AgentHwTest::SetUp();
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    std::vector<PortID> ports = {
        ensemble.masterLogicalPortIds()[0], ensemble.masterLogicalPortIds()[1]};
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ports, true /*interfaceHasSubnet*/);
    return cfg;
  }

  std::vector<std::string> changeKernelIPAddress(
      cfg::SwitchConfig& config,
      bool isIpv4) {
    std::vector<std::string> intfIPList;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      if (config.interfaces()[i].scope() == cfg::Scope::GLOBAL) {
        continue;
      }

      // change ipv4 address of the interface
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);
        if (isIpv4) {
          if (intfIP.find("::") == std::string::npos) {
            auto ipDecimal = fmt::format("{}", i + 1);
            config.interfaces()[i].ipAddresses()[j] =
                fmt::format("{}.2.2.2/24", ipDecimal);
            intfIPList.push_back(config.interfaces()[i].ipAddresses()[j]);
          }
        } else {
          if (intfIP.find("::") != std::string::npos) {
            auto ipDecimal =
                fmt::format("{}", i + config.interfaces()->size() + 1);
            config.interfaces()[i].ipAddresses()[j] =
                fmt::format("{}::/64", ipDecimal);
            intfIPList.push_back(config.interfaces()[i].ipAddresses()[j]);
          }
        }
      }
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());
    return intfIPList;
  }

  void restoreKernelIPAddress(
      cfg::SwitchConfig& config,
      bool isIpv4,
      const std::vector<std::string>& intfOldIPs) {
    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::ENABLED;
      auto portType = config.ports()[i].portType().value();
      config.ports()[i].loopbackMode() =
          getAsics().cbegin()->second->getDesiredLoopbackMode(portType);
    }
    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());
    for (int i = 0; i < config.interfaces()->size(); i++) {
      if (config.interfaces()[i].scope() == cfg::Scope::GLOBAL) {
        continue;
      }

      // change ipv4 address of the interface
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);
        if (isIpv4) {
          if (intfIP.find("::") == std::string::npos) {
            config.interfaces()[i].ipAddresses()[j] = intfOldIPs[i];
          }
        } else {
          if (intfIP.find("::") != std::string::npos) {
            config.interfaces()[i].ipAddresses()[j] = intfOldIPs[i];
          }
        }
      }
    }
    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());
  }

  void printInterfaceDetails(const cfg::SwitchConfig& config) {
    // Get TunManager pointer
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();

    for (int i = 0; i < config.interfaces()->size(); i++) {
      std::vector<std::string> intfIPv4s;
      std::vector<std::string> intfIPv6s;

      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6s.push_back(intfIP);
        } else {
          intfIPv4s.push_back(intfIP);
        }
      }

      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());

      // Convert vectors to comma-separated strings for logging
      std::string ipv4List = folly::join(", ", intfIPv4s);
      std::string ipv6List = folly::join(", ", intfIPv6s);

      XLOG(INFO) << "Interface ID: "
                 << (InterfaceID)config.interfaces()[i].intfID().value()
                 << ", Status: " << (status ? "UP" : "DOWN") << ", IPv4: ["
                 << ipv4List << "]" << ", IPv6: [" << ipv6List << "]";
    }
  }

  // intfs_ changes on the TunManager event base thread, so read it there.
  void printProbedInterfaceDetails() {
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
    tunMgr_->evb_->runInFbossEventBaseThreadAndWait(
        [this]() { printProbedInterfaceDetailsOnEvb(); });
  }

  void printProbedInterfaceDetailsOnEvb() {
    // Get TunManager pointer
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();

    XLOG(INFO) << "=== Probed Interface Details ===";

    for (const auto& [interfaceId, intf] : tunMgr_->intfs_) {
      std::vector<std::string> intfIPv4s;
      std::vector<std::string> intfIPv6s;

      // Categorize addresses by IP version
      for (const auto& [addr, mask] : intf->getAddresses()) {
        if (addr.isV6()) {
          intfIPv6s.push_back(addr.str());
        } else {
          intfIPv4s.push_back(addr.str());
        }
      }

      // Convert vectors to comma-separated strings for logging
      std::string ipv4List = folly::join(", ", intfIPv4s);
      std::string ipv6List = folly::join(", ", intfIPv6s);

      XLOG(INFO) << "Interface ID: " << interfaceId
                 << ", Name: " << intf->getName()
                 << ", IfIndex: " << intf->getIfIndex()
                 << ", Status: " << (intf->getStatus() ? "UP" : "DOWN")
                 << ", IPv4: [" << ipv4List << "]" << ", IPv6: [" << ipv6List
                 << "]";
    }

    XLOG(INFO) << "=== End Probed Interface Details ===";
  }

  void printKernelInformation() {
    XLOG(INFO) << "=== Kernel Network Information ===";

    // IPv4 route table (all tables)
    XLOG(INFO) << "--- IPv4 Routes (All Tables) ---";
    std::string cmd = "ip route show table all";
    auto output = runShellCmd(cmd);
    XLOG(INFO) << "Command: " << cmd;
    XLOG(INFO) << "Output:\n" << output;

    // IPv6 route table (all tables)
    XLOG(INFO) << "--- IPv6 Routes (All Tables) ---";
    cmd = "ip -6 route show table all";
    output = runShellCmd(cmd);
    XLOG(INFO) << "Command: " << cmd;
    XLOG(INFO) << "Output:\n" << output;

    // IPv4 rule table
    XLOG(INFO) << "--- IPv4 Rules ---";
    cmd = "ip rule";
    output = runShellCmd(cmd);
    XLOG(INFO) << "Command: " << cmd;
    XLOG(INFO) << "Output:\n" << output;

    // IPv6 rule table
    XLOG(INFO) << "--- IPv6 Rules ---";
    cmd = "ip -6 rule";
    output = runShellCmd(cmd);
    XLOG(INFO) << "Command: " << cmd;
    XLOG(INFO) << "Output:\n" << output;

    // IPv4 addresses
    XLOG(INFO) << "--- IPv4 Addresses ---";
    cmd = "ip -4 a";
    output = runShellCmd(cmd);
    XLOG(INFO) << "Command: " << cmd;
    XLOG(INFO) << "Output:\n" << output;

    // IPv6 addresses
    XLOG(INFO) << "--- IPv6 Addresses ---";
    cmd = "ip -6 a";
    output = runShellCmd(cmd);
    XLOG(INFO) << "Command: " << cmd;
    XLOG(INFO) << "Output:\n" << output;

    XLOG(INFO) << "=== End Kernel Network Information ===";
  }
};

// Test that the tunnel manager is able to create the source route rule
// entries, tunnel address entries and default route entries for IPv4 in the
// kernel
TEST_F(AgentTunnelMgrTest, checkKernelIPv4Entries) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Apply the config
      applyNewConfig(config);
      waitForStateUpdates(getAgentEnsemble()->getSw());

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv4)));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Test that the tunnel manager is able to create the source route rule
// entries, tunnel address entries and default route entries for IPv6 in the
// kernel
TEST_F(AgentTunnelMgrTest, checkKernelIPv6Entries) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Apply the config
      applyNewConfig(config);
      waitForStateUpdates(getAgentEnsemble()->getSw());

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6), false, true));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

/**
 * Test verifies tunnel manager's probe and cleanup functionality:
 * - Creates state (interfaces, addresses, source rules, default routes) for 2
 * interfaces
 * - Verifies state exists in kernel
 * - Calls probe and cleanup code (simulating cold/warm boot behavior)
 * - Verifies kernel has completely clean state after cleanup
 */
TEST_F(AgentTunnelMgrTest, checkProbedDataCleanup) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());

    printInterfaceDetails(config);
    printKernelInformation();

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());

      XLOG(INFO) << "Interface ID: "
                 << (InterfaceID)config.interfaces()[i].intfID().value()
                 << ", Status: " << (status ? "UP" : "DOWN")
                 << ", IPv4: " << intfIPv4 << ", IPv6: " << intfIPv6;
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6), false, true));
        });
      }
    }

    // Get TunManager pointer
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
    auto socketExists = tunMgr_->isValidNlSocket();

    if (socketExists) {
      // Verify probe data is not cleaned up during warmboot, since interfaces
      // in kernel and switchState are same. During the coldboot case for tests,
      // the kernel will have nothing, while switchState will have the
      // interfacs, so cleanup will run.
      if (getAgentEnsemble()->getBootType() == BootType::WARM_BOOT) {
        EXPECT_EQ(tunMgr_->probedStateCleanedUp_, false);
        // Access thread local status from appropriate thread.
        int64_t statsValue = 0;
        tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
          statsValue = getSw()->stats()->getProbedStateCleanupStatus();
        });
        EXPECT_EQ(statsValue, 0);
      } else {
        EXPECT_EQ(tunMgr_->probedStateCleanedUp_, true);
        // Flush thread-local stats to make them immediately available
        ThriftHandler handler(getSw());
        handler.flushCountersNow();
        // Access thread local status from appropriate thread.
        int64_t statsValue = 0;
        tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
          statsValue = getSw()->stats()->getProbedStateCleanupStatus();
        });
        EXPECT_EQ(statsValue, 1);
      }
      EXPECT_EQ(tunMgr_->initialCleanupDone_, true);

      // Temporarily unregister TunManager from state observation to eliminate
      // interfaces being created.
      if (tunMgr_->observingState_) {
        getSw()->unregisterStateObserver(tunMgr_);
      }

      XLOG(INFO) << "Starting probe and cleanup of kernel data";
      tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
        // One task, so no sync runs between reset and probe. The lock guards
        // the interfaces that sendPacketToHost() reads on the RX thread.
        std::lock_guard<std::mutex> lock(tunMgr_->mutex_);
        tunMgr_->probeDone_ = false;
        tunMgr_->doProbe(lock);
        // Force cleanup by calling deleteAllProbedData directly to ensure it
        // happens regardless of interface mapping comparison
        tunMgr_->deleteAllProbedData();
      });

      XLOG(INFO) << "Stopping probe and clean up of probe data";

      // Check that the kernel entries are removed after probe
      for (int i = 0; i < config.interfaces()->size(); i++) {
        InterfaceID intfID =
            InterfaceID(config.interfaces()[i].intfID().value());

        // Get IPv4 address for this interface
        auto ipv4Addr = getSwitchIntfIP(getProgrammedState(), intfID);
        utility::checkKernelIpEntriesRemoved(
            tunMgr_, getProgrammedState(), intfID, ipv4Addr.str(), true);

        // Get IPv6 address for this interface
        auto ipv6Addr = getSwitchIntfIPv6(getProgrammedState(), intfID);
        utility::checkKernelIpEntriesRemoved(
            tunMgr_, getProgrammedState(), intfID, ipv6Addr.str(), false);
      }

      // Re-register TunManager for state observation after test verification
      if (tunMgr_->observingState_) {
        getSw()->registerStateObserver(tunMgr_, "TunManager");
      }
    } else {
      XLOG(INFO) << "Socket does not exist";
    }

    // Recreate interfaces after deleting probed data
    printProbedInterfaceDetails();
    printInterfaceDetails(config);
    printKernelInformation();

    XLOG(INFO) << "Recreate kernel data";
    tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
      tunMgr_->probeDone_ = false;
      tunMgr_->probe();
    });

    auto state = getAgentEnsemble()->getSw()->getState();
    tunMgr_->evb_->runInFbossEventBaseThread(
        [tunMgr_, state]() { tunMgr_->sync(state); });
    printProbedInterfaceDetails();
    printInterfaceDetails(config);
    printKernelInformation();
  };

  verifyAcrossWarmBoots(setup, verify);
}

/**
 * Test verifies tunnel manager's probe and cleanup functionality for DOWN
 * interfaces:
 * - Creates state (interfaces, addresses, source rules, default routes) for 2
 * interfaces with ports disabled (interfaces DOWN)
 * - Verifies state exists in kernel
 * - Verifies kernel has clean state after cleanup, skipping source rule
 * validation for down interfaces (source rules persist for down interfaces
 * per kernel limitation)
 */
TEST_F(AgentTunnelMgrTest, checkProbedDataCleanupInterfaceDown) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());

    printInterfaceDetails(config);
    printKernelInformation();

    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());

      XLOG(INFO) << "Interface ID: "
                 << (InterfaceID)config.interfaces()[i].intfID().value()
                 << ", Status: " << (status ? "UP" : "DOWN")
                 << ", IPv4: " << intfIPv4 << ", IPv6: " << intfIPv6;
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6), false, true));
        });
      }
    }

    for (int i = 0; i < config.ports()->size(); i++) {
      XLOG(DBG2) << "Disabling port at index: " << i;
      config.ports()[i].state() = cfg::PortState::DISABLED;
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    printInterfaceDetails(config);

    // Get TunManager pointer
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
    auto socketExists = tunMgr_->isValidNlSocket();

    if (socketExists) {
      // Verify probe data is not cleaned up during warmboot, since interfaces
      // in kernel and switchState are same. During the coldboot case for tests,
      // the kernel will have nothing, while switchState will have the
      if (getAgentEnsemble()->getBootType() == BootType::WARM_BOOT) {
        EXPECT_EQ(tunMgr_->probedStateCleanedUp_, false);
        int64_t statsValue = 0;
        // Access thread local status from appropriate thread.
        tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
          statsValue = getSw()->stats()->getProbedStateCleanupStatus();
        });
        EXPECT_EQ(statsValue, 0);
      } else {
        EXPECT_EQ(tunMgr_->probedStateCleanedUp_, true);
        // Flush thread-local stats to make them immediately available
        ThriftHandler handler(getSw());
        handler.flushCountersNow();
        int64_t statsValue = 0;
        // Access thread local status from appropriate thread.
        tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
          statsValue = getSw()->stats()->getProbedStateCleanupStatus();
        });
        EXPECT_EQ(statsValue, 1);
      }
      EXPECT_EQ(tunMgr_->initialCleanupDone_, true);
      // Unregister TunManager from state observation to eliminate interfaces
      // from being created.
      if (tunMgr_->observingState_) {
        getSw()->unregisterStateObserver(tunMgr_);
      }

      XLOG(INFO) << "Starting probe and cleanup of kernel data";
      tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
        tunMgr_->probeDone_ = false;
        tunMgr_->probe();
      });
      printProbedInterfaceDetails();
      printKernelInformation();

      // Force cleanup by calling deleteAllProbedData directly to ensure it
      // happens regardless of interface mapping comparison
      tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
        std::lock_guard<std::mutex> lock(tunMgr_->mutex_);
        tunMgr_->deleteAllProbedData();
      });

      XLOG(INFO) << "Stopping probe and clean up of probe data";

      // Check that the kernel entries are removed after probe
      for (int i = 0; i < config.interfaces()->size(); i++) {
        InterfaceID intfID =
            InterfaceID(config.interfaces()[i].intfID().value());

        // Get IPv4 address for this interface
        auto ipv4Addr = getSwitchIntfIP(getProgrammedState(), intfID);
        utility::checkKernelIpEntriesRemovedStrict(
            intfID, ipv4Addr.str(), true);

        // Get IPv6 address for this interface
        auto ipv6Addr = getSwitchIntfIPv6(getProgrammedState(), intfID);
        utility::checkKernelIpEntriesRemovedStrict(
            intfID, ipv6Addr.str(), false);
      }

      // Re-register TunManager for cleanup
      if (tunMgr_->observingState_) {
        getSw()->registerStateObserver(tunMgr_, "TunManager");
      }
    } else {
      XLOG(INFO) << "Socket does not exist";
    }

    // Recreate interfaces after deleting probed data
    printProbedInterfaceDetails();
    printInterfaceDetails(config);
    printKernelInformation();

    XLOG(INFO) << "Recreate kernel data";
    tunMgr_->evb_->runInFbossEventBaseThreadAndWait([&]() {
      tunMgr_->probeDone_ = false;
      tunMgr_->probe();
    });

    auto state = getAgentEnsemble()->getSw()->getState();
    tunMgr_->evb_->runInFbossEventBaseThread(
        [tunMgr_, state]() { tunMgr_->sync(state); });
    printProbedInterfaceDetails();
    printInterfaceDetails(config);
    printKernelInformation();
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Test that the tunnel manager is able to handle ipv4 address change of the
// interface
TEST_F(AgentTunnelMgrTest, changeIPv4Address) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      if (config.interfaces()[i].scope() == cfg::Scope::GLOBAL) {
        continue;
      }
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv4), true, true));
        });
      }

      // change ipv4 address of the interface
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        if (config.interfaces()[i].ipAddresses()[j].find("::") ==
            std::string::npos) {
          auto ipDecimal = fmt::format("{}", i + 1);
          config.interfaces()[i].ipAddresses()[j] =
              fmt::format("{}.2.2.2/24", ipDecimal);
          intfIPv4 = fmt::format("{}.2.2.2", ipDecimal);
        }
      }

      // Apply the config
      applyNewConfig(config);
      waitForStateUpdates(getAgentEnsemble()->getSw());

      // Route entries installation is currently not consistent after the ip
      // address change. So, passing false for checkRouteEntry.
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv4), true, false));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Test that the tunnel manager is able to handle ipv6 address change of the
// interface
TEST_F(AgentTunnelMgrTest, changeIPv6Address) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      if (config.interfaces()[i].scope() == cfg::Scope::GLOBAL) {
        continue;
      }
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route
      // rule entries are not created if the interface is not up. So,
      // checking for the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6), false));
        });
      }

      // change ipv6 address of the interface
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        if (config.interfaces()[i].ipAddresses()[j].find("::") !=
            std::string::npos) {
          auto ipDecimal = fmt::format("{}", i + 1);
          config.interfaces()[i].ipAddresses()[j] =
              fmt::format("{}::2/64", ipDecimal);
          intfIPv6 = fmt::format("{}::2", ipDecimal);
        }
      }

      // Apply the config
      applyNewConfig(config);
      waitForStateUpdates(getAgentEnsemble()->getSw());

      // Route entries installation is currently not consistent after the ip
      // address change. So, passing false for checkRouteEntry.
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6), false, false));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Test to check if there are no duplicate kernel entries created
TEST_F(AgentTunnelMgrTest, checkDuplicateEntries) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.interfaces()->size(); i++) {
      XLOG(DBG2) << "Interface Id: "
                 << (InterfaceID)config.interfaces()[i].intfID().value();
      if (config.interfaces()[i].scope() == cfg::Scope::GLOBAL) {
        continue;
      }

      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6), false));
        });
      }

      // Applying the same config again
      // Made change in TunManager to reprogram source route rule upon
      // interface up. Noticed duplicate entry for 1.1.1.1 and 1:: for source
      // route rule.

      // Applying same ipv4 and ipv6 address on the interface

      // Apply the config
      applyNewConfig(config);

      std::string intfIPv4New;
      std::string intfIPv6New;
      // change ipv4 and ipv6 address of the interface
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        if (config.interfaces()[i].ipAddresses()[j].find("::") ==
            std::string::npos) {
          intfIPv4New = utility::genInterfaceAddress(i + 1, true, 31, 31);
          config.interfaces()[i].ipAddresses()[j] = intfIPv4New;
          intfIPv4New = folly::to<std::string>(
              folly::IPAddress::createNetwork(
                  config.interfaces()[i].ipAddresses()[j], -1, false)
                  .first);
        } else {
          intfIPv6New = utility::genInterfaceAddress(i + 1, false, 127, 127);
          config.interfaces()[i].ipAddresses()[j] = intfIPv6New;
          intfIPv6New = folly::to<std::string>(
              folly::IPAddress::createNetwork(
                  config.interfaces()[i].ipAddresses()[j], -1, false)
                  .first);
        }
      }

      // Apply the config
      applyNewConfig(config);
      waitForStateUpdates(getAgentEnsemble()->getSw());

      // Route entries installation is currently not consistent after the ip
      // address change. So, passing false for checkRouteEntry.
      if (status) {
        // If source route rule is added again for the same IP address, it
        // would create duplicate entries.
        utility::checkKernelEntriesRemoved(
            folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
        // Use WITH_RETRIES to handle race condition with TunManager async
        // processing.
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv4New), true, false));
        });
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6New), false, false));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4New),
          folly::to<std::string>(intfIPv6New));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4New),
          folly::to<std::string>(intfIPv6New));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTunnelMgrTest, checkKernelIPv4EntriesPortsDown) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::DISABLED;
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    for (int i = 0; i < config.interfaces()->size(); i++) {
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv4), true, false));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTunnelMgrTest, checkKernelIPv4EntriesPortsDownUp) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::DISABLED;
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::ENABLED;
      auto portType = config.ports()[i].portType().value();
      config.ports()[i].loopbackMode() =
          getAsics().cbegin()->second->getDesiredLoopbackMode(portType);
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    for (int i = 0; i < config.interfaces()->size(); i++) {
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv4)));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTunnelMgrTest, checkKernelIPv6EntriesPortsDownUp) {
  auto setup = [=]() {};
  auto verify = [=, this]() {
    auto config = initialConfig(*getAgentEnsemble());
    std::string intfIPv4;
    std::string intfIPv6;
    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::DISABLED;
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::ENABLED;
      auto portType = config.ports()[i].portType().value();
      config.ports()[i].loopbackMode() =
          getAsics().cbegin()->second->getDesiredLoopbackMode(portType);
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    for (int i = 0; i < config.interfaces()->size(); i++) {
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        std::string intfIP = folly::to<std::string>(
            folly::IPAddress::createNetwork(
                config.interfaces()[i].ipAddresses()[j], -1, false)
                .first);

        if (intfIP.find("::") != std::string::npos) {
          intfIPv6 = std::move(intfIP);
        } else {
          intfIPv4 = std::move(intfIP);
        }
      }

      // Get TunManager pointer
      auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
      auto status = tunMgr_->getIntfStatus(
          getProgrammedState(),
          (InterfaceID)config.interfaces()[i].intfID().value());
      // There could be a race condition where the interface is up, but the
      // socket is not created. So, checking for the socket existence.
      auto socketExists = tunMgr_->isValidNlSocket();

      // There is a known limitation in the kernel that the source route rule
      // entries are not created if the interface is not up. So, checking for
      // the kernel entries if the interface is  up
      // Use WITH_RETRIES to handle race condition with TunManager async
      // processing.
      if (status && socketExists) {
        WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
          EXPECT_EVENTUALLY_TRUE(
              utility::checkKernelEntriesExistBool(
                  folly::to<std::string>(intfIPv6), false, true));
        });
      }

      // Clear kernel entries
      utility::clearKernelEntries(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));

      // Check that the kernel entries are removed
      utility::checkKernelEntriesRemoved(
          folly::to<std::string>(intfIPv4), folly::to<std::string>(intfIPv6));
    }

    utility::clearAllKernelEntries();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentTunnelMgrTest, changeIpv4AddressPortDownUp) {
  auto setup = [=]() {};

  auto verify = [=, this]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    std::string intfIPv4;
    std::vector<std::string> intfOldIPv4s;
    std::vector<std::string> intfNewIPv4s;
    InterfaceID intfID = getInterfaceIDForPort(
        getAgentEnsemble()->masterLogicalPortIds()[0],
        getAgentEnsemble()->getSw()->getState());
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();

    intfOldIPv4s = utility::getInterfaceIpAddress(config, true);
    utility::checkKernelIpEntriesExist(
        tunMgr_, getProgrammedState(), intfID, intfOldIPv4s[0], true);
    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::DISABLED;
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    // change ipv6 address of the interface
    intfNewIPv4s = changeKernelIPAddress(config, true);

    // Bring up one port
    bringUpPort(getAgentEnsemble()->masterLogicalPortIds()[0]);

    utility::checkKernelIpEntriesExist(
        tunMgr_, getProgrammedState(), intfID, intfNewIPv4s[0], true);

    utility::checkKernelIpEntriesRemoved(
        tunMgr_, getProgrammedState(), intfID, intfOldIPv4s[0], true);

    config.ports()[0].state() = cfg::PortState::ENABLED;
    auto portType = config.ports()[0].portType().value();
    config.ports()[0].loopbackMode() =
        getAsics().cbegin()->second->getDesiredLoopbackMode(portType);
    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());
  };

  auto setupPostWarmboot = [=]() {};

  // This is just to bring back the set-up to the original state
  auto verifyPostWarmboot = [=, this]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    InterfaceID intfID = (InterfaceID)config.interfaces()[0].intfID().value();
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
    std::vector<std::string> intfIPv6s;
    intfIPv6s = utility::getInterfaceIpAddress(config, true);
    utility::checkKernelIpEntriesExist(
        tunMgr_, getProgrammedState(), intfID, intfIPv6s[0], true);
    utility::clearAllKernelEntries();

    utility::checkKernelIpEntriesRemoved(
        tunMgr_, getProgrammedState(), intfID, intfIPv6s[0], true);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(AgentTunnelMgrTest, changeIpv6AddressPortDownUp) {
  auto setup = [=]() {};

  auto verify = [=, this]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    std::string intfIPv6;
    std::vector<std::string> intfOldIPv6s;
    std::vector<std::string> intfNewIPv6s;
    InterfaceID intfID = getInterfaceIDForPort(
        getAgentEnsemble()->masterLogicalPortIds()[0],
        getAgentEnsemble()->getSw()->getState());
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();

    intfOldIPv6s = utility::getInterfaceIpAddress(config, false);
    utility::checkKernelIpEntriesExist(
        tunMgr_, getProgrammedState(), intfID, intfOldIPv6s[0], false);

    for (int i = 0; i < config.ports()->size(); i++) {
      config.ports()[i].state() = cfg::PortState::DISABLED;
    }

    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    // change ipv6 address of the interface
    intfNewIPv6s = changeKernelIPAddress(config, false);

    // Bring up one port
    bringUpPort(getAgentEnsemble()->masterLogicalPortIds()[0]);

    utility::checkKernelIpEntriesExist(
        tunMgr_, getProgrammedState(), intfID, intfNewIPv6s[0], false);

    utility::checkKernelIpEntriesRemoved(
        tunMgr_, getProgrammedState(), intfID, intfOldIPv6s[0], false);

    config.ports()[0].state() = cfg::PortState::ENABLED;
    auto portType = config.ports()[0].portType().value();
    config.ports()[0].loopbackMode() =
        getAsics().cbegin()->second->getDesiredLoopbackMode(portType);
    // Apply the config
    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());
  };

  auto setupPostWarmboot = [=]() {};

  // This is just to bring back the set-up to the original state
  auto verifyPostWarmboot = [=, this]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    InterfaceID intfID = (InterfaceID)config.interfaces()[0].intfID().value();
    auto tunMgr_ = getAgentEnsemble()->getSw()->getTunManager();
    std::vector<std::string> intfIPv6s;
    intfIPv6s = utility::getInterfaceIpAddress(config, false);
    utility::checkKernelIpEntriesExist(
        tunMgr_, getProgrammedState(), intfID, intfIPv6s[0], false);
    utility::clearAllKernelEntries();

    utility::checkKernelIpEntriesRemoved(
        tunMgr_, getProgrammedState(), intfID, intfIPv6s[0], false);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

// Verifies that the fix for SEV S484794 prevents OOB traffic leak on port down.
// When a port goes down, TunManager now programs RTN_UNREACHABLE routes for the
// connected subnets, preventing traffic from falling back to eth0 via the
// default route. This test disables the first port and verifies the affected
// interface route becomes unreachable, while unaffected interfaces continue
// routing via their fboss TUN interfaces.
TEST_F(AgentTunnelMgrTest, verifyNoOobLeakOnPortDown) {
  // Parse device name from "ip route get" output (e.g., "dev eth0")
  auto parseDevFromRouteOutput = [](const std::string& output) -> std::string {
    auto pos = output.find("dev ");
    if (pos == std::string::npos) {
      return "";
    }
    auto start = pos + 4;
    auto end = output.find(' ', start);
    if (end == std::string::npos) {
      end = output.size();
    }
    return output.substr(start, end - start);
  };

  auto setup = [=]() {};
  auto verify = [=, this]() {
    // Clean up any leftover kernel entries from previous runs
    SCOPE_EXIT {
      utility::clearAllKernelEntries();
    };
    auto config = initialConfig(*getAgentEnsemble());

    applyNewConfig(config);
    waitForStateUpdates(getAgentEnsemble()->getSw());

    auto tunMgr = getAgentEnsemble()->getSw()->getTunManager();
    ASSERT_TRUE(tunMgr->isValidNlSocket())
        << "TunManager netlink socket is not valid, cannot verify routing";

    // Step 1: Identify the first port
    auto firstPort = getAgentEnsemble()->masterLogicalPortIds()[0];
    XLOG(INFO) << "Target port: " << firstPort;

    // Step 2: Identify the interface for that port
    auto affectedIntfID = getInterfaceIDForPort(
        firstPort, getAgentEnsemble()->getSw()->getState());
    XLOG(INFO) << "Affected interface: " << affectedIntfID;

    // Step 3: Gather all interface information (IPv6)
    struct IntfInfo {
      InterfaceID intfID;
      std::string intfIP;
      std::string peerIP;
    };
    IntfInfo affectedIntf;
    std::vector<IntfInfo> unaffectedIntfs;

    for (int i = 0; i < config.interfaces()->size(); i++) {
      if (*config.interfaces()[i].isVirtual()) {
        continue;
      }
      auto intfID = InterfaceID(config.interfaces()[i].intfID().value());
      for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
        auto network = folly::IPAddress::createNetwork(
            config.interfaces()[i].ipAddresses()[j], -1, false);
        if (network.first.isV6() && !network.first.isLinkLocal()) {
          auto v6Bytes = network.first.asV6().toByteArray();
          v6Bytes[15] ^= 1;
          auto peerAddr = folly::IPAddressV6(v6Bytes);
          IntfInfo info{intfID, network.first.str(), peerAddr.str()};
          if (intfID == affectedIntfID) {
            affectedIntf = info;
          } else {
            unaffectedIntfs.push_back(info);
          }
          XLOG(INFO) << "Interface " << intfID << " IP: " << network.first
                     << " peer: " << peerAddr
                     << (intfID == affectedIntfID ? " (AFFECTED)" : "");
          break;
        }
      }
    }

    ASSERT_FALSE(affectedIntf.intfIP.empty())
        << "Failed to find IPv6 address for affected interface "
        << affectedIntfID;

    // Step 4: Before port down — affected interface route goes via its TUN
    auto affectedTunName = folly::to<std::string>("fboss", affectedIntf.intfID);
    WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
      auto cmd = folly::to<std::string>(
          "ip -6 route get ",
          affectedIntf.peerIP,
          " from ",
          affectedIntf.intfIP);
      auto output = runShellCmd(cmd);
      auto devName = parseDevFromRouteOutput(output);
      XLOG(INFO) << "Before port down [affected intf " << affectedIntf.intfID
                 << "] - " << cmd << ": " << output << " (dev: " << devName
                 << ")";
      EXPECT_EVENTUALLY_EQ(devName, affectedTunName);
    });

    // Step 5: Bring down only the first port using link toggler
    XLOG(INFO) << "Bringing down port " << firstPort;
    bringDownPort(firstPort);

    // Determine management interface by querying the V6 default route
    // (2001:4860:4860::8888 is Google's public DNS server)
    auto mgmtOutput = runShellCmd("ip -6 route get 2001:4860:4860::8888");
    auto mgmtDev = parseDevFromRouteOutput(mgmtOutput);
    XLOG(INFO) << "Management interface (default route): " << mgmtDev;

    // Step 6: After port down — affected interface route is unreachable
    // (fix for S484794: unreachable routes block fallback to eth0)
    WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
      auto cmd = folly::to<std::string>(
          "ip -6 route get ",
          affectedIntf.peerIP,
          " from ",
          affectedIntf.intfIP,
          " 2>&1");
      auto output = runShellCmd(cmd);
      XLOG(INFO) << "After port down [affected intf " << affectedIntf.intfID
                 << "] - " << cmd << ": " << output;
      // Route is unreachable, NOT falling back to management interface
      EXPECT_EVENTUALLY_TRUE(
          output.find("No route to host") != std::string::npos);
    });

    // Step 7: Unaffected interfaces still route via their specific TUNs
    for (const auto& info : unaffectedIntfs) {
      auto tunName = folly::to<std::string>("fboss", info.intfID);
      WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(100), {
        auto cmd = folly::to<std::string>(
            "ip -6 route get ", info.peerIP, " from ", info.intfIP);
        auto output = runShellCmd(cmd);
        auto devName = parseDevFromRouteOutput(output);
        XLOG(INFO) << "After port down [unaffected intf " << info.intfID
                   << "] - " << cmd << ": " << output << " (dev: " << devName
                   << ")";
        EXPECT_EVENTUALLY_EQ(devName, tunName);
      });
    }
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
