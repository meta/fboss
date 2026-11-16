/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

namespace {
auto constexpr kInterfaceId = 42;
} // namespace

namespace facebook::fboss {
class AddrToLocalIntfTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto cfg = testConfigA(cfg::SwitchType::VOQ);
    handle_ = createTestHandle(&cfg);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    waitForStateUpdates(sw_);
  }

  HwSwitchMatcher matcher() const {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  }

  std::shared_ptr<Interface> createInterface(
      InterfaceID intfID,
      std::map<folly::IPAddress, uint8_t>& addresses) {
    auto intf = std::make_shared<Interface>(
        InterfaceID(intfID),
        std::optional<VlanID>(std::nullopt),
        folly::StringPiece("100" + folly::to<std::string>(intfID)),
        folly::MacAddress{},
        9000,
        false,
        false,
        cfg::InterfaceType::SYSTEM_PORT);
    intf->setAddresses(addresses);
    return intf;
  }

  void addInterface(const std::shared_ptr<Interface>& intf) {
    sw_->updateStateBlocking(
        "Add interface", [&](const std::shared_ptr<SwitchState>& state) {
          auto newState = state->clone();
          newState->getInterfaces()->modify(&newState)->addNode(
              intf, matcher());
          return newState;
        });
  }

  void updateInterface(const std::shared_ptr<Interface>& intf) {
    sw_->updateStateBlocking(
        "Update interface", [&](const std::shared_ptr<SwitchState>& state) {
          auto newState = state->clone();
          newState->getInterfaces()->modify(&newState)->updateNode(
              intf, matcher());
          return newState;
        });
  }

  void rmInterface(const std::shared_ptr<Interface>& intf) {
    sw_->updateStateBlocking(
        "Remove interface", [&](const std::shared_ptr<SwitchState>& state) {
          auto newState = state->clone();
          newState->getInterfaces()->modify(&newState)->removeNode(intf);
          return newState;
        });
  }

 protected:
  SwSwitch* sw_{nullptr};
  std::unique_ptr<HwTestHandle> handle_{nullptr};
};

TEST_F(AddrToLocalIntfTest, addRemoveInterface) {
  auto initialSize = sw_->getAddrToLocalIntfMap().size();

  std::map<folly::IPAddress, uint8_t> intf0Addr = {};
  auto intf0 = createInterface(InterfaceID(kInterfaceId), intf0Addr);

  addInterface(intf0);
  EXPECT_EQ(sw_->getAddrToLocalIntfMap().size(), initialSize);

  std::map<folly::IPAddress, uint8_t> intf1Addr = {
      {folly::IPAddressV6("1::1"), 64}, {folly::IPAddressV6("2::1"), 64}};
  auto intf1 = createInterface(InterfaceID(kInterfaceId + 1), intf1Addr);

  std::map<folly::IPAddress, uint8_t> intf2Addr = {
      {folly::IPAddressV6("3::1"), 64},
      {folly::IPAddressV6("4::1"), 64},
      {folly::IPAddressV6("5::1"), 64}};
  auto intf2 = createInterface(InterfaceID(kInterfaceId + 2), intf2Addr);

  addInterface(intf1);
  addInterface(intf2);
  EXPECT_EQ(
      sw_->getAddrToLocalIntfMap().size(),
      initialSize + intf1Addr.size() + intf2Addr.size());
  for (const auto& [addr, _] : intf1Addr) {
    EXPECT_EQ(
        sw_->getAddrToLocalIntfMap()
            .find(std::make_pair(RouterID(0), addr))
            ->second,
        InterfaceID(kInterfaceId + 1));
  }
  for (const auto& [addr, _] : intf2Addr) {
    EXPECT_EQ(
        sw_->getAddrToLocalIntfMap()
            .find(std::make_pair(RouterID(0), addr))
            ->second,
        InterfaceID(kInterfaceId + 2));
  }

  rmInterface(intf0);
  EXPECT_EQ(
      sw_->getAddrToLocalIntfMap().size(),
      initialSize + intf1Addr.size() + intf2Addr.size());

  rmInterface(intf1);
  EXPECT_EQ(
      sw_->getAddrToLocalIntfMap().size(), initialSize + intf2Addr.size());

  rmInterface(intf2);
  EXPECT_EQ(sw_->getAddrToLocalIntfMap().size(), initialSize);
  for (const auto& [addr, _] : intf1Addr) {
    EXPECT_EQ(
        sw_->getAddrToLocalIntfMap().find(std::make_pair(RouterID(0), addr)),
        sw_->getAddrToLocalIntfMap().end());
  }
  for (const auto& [addr, _] : intf2Addr) {
    EXPECT_EQ(
        sw_->getAddrToLocalIntfMap().find(std::make_pair(RouterID(0), addr)),
        sw_->getAddrToLocalIntfMap().end());
  }
}

TEST_F(AddrToLocalIntfTest, updateInterface) {
  auto initialSize = sw_->getAddrToLocalIntfMap().size();

  std::map<folly::IPAddress, uint8_t> intf0Addr = {};
  auto intf0 = createInterface(InterfaceID(kInterfaceId), intf0Addr);

  addInterface(intf0);
  EXPECT_EQ(sw_->getAddrToLocalIntfMap().size(), initialSize);

  std::map<folly::IPAddress, uint8_t> intf0UpdatedAddr = {
      {folly::IPAddressV6("1::1"), 64}, {folly::IPAddressV6("2::1"), 64}};
  auto intf0Updated =
      createInterface(InterfaceID(kInterfaceId), intf0UpdatedAddr);

  updateInterface(intf0Updated);
  EXPECT_EQ(
      sw_->getAddrToLocalIntfMap().size(),
      initialSize + intf0UpdatedAddr.size());

  for (const auto& [addr, _] : intf0UpdatedAddr) {
    EXPECT_EQ(
        sw_->getAddrToLocalIntfMap()
            .find(std::make_pair(RouterID(0), addr))
            ->second,
        InterfaceID(kInterfaceId));
  }

  rmInterface(intf0Updated);
  EXPECT_EQ(sw_->getAddrToLocalIntfMap().size(), initialSize);

  for (const auto& [addr, _] : intf0UpdatedAddr) {
    EXPECT_EQ(
        sw_->getAddrToLocalIntfMap().find(std::make_pair(RouterID(0), addr)),
        sw_->getAddrToLocalIntfMap().end());
  }
}

} // namespace facebook::fboss
