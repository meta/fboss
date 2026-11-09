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

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <fboss/cli/fboss2/commands/show/transceiver/gen-cpp2/model_types.h>
#include <string_view>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/Table.h"

#include <cstdint>

namespace facebook::fboss {

using utils::Table;

struct CmdShowTransceiverTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = cli::ShowTransceiverModel;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowTransceiver
    : public CmdHandler<CmdShowTransceiver, CmdShowTransceiverTraits> {
 public:
  using ObjectArgType = CmdShowTransceiver::ObjectArgType;
  using RetType = CmdShowTransceiver::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& queriedPorts);

  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();

 private:
  // These power thresholds are ported directly from fb_toolkit.  Eventually
  // they will be codified into a DNTT thrift service and we will be able to
  // query threshold specs instead of hardcoding.
  const double LOW_CURRENT_ERR = 0.00;
  const double LOW_CURRENT_WARN = 6.00;
  const double LOW_POWER_ERR = -9.00;
  const double LOW_POWER_WARN = -7.00;
  const double LOW_SNR_ERR = 19.00;
  const double LOW_SNR_WARN = 20.00;
  // ZR media_snr_min_db from configerator
  // neteng/ngt/link/link_parameter_thresholds
  const double ZR_LOW_SNR_ERR = 12.00;
  const double ZR_LOW_SNR_WARN = 13.00;

  std::map<int, PortInfoThrift> queryPortInfo(
      apache::thrift::Client<FbossCtrl>* agent) const;

  Table::StyledCell statusToString(std::optional<bool> isUp) const;

  Table::StyledCell listToString(
      std::vector<double> listToPrint,
      double lowWarningThreshold,
      double lowErrorThreshold);

  Table::StyledCell coloredSensorValue(std::string value, FlagLevels flags);

  std::map<int, PortStatus> queryPortStatus(
      apache::thrift::Client<FbossCtrl>* agent,
      std::map<int, PortInfoThrift> portEntries) const;

  std::map<int, TransceiverInfo> queryTransceiverInfo(
      apache::thrift::Client<QsfpService>* qsfpService) const;

  std::map<int32_t, std::string> queryTransceiverValidationInfo(
      apache::thrift::Client<QsfpService>* qsfpService,
      std::map<int, PortStatus> portStatusEntries) const;

  const std::pair<std::string, std::string> getTransceiverValidationStrings(
      std::map<int32_t, std::string>& transceiverEntries,
      int32_t transceiverId) const;

  RetType createModel(
      const std::set<std::string>& queriedPorts,
      std::map<int, PortStatus> portStatusEntries,
      std::map<int, TransceiverInfo> transceiverEntries,
      std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries,
      std::map<int32_t, std::string> transceiverValidationEntries) const;
};

} // namespace facebook::fboss
