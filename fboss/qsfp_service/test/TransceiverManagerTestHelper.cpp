/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/test/TransceiverManagerTestHelper.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_validation_types.h"

#include "fboss/qsfp_service/test/FakeConfigsHelper.h"

namespace facebook::fboss {

void TransceiverManagerTestHelper::SetUp() {
  setupFakeAgentConfig(agentCfgPath);
  cfg::QsfpServiceConfig qsfpCfg;
  cfg::TransceiverFirmware fwVersions;
  cfg::Firmware fw;
  cfg::FirmwareVersion fwVersionApp, fwVersionDsp;
  fwVersionApp.version() = getFakeAppFwVersionFull();
  fwVersionApp.fwType() = cfg::FirmwareType::APPLICATION;
  fwVersionDsp.version() = getFakeDspFwVersion();
  fwVersionDsp.fwType() = cfg::FirmwareType::DSP;
  fw.versions() = {fwVersionApp, fwVersionDsp};
  fwVersions.versionsMap()[getFakePartNumber()] = fw;
  fwVersions.fwHandleMap()[getFakePartNumber()] = getFakeFwStorageHandle();
  qsfpCfg.transceiverFirmwareVersions() = fwVersions;

  auto createTransceiverAttributes =
      [](std::vector<cfg::PortProfileID> supportedPortProfiles) {
        TransceiverAttributes config;
        FirmwarePair pair1, pair2;
        pair1.applicationFirmwareVersion() = "1";
        pair1.dspFirmwareVersion() = "1";
        pair2.applicationFirmwareVersion() = "2";
        pair2.dspFirmwareVersion() = "2";

        config.mediaInterfaceCode() = MediaInterfaceCode::CR4_100G;
        config.supportedFirmwareVersions() = {pair1, pair2};
        config.supportedPortProfiles() = supportedPortProfiles;

        return config;
      };

  VendorConfig fbossVendorOne, fbossVendorTwo;
  fbossVendorOne.vendorName() = "FBOSSONE";
  fbossVendorTwo.vendorName() = "FBOSSTWO";
  TransceiverAttributes partConfigOne = createTransceiverAttributes({
      cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC,
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL,
  });
  TransceiverAttributes partConfigTwo = createTransceiverAttributes({
      cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC,
  });
  TransceiverAttributes partConfigThree = createTransceiverAttributes({
      cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC,
  });
  fbossVendorOne.partNumberToTransceiverAttributes() = {
      {"SPTSMP3CLCK10", partConfigOne}, {"LUX426C4AD", partConfigTwo}};
  fbossVendorTwo.partNumberToTransceiverAttributes() = {
      {"TR-FC13H-HFZ", partConfigThree}};
  qsfpCfg.transceiverValidationConfig() = {fbossVendorOne, fbossVendorTwo};

  setupFakeQsfpConfig(qsfpCfgPath, qsfpCfg);

  // Set up fake qsfp_service volatile directory
  gflags::SetCommandLineOptionWithMode(
      "qsfp_service_volatile_dir",
      qsfpSvcVolatileDir.c_str(),
      gflags::SET_FLAGS_DEFAULT);

  gflags::SetCommandLineOptionWithMode(
      "qsfp_data_refresh_interval", "0", gflags::SET_FLAGS_DEFAULT);

  gflags::SetCommandLineOptionWithMode(
      "enable_tcvr_validation", "t", gflags::SET_FLAGS_DEFAULT);

  // Create a wedge manager
  transceiverManager_ =
      std::make_unique<MockWedgeManager>(numModules, 8 /* portsPerModule */);
  transceiverManager_->init();
  tcvrConfig_ = transceiverManager_->getTransceiverConfig();
}

void TransceiverManagerTestHelper::resetTransceiverManager() {
  transceiverManager_.reset();
  transceiverManager_ =
      std::make_unique<MockWedgeManager>(numModules, 8 /* portsPerModule */);
}
} // namespace facebook::fboss
