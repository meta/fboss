// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/prbs/capabilities/CmdShowInterfacePrbsCapabilities.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/PrbsUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

#include "fboss/lib/phy/gen-cpp2/phy_types.h"

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <cstdint>
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {

namespace {

std::vector<prbs::PrbsPolynomial> getPrbsPolynomials(
    const HostInfo& hostInfo,
    const std::string& interfaceName,
    const phy::PortComponent& component) {
  std::vector<prbs::PrbsPolynomial> polynomials;
  try {
    if (component == phy::PortComponent::TRANSCEIVER_LINE ||
        component == phy::PortComponent::TRANSCEIVER_SYSTEM) {
      auto qsfpClient =
          utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);
      qsfpClient->sync_getSupportedPrbsPolynomials(
          polynomials, interfaceName, component);
    } else if (component == phy::PortComponent::ASIC) {
      auto agentClient =
          utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
      agentClient->sync_getSupportedPrbsPolynomials(
          polynomials, interfaceName, component);
    }
  } catch (const std::exception& e) {
    std::cerr << e.what();
  }
  return polynomials;
}

CmdShowInterfacePrbsCapabilitiesTraits::RetType createModel(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs,
    const std::vector<phy::PortComponent>& components) {
  CmdShowInterfacePrbsCapabilitiesTraits::RetType model;
  for (const auto& intf : queriedIfs) {
    cli::InterfaceEntry intfEntry;
    for (const auto& component : components) {
      cli::ComponentEntry entry;
      entry.interfaceName() = intf;
      entry.component() = component;
      entry.prbsCapabilties() = getPrbsPolynomials(hostInfo, intf, component);
      intfEntry.componentEntries()->push_back(entry);
    }
    if (intfEntry.componentEntries()->size()) {
      model.interfaceEntries()->push_back(intfEntry);
    }
  }
  return model;
}

} // namespace

CmdShowInterfacePrbsCapabilitiesTraits::RetType
CmdShowInterfacePrbsCapabilities::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs,
    const std::vector<std::string>& components) {
  if (queriedIfs.empty()) {
    std::vector<std::string> allIfs;
    std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries_;
    auto agentClient =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    agentClient->sync_getAllPortInfo(portEntries_);
    allIfs.reserve(portEntries_.size());
    for (const auto& port : portEntries_) {
      allIfs.push_back(port.second.name().value());
    }
    return createModel(
        hostInfo,
        allIfs,
        prbsComponents(components, true /* returnAllIfEmpty */));
  } else {
    return createModel(
        hostInfo,
        queriedIfs,
        prbsComponents(components, true /* returnAllIfEmpty */));
  }
}

void CmdShowInterfacePrbsCapabilities::printOutput(
    const RetType& model,
    std::ostream& out) {
  utils::Table table;
  table.setHeader({"Interface", "Component", "PRBS Polynomials"});
  for (const auto& intfEntry : *model.interfaceEntries()) {
    for (const auto& entry : *intfEntry.componentEntries()) {
      auto polynomialStrings = folly::gen::from(*entry.prbsCapabilties()) |
          folly::gen::mapped([](prbs::PrbsPolynomial p) {
                                 return apache::thrift::util::enumNameSafe(p);
                               }) |
          folly::gen::as<std::vector>();
      table.addRow(
          {*entry.interfaceName(),
           apache::thrift::util::enumNameSafe(*entry.component()),
           folly::join(",", polynomialStrings)});
    }
  }
  out << table << std::endl;
}

std::string_view CmdShowInterfacePrbsCapabilitiesTraits::description() {
  return "Displays the PRBS polynomials each component of an interface supports: for every component (ASIC, gearbox system and line, transceiver system and line) the list of supported polynomials, left blank where the component supports none. Accepts an optional component name to narrow the output. Use it to find which PRBS patterns a link can run before starting a test.";
}

CmdShowInterfacePrbsCapabilities::RetType
CmdShowInterfacePrbsCapabilities::sampleModel() {
  RetType model;
  cli::InterfaceEntry interfaceEntry;

  cli::ComponentEntry asic;
  asic.interfaceName() = "eth1/1/1";
  asic.component() = phy::PortComponent::ASIC;
  asic.prbsCapabilties() = {
      prbs::PrbsPolynomial::PRBS7,
      prbs::PrbsPolynomial::PRBS15,
      prbs::PrbsPolynomial::PRBS23,
      prbs::PrbsPolynomial::PRBS31,
      prbs::PrbsPolynomial::PRBS9,
      prbs::PrbsPolynomial::PRBS11,
      prbs::PrbsPolynomial::PRBS58,
  };
  interfaceEntry.componentEntries()->push_back(asic);

  for (const auto component :
       {phy::PortComponent::GB_SYSTEM, phy::PortComponent::GB_LINE}) {
    cli::ComponentEntry gearbox;
    gearbox.interfaceName() = "eth1/1/1";
    gearbox.component() = component;
    gearbox.prbsCapabilties() = {};
    interfaceEntry.componentEntries()->push_back(gearbox);
  }

  for (const auto component :
       {phy::PortComponent::TRANSCEIVER_SYSTEM,
        phy::PortComponent::TRANSCEIVER_LINE}) {
    cli::ComponentEntry transceiver;
    transceiver.interfaceName() = "eth1/1/1";
    transceiver.component() = component;
    transceiver.prbsCapabilties() = {
        prbs::PrbsPolynomial::PRBS31Q,
        prbs::PrbsPolynomial::PRBS31,
        prbs::PrbsPolynomial::PRBS15Q,
        prbs::PrbsPolynomial::PRBS15,
        prbs::PrbsPolynomial::PRBSSSPRQ,
    };
    interfaceEntry.componentEntries()->push_back(transceiver);
  }

  model.interfaceEntries()->push_back(interfaceEntry);
  return model;
}

// Template instantiations
template void CmdHandler<
    CmdShowInterfacePrbsCapabilities,
    CmdShowInterfacePrbsCapabilitiesTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfacePrbsCapabilities,
    CmdShowInterfacePrbsCapabilitiesTraits>::getValidFilters();

} // namespace facebook::fboss
