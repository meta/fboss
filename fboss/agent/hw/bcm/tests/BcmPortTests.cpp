/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmPortUtils.h"
#include "fboss/agent/hw/bcm/tests/BcmTest.h"
#include "fboss/agent/hw/bcm/tests/BcmTestUtils.h"
#include "fboss/agent/hw/test/HwPortUtils.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

#include <fmt/format.h>

extern "C" {
#include <bcm/port.h>
}

namespace {
bool isFlexModeSupported(
    facebook::fboss::BcmTestPlatform* platform,
    facebook::fboss::FlexPortMode desiredMode) {
  for (auto flexMode : platform->getSupportedFlexPortModes()) {
    if (flexMode == desiredMode) {
      return true;
    }
  }
  return false;
}

void getSflowRates(
    int unit,
    bcm_port_t port,
    int* ingressRate,
    int* egressRate) {
  auto rv = bcm_port_sample_rate_get(unit, port, ingressRate, egressRate);
  facebook::fboss::bcmCheckError(rv, "failed to get port sflow rates");
}

} // namespace

namespace facebook::fboss {

class BcmPortTest : public BcmTest {
 protected:
  std::vector<PortID> initialConfiguredPorts() const {
    return {masterLogicalPortIds()[0], masterLogicalPortIds()[1]};
  }
  cfg::SwitchConfig initialConfig() const override {
    return utility::oneL3IntfTwoPortConfig(
        getHwSwitch()->getPlatform()->getPlatformMapping(),
        getHwSwitch()->getPlatform()->getAsic(),
        masterLogicalPortIds()[0],
        masterLogicalPortIds()[1],
        getHwSwitch()->getPlatform()->supportsAddRemovePort());
  }
};

TEST_F(BcmPortTest, PortApplyConfig) {
  auto setup = [this]() { applyNewConfig(initialConfig()); };
  auto verify = [this]() {
    for (auto portId : initialConfiguredPorts()) {
      // check port should exist in portTable_
      auto bcmPort =
          getHwSwitch()->getPortTable()->getBcmPortIf(PortID(portId));
      ASSERT_TRUE(bcmPort != nullptr);
      utility::assertPortStatus(getHwSwitch(), portId);
      int loopbackMode;
      auto rv = bcm_port_loopback_get(getUnit(), portId, &loopbackMode);
      bcmCheckError(rv, "Failed to get loopback mode for port:", portId);
      EXPECT_EQ(BCM_PORT_LOOPBACK_NONE, loopbackMode);

      // default inter-packet gap if there's no override
      static auto constexpr kDefaultPortInterPacketGapBits = 96;
      EXPECT_EQ(
          bcmPort->getInterPacketGapBits(), kDefaultPortInterPacketGapBits);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, PortSflowConfig) {
  auto constexpr kIngressRate = 10;
  auto constexpr kEgressRate = 20;
  auto setup = [this, kIngressRate, kEgressRate]() {
    auto newCfg = initialConfig();
    for (auto portId : initialConfiguredPorts()) {
      auto portCfg = utility::findCfgPort(newCfg, portId);
      portCfg->sFlowIngressRate() = kIngressRate;
      portCfg->sFlowEgressRate() = kEgressRate;
    }
    applyNewConfig(newCfg);
  };
  auto verify = [this, kIngressRate, kEgressRate]() {
    for (auto portId : initialConfiguredPorts()) {
      int ingressSamplingRate, egressSamplingRate;
      getSflowRates(
          getUnit(), portId, &ingressSamplingRate, &egressSamplingRate);
      ASSERT_EQ(kIngressRate, ingressSamplingRate);
      ASSERT_EQ(kEgressRate, egressSamplingRate);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, PortLoopbackMode) {
  auto setup = [this]() {
    auto newCfg = initialConfig();
    std::vector<cfg::PortLoopbackMode> loopbackModes = {
        cfg::PortLoopbackMode::MAC, cfg::PortLoopbackMode::PHY};
    for (auto index : {0, 1}) {
      auto portId = initialConfiguredPorts()[index];
      auto portCfg = utility::findCfgPort(newCfg, portId);
      portCfg->loopbackMode() = loopbackModes[index];
    }
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    std::map<PortID, int> port2LoopbackMode = {
        {PortID(masterLogicalPortIds()[0]), BCM_PORT_LOOPBACK_MAC},
        {PortID(masterLogicalPortIds()[1]), BCM_PORT_LOOPBACK_PHY},
    };
    utility::assertPortsLoopbackMode(getHwSwitch(), port2LoopbackMode);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, PortLoopbackModeMAC40G) {
  if (!isFlexModeSupported(getPlatform(), FlexPortMode::ONEX40G)) {
    GTEST_SKIP();
  }

  auto setup = [this]() {
    auto newCfg = initialConfig();
    for (auto portId : initialConfiguredPorts()) {
      auto portCfg = utility::findCfgPort(newCfg, portId);
      portCfg->loopbackMode() = cfg::PortLoopbackMode::MAC;
      utility::updatePortSpeed(
          getHwSwitch()->getPlatform()->getPlatformMapping(),
          getHwSwitch()->getPlatform()->supportsAddRemovePort(),
          newCfg,
          portId,
          cfg::PortSpeed::FORTYG);
    }
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    for (auto portId : initialConfiguredPorts()) {
      utility::assertPortLoopbackMode(
          getHwSwitch(), portId, BCM_PORT_LOOPBACK_MAC);
      utility::assertPort(getHwSwitch(), portId, true, cfg::PortSpeed::FORTYG);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, PortLoopbackModePHY40G) {
  if (!isFlexModeSupported(getPlatform(), FlexPortMode::ONEX40G)) {
    GTEST_SKIP();
  }

  auto setup = [this]() {
    auto newCfg = initialConfig();
    for (auto portId : initialConfiguredPorts()) {
      auto portCfg = utility::findCfgPort(newCfg, portId);
      portCfg->loopbackMode() = cfg::PortLoopbackMode::PHY;
      utility::updatePortSpeed(
          getHwSwitch()->getPlatform()->getPlatformMapping(),
          getHwSwitch()->getPlatform()->supportsAddRemovePort(),
          newCfg,
          portId,
          cfg::PortSpeed::FORTYG);
    }
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    for (auto portId : initialConfiguredPorts()) {
      utility::assertPortLoopbackMode(
          getHwSwitch(), portId, BCM_PORT_LOOPBACK_PHY);
      utility::assertPort(getHwSwitch(), portId, true, cfg::PortSpeed::FORTYG);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, PortLoopbackModeMAC100G) {
  if (!isFlexModeSupported(getPlatform(), FlexPortMode::ONEX100G)) {
    GTEST_SKIP();
  }

  auto setup = [this]() {
    auto newCfg = initialConfig();
    for (auto portId : initialConfiguredPorts()) {
      auto portCfg = utility::findCfgPort(newCfg, portId);
      portCfg->loopbackMode() = cfg::PortLoopbackMode::MAC;
      utility::updatePortSpeed(
          getHwSwitch()->getPlatform()->getPlatformMapping(),
          getHwSwitch()->getPlatform()->supportsAddRemovePort(),
          newCfg,
          portId,
          cfg::PortSpeed::HUNDREDG);
    }
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    for (auto portId : initialConfiguredPorts()) {
      utility::assertPortLoopbackMode(
          getHwSwitch(), portId, BCM_PORT_LOOPBACK_MAC);
      utility::assertPort(
          getHwSwitch(), portId, true, cfg::PortSpeed::HUNDREDG);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, PortLoopbackModePHY100G) {
  if (!isFlexModeSupported(getPlatform(), FlexPortMode::ONEX100G)) {
    GTEST_SKIP();
  }

  auto setup = [this]() {
    auto newCfg = initialConfig();
    for (auto portId : initialConfiguredPorts()) {
      auto portCfg = utility::findCfgPort(newCfg, portId);
      portCfg->loopbackMode() = cfg::PortLoopbackMode::PHY;
      utility::updatePortSpeed(
          getHwSwitch()->getPlatform()->getPlatformMapping(),
          getHwSwitch()->getPlatform()->supportsAddRemovePort(),
          newCfg,
          portId,
          cfg::PortSpeed::HUNDREDG);
    }
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    for (auto portId : initialConfiguredPorts()) {
      utility::assertPortLoopbackMode(
          getHwSwitch(), portId, BCM_PORT_LOOPBACK_PHY);
      utility::assertPort(
          getHwSwitch(), portId, true, cfg::PortSpeed::HUNDREDG);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, SampleDestination) {
  // sample destination can't be configured if sflow sampling isn't supported
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING)) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    std::vector<cfg::SampleDestination> sampleDestinations = {
        cfg::SampleDestination::CPU, cfg::SampleDestination::MIRROR};
    for (auto index : {0, 1}) {
      auto portId = initialConfiguredPorts()[index];
      auto portCfg = utility::findCfgPort(newCfg, portId);
      portCfg->sampleDest() = sampleDestinations[index];
      if (sampleDestinations[index] == cfg::SampleDestination::MIRROR) {
        portCfg->ingressMirror() = "sflow";
      }
    }
    cfg::MirrorTunnel tunnel;
    cfg::SflowTunnel sflowTunnel;
    *sflowTunnel.ip() = "10.0.0.1";
    sflowTunnel.udpSrcPort() = 6545;
    sflowTunnel.udpDstPort() = 6343;
    tunnel.sflowTunnel() = sflowTunnel;
    newCfg.mirrors()->resize(1);
    *newCfg.mirrors()[0].name() = "sflow";
    newCfg.mirrors()[0].destination()->tunnel() = tunnel;
    return applyNewConfig(newCfg);
  };
  auto verify = [=, this]() {
    std::map<PortID, int> port2SampleDestination = {
        {PortID(masterLogicalPortIds()[0]), BCM_PORT_CONTROL_SAMPLE_DEST_CPU},
        {PortID(masterLogicalPortIds()[1]),
         BCM_PORT_CONTROL_SAMPLE_DEST_MIRROR},
    };
    utility::assertPortsSampleDestination(
        getHwSwitch(), port2SampleDestination);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, NoSampleDestinationSet) {
  // sample destination can't be configured if sflow sampling isn't supported
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING)) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() { return applyNewConfig(initialConfig()); };
  auto verify = [=, this]() {
    std::map<PortID, int> port2SampleDestination = {
        {PortID(masterLogicalPortIds()[0]), BCM_PORT_CONTROL_SAMPLE_DEST_CPU}};
    utility::assertPortsSampleDestination(
        getHwSwitch(), port2SampleDestination);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, IngressMirror) {
  auto setup = [this]() {
    auto newCfg = initialConfig();
    newCfg.mirrors()->resize(1);
    *newCfg.mirrors()[0].name() = "mirror";
    cfg::MirrorTunnel tunnel;
    cfg::GreTunnel greTunnel;
    *greTunnel.ip() = "1.1.1.10";
    tunnel.greTunnel() = greTunnel;
    newCfg.mirrors()[0].destination()->tunnel() = tunnel;

    auto portId = masterLogicalPortIds()[1];
    auto portCfg = utility::findCfgPort(newCfg, portId);
    portCfg->ingressMirror() = "mirror";
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    auto* bcmPort = getHwSwitch()->getPortTable()->getBcmPort(
        PortID(masterLogicalPortIds()[1]));
    ASSERT_TRUE(bcmPort->getIngressPortMirror().has_value());
    EXPECT_EQ(bcmPort->getIngressPortMirror().value(), "mirror");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, EgressMirror) {
  auto setup = [this]() {
    auto newCfg = initialConfig();
    newCfg.mirrors()->resize(1);
    *newCfg.mirrors()[0].name() = "mirror";
    cfg::MirrorTunnel tunnel;
    cfg::GreTunnel greTunnel;
    *greTunnel.ip() = "1.1.1.10";
    tunnel.greTunnel() = greTunnel;
    newCfg.mirrors()[0].destination()->tunnel() = tunnel;

    auto portId = masterLogicalPortIds()[1];
    auto portCfg = utility::findCfgPort(newCfg, portId);
    portCfg->egressMirror() = "mirror";
    applyNewConfig(newCfg);
  };
  auto verify = [this]() {
    auto* bcmPort = getHwSwitch()->getPortTable()->getBcmPort(
        PortID(masterLogicalPortIds()[1]));
    ASSERT_TRUE(bcmPort->getEgressPortMirror().has_value());
    EXPECT_EQ(bcmPort->getEgressPortMirror().value(), "mirror");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, SampleDestinationMirror) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    cfg::Mirror mirror;
    cfg::MirrorTunnel tunnel;
    cfg::SflowTunnel sflowTunnel;
    *sflowTunnel.ip() = "10.0.0.1";
    sflowTunnel.udpSrcPort() = 6545;
    sflowTunnel.udpDstPort() = 6343;
    tunnel.sflowTunnel() = sflowTunnel;
    *mirror.name() = "mirror";
    mirror.destination()->tunnel() = tunnel;
    newCfg.mirrors()->push_back(mirror);

    auto portId = masterLogicalPortIds()[1];
    auto portCfg = utility::findCfgPort(newCfg, portId);
    portCfg->ingressMirror() = "mirror";
    portCfg->sampleDest() = cfg::SampleDestination::MIRROR;
    return applyNewConfig(newCfg);
  };
  auto verify = [=, this]() {
    auto* bcmPort = getHwSwitch()->getPortTable()->getBcmPort(
        PortID(masterLogicalPortIds()[1]));
    ASSERT_TRUE(bcmPort->getIngressPortMirror().has_value());
    EXPECT_EQ(bcmPort->getIngressPortMirror().value(), "mirror");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, SampleDestinationCpu) {
  auto setup = [=, this]() {
    auto newCfg = initialConfig();
    cfg::Mirror mirror;
    cfg::MirrorTunnel tunnel;
    cfg::GreTunnel greTunnel;
    *greTunnel.ip() = "1.1.1.10";
    tunnel.greTunnel() = greTunnel;
    *mirror.name() = "mirror";
    mirror.destination()->tunnel() = tunnel;
    newCfg.mirrors()->push_back(mirror);

    auto portId = masterLogicalPortIds()[1];
    auto portCfg = utility::findCfgPort(newCfg, portId);
    portCfg->ingressMirror() = "mirror";
    portCfg->sampleDest() = cfg::SampleDestination::CPU;
    return applyNewConfig(newCfg);
  };
  auto verify = [=, this]() {
    auto* bcmPort = getHwSwitch()->getPortTable()->getBcmPort(
        PortID(masterLogicalPortIds()[1]));
    ASSERT_TRUE(bcmPort->getIngressPortMirror().has_value());
    EXPECT_EQ(bcmPort->getIngressPortMirror().value(), "mirror");
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, AssertL3Enabled) {
  // Enable all master ports
  auto setup = [this]() {
    applyNewConfig(
        utility::oneL3IntfNPortConfig(
            getHwSwitch()->getPlatform()->getPlatformMapping(),
            getHwSwitch()->getPlatform()->getAsic(),
            masterLogicalPortIds(),
            getHwSwitch()->getPlatform()->supportsAddRemovePort(),
            getPlatform()->getAsic()->desiredLoopbackModes()));
  };
  auto verify = [this]() {
    std::array<std::tuple<std::string, bcm_port_control_t>, 2> l3Options = {
        std::make_tuple("bcmPortControlIP4", bcmPortControlIP4),
        std::make_tuple("bcmPortControlIP6", bcmPortControlIP6)};
    for (const auto& portMap :
         std::as_const(*getProgrammedState()->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if (!port.second->isEnabled()) {
          continue;
        }
        for (auto l3Option : l3Options) {
          int currVal{0};
          auto rv = bcm_port_control_get(
              getUnit(),
              static_cast<int>(port.second->getID()),
              std::get<1>(l3Option),
              &currVal);
          bcmCheckError(
              rv,
              fmt::format(
                  "Failed to get {} for port {} : {}",
                  std::get<0>(l3Option),
                  static_cast<int>(port.second->getID()),
                  bcm_errmsg(rv)));
          // Any enabled port should enable IP4 and IP6
          EXPECT_EQ(currVal, 1);
        }
      }
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, PortFdrStats) {
  // Feature supported in Tomahawk4 and above. This test is marked known bad
  // in platforms with unsupported ASICs.
  EXPECT_TRUE(
      getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::FEC_DIAG_COUNTERS));

  auto setup = [this]() { applyNewConfig(initialConfig()); };
  auto verify = [this]() {
    tcData().publishStats();
    for (auto portId : initialConfiguredPorts()) {
      auto port = getHwSwitch()->getPortTable()->getBcmPort(portId);
      EXPECT_TRUE(port->getFdrEnabled());

      auto fdrKey = folly::to<std::string>(
          port->getPortName(), ".", kErrorsPerCodeword(), ".", 0, ".rate");
      EXPECT_TRUE(tcData().hasCounter(fdrKey));
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmPortTest, SetInterPacketGapBits) {
  if (!getPlatform()->getPlatformMapping()->supportsInterPacketGapBits()) {
    return;
  }
  static auto constexpr expectedInterPacketGapBits = 352;
  // Enable all master ports
  auto setup = [this]() {
    applyNewConfig(
        utility::oneL3IntfNPortConfig(
            getHwSwitch()->getPlatform()->getPlatformMapping(),
            getHwSwitch()->getPlatform()->getAsic(),
            masterLogicalPortIds(),
            getHwSwitch()->getPlatform()->supportsAddRemovePort(),
            getPlatform()->getAsic()->desiredLoopbackModes()));
  };
  auto verify = [this]() {
    for (const auto& portMap :
         std::as_const(*getProgrammedState()->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if (!port.second->isEnabled()) {
          continue;
        }
        // platform mapping is used in apply thrift config, and not platform
        // inter packet gap bits setting is only applicable in Elbert platform
        EXPECT_TRUE(port.second->getProfileConfig().interPacketGapBits());
        EXPECT_EQ(
            *port.second->getProfileConfig().interPacketGapBits(),
            expectedInterPacketGapBits);

        // Check BcmPort is also programmed to use the override IGP
        auto bcmPort =
            getHwSwitch()->getPortTable()->getBcmPort(port.second->getID());
        EXPECT_EQ(bcmPort->getInterPacketGapBits(), expectedInterPacketGapBits);
      }
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}
} // namespace facebook::fboss
