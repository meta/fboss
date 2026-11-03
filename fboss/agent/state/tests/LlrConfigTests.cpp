/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/LlrConfig.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;
using std::make_shared;

namespace {

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

constexpr auto kLlrName = "llrProfile";

// A fully-populated LlrConfig with non-default values so tests can verify each
// field is resolved onto the Port rather than left at its thrift default.
cfg::LlrConfig makeLlrConfig() {
  cfg::LlrConfig llr;
  llr.outstandingFramesMax() = 512;
  llr.outstandingBytesMax() = 300000;
  llr.replayTimerMax() = 5000;
  llr.replayCountMax() = 3;
  llr.pcsLostTimeout() = 1000;
  llr.dataAgeTimeout() = 200000;
  llr.initFrameAction() = cfg::LlrFrameAction::DISCARD;
  llr.flushFrameAction() = cfg::LlrFrameAction::BLOCK;
  llr.reInitOnFlush() = true;
  llr.ctlosTargetSpacing() = 4096;
  return llr;
}

void setAsicType(cfg::SwitchConfig& config, cfg::AsicType asicType) {
  config.switchSettings()->switchIdToSwitchInfo() = {
      {0, createSwitchInfo(cfg::SwitchType::NPU, asicType)}};
}

} // namespace

// A port referencing a named LlrConfig resolves both the name and an embedded
// copy of the profile onto the Port, and the profile lands in the SwitchState
// LlrConfig map (LLR-capable ASIC).
TEST(LlrConfigTest, resolveOntoPort) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  setAsicType(config, cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1);
  config.llrConfigs() = {{kLlrName, makeLlrConfig()}};
  config.ports()[0].llrConfigName() = kLlrName;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  ASSERT_TRUE(port->getLlrConfigName().has_value());
  EXPECT_EQ(*port->getLlrConfigName(), kLlrName);

  // The resolved copy on the port carries every register from the config.
  ASSERT_TRUE(port->getLlrConfig().has_value());
  auto resolved = port->getLlrConfig().value();
  ASSERT_NE(nullptr, resolved);
  EXPECT_EQ(resolved->getOutstandingFramesMax(), 512);
  EXPECT_EQ(resolved->getOutstandingBytesMax(), 300000);
  EXPECT_EQ(resolved->getReplayTimerMax(), 5000);
  EXPECT_EQ(resolved->getReplayCountMax(), 3);
  EXPECT_EQ(resolved->getPcsLostTimeout(), 1000);
  EXPECT_EQ(resolved->getDataAgeTimeout(), 200000);
  EXPECT_EQ(resolved->getInitFrameAction(), cfg::LlrFrameAction::DISCARD);
  EXPECT_EQ(resolved->getFlushFrameAction(), cfg::LlrFrameAction::BLOCK);
  EXPECT_TRUE(resolved->getReInitOnFlush());
  EXPECT_EQ(resolved->getCtlosTargetSpacing(), 4096);

  // The profile is also present in the SwitchState LlrConfig map.
  auto llrConfigs = stateV1->getLlrConfigs();
  ASSERT_NE(nullptr, llrConfigs);
  auto node = llrConfigs->getNodeIf(kLlrName);
  ASSERT_NE(nullptr, node);
  EXPECT_EQ(node->getReplayCountMax(), 3);
}

// Clearing a port's llrConfigName removes the resolved config from the Port.
TEST(LlrConfigTest, unconfigureClearsPort) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  setAsicType(config, cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1);
  config.llrConfigs() = {{kLlrName, makeLlrConfig()}};
  config.ports()[0].llrConfigName() = kLlrName;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  ASSERT_TRUE(stateV1->getPorts()
                  ->getNodeIf(PortID(1))
                  ->getLlrConfigName()
                  .has_value());

  config.ports()[0].llrConfigName().reset();
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto port = stateV2->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  EXPECT_FALSE(port->getLlrConfigName().has_value());
  EXPECT_FALSE(port->getLlrConfig().has_value());
}

// Loud rejection: a port with an LlrConfig on an ASIC that does not support
// LINK_LAYER_RETRANSMISSION is rejected at config time.
TEST(LlrConfigTest, rejectOnUnsupportedAsic) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  setAsicType(config, cfg::AsicType::ASIC_TYPE_TOMAHAWK5);
  config.llrConfigs() = {{kLlrName, makeLlrConfig()}};
  config.ports()[0].llrConfigName() = kLlrName;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

// A port referencing an LlrConfig name that is absent from the llrConfigs map
// is rejected.
TEST(LlrConfigTest, rejectMissingConfigName) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  setAsicType(config, cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1);
  config.ports()[0].llrConfigName() = kLlrName;

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

// Loud rejection: LlrConfig thrift fields are signed and wider than their
// unsigned SAI attribute; a value that is negative or exceeds the SAI width is
// rejected at config time rather than silently wrapping when programmed.
TEST(LlrConfigTest, rejectOutOfRange) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  setAsicType(config, cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1);
  config.ports()[0].llrConfigName() = kLlrName;

  // replayCountMax maps to a u8 SAI attribute; 256 overflows it.
  auto badReplayCount = makeLlrConfig();
  badReplayCount.replayCountMax() = 256;
  config.llrConfigs() = {{kLlrName, badReplayCount}};
  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);

  // ctlosTargetSpacing maps to a u16 SAI attribute; 70000 overflows it.
  auto badCtlos = makeLlrConfig();
  badCtlos.ctlosTargetSpacing() = 70000;
  config.llrConfigs() = {{kLlrName, badCtlos}};
  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);

  // A negative value (thrift is signed) is rejected too.
  auto negative = makeLlrConfig();
  negative.dataAgeTimeout() = -1;
  config.llrConfigs() = {{kLlrName, negative}};
  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

// Loud rejection: values that fit the SAI attribute width but exceed the
// tighter UE Spec 1.0.2 Table 5-9 bounds are rejected at config time.
TEST(LlrConfigTest, rejectOutOfSpecRange) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  setAsicType(config, cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1);
  config.ports()[0].llrConfigName() = kLlrName;

  auto expectRejected = [&](const cfg::LlrConfig& llr) {
    config.llrConfigs() = {{kLlrName, llr}};
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
  };

  // outstanding_seq_max spec max is 524288.
  auto badFrames = makeLlrConfig();
  badFrames.outstandingFramesMax() = 524289;
  expectRejected(badFrames);

  // replay_timer_max spec max is 65535 ns.
  auto badReplayTimer = makeLlrConfig();
  badReplayTimer.replayTimerMax() = 65536;
  expectRejected(badReplayTimer);

  // ctlos_target_spacing spec range is 400..16384 (both bounds enforced).
  auto ctlosTooLow = makeLlrConfig();
  ctlosTooLow.ctlosTargetSpacing() = 399;
  expectRejected(ctlosTooLow);

  auto ctlosTooHigh = makeLlrConfig();
  ctlosTooHigh.ctlosTargetSpacing() = 16385;
  expectRejected(ctlosTooHigh);
}

namespace {
std::shared_ptr<Port> makeLlrPort(
    const std::optional<std::string>& name,
    std::optional<int32_t> outstandingBytesMax) {
  state::PortFields fields;
  fields.portId() = PortID(1);
  fields.portName() = "port1";
  auto port = std::make_shared<Port>(std::move(fields));
  port->setLlrConfigName(name);
  if (outstandingBytesMax.has_value()) {
    // An lvalue: passing a temporary here selects NodeBaseT's variadic
    // constructor over LlrConfig(const std::string&).
    const std::string id = name.value_or("llr");
    auto llr = std::make_shared<LlrConfig>(id);
    llr->setOutstandingBytesMax(*outstandingBytesMax);
    port->setLlrConfig(llr);
  }
  return port;
}
} // namespace

// llrConfigChanged compares the resolved profile by content.
// ThriftConfigApplier rebuilds every node in the LlrConfig map whenever any
// profile changes, so a port whose own profile is untouched is handed a new
// node holding identical config; comparing nodes by identity would call that a
// change.
TEST(LlrConfigTest, llrConfigChangedComparesByContent) {
  // Neither port has LLR.
  EXPECT_FALSE(llrConfigChanged(
      makeLlrPort(std::nullopt, std::nullopt),
      makeLlrPort(std::nullopt, std::nullopt)));

  // Distinct nodes holding identical config are not a change.
  auto a = makeLlrPort("llr", 105800);
  auto b = makeLlrPort("llr", 105800);
  EXPECT_NE(a->getLlrConfig().value(), b->getLlrConfig().value());
  EXPECT_FALSE(llrConfigChanged(a, b));

  // A different profile name is a change even when the content matches: the
  // two resolve to separate SAI profile objects.
  EXPECT_FALSE(llrConfigChanged(a, makeLlrPort("llr", 105800)));
  EXPECT_TRUE(llrConfigChanged(a, makeLlrPort("llrOther", 105800)));

  // Retuning under the same name is a change.
  EXPECT_TRUE(llrConfigChanged(a, makeLlrPort("llr", 64000)));

  // Binding and unbinding are changes.
  EXPECT_TRUE(llrConfigChanged(makeLlrPort(std::nullopt, std::nullopt), a));
  EXPECT_TRUE(llrConfigChanged(a, makeLlrPort(std::nullopt, std::nullopt)));
}
