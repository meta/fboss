// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/counters/mka/CmdShowInterfaceCountersMKA.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

namespace facebook::fboss {

namespace {

cli::InterfaceCountersMKAModel createModel(
    const std::map<std::string, facebook::fboss::MacsecStats>&
        intfsMKAStatsMap) {
  cli::InterfaceCountersMKAModel model;
  model.intfMKAStats() = intfsMKAStatsMap;
  return model;
}

} // namespace

CmdShowInterfaceCountersMKA::RetType CmdShowInterfaceCountersMKA::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& queriedIfs) {
  auto client =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  std::map<std::string, facebook::fboss::MacsecStats> intfsMKAStatsMap;

  if (queriedIfs.empty()) {
    client->sync_getAllMacsecPortStats(intfsMKAStatsMap, true);
  } else {
    client->sync_getMacsecPortStats(intfsMKAStatsMap, queriedIfs, true);
  }

  return createModel(intfsMKAStatsMap);
}

void CmdShowInterfaceCountersMKA::printOutput(
    const RetType& model,
    std::ostream& out) {
  utils::Table table;

  auto printPortStats = [&out](const auto& modelIntfMKAStats) {
    const auto& inStats = *modelIntfMKAStats.ingressPortStats();
    const auto& outStats = *modelIntfMKAStats.egressPortStats();

    utils::Table portStatsTable;
    portStatsTable.setHeader({"Counter Name", "IN", "OUT"});
    portStatsTable.addRow(
        {"PreMacsecDropPackets",
         std::to_string(*inStats.preMacsecDropPkts()),
         std::to_string(*outStats.preMacsecDropPkts())});
    portStatsTable.addRow(
        {"ControlPackets",
         std::to_string(*inStats.controlPkts()),
         std::to_string(*outStats.controlPkts())});
    portStatsTable.addRow(
        {"DataPackets",
         std::to_string(*inStats.dataPkts()),
         std::to_string(*outStats.dataPkts())});
    portStatsTable.addRow(
        {"(En/De)cryptedOctets",
         std::to_string(*inStats.octetsEncrypted()),
         std::to_string(*outStats.octetsEncrypted())});
    portStatsTable.addRow(
        {"MacsecTagPkts",
         std::to_string(*inStats.noMacsecTagPkts()),
         std::to_string(*outStats.noMacsecTagPkts())});
    portStatsTable.addRow(
        {"BadOrNoMacsecTagDroppedPkts",
         std::to_string(*outStats.inBadOrNoMacsecTagDroppedPkts()),
         "--"});
    portStatsTable.addRow(
        {"NoSciDroppedPkts",
         std::to_string(*inStats.inNoSciDroppedPkts()),
         "--"});
    portStatsTable.addRow(
        {"UnknownSciPkts", std::to_string(*inStats.inUnknownSciPkts()), "--"});
    portStatsTable.addRow(
        {"OverrunDroppedPkts",
         std::to_string(*inStats.inOverrunDroppedPkts()),
         "--"});
    portStatsTable.addRow(
        {"DelayedPkts", std::to_string(*inStats.inDelayedPkts()), "--"});
    portStatsTable.addRow(
        {"LateDroppedPkts",
         std::to_string(*inStats.inLateDroppedPkts()),
         "--"});
    portStatsTable.addRow(
        {"NotValidDroppedPkts",
         std::to_string(*inStats.inNotValidDroppedPkts()),
         "--"});
    portStatsTable.addRow(
        {"InvalidPkts", std::to_string(*inStats.inInvalidPkts()), "--"});
    portStatsTable.addRow(
        {"NoSaDroppedPkts",
         std::to_string(*inStats.inNoSaDroppedPkts()),
         "--"});
    portStatsTable.addRow(
        {"UnusedSaPkts", std::to_string(*inStats.inUnusedSaPkts()), "--"});
    portStatsTable.addRow(
        {"TooLongDroppedPkts",
         "--",
         std::to_string(*outStats.outTooLongDroppedPkts())});
    portStatsTable.addRow(
        {"CurrentXPN",
         std::to_string(*inStats.inCurrentXpn()),
         std::to_string(*outStats.outCurrentXpn())});

    out << portStatsTable << std::endl;
  };

  auto printFlowStatsMap = [&out](const auto& modelIntfMKAStats) {
    const auto& inStatsList = *modelIntfMKAStats.ingressFlowStats();
    const auto& outStatsList = *modelIntfMKAStats.egressFlowStats();

    auto printFlowStats = [&out](const auto& flowStat, bool ingress) {
      const auto& sci = *flowStat.sci();
      const auto& stats = *flowStat.flowStats();

      out << "SCI INFO: " << std::endl;
      out << "MAC: " << *sci.macAddress() << std::endl;
      out << "PORT: " << *sci.port() << std::endl;
      utils::Table table;
      table.setHeader({"Flow Counter Name", "Value"});
      table.addRow(
          {"UnicastUncontrolledPackets",
           std::to_string(*stats.ucastUncontrolledPkts())});
      table.addRow(
          {"UnicastControlledPackets",
           std::to_string(*stats.ucastControlledPkts())});
      table.addRow(
          {"MulticastUncontrolledPackets",
           std::to_string(*stats.mcastUncontrolledPkts())});
      table.addRow(
          {"MulticastControlledPackets",
           std::to_string(*stats.mcastControlledPkts())});
      table.addRow(
          {"BroadcastUncontrolledPackets",
           std::to_string(*stats.bcastUncontrolledPkts())});
      table.addRow(
          {"BroadcastControlledPackets",
           std::to_string(*stats.bcastControlledPkts())});
      table.addRow({"ControlPackets", std::to_string(*stats.controlPkts())});
      table.addRow(
          {"OtherErroredPackets", std::to_string(*stats.otherErrPkts())});
      table.addRow(
          {"OctetsUncontrolled", std::to_string(*stats.octetsUncontrolled())});
      table.addRow(
          {"OctetsControlled", std::to_string(*stats.octetsControlled())});

      if (ingress) {
        table.addRow(
            {"UntaggedPackets", std::to_string(*stats.untaggedPkts())});
        table.addRow({"BadTagPackets", std::to_string(*stats.inBadTagPkts())});
        table.addRow({"NoSciPackets", std::to_string(*stats.noSciPkts())});
        table.addRow(
            {"UnknownSciPackets", std::to_string(*stats.unknownSciPkts())});
      } else {
        table.addRow(
            {"CommonOctets", std::to_string(*stats.outCommonOctets())});
        table.addRow(
            {"TooLongPackets", std::to_string(*stats.outTooLongPkts())});
      }
      out << table << std::endl;
    };

    if (!inStatsList.empty()) {
      out << "Ingress Flow Stats" << std::endl;
      out << std::string(20, '-') << std::endl;
      for (const auto& inStat : inStatsList) {
        printFlowStats(inStat, true);
      }
    }

    if (!outStatsList.empty()) {
      out << "Egress Flow Stats" << std::endl;
      out << std::string(20, '-') << std::endl;
      for (const auto& outStat : outStatsList) {
        printFlowStats(outStat, false);
      }
    }
  };

  auto printSaStatsMap = [&out](const auto& modelIntfMKAStats) {
    const auto& inStatsList = *modelIntfMKAStats.rxSecureAssociationStats();
    const auto& outStatsList = *modelIntfMKAStats.txSecureAssociationStats();

    auto printSaStats = [&out](const auto& saStat, bool ingress) {
      const auto& stats = *saStat.saStats();
      const auto& sa = *saStat.saId();
      const auto& sci = *sa.sci();

      out << "SA INFO: " << std::endl;
      out << "MAC: " << *sci.macAddress() << std::endl;
      out << "PORT: " << *sci.port() << std::endl;
      out << "Association#: " << *sa.assocNum() << std::endl;

      utils::Table table;
      table.setHeader({"SA Counter Name", "Value"});
      table.addRow(
          {"OctetsEncrypted", std::to_string(*stats.octetsEncrypted())});
      table.addRow(
          {"OctetsProtected", std::to_string(*stats.octetsProtected())});
      if (ingress) {
        table.addRow(
            {"UncheckedPackets", std::to_string(*stats.inUncheckedPkts())});
        table.addRow(
            {"DelayedPackets", std::to_string(*stats.inDelayedPkts())});
        table.addRow({"LatePackets", std::to_string(*stats.inLatePkts())});
        table.addRow(
            {"InvalidPackets", std::to_string(*stats.inInvalidPkts())});
        table.addRow(
            {"NotValidPackets", std::to_string(*stats.inNotValidPkts())});
        table.addRow({"NoSaPackets", std::to_string(*stats.inNoSaPkts())});
        table.addRow(
            {"UnusedSaPackets", std::to_string(*stats.inUnusedSaPkts())});
        table.addRow({"InCurrentXpn", std::to_string(*stats.inCurrentXpn())});
        table.addRow({"OkPackets", std::to_string(*stats.inOkPkts())});
      } else {
        table.addRow(
            {"EncryptedPackets", std::to_string(*stats.outEncryptedPkts())});
        table.addRow(
            {"ProtectedPackets", std::to_string(*stats.outProtectedPkts())});
        table.addRow({"OutCurentXpn", std::to_string(*stats.outCurrentXpn())});
      }
      out << table << std::endl;
    };

    if (!inStatsList.empty()) {
      out << "Ingress SA Stats" << std::endl;
      out << std::string(20, '-') << std::endl;
      for (const auto& inStat : inStatsList) {
        printSaStats(inStat, true);
      }
    }

    if (!outStatsList.empty()) {
      out << "Egress SA Stats" << std::endl;
      out << std::string(20, '-') << std::endl;
      for (const auto& outStat : outStatsList) {
        printSaStats(outStat, false);
      }
    }
  };

  auto printAclStats = [&out](const auto& modelIntfMKAStats) {
    const auto& inStats = *modelIntfMKAStats.ingressAclStats();
    const auto& outStats = *modelIntfMKAStats.egressAclStats();

    utils::Table aclTable;
    aclTable.setHeader({"Counter Name", "IN", "OUT"});
    aclTable.addRow(
        {"DefaultAclPackets",
         std::to_string(*inStats.defaultAclStats()),
         std::to_string(*outStats.defaultAclStats())});
    out << aclTable << std::endl;
  };

  for (auto& m : model.intfMKAStats().value()) {
    out << "Port: " << m.first << std::endl;
    out << std::string(20, '=') << std::endl;

    // Print port stats
    printPortStats(m.second);
    // Print flow stats
    printFlowStatsMap(m.second);
    // Print SA stats
    printSaStatsMap(m.second);
    // Print ACL stats
    printAclStats(m.second);
  }
}

// Template instantiations
template void CmdHandler<
    CmdShowInterfaceCountersMKA,
    CmdShowInterfaceCountersMKATraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdShowInterfaceCountersMKA,
    CmdShowInterfaceCountersMKATraits>::getValidFilters();

} // namespace facebook::fboss
