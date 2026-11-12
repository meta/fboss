// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"
#include "fboss/qsfp_service/PortManager.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h" // @manual=//fboss/qsfp_service/platforms/wedge:wedge-platform-default
#include "fboss/qsfp_service/platforms/wedge/WedgeManagerInit.h" // @manual=//fboss/qsfp_service/platforms/wedge:wedge-platform-default
#include "fboss/util/qsfp/QsfpUtilContainer.h"
#include "fboss/util/qsfp/QsfpUtilTx.h"
#include "fboss/util/wedge_qsfp_util.h"

#include <folly/gen/Base.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>

#include <sysexits.h>
#include <iostream>
#include <iterator>

using namespace facebook::fboss;
using std::make_pair;
using std::chrono::seconds;
using std::chrono::steady_clock;

std::vector<FlagCommand> kCommands = {
    {"pause_remediation", {}},
    {"get_remediation_until_time", {}},
    {"read_reg", {"offset", "length", "page", "i2c_address"}},
    {"write_reg", {"offset", "length", "page", "i2c_address"}},
    {"clear_low_power", {}},
    {"set_low_power", {}},
    {"tx_enable", {}},
    {"tx_disable", {}},
    {"set_40g", {}},
    {"set_100g", {}},
    {"app_sel", {}},
    {"cdr_enable", {}},
    {"cdr_disable", {}},
    {"qsfp_hard_reset", {}},
    {"qsfp_reset", {"qsfp_reset_type", "qsfp_reset_action"}},
    {"electrical_loopback", {}},
    {"optical_loopback", {}},
    {"clear_loopback", {}},
    {"update_module_firmware", {"firmware_filename"}},
    {"cdb_command", {}},
    {"get_module_fw_info", {}},
    {"update_bulk_module_fw",
     {"port_range", "firmware_filename", "module_type", "fw_version"}},
    {"batch_ops", {"batchfile"}},
};

void listCommands() {
  std::cerr
      << "Commands in this tool are flags so you can combine them "
         "and for example set_40g and tx_enable on all ports specified.\n\n"
         "Valid commands and their flags:\n";

  std::cerr << "Command:\n    <None>: Print port details. Prints port "
               "summary for all ports if no port specified.\nFlags:\n\n";

  std::copy(
      std::begin(kCommands),
      std::end(kCommands),
      std::ostream_iterator<FlagCommand>(std::cerr, "\n"));
}

int32_t getTransceiverIdForPort(
    std::string& portNameStr,
    std::map<std::string, std::vector<int32_t>>& portNameToTransceiverIds) {
  if (portNameToTransceiverIds.find(portNameStr) ==
      portNameToTransceiverIds.end()) {
    throw FbossError(
        "Couldn't find a transceiverID for portName:", portNameStr);
  }
  return portNameToTransceiverIds.at(portNameStr)[0];
}

std::set<std::string> getPortNames(
    int32_t tcvrId,
    std::map<int32_t, std::set<std::string>>& tcvrIdToPortNames) {
  if (tcvrIdToPortNames.find(tcvrId) == tcvrIdToPortNames.end()) {
    throw FbossError("Couldn't find a portName for transceiverID:", tcvrId);
  }
  return tcvrIdToPortNames.at(tcvrId);
}

std::map<std::string, std::vector<int32_t>> getPortNameToTransceiverIds(
    WedgeManager* wedgeManager,
    folly::EventBase& evb) {
  std::map<std::string, std::vector<int32_t>> result;
  if (FLAGS_direct_i2c) {
    for (auto& portMap : wedgeManager->getPortNameToModuleMap()) {
      result[portMap.first].push_back(portMap.second);
    }
  } else {
    for (auto& portMap : getPortTransceiverIDs(evb)) {
      result[portMap.first] = portMap.second;
    }
  }
  return result;
}

std::map<int32_t, std::set<std::string>> getTransceiverIdToPortNames(
    std::map<std::string, std::vector<int32_t>> portNameToTransceiverIds) {
  std::map<int32_t, std::set<std::string>> result;
  for (auto& portNameToTransceiverId : portNameToTransceiverIds) {
    for (auto& tcvrId : portNameToTransceiverId.second) {
      result[tcvrId].insert(portNameToTransceiverId.first);
    }
  }
  return result;
}

int getNumQsfpModules(
    std::map<int32_t, std::set<std::string>> tcvrIdsToPortNames) {
  return tcvrIdsToPortNames.size();
}

int main(int argc, char* argv[]) {
  const folly::Init init(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
      "minloglevel", "0", gflags::SET_FLAGS_DEFAULT);
  initPlatformDescriptorPathFromQsfpConfig();
  folly::EventBase evb;

  if (FLAGS_list_commands) {
    listCommands();
    return EX_OK;
  }

  if (FLAGS_ssl_policy != "encrypted" && FLAGS_ssl_policy != "plaintext") {
    fprintf(stderr, "--ssl-policy must be either encrypted or plaintext\n");
    return EX_USAGE;
  }

  if (FLAGS_set_100g && FLAGS_set_40g) {
    fprintf(stderr, "Cannot set both 40g and 100g\n");
    return EX_USAGE;
  }
  if (FLAGS_cdr_enable && FLAGS_cdr_disable) {
    fprintf(stderr, "Cannot set and clear the CDR bits\n");
    return EX_USAGE;
  }
  if (FLAGS_clear_low_power && FLAGS_set_low_power) {
    fprintf(stderr, "Cannot set and clear lp mode\n");
    return EX_USAGE;
  }

  if (FLAGS_port_info_summary && !FLAGS_direct_i2c) {
    fprintf(stderr, "--port_info_summary requires --direct_i2c\n");
    return EX_USAGE;
  }

  // Verify the select command is working properly with regard to the
  // --direct-i2c option
  if (!verifyDirectI2cCompliance()) {
    return EX_USAGE;
  }

  std::vector<unsigned int> ports;
  std::vector<std::string> portNames;
  bool good = true;
  std::unique_ptr<facebook::fboss::WedgeManager> wedgeManager;
  if (FLAGS_direct_i2c) {
    wedgeManager = createQsfpManagers().first;
  }
  std::map<std::string, std::vector<int32_t>> portNameToTransceiverIds =
      getPortNameToTransceiverIds(wedgeManager.get(), evb);
  std::map<int32_t, std::set<std::string>> transceiverIdToPortNames =
      getTransceiverIdToPortNames(portNameToTransceiverIds);
  int numQsfpModules = getNumQsfpModules(transceiverIdToPortNames);
  if (argc == 1) {
    folly::gen::range(1, numQsfpModules + 1) | folly::gen::appendTo(ports);
  } else {
    for (int n = 1; n < argc; ++n) {
      unsigned int portNum;
      std::string portStr = argv[n];
      try {
        if (argv[n][0] == 'x' && argv[n][1] == 'e') {
          portNum = 1 + folly::to<unsigned int>(argv[n] + 2);
        } else if (isalpha(portStr[0])) {
          portNum =
              getTransceiverIdForPort(portStr, portNameToTransceiverIds) + 1;
          portNames.emplace_back(portStr);
        } else {
          portNum = folly::to<unsigned int>(argv[n]);
          auto portNameList =
              getPortNames(portNum - 1, transceiverIdToPortNames);
          if (portNameList.empty()) {
            throw FbossError(
                "Couldn't find a portName for transceiverID (1-indexed):",
                portNum);
          }
          portNames.insert(
              portNames.end(), portNameList.begin(), portNameList.end());
        }
        ports.push_back(portNum);
      } catch (const std::exception& ex) {
        fprintf(
            stderr,
            "error: invalid port name/number \"%s\": %s\n",
            argv[n],
            ex.what());
        good = false;
      }
    }
  }
  if (!good) {
    return EX_USAGE;
  }

  if (FLAGS_pause_remediation) {
    try {
      setPauseRemediation(evb, portNames);
    } catch (const std::exception& ex) {
      fprintf(
          stderr, "error pausing remediation of qsfp_service: %s\n", ex.what());
      return EX_SOFTWARE;
    }
    return EX_OK;
  }

  if (FLAGS_get_remediation_until_time) {
    try {
      doGetRemediationUntilTime(evb, portNames);
      return EX_OK;
    } catch (const std::exception& ex) {
      fprintf(
          stderr,
          "error getting remediationUntil time from qsfp_service: %s\n",
          ex.what());
      return EX_SOFTWARE;
    }
    return EX_OK;
  }

  TransceiverI2CApi* bus = nullptr;
  DirectI2cInfo i2cInfo;
  if (FLAGS_direct_i2c) {
    bus = QsfpUtilContainer::getInstance()->getTransceiverBus();
    i2cInfo = DirectI2cInfo{bus, wedgeManager.get()};
  }

  bool printInfo =
      !(FLAGS_clear_low_power || FLAGS_tx_disable || FLAGS_tx_enable ||
        FLAGS_set_100g || FLAGS_set_40g || FLAGS_cdr_enable ||
        FLAGS_cdr_disable || FLAGS_set_low_power || FLAGS_qsfp_hard_reset ||
        FLAGS_electrical_loopback || FLAGS_optical_loopback ||
        FLAGS_host_output_loopback || FLAGS_media_output_loopback ||
        FLAGS_clear_loopback || FLAGS_read_reg || FLAGS_write_reg ||
        FLAGS_update_module_firmware || FLAGS_get_module_fw_info ||
        FLAGS_app_sel || FLAGS_cdb_command || FLAGS_update_bulk_module_fw ||
        FLAGS_vdm_info || FLAGS_prbs_start || FLAGS_prbs_stop ||
        FLAGS_prbs_stats || FLAGS_module_io_stats || FLAGS_batch_ops ||
        FLAGS_capabilities || FLAGS_qsfp_reset || FLAGS_dump_tcvr_i2c_log);

  if (FLAGS_direct_i2c || !printInfo) {
    try {
      if (FLAGS_direct_i2c) {
        tryOpenBus(bus);
      }
    } catch (const std::exception& ex) {
      fprintf(stderr, "error: unable to open device: %s\n", ex.what());
      return EX_IOERR;
    }
  } else {
    try {
      std::vector<int32_t> idx = zeroBasedPortIds(ports);
      if (FLAGS_client_parser) {
        auto domDataUnionMap = fetchDataFromQsfpService(idx, evb);
        for (auto& tcvrId : idx) {
          auto iter = domDataUnionMap.find(tcvrId);
          if (iter == domDataUnionMap.end()) {
            fprintf(stderr, "Port %d is not present.\n", tcvrId + 1);
          } else {
            auto logicalPorts = folly::join(
                ", ", getPortNames(tcvrId, transceiverIdToPortNames));
            printPortDetail(iter->second, iter->first + 1, logicalPorts);
          }
        }
      } else {
        auto transceiverInfo = fetchInfoFromQsfpService(idx, evb);
        for (auto& i : idx) {
          auto iter = transceiverInfo.find(i);
          if (iter == transceiverInfo.end()) {
            fprintf(
                stderr, "qsfp_service didn't return data for Port %d\n", i + 1);
          } else {
            auto logicalPorts =
                folly::join(", ", getPortNames(i, transceiverIdToPortNames));
            printPortDetailService(
                iter->second, iter->first + 1, FLAGS_verbose, logicalPorts);
          }
        }
      }
      return EX_OK;
    } catch (const std::exception& e) {
      fprintf(stderr, "Exception talking to qsfp_service: %s\n", e.what());
      return EX_SOFTWARE;
    }
  }

  if (FLAGS_read_reg) {
    return doReadReg(bus, ports, FLAGS_offset, FLAGS_length, FLAGS_page, evb);
  }

  if (FLAGS_write_reg) {
    std::vector<uint8_t> data;
    std::stringstream ss(FLAGS_data);
    std::string item;
    while (std::getline(ss, item, ',')) {
      try {
        if (item.substr(0, 2) == "0x") {
          data.push_back(std::stoi(item, nullptr, 16));
        } else {
          data.push_back(std::stoi(item, nullptr, 10));
        }
      } catch (const std::invalid_argument&) {
        fprintf(stderr, "Invalid integer value: %s\n", item.c_str());
        return EX_SOFTWARE;
      }
    }

    if (data.empty()) {
      fprintf(stderr, "No data provided\n");
      return EX_SOFTWARE;
    }

    return doWriteReg(bus, ports, FLAGS_offset, FLAGS_page, data, evb);
  }

  if (FLAGS_batch_ops) {
    return doBatchOps(bus, ports, FLAGS_batchfile, evb);
  }

  if (FLAGS_tx_disable || FLAGS_tx_enable) {
    QsfpUtilTx txCtrl(i2cInfo, portNames, portNameToTransceiverIds, evb);
    return txCtrl.setTxDisable();
  }

  if (FLAGS_module_io_stats) {
    printModuleTransactionStats(zeroBasedPortIds(ports), evb);
    return EX_OK;
  }

  if (FLAGS_dump_tcvr_i2c_log) {
    return dumpTransceiverI2cLog(portNames, evb);
  }

  int retcode = EX_OK;
  std::vector<PortInfoSummary> portInfoSummaries;
  for (unsigned int portNum : ports) {
    if (FLAGS_clear_low_power && overrideLowPower(portNum, false)) {
      printf("QSFP %d: cleared low power flags\n", portNum);
    }
    if (FLAGS_set_low_power && overrideLowPower(portNum, true)) {
      printf("QSFP %d: set low power flags\n", portNum);
    }

    if (FLAGS_set_40g && rateSelect(portNum, 0x0)) {
      printf("QSFP %d: set to optimize for 10G channels\n", portNum);
    }
    if (FLAGS_set_100g && rateSelect(portNum, 0xaa)) {
      printf("QSFP %d: set to optimize for 25G channels\n", portNum);
    }
    if (FLAGS_app_sel && appSel(bus, portNum, FLAGS_app_sel)) {
      printf("QSFP %d: set to application %d\n", portNum, FLAGS_app_sel);
    }

    if (FLAGS_cdr_enable && setCdr(bus, portNum, 0xff)) {
      printf("QSFP %d: CDR enabled\n", portNum);
    }

    if (FLAGS_cdr_disable && setCdr(bus, portNum, 0x00)) {
      printf("QSFP %d: CDR disabled\n", portNum);
    }

    if (FLAGS_qsfp_hard_reset && doQsfpHardReset(bus, portNum)) {
      printf("QSFP %d: Hard reset done\n", portNum);
    }

    if (FLAGS_optical_loopback || FLAGS_electrical_loopback ||
        FLAGS_host_output_loopback || FLAGS_media_output_loopback ||
        FLAGS_clear_loopback) {
      LoopbackMode loopback{noLoopback};
      std::string lbModeStr{"noLoopback"};
      if (FLAGS_optical_loopback) {
        loopback = opticalLoopback;
        lbModeStr = "opticalLoopback";
      } else if (FLAGS_electrical_loopback) {
        loopback = electricalLoopback;
        lbModeStr = "electricalLoopback";
      } else if (FLAGS_host_output_loopback) {
        loopback = hostOutputLoopback;
        lbModeStr = "hostOutputLoopback";
      } else if (FLAGS_media_output_loopback) {
        loopback = mediaOutputLoopback;
        lbModeStr = "mediaOutputLoopback";
      }

      if (setTransceiverLoopback(i2cInfo, portNames, loopback, evb)) {
        printf(
            "QSFP %d: done setting module loopback mode %s\n",
            portNum,
            lbModeStr.c_str());
      } else {
        printf(
            "QSFP %d: setting module loopback mode %s failed\n",
            portNum,
            lbModeStr.c_str());
      }
    }

    if (FLAGS_vdm_info && printVdmInfo(i2cInfo, portNum, evb)) {
      printf("QSFP %d: printed module vdm info\n", portNum);
      return EX_OK;
    }

    if (FLAGS_direct_i2c && printInfo) {
      try {
        auto portNameList = getPortNames(portNum - 1, transceiverIdToPortNames);
        DOMDataUnion dataUnion;
        fetchDataFromLocalI2CBus(i2cInfo, portNum, dataUnion);
        if (FLAGS_port_info_summary) {
          auto firstPort = portNameList.empty() ? "" : *portNameList.begin();
          auto summary = getPortInfoSummary(dataUnion, portNum, firstPort);
          if (summary) {
            portInfoSummaries.push_back(std::move(*summary));
          }
        } else {
          auto logicalPorts = folly::join(", ", portNameList);
          printPortDetail(dataUnion, portNum, logicalPorts, &i2cInfo);
        }
      } catch (const I2cError& ex) {
        // This generally means the QSFP module is not present.
        fprintf(stderr, "Port %d: not present: %s\n", portNum, ex.what());
        retcode = EX_SOFTWARE;
      } catch (const std::exception& ex) {
        fprintf(stderr, "error parsing QSFP data %u: %s\n", portNum, ex.what());
        retcode = EX_SOFTWARE;
      }
    }

    if (FLAGS_update_module_firmware) {
      printf("This action may bring down the port and interrupt the traffic\n");
      if (FLAGS_firmware_filename.empty()) {
        fprintf(
            stderr,
            "QSFP %d: Fail to upgrade firmware. Specify firmware using --firmware_filename\n",
            portNum);
      } else {
        cliModulefirmwareUpgrade(i2cInfo, portNum, FLAGS_firmware_filename);
      }
    }

    if (FLAGS_cdb_command) {
      if (getModuleTypeDirect(bus, portNum) !=
          TransceiverManagementInterface::CMIS) {
        printf("This command is applicable to CMIS module only\n");
      } else {
        doCdbCommand(i2cInfo, portNum);
      }
    }

    if (FLAGS_prbs_start || FLAGS_prbs_stop || FLAGS_prbs_stats) {
      if (!FLAGS_direct_i2c) {
        throw FbossError("Prbs commands are only supported in direct i2c mode");
      }
      std::vector<PortID> swPortList;
      std::vector<std::string> swPortNames;
      if (wedgeManager.get()) {
        swPortList = wedgeManager->getAllPlatformPorts(
            static_cast<TransceiverID>(portNum - 1));

        for (auto swPort : swPortList) {
          auto pName = wedgeManager->getPortNameByPortId(swPort);
          if (pName.has_value()) {
            swPortNames.push_back(pName.value());
          }
        }
      }

      if (FLAGS_prbs_start) {
        printf(
            "Starting PRBS on Module %d, this will bring down link\n", portNum);
        setModulePrbs(i2cInfo, swPortNames, true);
      } else if (FLAGS_prbs_stop) {
        printf("Stopping PRBS on Module %d\n", portNum);
        setModulePrbs(i2cInfo, swPortNames, false);
      } else if (FLAGS_prbs_stats) {
        printf("Showing PRBS stats for Module %d\n", portNum);
        getModulePrbsStats(i2cInfo, swPortList);
      }
    }
  }
  if (FLAGS_port_info_summary) {
    printPortInfoSummary(portInfoSummaries);
  }

  if (FLAGS_qsfp_reset) {
    // Do a reset for the chosen ports.
    if (FLAGS_direct_i2c) {
      // Do a direct qsfp hard reset via the CPLD/GPIO.
      for (unsigned int portNum : ports) {
        if (doQsfpHardReset(bus, portNum)) {
          printf("QSFP %d: Hard reset directly via HW done\n", portNum);
        } else {
          fprintf(
              stderr, "QSFP %d: Hard reset directly via HW failed\n", portNum);
        }
      }
    } else {
      // Do a QSFP reset through the qsfp_service.
      // The reset will depend on the flags: FLAGS_qsfp_reset_type and
      // FLAGS_qsfp_reset_action which are defaulted to HARD_RESET and
      // RESET_THEN_CLEAR.
      retcode = resetQsfp(portNames, evb);
      if (retcode != EX_OK) {
        fprintf(stderr, "Failed to reset QSFP modules via qsfp_service\n");
        return retcode;
      } else {
        printf("Successfully reset QSFP modules via qsfp_service\n");
      }
    }
  }

  if (FLAGS_capabilities) {
    if (printDiagsInfo(evb)) {
      printf("All QSFP: module capabilities info printed\n");
    } else {
      printf("All QSFP: module capabilities info not found\n");
    }
  }

  if (FLAGS_get_module_fw_info) {
    if (ports.size() < 1) {
      fprintf(
          stderr,
          "Pl specify 1 module or 2 modules for the range: <ModuleA> <moduleB>\n");
    } else if (ports.size() == 1) {
      get_module_fw_info(i2cInfo, ports[0], ports[0]);
    } else {
      get_module_fw_info(i2cInfo, ports[0], ports[1]);
    }
  }

  if (FLAGS_update_bulk_module_fw) {
    if (FLAGS_port_range.empty()) {
      fprintf(stderr, "Pl specify the port range ie: 1,3,5-8\n");
      return EX_USAGE;
    }
    if (FLAGS_firmware_filename.empty()) {
      fprintf(
          stderr, "Pl specify firmware filename using --firmware_filename\n");
      return EX_USAGE;
    }
    if (FLAGS_module_type.empty()) {
      fprintf(
          stderr,
          "Pl specify module type using --module_type (ie: finisar-200g)\n");
      return EX_USAGE;
    }
    if (FLAGS_fw_version.empty()) {
      fprintf(
          stderr,
          "Pl specify firmware version using --fw_version (ie: 7.8 or ca.f8)\n");
      return EX_USAGE;
    }

    cliModulefirmwareUpgrade(
        i2cInfo, FLAGS_port_range, FLAGS_firmware_filename);
  }

  return retcode;
}
