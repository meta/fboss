// Copyright 2004-present Facebook. All Rights Reserved.
#pragma once

#include "fboss/agent/types.h"
#include "fboss/lib/firmware_storage/FbossFirmware.h"
#include "fboss/lib/usb/TransceiverI2CApi.h"
#include "fboss/lib/usb/TransceiverPlatformApi.h"
#include "fboss/lib/usb/TransceiverPlatformI2cApi.h"
#include "fboss/qsfp_service/TransceiverManager.h"
#include "fboss/qsfp_service/lib/QsfpClient.h"
#include "fboss/qsfp_service/module/FirmwareUpgrader.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeQsfp.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

DECLARE_bool(clear_low_power);
DECLARE_bool(set_low_power);
DECLARE_bool(tx_disable);
DECLARE_bool(tx_enable);
DECLARE_int32(channel);
DECLARE_bool(set_40g);
DECLARE_bool(set_100g);
DECLARE_int32(app_sel);
DECLARE_bool(cdr_enable);
DECLARE_bool(cdr_disable);
DECLARE_int32(open_timeout);
DECLARE_bool(direct_i2c);
DECLARE_bool(qsfp_hard_reset);
DECLARE_bool(qsfp_reset);
DECLARE_int32(reset_type);
DECLARE_int32(reset_action);
DECLARE_bool(electrical_loopback);
DECLARE_bool(optical_loopback);
DECLARE_bool(host_output_loopback);
DECLARE_bool(media_output_loopback);
DECLARE_bool(clear_loopback);
DECLARE_bool(skip_check);
DECLARE_bool(read_reg);
DECLARE_bool(write_reg);
DECLARE_int32(offset);
DECLARE_string(data);
DECLARE_int32(length);
DECLARE_int32(page);
DECLARE_int32(pause_remediation);
DECLARE_bool(get_remediation_until_time);
DECLARE_bool(update_module_firmware);
DECLARE_string(firmware_filename);
DECLARE_uint32(msa_password);
DECLARE_uint32(image_header_len);
DECLARE_bool(get_module_fw_info);
DECLARE_bool(cdb_command);
DECLARE_int32(command_code);
DECLARE_string(cdb_payload);
DECLARE_bool(update_bulk_module_fw);
DECLARE_string(module_type);
DECLARE_string(fw_version);
DECLARE_string(port_range);
DECLARE_bool(dsp_image);
DECLARE_bool(client_parser);
DECLARE_bool(verbose);
DECLARE_bool(list_commands);
DECLARE_bool(vdm_info);
DECLARE_bool(batch_ops);
DECLARE_string(batchfile);
DECLARE_uint32(i2c_address);
DECLARE_bool(prbs_start);
DECLARE_bool(prbs_stop);
DECLARE_bool(prbs_stats);
DECLARE_bool(generator);
DECLARE_bool(checker);
DECLARE_bool(module_io_stats);
DECLARE_bool(capabilities);
DECLARE_bool(dump_tcvr_i2c_log);
DECLARE_bool(port_info_summary);
DECLARE_string(ssl_policy);

// electricalLoopback = CMIS Host Side Input, opticalLoopback = CMIS Media Side
// Input. hostOutputLoopback and mediaOutputLoopback are CMIS only.
enum LoopbackMode {
  noLoopback,
  electricalLoopback,
  opticalLoopback,
  hostOutputLoopback,
  mediaOutputLoopback,
};

namespace facebook::fboss {

struct FlagCommand {
  const std::string command;
  const std::vector<std::string> flags;
};

struct DirectI2cInfo {
  TransceiverI2CApi* bus;
  TransceiverManager* transceiverManager;
};

struct PortInfoSummary {
  unsigned int port;
  std::string name;
  std::string vendor;
  std::string partNumber;
};

std::ostream& operator<<(std::ostream& os, const FlagCommand& cmd);

std::unique_ptr<facebook::fboss::QsfpServiceAsyncClient> getQsfpClient(
    folly::EventBase& evb);

bool overrideLowPower(unsigned int port, bool lowPower);

bool setCdr(TransceiverI2CApi* bus, unsigned int port, uint8_t value);

bool rateSelect(unsigned int port, uint8_t value);

bool appSel(TransceiverI2CApi* bus, unsigned int port, uint8_t value);

std::map<int32_t, TransceiverManagementInterface> getModuleType(
    const std::vector<unsigned int>& ports);

TransceiverManagementInterface getModuleTypeDirect(
    TransceiverI2CApi* bus,
    unsigned int port);

uint8_t getCmisMajorRevisionDirect(TransceiverI2CApi* bus, unsigned int port);

std::map<int32_t, TransceiverManagementInterface> getModuleTypeViaService(
    const std::vector<unsigned int>& ports,
    folly::EventBase& evb);

std::vector<int32_t> zeroBasedPortIds(const std::vector<unsigned int>& ports);

void doReadRegViaService(
    const std::vector<int32_t>& ports,
    int offset,
    int length,
    int page,
    folly::EventBase& evb,
    std::map<int32_t, ReadResponse>& response);

int doReadReg(
    TransceiverI2CApi* bus,
    std::vector<unsigned int>& ports,
    int offset,
    int length,
    int page,
    folly::EventBase& evb);

int readRegister(
    unsigned int port,
    int offset,
    int length,
    int page,
    uint8_t* buf);

bool doWriteRegViaService(
    const std::vector<int32_t>& ports,
    int offset,
    int page,
    const std::vector<uint8_t>& data,
    folly::EventBase& evb);

int doWriteReg(
    TransceiverI2CApi* bus,
    std::vector<unsigned int>& ports,
    int offset,
    int page,
    const std::vector<uint8_t>& data,
    folly::EventBase& evb);

int writeRegister(
    const std::vector<unsigned int>& ports,
    int offset,
    int page,
    uint8_t data);

int doBatchOps(
    TransceiverI2CApi* bus,
    std::vector<unsigned int>& ports,
    std::string batchfile,
    folly::EventBase& evb);

std::map<int32_t, DOMDataUnion> fetchDataFromQsfpService(
    const std::vector<int32_t>& ports,
    folly::EventBase& evb);

std::map<int32_t, TransceiverInfo> fetchInfoFromQsfpService(
    const std::vector<int32_t>& ports,
    folly::EventBase& evb);

// Function to create the WedgeQsfp HW Interface which enables i2c logging.
// Populates the DOMDataUnion for a transceiver.
// Expected to be used when --direct_i2c is set.
std::unique_ptr<WedgeQsfp> fetchDataFromLocalI2CBus(
    DirectI2cInfo& i2cInfo,
    const unsigned int port,
    DOMDataUnion& cmisDataOut);

DOMDataUnion getDOMDataUnionI2CBus(
    DirectI2cInfo i2cInfo,
    unsigned int port,
    WedgeQsfp* qsfpImpl);

folly::StringPiece sfpString(const uint8_t* buf, size_t offset, size_t len);

void printThresholds(
    const std::string& name,
    const uint8_t* data,
    std::function<double(uint16_t)> conversionCb);

std::string getLocalTime(std::time_t t);

void setPauseRemediation(
    folly::EventBase& evb,
    std::vector<std::string> portList);
void doGetRemediationUntilTime(
    folly::EventBase& evb,
    std::vector<std::string> portList);

void printChannelMonitor(
    unsigned int index,
    const uint8_t* buf,
    unsigned int rxMSB,
    unsigned int rxLSB,
    unsigned int txBiasMSB,
    unsigned int txBiasLSB,
    unsigned int txPowerMSB,
    unsigned int txPowerLSB,
    std::optional<double> rxSNR);

void printSffDetail(const DOMDataUnion& domDataUnion, unsigned int port);
void printSffDetailService(
    const TransceiverInfo& transceiverInfo,
    unsigned int port,
    bool verbose);
void printCmisDetail(const DOMDataUnion& domDataUnion, unsigned int port);
void printCmisDetailService(
    const TransceiverInfo& transceiverInfo,
    unsigned int port,
    bool verbose);

void printPortDetail(
    const DOMDataUnion& domDataUnion,
    unsigned int port,
    const std::string& portNames,
    DirectI2cInfo* i2cInfo = nullptr);
std::optional<PortInfoSummary> getPortInfoSummary(
    const DOMDataUnion& domDataUnion,
    unsigned int port,
    const std::string& firstPortName);
void printPortInfoSummary(const std::vector<PortInfoSummary>& summaries);
void printPortDetailService(
    const TransceiverInfo& transceiverInfo,
    unsigned int port,
    bool verbose,
    const std::string& portNames);

void tryOpenBus(TransceiverI2CApi* bus);

bool doQsfpHardReset(TransceiverI2CApi* bus, unsigned int port);

int resetQsfp(const std::vector<std::string>& ports, folly::EventBase& evb);

int dumpTransceiverI2cLog(
    const std::vector<std::string>& ports,
    folly::EventBase& evb);

bool doMiniphotonLoopbackDirect(
    TransceiverI2CApi* bus,
    unsigned int port,
    LoopbackMode mode);

bool cmisLoopbackDirect(
    TransceiverI2CApi* bus,
    unsigned int port,
    LoopbackMode mode);

bool setTransceiverLoopback(
    DirectI2cInfo i2cInfo,
    std::vector<std::string> portList,
    LoopbackMode mode,
    folly::EventBase& evb);

std::map<std::string, std::vector<int32_t>> getPortTransceiverIDs(
    folly::EventBase& evb);

bool cliModulefirmwareUpgrade(
    DirectI2cInfo i2cInfo,
    unsigned int port,
    std::string firmwareFilename);
bool cliModulefirmwareUpgrade(
    DirectI2cInfo i2cInfo,
    std::string portRangeStr,
    std::string firmwareFilename);

void get_module_fw_info(
    DirectI2cInfo i2cInfo,
    unsigned int moduleA,
    unsigned int moduleB);

void doCdbCommand(DirectI2cInfo i2cInfo, unsigned int module);

bool printVdmInfo(
    DirectI2cInfo i2cInfo,
    unsigned int port,
    folly::EventBase& evb);

bool getEepromCsumStatus(const DOMDataUnion& domDataUnion);

std::pair<std::unique_ptr<TransceiverI2CApi>, int>
getTransceiverIOBusFromPlatform(const std::string& /* platform */);

std::pair<std::unique_ptr<TransceiverPlatformApi>, int>
getTransceiverPlatformAPIFromMode(
    PlatformType /* platformType */,
    TransceiverI2CApi* /* i2cBus */);

std::pair<std::unique_ptr<TransceiverI2CApi>, int> getTransceiverIOBusFromMode(
    PlatformType /* platformType */);

std::pair<std::unique_ptr<facebook::fboss::TransceiverI2CApi>, int>
getTransceiverAPI();

/* This function creates and returns TransceiverPlatformApi object. For Fpga
 * controlled platform this function creates Platform specific TransceiverApi
 * object and returns it. For I2c controlled platform this function creates
 * TransceiverPlatformI2cApi object and keeps the platform specific I2CBus
 * object raw pointer inside it for reference. The returned object's Qsfp
 * control function is called here to use appropriate Fpga/I2c Api in this
 * function.
 */
std::pair<std::unique_ptr<facebook::fboss::TransceiverPlatformApi>, int>
getTransceiverPlatformAPI(facebook::fboss::TransceiverI2CApi* i2cBus);

bool isTrident2();

std::vector<unsigned int> portRangeStrToPortList(std::string portQualifier);

int getModulesPerController();

void bucketize(
    std::vector<unsigned int>& modlist,
    int modulesPerController,
    std::vector<std::vector<unsigned int>>& bucket);

std::vector<int> getPidForProcess(std::string proccessName);

void setModulePrbs(
    DirectI2cInfo i2cInfo,
    std::vector<std::string> portList,
    bool start);
void getModulePrbsStats(DirectI2cInfo i2cInfo, std::vector<PortID> portList);

bool verifyDirectI2cCompliance();

// Fall back to qsfp_service's platform descriptor path when none is passed.
void initPlatformDescriptorPathFromQsfpConfig();

void printModuleTransactionStats(
    const std::vector<int32_t>& ports,
    folly::EventBase& evb);

bool printDiagsInfo(folly::EventBase& evb);

} // namespace facebook::fboss
