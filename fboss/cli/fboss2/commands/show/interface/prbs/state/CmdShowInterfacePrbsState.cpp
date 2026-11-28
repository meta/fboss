// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/prbs/state/CmdShowInterfacePrbsState.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/FbossHwCtrl.h"
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

#include "fboss/lib/phy/gen-cpp2/phy_types.h"

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <fmt/format.h>
#include <cstdint>
#include <optional>
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {

namespace {

CmdShowInterfacePrbsStateTraits::RetType createModel(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs,
    bool ifsExplicitlyQueried,
    const std::vector<phy::PortComponent>& components,
    const std::map<
        phy::PortComponent,
        std::map<std::string, prbs::InterfacePrbsState>>& allPrbsStates);

std::optional<prbs::InterfacePrbsState> getPrbsState(
    const std::map<
        phy::PortComponent,
        std::map<std::string, prbs::InterfacePrbsState>>& allPrbsStates,
    const std::string& interface,
    const phy::PortComponent& component);

} // namespace

CmdShowInterfacePrbsStateTraits::RetType CmdShowInterfacePrbsState::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs,
    const std::vector<std::string>& components) {
  std::map<phy::PortComponent, std::map<std::string, prbs::InterfacePrbsState>>
      allPrbsStates;
  auto queriedPrbsComponents =
      prbsComponents(components, true /* returnAllIfEmpty */);
  for (const auto& component : queriedPrbsComponents) {
    auto prbsStates = getComponentPrbsStates(hostInfo, component);
    allPrbsStates[component] = prbsStates;
  }
  if (queriedIfs.empty()) {
    std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries_;
    auto agentClient =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    agentClient->sync_getAllPortInfo(portEntries_);
    std::vector<std::string> allIfs;
    allIfs.reserve(portEntries_.size());
    for (const auto& port : portEntries_) {
      allIfs.push_back(port.second.name().value());
    }
    return createModel(
        hostInfo,
        allIfs,
        false /* ifsExplicitlyQueried */,
        queriedPrbsComponents,
        allPrbsStates);
  } else {
    return createModel(
        hostInfo,
        queriedIfs,
        true /* ifsExplicitlyQueried */,
        queriedPrbsComponents,
        allPrbsStates);
  }
}

void CmdShowInterfacePrbsState::printOutput(
    const RetType& model,
    std::ostream& out) {
  utils::Table table;
  table.setHeader(
      {"Interface", "Component", "Generator", "Checker", "Polynomial"});
  for (const auto& intfEntry : *model.interfaceEntries()) {
    for (const auto& entry : *intfEntry.componentEntries()) {
      auto state = *(entry.prbsState());
      bool generator =
          state.generatorEnabled().has_value() && *state.generatorEnabled();
      bool checker =
          state.checkerEnabled().has_value() && *state.checkerEnabled();
      table.addRow(
          {*entry.interfaceName(),
           apache::thrift::util::enumNameSafe(*entry.component()),
           generator ? "Enabled" : "Disabled",
           checker ? "Enabled" : "Disabled",
           apache::thrift::util::enumNameSafe(*state.polynomial())});
    }
  }
  out << table << std::endl;
}

std::map<std::string, prbs::InterfacePrbsState>
CmdShowInterfacePrbsState::getComponentPrbsStates(
    const HostInfo& hostInfo,
    const phy::PortComponent& component) {
  std::map<std::string, prbs::InterfacePrbsState> prbsStates;
  if (component == phy::PortComponent::TRANSCEIVER_LINE ||
      component == phy::PortComponent::TRANSCEIVER_SYSTEM ||
      component == phy::PortComponent::GB_LINE ||
      component == phy::PortComponent::GB_SYSTEM) {
    auto qsfpClient =
        utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
    queryPrbsStates<apache::thrift::Client<QsfpService>>(
        std::move(qsfpClient), prbsStates, component);
  } else {
    auto numHwSwitches = utils::getNumHwSwitches(hostInfo);
    for (int i = 0; i < numHwSwitches; i++) {
      std::map<std::string, prbs::InterfacePrbsState> hwAgentPrbsStates;
      auto hwAgentClient =
          utils::createClient<apache::thrift::Client<FbossHwCtrl>>(hostInfo, i);
      queryPrbsStates<apache::thrift::Client<FbossHwCtrl>>(
          std::move(hwAgentClient), hwAgentPrbsStates, component);
      for (const auto& [interface, prbsState] : hwAgentPrbsStates) {
        prbsStates[interface] = prbsState;
      }
    }
  }
  return prbsStates;
}

namespace {

CmdShowInterfacePrbsStateTraits::RetType createModel(
    const HostInfo& /* hostInfo */,
    const std::vector<std::string>& queriedIfs,
    bool ifsExplicitlyQueried,
    const std::vector<phy::PortComponent>& components,
    const std::map<
        phy::PortComponent,
        std::map<std::string, prbs::InterfacePrbsState>>& allPrbsStates) {
  CmdShowInterfacePrbsStateTraits::RetType model;
  for (const auto& intf : queriedIfs) {
    cli::PrbsStateInterfaceEntry intfEntry;
    for (const auto& component : components) {
      // Each service reports only the ports that support PRBS on that
      // component (e.g. no recycle/eventor ports, no transceiver components
      // on empty cages), so a missing entry means "not applicable".
      auto prbsState = getPrbsState(allPrbsStates, intf, component);
      if (!prbsState) {
        continue;
      }
      cli::PrbsStateComponentEntry entry;
      entry.interfaceName() = intf;
      entry.component() = component;
      entry.prbsState() = *prbsState;
      intfEntry.componentEntries()->push_back(entry);
    }
    if (intfEntry.componentEntries()->size()) {
      model.interfaceEntries()->push_back(intfEntry);
    } else if (ifsExplicitlyQueried) {
      throw std::runtime_error(
          fmt::format("Could not find interface in PRBS states: {}", intf));
    }
  }
  return model;
}

std::optional<prbs::InterfacePrbsState> getPrbsState(
    const std::map<
        phy::PortComponent,
        std::map<std::string, prbs::InterfacePrbsState>>& allPrbsStates,
    const std::string& interface,
    const phy::PortComponent& component) {
  auto allPrbsStatesItr = allPrbsStates.find(component);
  if (allPrbsStatesItr == allPrbsStates.end()) {
    throw std::runtime_error(
        "Could not find component in PRBS states: " +
        std::to_string((int)component));
  }
  const auto& componentPrbsStates = allPrbsStatesItr->second;
  auto componentPrbsStatesItr = componentPrbsStates.find(interface);
  if (componentPrbsStatesItr == componentPrbsStates.end()) {
    return std::nullopt;
  }
  return componentPrbsStatesItr->second;
}

} // namespace

std::string_view CmdShowInterfacePrbsStateTraits::description() {
  return "Displays the current PRBS configuration for each component of an interface: whether the generator and checker are enabled and which polynomial is programmed. Components that do not report a polynomial show the raw enum value. Accepts an optional component name to narrow the output. Use it to confirm whether a PRBS test is running on a link.";
}

CmdShowInterfacePrbsState::RetType CmdShowInterfacePrbsState::sampleModel() {
  RetType model;
  cli::PrbsStateInterfaceEntry interfaceEntry;

  cli::PrbsStateComponentEntry asic;
  asic.interfaceName() = "eth1/1/1";
  asic.component() = phy::PortComponent::ASIC;
  asic.prbsState()->polynomial() = prbs::PrbsPolynomial::PRBS31;
  asic.prbsState()->generatorEnabled() = false;
  asic.prbsState()->checkerEnabled() = false;
  interfaceEntry.componentEntries()->push_back(asic);

  for (const auto component :
       {phy::PortComponent::GB_SYSTEM,
        phy::PortComponent::GB_LINE,
        phy::PortComponent::TRANSCEIVER_SYSTEM,
        phy::PortComponent::TRANSCEIVER_LINE}) {
    cli::PrbsStateComponentEntry entry;
    entry.interfaceName() = "eth1/1/1";
    entry.component() = component;
    entry.prbsState()->generatorEnabled() = false;
    entry.prbsState()->checkerEnabled() = false;
    interfaceEntry.componentEntries()->push_back(entry);
  }

  model.interfaceEntries()->push_back(interfaceEntry);
  return model;
}

// Template instantiations
template void
CmdHandler<CmdShowInterfacePrbsState, CmdShowInterfacePrbsStateTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfacePrbsState,
    CmdShowInterfacePrbsStateTraits>::getValidFilters();

} // namespace facebook::fboss
