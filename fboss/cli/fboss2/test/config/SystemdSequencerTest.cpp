// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/session/SystemdSequencer.h"
#include "fboss/cli/fboss2/test/config/MockSystemdInterface.h"

namespace facebook::fboss {

using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

TEST(SystemdSequencerTest, RestartAndWait) {
  auto systemd = std::make_unique<StrictMock<MockSystemdInterface>>();
  InSequence seq;
  EXPECT_CALL(*systemd, restartService("a"));
  EXPECT_CALL(*systemd, waitForServiceActive("a", _, _));

  SystemdSequencer sequencer(std::move(systemd));
  sequencer.restartAndWait("a");
}

TEST(SystemdSequencerTest, ResolveUnitPrefersClassic) {
  auto systemd = std::make_unique<StrictMock<MockSystemdInterface>>();
  EXPECT_CALL(*systemd, getMatchingServices("bgpd.service"))
      .WillOnce(Return(std::vector<std::string>{"bgpd.service"}));

  const SystemdSequencer sequencer(std::move(systemd));
  EXPECT_EQ(sequencer.resolveUnit("bgpd", "netos.service.fboss_bgp"), "bgpd");
}

TEST(SystemdSequencerTest, ResolveUnitFallsBackToNetosPattern) {
  auto systemd = std::make_unique<StrictMock<MockSystemdInterface>>();
  EXPECT_CALL(*systemd, getMatchingServices("fboss_hw_agent@0.service"))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(
      *systemd,
      getMatchingServices("netos.service.fboss_wedge_agent_*_0.service"))
      .WillOnce(Return(
          std::vector<std::string>{
              "netos.service.fboss_wedge_agent_brcm_0.service"}));

  const SystemdSequencer sequencer(std::move(systemd));
  EXPECT_EQ(
      sequencer.resolveUnit(
          "fboss_hw_agent@0", "netos.service.fboss_wedge_agent_*_0"),
      "netos.service.fboss_wedge_agent_brcm_0");
}

TEST(SystemdSequencerTest, ResolveUnitRejectsSeveralOrNoMatches) {
  for (const auto& matches :
       {std::vector<std::string>{},
        std::vector<std::string>{"x_1.service", "x_2.service"}}) {
    auto systemd = std::make_unique<NiceMock<MockSystemdInterface>>();
    ON_CALL(*systemd, getMatchingServices("classic.service"))
        .WillByDefault(Return(std::vector<std::string>{}));
    ON_CALL(*systemd, getMatchingServices("x_*.service"))
        .WillByDefault(Return(matches));

    const SystemdSequencer sequencer(std::move(systemd));
    EXPECT_THROW(sequencer.resolveUnit("classic", "x_*"), std::runtime_error);
  }
}

} // namespace facebook::fboss
