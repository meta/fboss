// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/io/Cursor.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <set>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "gmock/gmock.h"

using namespace facebook::fboss;
using ::testing::_;

namespace {
const uint8_t kNCStrictPriorityQueue = 7;

std::unique_ptr<HwTestHandle> setupTestHandle() {
  auto state = testStateAWithPortsUp();
  addSwitchInfo(
      state,
      cfg::SwitchType::NPU,
      0, /*SwitchId*/
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0, /* switchIndex*/
      std::nullopt, /* sysPort min*/
      std::nullopt, /*sysPort max()*/
      MockPlatform::getMockLocalMac().toString());
  return createTestHandle(state, SwitchFlags::DEFAULT);
}

TxMatchFn checkLldpPDU() {
  return [](const TxPacket* pkt) {
    folly::io::Cursor c(pkt->buf());
    auto dstMac = PktUtil::readMac(&c);
    if (dstMac != LldpManager::LLDP_DEST_MAC) {
      throw FbossError("expected LLDP dest MAC; got ", dstMac);
    }
  };
}

constexpr int64_t kIntervalMs = LldpManager::LLDP_INTERVAL;

std::vector<PortID> lldpTxPorts(SwSwitch* sw) {
  std::vector<PortID> ports;
  for (const auto& portMap : std::as_const(*sw->getState()->getPorts())) {
    for (const auto& [_, port] : std::as_const(*portMap.second)) {
      const auto type = port->getPortType();
      if (port->isPortUp() &&
          (type == cfg::PortType::INTERFACE_PORT ||
           type == cfg::PortType::MANAGEMENT_PORT ||
           type == cfg::PortType::HYPER_PORT_MEMBER)) {
        ports.push_back(port->getID());
      }
    }
  }
  return ports;
}

void recordLldpSends(SwSwitch* sw, std::vector<PortID>& sent) {
  EXPECT_HW_CALL(
      sw,
      sendPacketOutOfPortAsync_(
          TxPacketMatcher::createMatcher("Lldp PDU", checkLldpPDU()),
          _,
          std::optional<uint8_t>(kNCStrictPriorityQueue)))
      .WillRepeatedly(
          testing::Invoke(
              [&sent](TxPacket* pkt, PortID port, std::optional<uint8_t>) {
                delete pkt;
                sent.push_back(port);
                return true;
              }));
}

void setPortOperState(SwSwitch* sw, PortID id, bool up) {
  sw->updateState(
      "set port oper state", [id, up](const std::shared_ptr<SwitchState>& in) {
        auto state = in->clone();
        state->getPorts()->getNodeIf(id)->modify(&state)->setOperState(up);
        return state;
      });
  waitForStateUpdates(sw);
}

size_t numUnique(const std::vector<PortID>& ports) {
  return std::set<PortID>(ports.begin(), ports.end()).size();
}

TEST(LldpPacingTest, LldpSendPacedOnePortPerSlot) {
  gflags::FlagSaver flagSaver;
  FLAGS_lldp_tx_ports_per_slot = 1;
  std::vector<PortID> sent;
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  const size_t numPorts = lldpTxPorts(sw).size();
  ASSERT_GE(numPorts, 4) << "pacing tests need several LLDP ports up";
  recordLldpSends(sw, sent);
  LldpManager lldpManager(sw);

  auto delay = lldpManager.sendNextLldpSlot();
  EXPECT_EQ(sent.size(), 1);
  EXPECT_EQ(delay.count(), kIntervalMs / static_cast<int64_t>(numPorts));
  int64_t totalMs = delay.count();
  for (size_t slot = 1; slot < numPorts; ++slot) {
    totalMs += lldpManager.sendNextLldpSlot().count();
    EXPECT_EQ(sent.size(), slot + 1);
  }
  EXPECT_EQ(totalMs, kIntervalMs);
  EXPECT_EQ(numUnique(sent), numPorts);
}

TEST(LldpPacingTest, LldpSendBurstWhenPacingOff) {
  for (int perSlot : {0, -1}) {
    gflags::FlagSaver flagSaver;
    FLAGS_lldp_tx_ports_per_slot = perSlot;
    std::vector<PortID> sent;
    auto handle = setupTestHandle();
    auto sw = handle->getSw();
    const size_t numPorts = lldpTxPorts(sw).size();
    ASSERT_GE(numPorts, 4) << "pacing tests need several LLDP ports up";
    recordLldpSends(sw, sent);
    LldpManager lldpManager(sw);

    EXPECT_EQ(lldpManager.sendNextLldpSlot().count(), kIntervalMs);
    EXPECT_EQ(sent.size(), numPorts);
    EXPECT_EQ(numUnique(sent), numPorts);
  }
}

TEST(LldpPacingTest, LldpSendPacedSeveralPortsPerSlot) {
  for (size_t perSlot : {2, 3}) {
    gflags::FlagSaver flagSaver;
    FLAGS_lldp_tx_ports_per_slot = perSlot;
    std::vector<PortID> sent;
    auto handle = setupTestHandle();
    auto sw = handle->getSw();
    const size_t numPorts = lldpTxPorts(sw).size();
    ASSERT_GE(numPorts, 4) << "pacing tests need several LLDP ports up";
    recordLldpSends(sw, sent);
    LldpManager lldpManager(sw);

    const size_t numSlots = (numPorts + perSlot - 1) / perSlot;
    int64_t totalMs = 0;
    for (size_t slot = 0; slot < numSlots; ++slot) {
      const auto before = sent.size();
      totalMs += lldpManager.sendNextLldpSlot().count();
      EXPECT_GE(sent.size() - before, 1);
      EXPECT_LE(sent.size() - before, perSlot);
    }
    EXPECT_EQ(sent.size(), numPorts);
    EXPECT_EQ(numUnique(sent), numPorts);
    EXPECT_EQ(totalMs, kIntervalMs);
  }
}

TEST(LldpPacingTest, LldpSendPacedSkipsDownPorts) {
  gflags::FlagSaver flagSaver;
  FLAGS_lldp_tx_ports_per_slot = 1;
  std::vector<PortID> sent;
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  const auto ports = lldpTxPorts(sw);
  const size_t numPorts = ports.size();
  ASSERT_GE(numPorts, 2);
  recordLldpSends(sw, sent);
  LldpManager lldpManager(sw);

  lldpManager.sendNextLldpSlot();
  ASSERT_EQ(sent.size(), 1);
  const PortID downPort(ports[0] == sent[0] ? ports[1] : ports[0]);
  setPortOperState(sw, downPort, false);

  size_t emptySlots = 0;
  for (size_t slot = 1; slot < numPorts; ++slot) {
    const auto before = sent.size();
    lldpManager.sendNextLldpSlot();
    emptySlots += sent.size() == before ? 1 : 0;
  }
  EXPECT_EQ(emptySlots, 1);
  EXPECT_EQ(sent.size(), numPorts - 1);
  EXPECT_EQ(numUnique(sent), numPorts - 1);
  EXPECT_EQ(std::count(sent.begin(), sent.end(), downPort), 0);
}

TEST(LldpPacingTest, LldpSendPacedResnapshotsEachCycle) {
  gflags::FlagSaver flagSaver;
  FLAGS_lldp_tx_ports_per_slot = 1;
  std::vector<PortID> sent;
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  const size_t numPorts = lldpTxPorts(sw).size();
  ASSERT_GE(numPorts, 4) << "pacing tests need several LLDP ports up";
  recordLldpSends(sw, sent);
  LldpManager lldpManager(sw);

  for (size_t slot = 0; slot < numPorts; ++slot) {
    lldpManager.sendNextLldpSlot();
  }
  ASSERT_EQ(sent.size(), numPorts);
  const PortID downPort = sent[0];
  setPortOperState(sw, downPort, false);
  sent.clear();

  const size_t numUp = numPorts - 1;
  auto delay = lldpManager.sendNextLldpSlot();
  EXPECT_EQ(sent.size(), 1);
  EXPECT_EQ(delay.count(), kIntervalMs / numUp);
  int64_t totalMs = delay.count();
  for (size_t slot = 1; slot < numUp; ++slot) {
    totalMs += lldpManager.sendNextLldpSlot().count();
  }
  EXPECT_EQ(totalMs, kIntervalMs);
  EXPECT_EQ(sent.size(), numUp);
  EXPECT_EQ(numUnique(sent), numUp);
  EXPECT_EQ(std::count(sent.begin(), sent.end(), downPort), 0);
}

} // unnamed namespace
