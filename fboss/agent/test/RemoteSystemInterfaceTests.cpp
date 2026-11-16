// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/dynamic.h>
#include <gtest/gtest.h>

namespace {
const auto kInterfaceID = facebook::fboss::InterfaceID(101);
const auto kSystemPortID = facebook::fboss::SystemPortID(101);
const auto kIpV6Address = folly::IPAddressV6("201::10");
const auto kIpV4Address = folly::IPAddressV4("200.0.0.10");
} // namespace

namespace facebook::fboss {

class RemoteSystemInterfaceTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_multi_switch = true;
    auto config = initialConfig();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    waitForStateUpdates(sw_);
  }

  cfg::SwitchConfig initialConfig() const {
    return testConfigB();
  }

  void TearDown() override {
    sw_->stop(false, false);
  }

  void verifyNoGeneratedRemoteSystemPortInterfaces(int expectedSize = 0) {
    auto state = sw_->getState();
    auto remote = state->getRemoteInterfaces();

    EXPECT_EQ(remote->size(), expectedSize);

    auto remoteIntfs1 = remote->getMapNodeIf(
        HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(1)}));
    auto remoteIntfs2 = remote->getMapNodeIf(
        HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(2)}));

    EXPECT_EQ(nullptr, remoteIntfs1);
    EXPECT_EQ(nullptr, remoteIntfs2);
  }

 protected:
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

TEST_F(RemoteSystemInterfaceTest, ConfigureSystemPortInterfaces) {
  verifyNoGeneratedRemoteSystemPortInterfaces();
}

TEST_F(RemoteSystemInterfaceTest, ReConfigureSystemPortInterfaces) {
  auto matchAll = sw_->getScopeResolver()->scope(cfg::SwitchType::VOQ);
  ;
  sw_->updateStateBlocking(
      "Add System Interface", [=](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        auto remoteSysIntfs = state->getRemoteInterfaces()->modify(&newState);
        remoteSysIntfs->addNode(
            std::make_shared<Interface>(
                InterfaceID(512),
                std::optional<VlanID>(std::nullopt),
                folly::StringPiece("512"),
                folly::MacAddress("00:02:00:00:00:01"),
                9000,
                true,
                true,
                cfg::InterfaceType::SYSTEM_PORT),
            matchAll);
        remoteSysIntfs->addNode(
            std::make_shared<Interface>(
                InterfaceID(513),
                std::optional<VlanID>(std::nullopt),
                folly::StringPiece("513"),
                folly::MacAddress("00:02:00:00:00:02"),
                9000,
                true,
                true,
                cfg::InterfaceType::SYSTEM_PORT),
            matchAll);
        return newState;
      });

  verifyNoGeneratedRemoteSystemPortInterfaces(1);
  auto allRemoteSysIntfs0 =
      sw_->getState()->getRemoteInterfaces()->getMapNodeIf(matchAll);
  EXPECT_NE(nullptr, allRemoteSysIntfs0);
  EXPECT_NE(0, allRemoteSysIntfs0->size());
  auto newCfg = initialConfig();
  for (auto port : *newCfg.ports()) {
    port.minFrameSize() = 1024;
  }
  sw_->applyConfig("Reconfigure", newCfg);
  verifyNoGeneratedRemoteSystemPortInterfaces(1);
  auto allRemoteSysIntfs1 =
      sw_->getState()->getRemoteInterfaces()->getMapNodeIf(matchAll);
  EXPECT_NE(nullptr, allRemoteSysIntfs1);
  EXPECT_EQ(allRemoteSysIntfs0, allRemoteSysIntfs1);
  auto config = initialConfig();
  config.interfaces()->resize(0);
  sw_->applyConfig("Remove System Interface", config);
  waitForStateUpdates(sw_);
  auto newRemoteSystemIntfs = sw_->getState()->getRemoteInterfaces();
  EXPECT_EQ(newRemoteSystemIntfs->getNodeIf(InterfaceID(101)), nullptr);
}

TEST_F(RemoteSystemInterfaceTest, LocalScopedNeighborEntriesStayLocal) {
  verifyNoGeneratedRemoteSystemPortInterfaces();
  sw_->updateStateBlocking(
      "Add Ndp Entry", [=](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        auto interfaces = newState->getInterfaces()->modify(&newState);
        auto intf = interfaces->getNodeIf(kInterfaceID);
        auto ndpTable = intf->getNdpTable()->modify(kInterfaceID, &newState);
        ndpTable->addEntry(
            kIpV6Address,
            MacAddress("00:02:00:00:00:01"),
            PortDescriptor(kSystemPortID),
            kInterfaceID);
        return newState;
      });
  sw_->updateStateBlocking(
      "Add Arp Entry", [=](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        auto interfaces = newState->getInterfaces()->modify(&newState);
        auto intf = interfaces->getNodeIf(kInterfaceID);
        auto arpTable = intf->getArpTable()->modify(kInterfaceID, &newState);
        arpTable->addEntry(
            kIpV4Address,
            MacAddress("00:02:00:00:00:01"),
            PortDescriptor(kSystemPortID),
            kInterfaceID);
        return newState;
      });
  waitForStateUpdates(sw_);
  waitForStateUpdates(sw_);
  auto localIntf = sw_->getState()->getInterfaces()->getNodeIf(kInterfaceID);
  EXPECT_NE(localIntf, nullptr);
  EXPECT_NE(localIntf->getNdpTable()->getEntry(kIpV6Address), nullptr);
  EXPECT_NE(localIntf->getArpTable()->getEntry(kIpV4Address), nullptr);
  verifyNoGeneratedRemoteSystemPortInterfaces();
}
} // namespace facebook::fboss
