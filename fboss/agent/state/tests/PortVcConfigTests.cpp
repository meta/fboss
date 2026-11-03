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
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace facebook::fboss;
using std::make_shared;

namespace {

// The two lossless classes SUSWs run today: pg2 rdma, pg6 monitoring.
cfg::CbfcConfig makeCbfcConfig() {
  cfg::PortVcConfig rdma;
  rdma.id() = 2;
  rdma.name() = "rdma";
  rdma.senderEnable() = true;
  rdma.receiverEnable() = true;
  rdma.reservedCreditSize() = 36;

  cfg::PortVcConfig monitoring;
  monitoring.id() = 6;
  monitoring.name() = "monitoring";
  monitoring.senderEnable() = true;
  monitoring.receiverEnable() = true;
  monitoring.reservedCreditSize() = 12;

  cfg::CbfcConfig cbfcConfig;
  cbfcConfig.virtualChannels() = {rdma, monitoring};
  cbfcConfig.senderCreditLimit() = 8192;
  cbfcConfig.senderCreditSize() = 56;
  cbfcConfig.senderPacketOverhead() = 16;
  return cbfcConfig;
}

cfg::SwitchConfig makeConfigWithCbfc(const cfg::CbfcConfig& cbfcConfig) {
  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  config.ports()[0].cbfcConfigName() = "cbfc_foo";
  config.cbfcConfigs() = {{"cbfc_foo", cbfcConfig}};
  return config;
}

} // unnamed namespace

TEST(PortVcConfig, ConfigApplied) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(makeCbfcConfig());

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);

  EXPECT_EQ(port->getCbfcConfigName(), "cbfc_foo");
  EXPECT_EQ(port->getCbfcSenderCreditLimit(), 8192);
  EXPECT_EQ(port->getCbfcSenderCreditSize(), 56);
  EXPECT_EQ(port->getCbfcSenderPacketOverhead(), 16);

  auto vcs = port->getVirtualChannels();
  ASSERT_TRUE(vcs);
  ASSERT_EQ(vcs->size(), 2);

  auto rdma = vcs->at(0)->toThrift();
  EXPECT_EQ(*rdma.id(), 2);
  EXPECT_EQ(*rdma.name(), "rdma");
  EXPECT_TRUE(*rdma.senderEnable());
  EXPECT_TRUE(*rdma.receiverEnable());
  EXPECT_EQ(*rdma.reservedCreditSize(), 36);

  auto monitoring = vcs->at(1)->toThrift();
  EXPECT_EQ(*monitoring.id(), 6);
  EXPECT_EQ(*monitoring.reservedCreditSize(), 12);
}

TEST(PortVcConfig, ConfigNameMissingFromMap) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(makeCbfcConfig());
  // Port names a config set that is not in cbfcConfigs.
  config.ports()[0].cbfcConfigName() = "cbfc_bar";

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, NoCbfcConfigsMap) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  // Port names a config set but there is no map at all.
  config.ports()[0].cbfcConfigName() = "cbfc_foo";

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, RejectedOnManagementPort) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(makeCbfcConfig());
  config.ports()[0].portType() = cfg::PortType::MANAGEMENT_PORT;

  try {
    publishAndApplyConfig(stateV0, &config, platform.get());
    FAIL() << "CBFC on a management port must be rejected";
  } catch (const FbossError& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("only interface ports"));
  }
}

namespace {

cfg::SwitchConfig makeConfigWithCbfcAndPfc(bool pfcTx, bool pfcRx) {
  auto config = makeConfigWithCbfc(makeCbfcConfig());
  cfg::PortPgConfig pg;
  pg.id() = 2;
  config.portPgConfigs() = {{"pg_foo", {pg}}};
  cfg::PortPfc pfc;
  pfc.tx() = pfcTx;
  pfc.rx() = pfcRx;
  pfc.portPgConfigName() = "pg_foo";
  config.ports()[0].pfc() = pfc;
  return config;
}

void expectPfcAndCbfcRejected(bool pfcTx, bool pfcRx) {
  // No PG config here: enabling PFC with lossless priorities resolves a
  // switch-wide scope the mock config cannot provide, and the apply would fail
  // for that unrelated reason instead.
  auto config = makeConfigWithCbfc(makeCbfcConfig());
  cfg::PortPfc pfc;
  pfc.tx() = pfcTx;
  pfc.rx() = pfcRx;
  config.ports()[0].pfc() = pfc;
  auto platform = createMockPlatform();
  try {
    publishAndApplyConfig(make_shared<SwitchState>(), &config, platform.get());
    FAIL() << "PFC and CBFC on the same port must be rejected";
  } catch (const FbossError& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("PFC and CBFC"));
  }
}

} // unnamed namespace

TEST(PortVcConfig, RejectedWithPfcRx) {
  expectPfcAndCbfcRejected(/*pfcTx=*/false, /*pfcRx=*/true);
}

TEST(PortVcConfig, RejectedWithPfcTx) {
  expectPfcAndCbfcRejected(/*pfcTx=*/true, /*pfcRx=*/false);
}

TEST(PortVcConfig, AcceptedWithPfcDisabledAndPgConfigKept) {
  // PG carving is reachable only through PortPfc, so a CBFC port keeps it by
  // carrying pfc with tx and rx both off.
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfcAndPfc(/*pfcTx=*/false, /*pfcRx=*/false);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(port->getCbfcConfigName(), "cbfc_foo");
  ASSERT_TRUE(port->getPortPgConfigs());
  EXPECT_EQ(port->getPortPgConfigs()->size(), 1);
}

TEST(PortVcConfig, VcIdOutOfRange) {
  auto cbfcConfig = makeCbfcConfig();
  // PORT_VC_VALUE_MAX is 31; 32 is one past the last valid index.
  cbfcConfig.virtualChannels()[0].id() = 32;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, VcIdNegative) {
  auto cbfcConfig = makeCbfcConfig();
  // PortVcConfig.id is i16, so a negative index is representable.
  cbfcConfig.virtualChannels()[0].id() = -1;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, DuplicateVcId) {
  auto cbfcConfig = makeCbfcConfig();
  // Two VCs claiming the same index would race for the same credit counters.
  cbfcConfig.virtualChannels()[1].id() = 2;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, EmptyVirtualChannels) {
  auto cbfcConfig = makeCbfcConfig();
  // Legal thrift -- virtualChannels is a plain list -- but it would leave the
  // port with cbfcConfigName set and no virtual channels, which the SAI layer
  // reads as CBFC being configured.
  cbfcConfig.virtualChannels()->clear();
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, NegativeReservedCreditSize) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.virtualChannels()[0].reservedCreditSize() = -1;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, NegativeSenderCreditLimit) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderCreditLimit() = -1;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, SenderCreditLimitAboveMax) {
  auto cbfcConfig = makeCbfcConfig();
  // One past PORT_CBFC_SENDER_CREDIT_LIMIT_MAX. Anything at or above 2^32 also
  // wraps to 0 in the cast to sai_uint32_t, silently disabling the limit.
  cbfcConfig.senderCreditLimit() =
      cfg::switch_config_constants::PORT_CBFC_SENDER_CREDIT_LIMIT_MAX() + 1;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, SenderCreditLimitAtMax) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderCreditLimit() =
      cfg::switch_config_constants::PORT_CBFC_SENDER_CREDIT_LIMIT_MAX();
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  EXPECT_EQ(
      stateV1->getPorts()->getNodeIf(PortID(1))->getCbfcSenderCreditLimit(),
      cfg::switch_config_constants::PORT_CBFC_SENDER_CREDIT_LIMIT_MAX());
}

TEST(PortVcConfig, ChangeIsNotTreatedAsUnchanged) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(makeCbfcConfig());

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  // Change only CBFC. If the port-unchanged comparison ignores these fields,
  // updatePort returns nullptr and the change is silently dropped.
  auto changed = makeCbfcConfig();
  changed.virtualChannels()[0].reservedCreditSize() = 48;
  config.cbfcConfigs() = {{"cbfc_foo", changed}};

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto port = stateV2->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(
      *port->getVirtualChannels()->at(0)->toThrift().reservedCreditSize(), 48);
}

TEST(PortVcConfig, SenderCreditLimitChangeApplied) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(makeCbfcConfig());
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  auto changed = makeCbfcConfig();
  changed.senderCreditLimit() = 16384;
  config.cbfcConfigs() = {{"cbfc_foo", changed}};

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  EXPECT_EQ(
      stateV2->getPorts()->getNodeIf(PortID(1))->getCbfcSenderCreditLimit(),
      16384);
}

TEST(PortVcConfig, RemovalClearsState) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderCreditSize() = 56;
  cbfcConfig.senderPacketOverhead() = 16;
  auto config = makeConfigWithCbfc(cbfcConfig);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  ASSERT_TRUE(stateV1->getPorts()
                  ->getNodeIf(PortID(1))
                  ->getCbfcConfigName()
                  .has_value());

  // Drop the CBFC config entirely. The new port is cloned from the old one, so
  // stale values persist unless the setters are called unconditionally.
  config.ports()[0].cbfcConfigName().reset();
  config.cbfcConfigs().reset();

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto port = stateV2->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  EXPECT_FALSE(port->getCbfcConfigName().has_value());
  EXPECT_FALSE(port->getCbfcSenderCreditLimit().has_value());
  EXPECT_FALSE(port->getCbfcSenderCreditSize().has_value());
  EXPECT_FALSE(port->getCbfcSenderPacketOverhead().has_value());
  EXPECT_FALSE(port->getVirtualChannels());
}

TEST(PortVcConfig, AbsentWhenNotConfigured) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  EXPECT_FALSE(port->getCbfcConfigName().has_value());
  EXPECT_FALSE(port->getCbfcSenderCreditLimit().has_value());
  // Unset, not an empty list. The SAI layer skips a port on this test alone,
  // so a present-but-empty list would send it on to program hardware.
  EXPECT_FALSE(port->getVirtualChannels());
}

TEST(PortVcConfig, MissingSenderCreditSize) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderCreditSize().reset();
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, SenderCreditSizeApplied) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderCreditSize() = 32;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(port->getCbfcSenderCreditSize(), 32);
}

TEST(PortVcConfig, SenderCreditSizeChangeApplied) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderCreditSize() = 32;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  // Change only the credit size. If the port-unchanged comparison ignores it,
  // updatePort returns nullptr and the change is silently dropped.
  cbfcConfig.senderCreditSize() = 56;
  config.cbfcConfigs() = {{"cbfc_foo", cbfcConfig}};

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  EXPECT_EQ(
      stateV2->getPorts()->getNodeIf(PortID(1))->getCbfcSenderCreditSize(), 56);
}

TEST(PortVcConfig, UnsupportedSenderCreditSize) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderCreditSize() = 128;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, MissingSenderPacketOverhead) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderPacketOverhead().reset();
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(PortVcConfig, SenderPacketOverheadApplied) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderPacketOverhead() = 48;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto port = stateV1->getPorts()->getNodeIf(PortID(1));
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(port->getCbfcSenderPacketOverhead(), 48);
}

TEST(PortVcConfig, SenderPacketOverheadChangeApplied) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderPacketOverhead() = 0;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  cbfcConfig.senderPacketOverhead() = 16;
  config.cbfcConfigs() = {{"cbfc_foo", cbfcConfig}};

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  EXPECT_EQ(
      stateV2->getPorts()->getNodeIf(PortID(1))->getCbfcSenderPacketOverhead(),
      16);
}

TEST(PortVcConfig, UnsupportedSenderPacketOverhead) {
  auto cbfcConfig = makeCbfcConfig();
  cbfcConfig.senderPacketOverhead() = 8;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto config = makeConfigWithCbfc(cbfcConfig);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}
