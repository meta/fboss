// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/MockTunManager.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;
using ::testing::_;

namespace {
const RouterID kTestVrf{1001};
// testConfigA: intf 1 on VLAN 1, intf 55 on VLAN 55
const InterfaceID kDefaultVrfIntf{1};
const PortID kDefaultVrfPort{1};
const VlanID kDefaultVrfVlan{1};
const InterfaceID kVrfIntf{55};
const VlanID kVrfVlan{55};

class VrfPuntPathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = testConfigA();
    handle_ = createTestHandle(&config_, SwitchFlags::ENABLE_TUN);
    sw_ = handle_->getSw();
    tunMgr_ = dynamic_cast<MockTunManager*>(sw_->getTunManager());
    ASSERT_NE(tunMgr_, nullptr);
    EXPECT_CALL(*tunMgr_, sendPacketToHost_(_))
        .WillRepeatedly([this](auto args) {
          toHost_.wlock()->push_back(std::get<0>(args));
          return true;
        });
    VrfEntry vrf;
    vrf.vrfId() = kTestVrf;
    sw_->addVrfs({vrf});
    sw_->updateInterfaceVrfs({{kVrfIntf, kTestVrf}});
    for (const auto& [_, portMap] :
         std::as_const(*sw_->getState()->getPorts())) {
      for (const auto& [_, port] : std::as_const(*portMap)) {
        auto vlans = port->getVlans();
        if (vlans.find(kVrfVlan) != vlans.end()) {
          vrfPort_ = port->getID();
        }
      }
    }
  }

  // Receive a UDP packet on port and return the host interfaces it was sent to
  std::vector<InterfaceID> receive(
      PortID port,
      VlanID vlan,
      const folly::IPAddress& src,
      const folly::IPAddress& dst) {
    toHost_.wlock()->clear();
    auto mac = sw_->getState()->getInterfaces()->getNode(kVrfIntf)->getMac();
    auto pkt = utility::makeUDPTxPacket(
        [](uint32_t size) { return TxPacket::allocateTxPacket(size); },
        vlan,
        folly::MacAddress("02:00:00:00:00:99"),
        mac,
        src,
        dst,
        10000,
        20000);
    handle_->rxPacket(pkt->buf()->clone(), PortDescriptor(port), vlan);
    return *toHost_.rlock();
  }

  std::vector<InterfaceID> receiveInVrf(const char* src, const char* dst) {
    return receive(
        vrfPort_, kVrfVlan, folly::IPAddress(src), folly::IPAddress(dst));
  }

  std::vector<InterfaceID> receiveInDefaultVrf(
      const char* src,
      const char* dst) {
    return receive(
        kDefaultVrfPort,
        kDefaultVrfVlan,
        folly::IPAddress(src),
        folly::IPAddress(dst));
  }

  cfg::SwitchConfig config_;
  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_{nullptr};
  MockTunManager* tunMgr_{nullptr};
  PortID vrfPort_{0};
  folly::Synchronized<std::vector<InterfaceID>> toHost_;
};

using IntfIDs = std::vector<InterfaceID>;
} // namespace

// After SetUp (testConfigA):
//   intf 1:  VLAN 1, port 1, VRF 0
//            10.0.0.1/24, 192.168.0.1/24, 2401:db00:2110:3001::1/64, fe80::/64
//   intf 55: VLAN 55, vrfPort_, VRF 1001
//            10.0.55.1/24, 192.168.55.1/24, 2401:db00:2110:3055::1/64,
//            169.254.0.0/16

// A packet to a switch address reaches the host only when it is received in
// that address's VRF, and delivery follows the interface across VRF moves
TEST_F(VrfPuntPathTest, LocalDelivery) {
  const IntfIDs toVrfIntf{kVrfIntf};
  const IntfIDs toDefaultIntf{kDefaultVrfIntf};
  EXPECT_EQ(receiveInVrf("10.0.55.10", "10.0.55.1"), toVrfIntf);
  EXPECT_EQ(
      receiveInVrf("2401:db00:2110:3055::10", "2401:db00:2110:3055::1"),
      toVrfIntf);
  // v4 link-local address of intf 55
  EXPECT_EQ(receiveInVrf("169.254.0.10", "169.254.0.0"), toVrfIntf);
  EXPECT_EQ(receiveInDefaultVrf("10.0.0.10", "10.0.0.1"), toDefaultIntf);
  EXPECT_EQ(
      receiveInDefaultVrf("2401:db00:2110:3001::10", "2401:db00:2110:3001::1"),
      toDefaultIntf);

  EXPECT_TRUE(receiveInVrf("10.0.55.10", "10.0.0.1").empty());
  EXPECT_TRUE(receiveInVrf("2401:db00:2110:3055::10", "2401:db00:2110:3001::1")
                  .empty());
  EXPECT_TRUE(receiveInDefaultVrf("10.0.0.10", "10.0.55.1").empty());
  EXPECT_TRUE(
      receiveInDefaultVrf("2401:db00:2110:3001::10", "2401:db00:2110:3055::1")
          .empty());
  EXPECT_TRUE(receiveInDefaultVrf("169.254.0.10", "169.254.0.0").empty());

  sw_->updateInterfaceVrfs({{kVrfIntf, RouterID(0)}});
  EXPECT_EQ(receiveInVrf("10.0.55.10", "10.0.55.1"), toVrfIntf);
  EXPECT_EQ(receiveInDefaultVrf("10.0.0.10", "10.0.55.1"), toVrfIntf);
}
