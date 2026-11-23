// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/utils/PrbsUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

namespace facebook::fboss {

std::vector<phy::PortComponent> prbsComponents(
    const std::vector<std::string>& components,
    bool returnAllIfEmpty) {
  if (components.empty() && returnAllIfEmpty) {
    // If no components are requested, return all
    return {
        phy::PortComponent::ASIC,
        phy::PortComponent::GB_SYSTEM,
        phy::PortComponent::GB_LINE,
        phy::PortComponent::TRANSCEIVER_SYSTEM,
        phy::PortComponent::TRANSCEIVER_LINE};
  }
  if (components.empty()) {
    throw std::runtime_error(
        "No PRBS component specified, expecting one or more of: asic, "
        "xphy_system, xphy_line, transceiver_system, transceiver_line");
  }
  std::vector<phy::PortComponent> prbsComps;
  for (const auto& comp : components) {
    if (comp == "asic") {
      prbsComps.push_back(phy::PortComponent::ASIC);
    } else if (comp == "xphy_system") {
      prbsComps.push_back(phy::PortComponent::GB_SYSTEM);
    } else if (comp == "xphy_line") {
      prbsComps.push_back(phy::PortComponent::GB_LINE);
    } else if (comp == "transceiver_system") {
      prbsComps.push_back(phy::PortComponent::TRANSCEIVER_SYSTEM);
    } else if (comp == "transceiver_line") {
      prbsComps.push_back(phy::PortComponent::TRANSCEIVER_LINE);
    } else {
      throw std::runtime_error("Unsupported component: " + comp);
    }
  }
  return prbsComps;
}

} // namespace facebook::fboss
