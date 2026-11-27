/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/AgentHwTest.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

class AgentDiagShellStressTest : public AgentHwTest {
 protected:
  void runDiagCmds() {
    auto switchId = getCurrentSwitchIdForTesting();
    auto asic = hwAsicForSwitch(switchId);
    switch (asic->getAsicType()) {
      case cfg::AsicType::ASIC_TYPE_FAKE:
      case cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT:
      case cfg::AsicType::ASIC_TYPE_MOCK:
      case cfg::AsicType::ASIC_TYPE_TRIDENT2:
      case cfg::AsicType::ASIC_TYPE_ELBERT_8DD:
      case cfg::AsicType::ASIC_TYPE_SANDIA_PHY:
      case cfg::AsicType::ASIC_TYPE_JERICHO2:
      case cfg::AsicType::ASIC_TYPE_RAMON:
      case cfg::AsicType::ASIC_TYPE_RAMON3:
      case cfg::AsicType::ASIC_TYPE_CHENAB:
      case cfg::AsicType::ASIC_TYPE_AGERA3:
      case cfg::AsicType::ASIC_TYPE_CHENAB2:
        // No diag shell to test for these ASICs
        break;
      case cfg::AsicType::ASIC_TYPE_JERICHO3:
      case cfg::AsicType::ASIC_TYPE_JERICHO4:
      case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
        runBcmDnxCmds(switchId);
        break;
      case cfg::AsicType::ASIC_TYPE_EBRO:
      case cfg::AsicType::ASIC_TYPE_P200:
      case cfg::AsicType::ASIC_TYPE_GARONNE:
      case cfg::AsicType::ASIC_TYPE_YUBA:
      case cfg::AsicType::ASIC_TYPE_G202X:
        runLeabaDiagCmds(switchId);
        break;
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1:
        runBcmDiagCmds(switchId);
    }
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::HW_SWITCH};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // check diag shell with fabric ports enabled
    FLAGS_hide_fabric_ports = false;
  }

 private:
  static auto constexpr kNumDiagCmds = 500;
  static auto constexpr kNumDiagCmdsLeaba = 50;
  static auto constexpr kNumRestarts = 20;

  std::string makeDiagCmd(const std::string& cmd) const {
    return cmd + "\n";
  }

  void runBcmDiagCmds(SwitchID id) {
    std::string out;
    auto ensemble = getAgentEnsemble();
    // Start with an empty command for initialization
    ensemble->runDiagCommand(makeDiagCmd(""), out, id);
    for (int i = 0; i < kNumDiagCmds; i++) {
      // TODO: Change with more meaningful command
      ensemble->runDiagCommand(makeDiagCmd("ps"), out, id);
    }
    ensemble->runDiagCommand(makeDiagCmd("quit"), out, id);
    for (int i = 0; i < kNumRestarts; i++) {
      ensemble->runDiagCommand(makeDiagCmd(""), out, id);
      // TODO: Change with more meaningful command
      ensemble->runDiagCommand(makeDiagCmd("ps"), out, id);
      ensemble->runDiagCommand(makeDiagCmd("quit"), out, id);
    }
    // TODO: Validate some output.
    std::ignore = out;
  }

  void runLeabaDiagCmds(SwitchID id) {
    std::string out;
    auto ensemble = getAgentEnsemble();
    ensemble->runDiagCommand(makeDiagCmd(""), out, id);
    ensemble->runDiagCommand(
        makeDiagCmd("from leaba.debug_api import *"), out, id);
    ensemble->runDiagCommand(makeDiagCmd("dapi = DebugApi()"), out, id);
    ensemble->runDiagCommand(makeDiagCmd("from leaba_val import *"), out, id);
    ensemble->runDiagCommand(
        makeDiagCmd("set_dev(sdk.la_get_device(0))"), out, id);
    for (int i = 0; i < kNumDiagCmdsLeaba; i++) {
      ensemble->runDiagCommand(
          makeDiagCmd("dapi.dump_port_counters()"), out, id);
      ensemble->runDiagCommand(makeDiagCmd("get_counters()"), out, id);
    }
    std::ignore = out;
  }

  void runBcmDnxCmds(SwitchID id) {
    std::string out;
    auto ensemble = getAgentEnsemble();
    // This is to check the solution in D76308958
    // Run the following command sequence to check if the command hangs
    try {
      for (int i = 0; i < kNumRestarts; i++) {
        ensemble->runDiagCommand(makeDiagCmd(""), out, id);
        ensemble->runDiagCommand(
            makeDiagCmd("debug soc counter debug"), out, id);
        ensemble->runDiagCommand(makeDiagCmd("port status"), out, id);
        ensemble->runDiagCommand(makeDiagCmd("port status"), out, id);
        // Actuall call for quit
        ensemble->runDiagCommand(makeDiagCmd("\x0d"), out, id);
      }
    } catch (const std::exception& ex) {
      XLOG(FATAL) << "Diag command failed with exception for "
                  << "switchId: " << static_cast<int>(id)
                  << ", exception: " << ex.what();
    }
    // Disable debug counter to avoid persistent debug output from BCM SDK
    // that can block stdout during subsequent SAI API calls (see D76308958)
    ensemble->runDiagCommand(makeDiagCmd(""), out, id);
    ensemble->runDiagCommand(makeDiagCmd("debug soc counter off"), out, id);
    ensemble->runDiagCommand(makeDiagCmd("\x0d"), out, id);

    std::ignore = out;
  }
};

TEST_F(AgentDiagShellStressTest, stressDiagCmds) {
#if !defined(TAJO_SDK_VERSION_1_42_8)
  auto asic = hwAsicForSwitch(getCurrentSwitchIdForTesting());
  if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_TAJO) {
    GTEST_SKIP() << "Leaba Python REPL is not available via diag_shell for "
                    "this SDK";
  }
#endif
  auto setup = []() {};
  auto verify = [=, this]() { runDiagCmds(); };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
