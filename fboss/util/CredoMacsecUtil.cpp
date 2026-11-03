// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.
#include "fboss/util/CredoMacsecUtil.h"
#include <folly/FileUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"

using namespace facebook::fboss;
using namespace facebook::fboss::mka;
using namespace apache::thrift;

namespace {
void printSaStatsHelper(
    MacsecSaStats& saStats,
    std::string portName,
    bool ingress) {
  printf("Printing SA stats for %s\n", portName.c_str());
  printf("Direction: %s\n", ingress ? "ingress" : "egress");
  printf(
      "  SA direction: %s\n",
      *saStats.directionIngress() ? "Ingress" : "Egress");
  printf("  OctetsEncrypted: %ld\n", *saStats.octetsEncrypted());
  printf("  OctetsProtected: %ld\n", *saStats.octetsProtected());

  if (*saStats.directionIngress()) {
    printf("  InUncheckedPacktes: %ld\n", *saStats.inUncheckedPkts());
    printf("  InDelayedPacktes: %ld\n", *saStats.inDelayedPkts());
    printf("  InLatePacktes: %ld\n", *saStats.inLatePkts());
    printf("  InInvalidPacktes: %ld\n", *saStats.inInvalidPkts());
    printf("  InNotValidPacktes: %ld\n", *saStats.inNotValidPkts());
    printf("  InNoSaPacktes: %ld\n", *saStats.inNoSaPkts());
    printf("  InUnusedSaPacktes: %ld\n", *saStats.inUnusedSaPkts());
    printf("  InCurrentXpn: %ld\n", *saStats.inCurrentXpn());
    printf("  InOkPacktes: %ld\n", *saStats.inOkPkts());
  } else {
    printf("  OutEncryptedPacktes: %ld\n", *saStats.outEncryptedPkts());
    printf("  OutProtectedPacktes: %ld\n", *saStats.outProtectedPkts());
    printf("  OutCurrentXpn: %ld\n", *saStats.outCurrentXpn());
  }
}

void printPortStatsHelper(
    MacsecPortStats& portStats,
    std::string portName,
    bool ingress) {
  printf("Printing Port stats for %s\n", portName.c_str());
  printf("Direction: %s\n", ingress ? "ingress" : "egress");
  printf("  PreMacsecDropPackets: %ld\n", *portStats.preMacsecDropPkts());
  printf("  ControlPackets: %ld\n", *portStats.controlPkts());
  printf("  DataPackets: %ld\n", *portStats.dataPkts());
  if (ingress) {
    printf("  DecryptedOctets: %ld\n", *portStats.octetsEncrypted());
    printf(
        "  inBadOrNoMacsecTagDroppedPkts: %ld\n",
        *portStats.inBadOrNoMacsecTagDroppedPkts());
    printf("  inNoSciDroppedPkts: %ld\n", *portStats.inNoSciDroppedPkts());
    printf("  inUnknownSciPkts: %ld\n", *portStats.inUnknownSciPkts());
    printf("  inOverrunDroppedPkts: %ld\n", *portStats.inOverrunDroppedPkts());
    printf("  inDelayedPkts: %ld\n", *portStats.inDelayedPkts());
    printf("  inLateDroppedPkts: %ld\n", *portStats.inLateDroppedPkts());
    printf(
        "  inNotValidDroppedPkts: %ld\n", *portStats.inNotValidDroppedPkts());
    printf("  inInvalidPkts: %ld\n", *portStats.inInvalidPkts());
    printf("  inNoSaDroppedPkts: %ld\n", *portStats.inNoSaDroppedPkts());
    printf("  inUnusedSaPkts: %ld\n", *portStats.inUnusedSaPkts());
    printf("  inCurrentXpn: %ld\n", *portStats.inCurrentXpn());
  } else {
    printf("  EncryptedOctets: %ld\n", *portStats.octetsEncrypted());
    printf(
        "  outTooLongDroppedPkts: %ld\n", *portStats.outTooLongDroppedPkts());
    printf("  outCurrentXpn: %ld\n", *portStats.outCurrentXpn());
  }
}

void printFlowStatsHelper(
    MacsecFlowStats& flowStats,
    std::string portName,
    bool ingress) {
  printf("Printing Flow stats for %s\n", portName.c_str());
  printf("Direction: %s\n", ingress ? "ingress" : "egress");
  printf(
      "  Flow direction: %s\n",
      *flowStats.directionIngress() ? "Ingress" : "Egress");
  printf(
      "  UnicastUncontrolledPackets: %ld\n",
      *flowStats.ucastUncontrolledPkts());
  printf("  UnicastControlledPackets: %ld\n", *flowStats.ucastControlledPkts());
  printf(
      "  MulticastUncontrolledPackets: %ld\n",
      *flowStats.mcastUncontrolledPkts());
  printf(
      "  MulticastControlledPackets: %ld\n", *flowStats.mcastControlledPkts());
  printf(
      "  BroadcastUncontrolledPackets: %ld\n",
      *flowStats.bcastUncontrolledPkts());
  printf(
      "  BroadcastControlledPackets: %ld\n", *flowStats.bcastControlledPkts());
  printf("  ControlPackets: %ld\n", *flowStats.controlPkts());
  printf("  UntaggedPackets: %ld\n", *flowStats.untaggedPkts());
  printf("  OtherErroredPackets: %ld\n", *flowStats.otherErrPkts());
  printf("  OctetsUncontrolled: %ld\n", *flowStats.octetsUncontrolled());
  printf("  OctetsControlled: %ld\n", *flowStats.octetsControlled());
  if (*flowStats.directionIngress()) {
    printf(
        "  InTaggedControlledPackets: %ld\n",
        *flowStats.inTaggedControlledPkts());
    printf("  InUntaggedPackets: %ld\n", *flowStats.untaggedPkts());
    printf("  InBadTagPackets: %ld\n", *flowStats.inBadTagPkts());
    printf("  NoSciPackets: %ld\n", *flowStats.noSciPkts());
    printf("  UnknownSciPackets: %ld\n", *flowStats.unknownSciPkts());
    printf("  OverrunPkts: %ld\n", *flowStats.overrunPkts());
  } else {
    printf("  OutCommonOctets: %ld\n", *flowStats.outCommonOctets());
    printf("  OutTooLongPackets: %ld\n", *flowStats.outTooLongPkts());
  }
}

void printAclStatsHelper(
    MacsecAclStats& aclStats,
    std::string portName,
    bool ingress) {
  printf("Printing ACL stats for %s\n", portName.c_str());
  printf("Direction: %s\n", (ingress ? "ingress" : "egress"));
  printf("  defaultAclStats: %ld\n", aclStats.defaultAclStats().value());
}

} // namespace

namespace facebook {
namespace fboss {

DEFINE_bool(phy_port_map, false, "Print Phy Port map for this platform");
DEFINE_bool(add_sa_rx, false, "Add SA rx, use with --sa_config, --sc_config");
DEFINE_string(sa_config, "", "SA config JSON file");
DEFINE_string(sc_config, "", "SC config JSON file");
DEFINE_bool(add_sa_tx, false, "Add SA tx, use with --sa_config");
DEFINE_bool(
    phy_link_info,
    false,
    "Print link info for a port in a phy, use with --port");
DEFINE_bool(
    phy_serdes_info,
    false,
    "Print serdes info for a port in a phy, use with --port");
DEFINE_string(port, "", "Switch port");
DEFINE_bool(
    delete_sa_rx,
    false,
    "Delete SA rx, use with --sa_config, --sc_config");
DEFINE_bool(delete_sa_tx, false, "Delete SA tx, use with --sa_config");
DEFINE_bool(
    delete_all_sc,
    false,
    "Delete all SA, SC on a Phy, use with --port");
DEFINE_bool(
    setup_macsec_state,
    false,
    "Setup Macsec state for port, use with --port, --macsec_desired, drop_unencrypted");
DEFINE_bool(macsec_desired, false, "If macsec_desired for a port");
DEFINE_bool(drop_unencrypted, false, "If drop_unencrypted required for a port");
DEFINE_bool(
    get_all_sc_info,
    false,
    "Get all SC, SA info for a Phy, use with --port");
DEFINE_bool(
    get_port_stats,
    false,
    "Get Port stats, use with --port --ingress/--egress");
DEFINE_bool(ingress, false, "Ingress direction for port/SA/Flow stats");
DEFINE_bool(egress, false, "Egress direction for port/SA/Flow stats");
DEFINE_bool(
    get_flow_stats,
    false,
    "Get flow stats, use with --port --ingress/--egress");
DEFINE_bool(
    get_sa_stats,
    false,
    "Get SA stats, use with --port --ingress/--egress");
DEFINE_bool(get_acl_stats, false, "Get Macsec ACL stats, use with --port");
DEFINE_bool(
    get_allport_stats,
    false,
    "Get all port stats in this system, optionally use --port");
DEFINE_bool(get_sdk_state, false, "Get entire SAI state, use with --filename");
DEFINE_string(filename, "", "File name");

/*
 * getMacsecSaFromJson()
 * Reads MacSec SA information from JSON file to MKASak structure.
 * sa-json-file:
 *  {
 *       "sci": {
 *               "macAddress": "<mac-address>",
 *               "port": <port-id>
 *       },
 *       "l2Port": "<l2-port-string>",
 *       "assocNum": <association-number>,
 *       "keyHex": "<key-hex-value>",
 *       "keyIdHex": "<Key-id-hex-value>",
 *       "primary": <true/false>
 *  }
 */
bool CredoMacsecUtil::getMacsecSaFromJson(std::string saJsonFile, MKASak& sak) {
  bool retVal = false;

  if (std::string saJsonStr; folly::readFile(saJsonFile.c_str(), saJsonStr)) {
    sak = apache::thrift::SimpleJSONSerializer::deserialize<MKASak>(saJsonStr);
    retVal = true;
  } else {
    printf("SA config could not be read\n");
  }

  return retVal;
}

/*
 * getMacsecScFromJson()
 * Reads MacSec SC information from JSON file to MKASci structure.
 * sc-json-file:
 * {
 *       "macAddress": "<mac-address>",
 *       "port": <port-id>
 * }
 */
bool CredoMacsecUtil::getMacsecScFromJson(std::string scJsonFile, MKASci& sci) {
  bool retVal = false;

  if (std::string scJsonStr; folly::readFile(scJsonFile.c_str(), scJsonStr)) {
    sci = apache::thrift::SimpleJSONSerializer::deserialize<MKASci>(scJsonStr);
    retVal = true;
  } else {
    printf("SC config could not be read\n");
  }

  return retVal;
}

/*
 * printPhyPortMap()
 * Print the Phy port mapping information from the macsec handling process.
 * This function will make the thrift call to the qsfp_service (or phy_service)
 * process to get this phy port mapping information. ie:
 * PORT    SLOT    MDIO    PHYID   NAME        SAISW    SLICE
 *  1       2       1       0      eth4/2/1    10       9
 *  3       0       0       0      eth2/1/1    7        6
 *  5       3       4       0      eth5/5/1    16       15
 */
void CredoMacsecUtil::printPhyPortMap(QsfpServiceAsyncClient* fbMacsecHandler) {
  MacsecPortPhyMap macsecPortPhyMap{};
  fbMacsecHandler->sync_macsecGetPhyPortInfo(macsecPortPhyMap, {});

  printf("Printing the Phy port info map:\n");
  printf("PORT    SLOT    MDIO    PHYID    NAME       SAISW   SLICE\n");
  for (auto it : *macsecPortPhyMap.macsecPortPhyMap()) {
    int port = it.first;
    int slot = *it.second.slotId();
    int mdio = *it.second.mdioId();
    int phy = *it.second.phyId();
    int saiSwitch = *it.second.saiSwitchId();
    std::string name = *it.second.portName();

    printf(
        "%4d    %4d    %4d    %4d    %8s  %4d    %4d\n",
        port,
        slot,
        mdio,
        phy,
        name.c_str(),
        saiSwitch,
        saiSwitch - 1);
  }
}

/*
 * addSaRx()
 * Adds Macsec SA Rx on a given Phy. The syntax is:
 * credo_macsec_util --add_sa_rx --sa_config <sa-json-file> --sc_config
 * <sc-json-file> sa-json-file:
 *  {
 *       "sci": {
 *               "macAddress": "<mac-address>",
 *               "port": <port-id>
 *       },
 *       "l2Port": "<l2-port-string>",
 *       "assocNum": <association-number>,
 *       "keyHex": "<key-hex-value>",
 *       "keyIdHex": "<Key-id-hex-value>",
 *       "primary": <true/false>
 *  }
 * sc-json-file:
 * {
 *       "macAddress": "<mac-address>",
 *       "port": <port-id>
 * }
 */
void CredoMacsecUtil::addSaRx(QsfpServiceAsyncClient* fbMacsecHandler) {
  MKASak sak;
  MKASci sci;

  if (FLAGS_sa_config.empty() || FLAGS_sc_config.empty()) {
    printf("SA and SC config requied\n");
    return;
  }

  bool rc = getMacsecSaFromJson(FLAGS_sa_config, sak);
  if (!rc) {
    printf("SA config could not be read from file\n");
    return;
  }

  rc = getMacsecScFromJson(FLAGS_sc_config, sci);
  if (!rc) {
    printf("SC config could not be read from file\n");
    return;
  }

  rc = fbMacsecHandler->sync_sakInstallRx(sak, sci);
  printf("sakInstallRx is %s\n", rc ? "Successful" : "Failed");
}

/*
 * addSaTx()
 * Adds Macsec SA Tx on a given Phy. The syntax is:
 * credo_macsec_util --add_sa_tx --sa_config <sa-json-file>
 * sa-json-file is described in above command
 */
void CredoMacsecUtil::addSaTx(QsfpServiceAsyncClient* fbMacsecHandler) {
  MKASak sak;

  if (FLAGS_sa_config.empty()) {
    printf("SA config required\n");
    return;
  }

  bool rc = getMacsecSaFromJson(FLAGS_sa_config, sak);
  if (!rc) {
    printf("SA config could not be read from file\n");
    return;
  }

  rc = fbMacsecHandler->sync_sakInstallTx(sak);
  printf("sakInstallTx is %s\n", rc ? "Successful" : "Failed");
}

/*
 * deleteSaRx()
 * Deletes Macsec SA Rx on a given Phy. The syntax is:
 * credo_macsec_util --delete_sa_rx --sa_config <sa-json-file> --sc_config
 * <sc-json-file> sa-json-file and sc-json files described in above command
 */
void CredoMacsecUtil::deleteSaRx(QsfpServiceAsyncClient* fbMacsecHandler) {
  MKASak sak;
  MKASci sci;

  if (FLAGS_sa_config.empty() || FLAGS_sc_config.empty()) {
    printf("SA and SC config requied\n");
    return;
  }

  bool rc = getMacsecSaFromJson(FLAGS_sa_config, sak);
  if (!rc) {
    printf("SA config could not be read from file\n");
    return;
  }

  rc = getMacsecScFromJson(FLAGS_sc_config, sci);
  if (!rc) {
    printf("SC config could not be read from file\n");
    return;
  }

  rc = fbMacsecHandler->sync_sakDeleteRx(sak, sci);
  printf("sakDeleteRx is %s\n", rc ? "Successful" : "Failed");
}

/*
 * deleteSaTx()
 * Adds Macsec SA Tx on a given Phy. The syntax is:
 * credo_macsec_util --delete_sa_tx --sa_config <sa-json-file>
 * sa-json-file is described in above command
 */
void CredoMacsecUtil::deleteSaTx(QsfpServiceAsyncClient* fbMacsecHandler) {
  MKASak sak;

  if (FLAGS_sa_config.empty()) {
    printf("SA config requied\n");
    return;
  }

  bool rc = getMacsecSaFromJson(FLAGS_sa_config, sak);
  if (!rc) {
    printf("SA config could not be read from file\n");
    return;
  }

  rc = fbMacsecHandler->sync_sakDelete(sak);
  printf("sakDelete is %s\n", rc ? "Successful" : "Failed");
}

void CredoMacsecUtil::deleteAllSc(QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }

  bool rc = fbMacsecHandler->sync_deleteAllSc(FLAGS_port);
  printf("All SA, SC deletion %s\n", rc ? "Successful" : "Failed");
}

/*
 * setupMacsecPortState
 * This function will set the Macsec state in the Phy chip as per the
 * MacsecDesired and DropUnencrypted flag
 */
void CredoMacsecUtil::setupMacsecPortState(
    facebook::fboss::QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  printf(
      "For port=%s, going to set macsecDesired=%s and dropUnencrypted=%s\n",
      FLAGS_port.c_str(),
      (FLAGS_macsec_desired ? "True" : "False"),
      (FLAGS_drop_unencrypted ? "True" : "False"));

  bool rc = fbMacsecHandler->sync_setupMacsecState(
      {FLAGS_port}, FLAGS_macsec_desired, FLAGS_drop_unencrypted);
  printf(
      "setupMacsecPortState for %s %s\n",
      FLAGS_port.c_str(),
      (rc ? "Done" : "Failed"));
}

void CredoMacsecUtil::printPhyLinkInfo(
    QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }

  PortOperState phyLink =
      fbMacsecHandler->sync_macsecGetPhyLinkInfo(FLAGS_port);
  printf(
      "Port %s phy line status: %s\n",
      FLAGS_port.c_str(),
      (apache::thrift::util::enumNameSafe(phyLink)).c_str());
}

void CredoMacsecUtil::printPhySerdesInfo(
    QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }

  phy::PhyInfo phyInfo;
  fbMacsecHandler->sync_getPhyInfo(phyInfo, FLAGS_port);

  printf("Printing Eye values for the port %s\n", FLAGS_port.c_str());
  for (auto& laneEye : phyInfo.stats()->line()->pmd()->lanes().value()) {
    if (!laneEye.second.eyes().has_value()) {
      continue;
    }
    printf("  Lane id: %d\n", laneEye.first);
    for (auto eye : laneEye.second.eyes().value()) {
      printf(
          "    Width: %d\n", eye.width().has_value() ? eye.width().value() : 0);
      printf(
          "    Height: %d\n",
          eye.height().has_value() ? eye.height().value() : 0);
    }
  }
}

void CredoMacsecUtil::getAllScInfo(QsfpServiceAsyncClient* fbMacsecHandler) {
  std::string allScInfo;
  std::vector<HwObjectType> in{HwObjectType::MACSEC};
  fbMacsecHandler->sync_listHwObjects(allScInfo, in, true);

  printf("Printing all SC info\n");
  printf("%s\n", allScInfo.c_str());
}

void CredoMacsecUtil::getPortStats(QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  if (!(FLAGS_ingress ^ FLAGS_egress)) {
    printf("One direction (--ingress or --egress) is required\n");
    return;
  }

  std::map<std::string, MacsecStats> allportStats;
  fbMacsecHandler->sync_getAllMacsecPortStats(allportStats, true);

  auto portMacsecStatsItr = allportStats.find(FLAGS_port);
  if (portMacsecStatsItr == allportStats.end()) {
    printf("Wrong port name specified: %s\n", FLAGS_port.c_str());
    return;
  }

  auto& portMacsecStats = portMacsecStatsItr->second;
  if (FLAGS_ingress) {
    auto& portStats = portMacsecStats.ingressPortStats().value();
    printPortStatsHelper(portStats, FLAGS_port, FLAGS_ingress);
  } else {
    auto& portStats = portMacsecStats.egressPortStats().value();
    printPortStatsHelper(portStats, FLAGS_port, FLAGS_ingress);
  }
}

void CredoMacsecUtil::getFlowStats(QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  if ((!FLAGS_ingress && !FLAGS_egress) || (FLAGS_ingress && FLAGS_egress)) {
    printf("One direction (--ingress or --egress) is required\n");
    return;
  }

  std::map<std::string, MacsecStats> allportStats;
  fbMacsecHandler->sync_getAllMacsecPortStats(allportStats, true);

  auto portMacsecStatsItr = allportStats.find(FLAGS_port);
  if (portMacsecStatsItr == allportStats.end()) {
    printf("Wrong port name specified: %s\n", FLAGS_port.c_str());
    return;
  }

  auto& portMacsecStats = portMacsecStatsItr->second;
  if (FLAGS_ingress) {
    for (auto& flowStatsItr : portMacsecStats.ingressFlowStats().value()) {
      auto& sci = flowStatsItr.sci().value();
      auto& flowStats = flowStatsItr.flowStats().value();
      printf(
          "Flow stats for SCI: %s.%d\n",
          sci.macAddress().value().c_str(),
          sci.port().value());
      printFlowStatsHelper(flowStats, FLAGS_port, FLAGS_ingress);
    }
  } else {
    for (auto& flowStatsItr : portMacsecStats.egressFlowStats().value()) {
      auto& sci = flowStatsItr.sci().value();
      auto& flowStats = flowStatsItr.flowStats().value();
      printf(
          "Flow stats for SCI: %s.%d\n",
          sci.macAddress().value().c_str(),
          sci.port().value());
      printFlowStatsHelper(flowStats, FLAGS_port, FLAGS_ingress);
    }
  }
}

void CredoMacsecUtil::getSaStats(QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }
  if ((!FLAGS_ingress && !FLAGS_egress) || (FLAGS_ingress && FLAGS_egress)) {
    printf("One direction (--ingress or --egress) is required\n");
    return;
  }

  std::map<std::string, MacsecStats> allportStats;
  fbMacsecHandler->sync_getAllMacsecPortStats(allportStats, true);

  auto portMacsecStatsItr = allportStats.find(FLAGS_port);
  if (portMacsecStatsItr == allportStats.end()) {
    printf("Wrong port name specified: %s\n", FLAGS_port.c_str());
    return;
  }

  auto& portMacsecStats = portMacsecStatsItr->second;
  if (FLAGS_ingress) {
    for (auto& saStatsItr :
         portMacsecStats.rxSecureAssociationStats().value()) {
      auto& saId = saStatsItr.saId().value();
      auto& saStats = saStatsItr.saStats().value();
      printf(
          "SA stats for SCI: %s.%d, AN: %d\n",
          saId.sci()->macAddress().value().c_str(),
          saId.sci()->port().value(),
          saId.assocNum().value());
      printSaStatsHelper(saStats, FLAGS_port, FLAGS_ingress);
    }
  } else {
    for (auto& saStatsItr :
         portMacsecStats.txSecureAssociationStats().value()) {
      auto& saId = saStatsItr.saId().value();
      auto& saStats = saStatsItr.saStats().value();
      printf(
          "SA stats for SCI: %s.%d, AN: %d\n",
          saId.sci()->macAddress().value().c_str(),
          saId.sci()->port().value(),
          saId.assocNum().value());
      printSaStatsHelper(saStats, FLAGS_port, FLAGS_ingress);
    }
  }
}

void CredoMacsecUtil::getAclStats(QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_port == "") {
    printf("Port name is required\n");
    return;
  }

  std::map<std::string, MacsecStats> allportStats;
  fbMacsecHandler->sync_getAllMacsecPortStats(allportStats, true);

  auto portMacsecStatsItr = allportStats.find(FLAGS_port);
  if (portMacsecStatsItr == allportStats.end()) {
    printf("Wrong port name specified: %s\n", FLAGS_port.c_str());
    return;
  }

  auto& portMacsecStats = portMacsecStatsItr->second;
  auto& inAclStats = portMacsecStats.ingressAclStats().value();
  printAclStatsHelper(inAclStats, FLAGS_port, true);
  auto& outAclStats = portMacsecStats.egressAclStats().value();
  printAclStatsHelper(outAclStats, FLAGS_port, false);
}

void CredoMacsecUtil::getAllPortStats(QsfpServiceAsyncClient* fbMacsecHandler) {
  std::map<std::string, MacsecStats> allportStats;

  if (FLAGS_port == "") {
    fbMacsecHandler->sync_getAllMacsecPortStats(allportStats, true);
  } else {
    std::vector<std::string> ports;
    ports.push_back(FLAGS_port);
    fbMacsecHandler->sync_getMacsecPortStats(allportStats, ports, true);
  }

  for (auto& portStatsItr : allportStats) {
    auto portName = portStatsItr.first;
    printf("Printing stats for %s\n", portName.c_str());
    printPortStatsHelper(
        portStatsItr.second.ingressPortStats().value(), portName, true);
    printPortStatsHelper(
        portStatsItr.second.egressPortStats().value(), portName, false);
    for (auto& flowStatsItr : portStatsItr.second.ingressFlowStats().value()) {
      printFlowStatsHelper(flowStatsItr.flowStats().value(), portName, true);
    }
    for (auto& flowStatsItr : portStatsItr.second.egressFlowStats().value()) {
      printFlowStatsHelper(flowStatsItr.flowStats().value(), portName, false);
    }
    for (auto& saStatsItr :
         portStatsItr.second.rxSecureAssociationStats().value()) {
      printSaStatsHelper(saStatsItr.saStats().value(), portName, true);
    }
    for (auto& saStatsItr :
         portStatsItr.second.txSecureAssociationStats().value()) {
      printSaStatsHelper(saStatsItr.saStats().value(), portName, false);
    }
    printAclStatsHelper(
        portStatsItr.second.ingressAclStats().value(), portName, true);
    printAclStatsHelper(
        portStatsItr.second.egressAclStats().value(), portName, false);
    printf("\n");
  }
}

void CredoMacsecUtil::getSdkState(QsfpServiceAsyncClient* fbMacsecHandler) {
  if (FLAGS_filename == "") {
    printf("File name is required\n");
    return;
  }

  bool rc = false;
  try {
    rc = fbMacsecHandler->sync_getSdkState(FLAGS_filename);
  } catch (const std::exception& ex) {
    printf("getSdkState failed: %s\n", ex.what());
    return;
  }
  printf(
      "SAI state dump to file %s was %s",
      FLAGS_filename.c_str(),
      rc ? "Successful" : "Failed");
}

} // namespace fboss
} // namespace facebook
