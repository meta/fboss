// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <memory>
#include <utility>

#include "fboss/lib/firmware_storage/FbossFirmware.h"
#include "fboss/qsfp_service/module/CdbCommandBlock.h"

namespace facebook::fboss {

class TransceiverImpl;

/*
 * This class provides the data and function to perform firmware upgrade on a
 * optics module. This can be used from CLI utility or from the process like
 * qsfp_service. It will either get the firmware data from utility or interface
 * with firmware_storage library to get the data.
 */
class CmisFirmwareUpgrader {
 public:
  // Mapping the module type string to module type part number string
  // The module type string is provided by user CLI and the mapped module part
  // number string can be compared with modules's register page 1 byte 148-163
  static inline std::map<std::string, std::vector<std::string>> partNoMap{
      {"eoptolink-2x400g-fr4",
       {"EOLO-168HG-02-1B", "EOLO-168HG-02-9B", "EOLO-168HG-5H-LB"}},
      {"finisar-200g-fr4", {"FTCC1112E1PLL-FB"}},
      {"finisar-400g-fr4",
       {"FTCD4313E2PCL   ",
        "FTCD4313E2PCL-FB",
        "FTCD4313E2PCLFB1",
        "FTCD4313E2PCLFB2",
        "FTCD4313E2PCLFB3",
        "FTCD4313E2PCLFB4",
        "FTCD4313E2PCLFBC",
        "FTCD4313E2PCLFBE"}},
      {"finisar-400g-lr4", {"FTCD4323E2PCL   "}},
      {"finisar-2x400g-fr4", {"FTCE4717E1PCB-FB"}},
      {"innolight-200g-fr4",
       {"T-FX4FNT-HFB    ", "T-FX4FNT-HFP    ", "T-FX4FNT-HFS    "}},
      {"innolight-400g-fr4",
       {"T-DQ4CNT-NFB    ", "T-DQ4CNT-NF2    ", "T-DQ4CNT-NFM    "}},
      {"innolight-2x400g-fr4", {"T-OL8CNT-NFM    "}},
      {"intel-200g-fr4", {"SPTSMP3CLCK8    ", "SPTSMP3CLCK9    "}},
      {"intel-400g-fr4", {"SPTSHP3CLCKS    "}},
  };

  // Modules complying with CMIS 5.0 and later don't need the MSA password to
  // be written to unlock the privileged firmware download operation.
  static constexpr uint8_t kMsaPasswordRequiredBelowCmisMajorRev = 5;

  // Minimum CMIS major revision for which we poll for module ready after
  // issuing the firmware Run command. This is an independent policy from the
  // MSA password requirement above (it just happens to share the same
  // threshold today) so the two can evolve separately.
  static constexpr uint8_t kMinCmisMajorRevForModuleReadyPoll = 5;

  // Constructor. The caller is responsible for interfacing with Firmware
  // Store and provide the FbossFirmware object. cmisMajorRevision is the
  // major number of the CMIS revision the module complies with (Lower Page
  // byte 1, upper nibble) and is mandatory - the caller decides how to obtain
  // it, and what to report when it can't be read.
  CmisFirmwareUpgrader(
      TransceiverImpl* bus,
      unsigned int modId,
      FbossFirmware* fbossFirmware,
      uint8_t cmisMajorRevision,
      uint64_t cdbWriteDelayUsec = POST_I2C_WRITE_DELAY_CDB_US);

  // Function to trigger the firmware download to the QSFP module of CMIS type
  bool cmisModuleFirmwareUpgrade();

 private:
  // Get Rid of this: Convert it to TransceiverImpl* interface....
  // Bus class for moduleRead/Write() functions
  TransceiverImpl* bus_;
  // module Id for upgrade
  unsigned int moduleId_;
  // FbossFirmware object
  FbossFirmware* fbossFirmware_;
  // Firmware image pointer
  folly::io::Cursor imageCursor_{nullptr};
  // Image IO buffer. This contains the entire firmware image file content
  // mapped in memory
  std::unique_ptr<folly::IOBuf> fileIOBuffer_;
  // MSA password for privilege operation
  std::array<uint8_t, 4> msaPassword_{};
  // CMIS major revision the module complies with
  uint8_t cmisMajorRevision_;
  // Default image header length
  uint32_t imageHeaderLen_;
  // Image type (App/Dsp)
  bool appImage_{true};
  // Post-write delay for CDB I2C transactions during firmware upgrade
  uint64_t cdbWriteDelayUsec_;

  // Private function to finally download firmware image on module using cdb
  // process
  bool cmisModuleFirmwareDownload(const uint8_t* imageBuf, int imageLen);

  // Resolve effective CDB command timeout for firmware upgrade.
  // Priority: explicit gflag > MaxDurationWrite (tunable only, capped) > gflag
  // default.
  uint64_t resolveFwUpgradeCdbTimeout(CdbCommandBlock& commandBlock);

  // Check if module is tunable by reading MEDIA_INTERFACE_TECHNOLOGY register
  bool isTunableModule() const;

  // Write the given value to the module password entry register - the MSA
  // password to unlock the privileged firmware download operation, or an
  // all-zero value to revert it. No-op on modules that don't need it
  void writeMsaPasswordIfNeeded(const std::array<uint8_t, 4>& password);

  // Poll for module to reach ready state after firmware run command
  bool pollForModuleReady();

  // Check if CdbCmdCompleteFlag is supported by reading the CDB advertisement
  bool isCdbCmdCompleteFlagSupported() const;
};

} // namespace facebook::fboss
