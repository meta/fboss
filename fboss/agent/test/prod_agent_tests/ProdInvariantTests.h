#pragma once
#include <optional>
#include <vector>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/Main.h"
#include "fboss/agent/SetupThrift.h"
#include "fboss/agent/hw/test/ProdConfigFactory.h"
#include "fboss/agent/test/prod_agent_tests/ProdAgentTests.h"

class HwSwitch;

namespace facebook::fboss {

class ProdInvariantTest : public ProdAgentTests {
 protected:
  void sendTraffic(int numPackets);
  void setupAgentTestEcmp(const std::vector<PortDescriptor>& ecmpPorts);
  virtual void SetUp() override;
  virtual void setupConfigFlag();
  cfg::SwitchConfig initialConfig(const AgentEnsemble& ensemble) override;
  virtual cfg::SwitchConfig getConfigFromFlag();
  void verifyAcl();
  void verifyCopp();
  void verifySafeDiagCommands();
  void verifyLoadBalancing(int numPackets = 10000);
  virtual void verifyDscpToQueueMapping();
  void verifyQueuePerHostMapping(bool dscpMarkingTest);
  std::vector<PortDescriptor> ecmpPorts_{};
  bool checkBaseConfigPortsEmpty();
  void verifyThriftHandler();
  void verifySwSwitchHandler();
  void verifyHwSwitchHandler();
  void set_mmu_lossless(bool mmu_lossless) {
    mmuLosslessMode_ = mmu_lossless;
  }
  bool is_mmu_lossless_mode() {
    return mmuLosslessMode_;
  }
  std::vector<PortID> getAllPlatformPorts(
      const std::map<int32_t, cfg::PlatformPortEntry>& platformPorts);
  void printDiagCmd(const std::string& cmd);
  // Role to generate a config for when no prod agent.conf is supplied.
  // nullopt yields the plain RSW config, which carries no PFC.
  virtual std::optional<utility::ProdMmuLosslessRole> getProdRole() const {
    return std::nullopt;
  }

 protected:
  std::optional<bool> useProdConfig_ = std::nullopt;
  PortID getDownlinkPort();
  // Config-derived port lists span every NPU; each run targets the one named
  // by FLAGS_switch_id_for_testing. Narrow a port list to that switch.
  std::vector<PortID> portsForSwitchUnderTest(
      const std::vector<PortID>& ports) const;
  std::map<PortID, HwPortStats> getLatestPortStats(
      const std::vector<PortID>& ports);
  std::vector<PortID> getEcmpPortIds();
  void setCmdLineFlagOverrides() const override {
    ProdAgentTests::setCmdLineFlagOverrides();
    FLAGS_enable_lacp = false;
    FLAGS_prod_invariant_config_test = true;
  }

 private:
  bool mmuLosslessMode_ = false;
};

int ProdInvariantTestMain(int argc, char** argv, PlatformInitFn initPlatformFn);
} // namespace facebook::fboss
