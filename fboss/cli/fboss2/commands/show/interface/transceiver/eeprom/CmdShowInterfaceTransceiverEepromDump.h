// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/eeprom/CmdShowInterfaceTransceiverEeprom.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

#include <cstdint>
#include <string>
#include <vector>

namespace facebook::fboss {

struct CmdShowInterfaceTransceiverEepromDumpTraits : public ReadCommandTraits,
                                                     public CliDocsExempt {
  using ParentCmd = CmdShowInterfaceTransceiverEeprom;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::string;
};

class CmdShowInterfaceTransceiverEepromDump
    : public CmdHandler<
          CmdShowInterfaceTransceiverEepromDump,
          CmdShowInterfaceTransceiverEepromDumpTraits> {
 public:
  using ObjectArgType =
      CmdShowInterfaceTransceiverEepromDumpTraits::ObjectArgType;
  using RetType = CmdShowInterfaceTransceiverEepromDumpTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::PortList& queriedIfs,
      const utils::Message& eepromArgs);

  void printOutput(const RetType& output, std::ostream& out = std::cout);

 private:
  struct PageReadConfig {
    std::string name;
    int page; // -1 means don't set page
    int offset;
    int length;
  };

  static std::vector<PageReadConfig> getCmisPages();
  static std::vector<PageReadConfig> getSff8636Pages();
  static std::string
  formatHexDump(const uint8_t* data, int offset, size_t length);
};

} // namespace facebook::fboss
