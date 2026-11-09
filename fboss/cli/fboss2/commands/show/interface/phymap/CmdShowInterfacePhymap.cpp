// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/phymap/CmdShowInterfacePhymap.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/mka_service/if/gen-cpp2/mka_structs_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

#include <boost/algorithm/string.hpp>

namespace facebook::fboss {

using utils::Table;

namespace {

CmdShowInterfacePhymapTraits::RetType createModel(
    const facebook::fboss::mka::MacsecPortPhyMap& portsPhyMap) {
  CmdShowInterfacePhymapTraits::RetType model;
  model.portsPhyMap() = portsPhyMap;

  return model;
}

} // namespace

CmdShowInterfacePhymapTraits::RetType CmdShowInterfacePhymap::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  auto client =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  std::vector<int32_t> macsecCapablePorts;
  client->sync_getMacsecCapablePorts(macsecCapablePorts);

  facebook::fboss::mka::MacsecPortPhyMap portsPhyMap;
  if (!macsecCapablePorts.empty()) {
    client->sync_macsecGetPhyPortInfo(portsPhyMap, queriedIfs);
  }

  return createModel(portsPhyMap);
}

void CmdShowInterfacePhymap::printOutput(
    const RetType& model,
    std::ostream& out) {
  auto printPortMap = [&out](const auto& portsPhyMap) {
    Table table;
    table.setHeader(
        {"SW Port",
         "Port Name",
         "SlotId",
         "MdioId",
         "PhyId",
         "SaiSwitchId",
         "SliceId"});

    for (auto& phyMap : portsPhyMap) {
      table.addRow(
          {std::to_string(phyMap.first),
           phyMap.second.portName().value(),
           std::to_string(phyMap.second.slotId().value()),
           std::to_string(phyMap.second.mdioId().value()),
           std::to_string(phyMap.second.phyId().value()),
           std::to_string(phyMap.second.saiSwitchId().value()),
           std::to_string(phyMap.second.saiSwitchId().value() - 1)});
    }
    out << table << std::endl;
  };

  auto& portsPhyMap = model.portsPhyMap().value();
  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          portsPhyMap.macsecPortPhyMap())) {
    out << "No Phy port map for this platform" << std::endl;
    return;
  }

  out << "Printing Port PHY map for system:" << std::endl;
  printPortMap(portsPhyMap.macsecPortPhyMap().value());
}

// Template instantiations
template void
CmdHandler<CmdShowInterfacePhymap, CmdShowInterfacePhymapTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfacePhymap,
    CmdShowInterfacePhymapTraits>::getValidFilters();

} // namespace facebook::fboss
