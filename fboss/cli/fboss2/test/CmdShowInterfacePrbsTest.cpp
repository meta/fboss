// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/show/interface/prbs/capabilities/CmdShowInterfacePrbsCapabilities.h"
#include "fboss/cli/fboss2/commands/show/interface/prbs/state/CmdShowInterfacePrbsState.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

using PrbsStates = std::map<std::string, prbs::InterfacePrbsState>;

prbs::InterfacePrbsState makePrbsState(bool enabled) {
  prbs::InterfacePrbsState state;
  state.polynomial() = prbs::PrbsPolynomial::PRBS31;
  state.generatorEnabled() = enabled;
  state.checkerEnabled() = enabled;
  return state;
}

std::map<int32_t, PortInfoThrift> makePortInfos(
    const std::vector<std::string>& names) {
  std::map<int32_t, PortInfoThrift> ports;
  int32_t portId = 1;
  for (const auto& name : names) {
    PortInfoThrift port;
    port.portId() = portId;
    port.name() = name;
    ports[portId++] = port;
  }
  return ports;
}

std::map<std::string, std::vector<phy::PortComponent>> componentsByInterface(
    const cli::ShowPrbsStateModel& model) {
  std::map<std::string, std::vector<phy::PortComponent>> components;
  for (const auto& intfEntry : *model.interfaceEntries()) {
    for (const auto& entry : *intfEntry.componentEntries()) {
      components[*entry.interfaceName()].push_back(*entry.component());
    }
  }
  return components;
}

} // namespace

class CmdShowInterfacePrbsTestFixture : public CmdHandlerTestBase {
 public:
  // One hw switch reporting `asicStates`; qsfp_service reporting
  // `transceiverLineStates` for TRANSCEIVER_LINE, nothing for
  // TRANSCEIVER_SYSTEM, and rejecting the gearbox components like a platform
  // without an external PHY.
  void setupPrbsServices(
      const PrbsStates& asicStates,
      const PrbsStates& transceiverLineStates) {
    setupMockedAgentServer();
    setupMockedHwAgentServer();

    EXPECT_CALL(getMockAgent(), getMultiSwitchRunState(_))
        .WillRepeatedly([](MultiSwitchRunState& runState) {
          runState.hwIndexToRunState()->insert({0, SwitchRunState::CONFIGURED});
        });
    EXPECT_CALL(
        getMockHwAgent(),
        getAllInterfacePrbsStates(_, phy::PortComponent::ASIC))
        .WillOnce([asicStates](PrbsStates& out, phy::PortComponent) {
          out = asicStates;
        });
    EXPECT_CALL(getQsfpService(), getAllInterfacePrbsStates(_, _))
        .WillRepeatedly([transceiverLineStates](
                            PrbsStates& out, phy::PortComponent component) {
          if (component == phy::PortComponent::GB_SYSTEM ||
              component == phy::PortComponent::GB_LINE) {
            throw thrift::FbossBaseError("Unsupported component");
          }
          if (component == phy::PortComponent::TRANSCEIVER_LINE) {
            out = transceiverLineStates;
          }
        });
  }
};

TEST_F(CmdShowInterfacePrbsTestFixture, wikiDocHooksCapabilities) {
  EXPECT_FALSE(CmdShowInterfacePrbsCapabilitiesTraits::description().empty());
  EXPECT_FALSE(
      CmdShowInterfacePrbsCapabilities::sampleModel()
          .interfaceEntries()
          ->empty());
}

TEST_F(CmdShowInterfacePrbsTestFixture, wikiDocHooksState) {
  EXPECT_FALSE(CmdShowInterfacePrbsStateTraits::description().empty());
  EXPECT_FALSE(
      CmdShowInterfacePrbsState::sampleModel().interfaceEntries()->empty());
}

TEST_F(
    CmdShowInterfacePrbsTestFixture,
    queryClientSkipsPortsWithoutPrbsStateWhenListingAll) {
  // The hw agent reports no recycle or eventor ports; qsfp_service reports
  // the recycle port but not the eventor port.
  setupPrbsServices(
      {{"eth1/1/1", makePrbsState(true)}},
      {{"eth1/1/1", makePrbsState(false)},
       {"rcy1/1/1177", makePrbsState(false)}});
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce([](std::map<int32_t, PortInfoThrift>& out) {
        out = makePortInfos({"eth1/1/1", "rcy1/1/1177", "evt1/1/1553"});
      });

  auto model = CmdShowInterfacePrbsState().queryClient(localhost(), {}, {});

  EXPECT_THAT(
      componentsByInterface(model),
      ElementsAre(
          Pair(
              "eth1/1/1",
              ElementsAre(
                  phy::PortComponent::ASIC,
                  phy::PortComponent::TRANSCEIVER_LINE)),
          Pair(
              "rcy1/1/1177",
              ElementsAre(phy::PortComponent::TRANSCEIVER_LINE))));
  const auto& asicEntry =
      model.interfaceEntries()->at(0).componentEntries()->at(0);
  EXPECT_TRUE(*asicEntry.prbsState()->generatorEnabled());
  EXPECT_EQ(*asicEntry.prbsState()->polynomial(), prbs::PrbsPolynomial::PRBS31);
}

TEST_F(
    CmdShowInterfacePrbsTestFixture,
    queryClientShowsReportedComponentsForQueriedPort) {
  setupPrbsServices(
      {{"eth1/1/1", makePrbsState(false)}},
      {{"rcy1/1/1177", makePrbsState(false)}});
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_)).Times(0);

  auto model =
      CmdShowInterfacePrbsState().queryClient(localhost(), {"rcy1/1/1177"}, {});

  EXPECT_THAT(
      componentsByInterface(model),
      ElementsAre(Pair(
          "rcy1/1/1177", ElementsAre(phy::PortComponent::TRANSCEIVER_LINE))));
}

TEST_F(
    CmdShowInterfacePrbsTestFixture,
    queryClientThrowsForQueriedPortWithoutPrbsState) {
  setupPrbsServices(
      {{"eth1/1/1", makePrbsState(false)}},
      {{"eth1/1/1", makePrbsState(false)}});
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_)).Times(0);

  try {
    CmdShowInterfacePrbsState().queryClient(
        localhost(), {"eth1/1/1", "eth1/99/1"}, {});
    ADD_FAILURE() << "expected queryClient to throw for eth1/99/1";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(
        e.what(),
        HasSubstr("Could not find interface in PRBS states: eth1/99/1"));
  }
}

} // namespace facebook::fboss
