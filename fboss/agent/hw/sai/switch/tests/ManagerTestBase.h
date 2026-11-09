/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/types.h"

#include <array>
#include <string>
#include <unordered_map>

#include <fmt/format.h>
#include <gtest/gtest.h>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class ArpEntry;
class Interface;
class Port;
class ResolvedNextHop;
class Vlan;
class SwitchState;

class ManagerTestBase : public ::testing::Test {
 public:
  // Using a plain enum (rather than enum class) because the setup treats
  // them as a bitmask.
  enum SetupStage : uint32_t {
    BLANK = 0,
    PORT = 1,
    VLAN = 2,
    INTERFACE = 4,
    NEIGHBOR = 8,
    QUEUE = 16,
    LAG = 32,
    SYSTEM_PORT = 64,
    VRF = 128,
  };
  ~ManagerTestBase() override;
  void setupSaiPlatform();
  virtual std::unique_ptr<SaiPlatform> createSaiPlatformForTest(
      std::unique_ptr<PlatformProductInfo> productInfo);

  /*
   * TestPort, TestRemoteHost, and TestInterface are helper structs for
   * creating a really basic test setup consistently across the SAI manager
   * tests. They are not strictly necessary at all, but housing the data this
   * way made writing quick setup methods easier than config or creating
   * SwitchState objects directly. If these start getting complex, we
   * SHOULD get rid of them and use one of those clumsier, but more direct
   * interfaces directly.
   */
  struct TestPort {
    int id{0};
    bool enabled{true};
    cfg::PortSpeed portSpeed{cfg::PortSpeed::TWENTYFIVEG};
  };
  struct TestRemoteHost {
    int id{0};
    TestPort port;
    folly::IPAddress ip;
    folly::MacAddress mac;
  };
  struct TestInterface {
    int id{0};
    folly::MacAddress routerMac;
    folly::IPAddress routerIp;
    folly::CIDRNetwork subnet;
    std::vector<TestRemoteHost> remoteHosts;
    int mtu{1500};
    RouterID routerId{0};
    TestInterface() {}
    TestInterface(int id, size_t numHosts) : id(id) {
      if (id > 245) {
        XLOG(FATAL) << "TestInterface doesn't support id >245";
      }
      if (numHosts > 9) {
        XLOG(FATAL) << "TestInterface doesn't support >9 attached hosts";
      }
      routerMac = folly::MacAddress{fmt::format("42:42:42:42:42:{:x}", id)};
      auto subnetBase = fmt::format("10.10.{}", id + 10);
      routerIp = folly::IPAddress{fmt::format("{}.0", subnetBase)};
      subnet = folly::CIDRNetwork{routerIp, 24};
      remoteHosts.resize(numHosts);
      size_t count = 0;
      for (auto& remoteHost : remoteHosts) {
        remoteHost.ip =
            folly::IPAddress{fmt::format("{}.{}", subnetBase, count)};
        remoteHost.mac =
            folly::MacAddress{fmt::format("10:10:10:10:10:{:x}", count)};
        remoteHost.port.id = id * 10 + count;
        ++count;
      }
    }
    explicit TestInterface(int id) : TestInterface(id, 1) {}
  };
  struct TestRoute {
    folly::CIDRNetwork destination;
    std::vector<TestInterface> nextHopInterfaces;
  };

  // dscp, tc, queue id
  using TestQosMapping = std::tuple<uint8_t, uint16_t, uint16_t>;
  using TestQosPolicy = std::vector<TestQosMapping>;

  void SetUp() override;
  void TearDown() override;
  /*
   * Pretend warm boot where we
   * - Ask the store do a warm boot exit
   * - Leak platform (so as to not dereference dead objects from thee
   *   store when we desroy platform->saiSwitch->managerTable
   * - Reload store for tests to verify that the right objects were
   *   recovered by the store
   */
  void pseudoWarmBootExitAndStoreReload();

  std::shared_ptr<ArpEntry> makePendingArpEntry(
      int id,
      const TestRemoteHost& testRemoteHost,
      cfg::InterfaceType type = cfg::InterfaceType::VLAN) const;

  std::shared_ptr<ArpEntry> makeArpEntry(
      int id,
      folly::IPAddressV4 ip,
      folly::MacAddress mac,
      std::optional<sai_uint32_t> metadata = std::nullopt,
      cfg::InterfaceType type = cfg::InterfaceType::VLAN) const;
  std::shared_ptr<ArpEntry> makeArpEntry(
      int id,
      const TestRemoteHost& testRemoteHost,
      std::optional<sai_uint32_t> metadata = std::nullopt,
      std::optional<sai_uint32_t> encapIndex = std::nullopt,
      bool isLocal = true,
      cfg::InterfaceType type = cfg::InterfaceType::VLAN,
      bool noHostRoute = false) const;

  std::shared_ptr<ArpEntry> resolveArp(
      int id,
      const TestRemoteHost& testRemoteHost,
      cfg::InterfaceType type = cfg::InterfaceType::VLAN,
      std::optional<sai_uint32_t> metadata = std::nullopt,
      std::optional<sai_uint32_t> encapIndex = std::nullopt,
      bool isLocal = true,
      bool noHostRoute = false);

  std::shared_ptr<ArpEntry> makeArpEntry(
      const SystemPort& sysPort,
      folly::IPAddressV4 ip,
      folly::MacAddress mac,
      std::optional<sai_uint32_t> encapIndex) const;

  std::shared_ptr<ArpEntry> resolveArp(
      const SystemPort& sysPort,
      folly::IPAddressV4 ip,
      folly::MacAddress mac,
      std::optional<sai_uint32_t> encapIndex) const;

  InterfaceID getIntfID(
      const TestInterface& testInterface,
      cfg::InterfaceType type) const {
    return getIntfID(testInterface.id, type);
  }
  InterfaceID getIntfID(int id, cfg::InterfaceType type) const;
  int64_t getSysPortId(const TestInterface& testInterface) const {
    return getSysPortId(testInterface.id);
  }
  int64_t getSysPortId(int id) const;

  std::shared_ptr<Interface> makeInterface(
      const TestInterface& testInterface,
      cfg::InterfaceType type = cfg::InterfaceType::VLAN) const;

  // A port router interface bound to an aggregate port rather than to a
  // physical port.
  std::shared_ptr<Interface> makeAggregatePortInterface(
      const TestInterface& testInterface,
      AggregatePortID aggregatePortID) const;

  std::shared_ptr<SystemPort> makeSystemPort(
      const std::optional<std::string>& qosPolicy = std::nullopt,
      int64_t sysPortId = 1,
      int64_t switchId = 1) const;

  std::shared_ptr<Interface> makeInterface(
      const SystemPort& sysPort,
      const std::vector<folly::CIDRNetwork>& addresses) const;

  ResolvedNextHop makeNextHop(const TestInterface& testInterface) const;

  ResolvedNextHop makeMplsNextHop(
      const TestInterface& testInterface,
      const LabelForwardingAction& action) const;

  // If `expectedSpeed` is set, use this speed to create the new Port
  // Otherwise it will just use testPort.portSpeed
  // Because xphy port needs both system and line side profileConfig and
  // pinConfigs while iphy port just need system side config, use a new input
  // `isXphyPort` to differentiate the preparation of profileConfig and
  // pinConfigs
  std::shared_ptr<Port> makePort(
      const TestPort& testPort,
      std::optional<cfg::PortSpeed> expectedSpeed = std::nullopt,
      bool isXphyPort = false) const;

  std::shared_ptr<Route<folly::IPAddressV4>> makeRoute(const TestRoute& route);

  std::shared_ptr<Vlan> makeVlan(const TestInterface& testInterface) const;

  std::shared_ptr<AggregatePort> makeAggregatePort(
      const TestInterface& testInterface) const;

  std::vector<QueueSaiId> getPortQueueSaiIds(const SaiPortHandle* portHandle);

  std::shared_ptr<PortQueue> makePortQueue(
      uint8_t queueId,
      cfg::StreamType streamType = cfg::StreamType::UNICAST,
      cfg::QueueScheduling schedType =
          cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN,
      uint8_t weight = 24,
      uint64_t minPps = 12000,
      uint64_t maxPps = 60000) const;

  QueueConfig makeQueueConfig(
      std::vector<uint8_t> queueIds,
      cfg::StreamType streamType = cfg::StreamType::UNICAST,
      cfg::QueueScheduling schedType =
          cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN,
      uint8_t weight = 24,
      uint64_t minPps = 12000,
      uint64_t maxPps = 60000) const;

  std::shared_ptr<QosPolicy> makeQosPolicy(
      const std::string& name,
      const TestQosPolicy& qosPolicy);

  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<SaiPlatform> saiPlatform;
  std::shared_ptr<SaiApiTable> saiApiTable;
  std::unique_ptr<ConcurrentIndices> concurrentIndices;
  SaiStore* saiStore{nullptr};
  SaiManagerTable* saiManagerTable{nullptr};

  std::array<TestInterface, 10> testInterfaces;
  std::array<TestInterface, 10> testRemoteInterfaces;
  uint32_t setupStage{SetupStage::BLANK};
  // Interface id to VRF, set before SetUp(); VRFs are created with
  // SetupStage::VRF
  std::unordered_map<int, RouterID> testInterfaceRouterIds;

  void applyNewState(const std::shared_ptr<SwitchState>& newState);

  std::shared_ptr<SwitchState> getProgrammedState();

  std::shared_ptr<SwitchState> programmedState;
  std::unique_ptr<NextHopIDManager> nextHopIDManager_;

  static constexpr int kSysPortOffset = 100;
  static constexpr double kSpeedConversionFactor = 1000.;
  static constexpr double kRateConversionFactor = 1024. * 1024. * 1024.;

  const SwitchIdScopeResolver& scopeResolver() const;

  std::unique_ptr<SwitchIdScopeResolver> resolver{};

 private:
  static constexpr uint8_t kPortQueueMax = 8;
};

} // namespace facebook::fboss
