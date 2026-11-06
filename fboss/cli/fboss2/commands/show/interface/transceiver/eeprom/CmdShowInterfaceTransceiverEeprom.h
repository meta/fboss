// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/CmdShowInterfaceTransceiver.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace facebook::fboss {

struct CmdShowInterfaceTransceiverEepromTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowInterfaceTransceiver;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;
  using ObjectArgType = utils::Message; // page, offset, length
  using RetType = std::string;

  static std::string_view description();
};

class CmdShowInterfaceTransceiverEeprom
    : public CmdHandler<
          CmdShowInterfaceTransceiverEeprom,
          CmdShowInterfaceTransceiverEepromTraits> {
 public:
  using ObjectArgType = CmdShowInterfaceTransceiverEepromTraits::ObjectArgType;
  using RetType = CmdShowInterfaceTransceiverEepromTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::PortList& queriedIfs,
      const ObjectArgType& args);

  void printOutput(const RetType& output, std::ostream& out = std::cout);

  static RetType sampleModel();

 private:
  static std::string
  formatHexDump(const uint8_t* data, int offset, size_t length);
};

} // namespace facebook::fboss
