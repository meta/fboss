/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook::fboss::utility {

class HwTransceiverUtils {
 public:
  static void verifyTransceiverSettings(
      const TcvrState& tcvrState,
      const std::string& portName,
      cfg::PortProfileID profile);

  // T114627923 Because some old firmware might not enable all capabilities
  // use `skipCheckingIndividualCapability` to skip checking individual
  // capability until we can make sure all modules have the latest firmware
  static void verifyDiagsCapability(
      const TcvrState& tcvrState,
      std::optional<DiagsCapability> diagsCapability,
      bool skipCheckingIndividualCapability = true);

  static void verifyPortNameToLaneMap(
      const std::vector<PortID>& portIDs,
      cfg::PortProfileID profile,
      const PlatformMapping* platformMapping,
      std::map<int32_t, TransceiverInfo>& tcvrInfo);

  static void verifyTempAndVccFlags(
      std::map<std::string, TransceiverInfo>& portToTransceiverInfoMap);

  static void verifyTcvrErrorStates(
      std::map<std::string, TransceiverInfo>& portToTransceiverInfoMap);

  static void verifyDatapathResetTimestamp(
      const std::string& portName,
      const TcvrState& tcvrState,
      const TcvrStats& tcvrStats,
      time_t timeReference,
      bool expectedReset);

  // Active optical cable: reports ACTIVE_CABLES media type encoding like an
  // AEC, but an optical transmitter. Its media interface union carries
  // activeCuCode, not smfCode.
  static bool isAoc(const TcvrState& tcvrState);

 private:
  static void verifyActiveOpticalCableProfile(
      TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces,
      cfg::PortProfileID profile);
  static void verifyOpticsSettings(
      const TcvrState& tcvrState,
      const std::string& portName,
      cfg::PortProfileID profile);
  static void verifyMediaInterfaceCompliance(
      const TcvrState& tcvrState,
      cfg::PortProfileID profile,
      const std::string& portName);
  static void verify10gProfile(
      const TcvrState& tcvrState,
      const TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verify25gProfile(
      const TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verify50gProfile(
      const TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verify100gProfile(
      const TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verify200gProfile(
      const TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verify400gProfile(
      const TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verifyCopper100gProfile(
      const TcvrState& tcvrState,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verifyCopper200gProfile(
      const TcvrState& tcvrState,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verifyCopper53gProfile(
      const TcvrState& tcvrState,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verifyCopper400gProfile(
      const TcvrState& tcvrState,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verifyOpticalProfileForSpeed(
      const TransceiverManagementInterface mgmtInterface,
      const std::vector<MediaInterfaceId>& mediaInterfaces,
      cfg::PortSpeed speed);
  static void verifyCopper800gProfile(
      const TcvrState& tcvrState,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verifyCopper1600gProfile(
      const TcvrState& tcvrState,
      const std::vector<MediaInterfaceId>& mediaInterfaces);
  static void verifyDataPathEnabled(
      const TcvrState& tcvrState,
      const std::string& portName);
};

} // namespace facebook::fboss::utility
