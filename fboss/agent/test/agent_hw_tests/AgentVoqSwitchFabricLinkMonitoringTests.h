// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/test/agent_hw_tests/AgentVoqSwitchTests.h"

namespace facebook::fboss {

class AgentVoqSwitchFabricLinkMonitoringTest : public AgentVoqSwitchTest {
 public:
  cfg::SwitchConfig initialConfig(const AgentEnsemble& ensemble) const override;

 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::VOQ_SWITCH_WITH_FABRIC_PORTS};
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    // Verifies fabric link monitoring across every fabric port.
    return std::nullopt;
  }

 private:
  void setCmdLineFlagOverrides() const override;

  void overrideTestEnsembleInitInfo(
      TestEnsembleInitInfo& initInfo) const override;

  void addFabricLinkMonitoringDsfNodes(
      cfg::SwitchConfig& config,
      const AgentEnsemble& ensemble) const;
};

} // namespace facebook::fboss
