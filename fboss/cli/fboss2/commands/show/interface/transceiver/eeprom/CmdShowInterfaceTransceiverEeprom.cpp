// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/transceiver/eeprom/CmdShowInterfaceTransceiverEeprom.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <fmt/format.h>
#include <folly/MapUtil.h>

namespace facebook::fboss {

std::string CmdShowInterfaceTransceiverEeprom::formatHexDump(
    const uint8_t* data,
    int offset,
    size_t length) {
  std::string result;
  for (size_t i = 0; i < length; i += 16) {
    result += fmt::format("  0x{:04X}:", offset + i);
    for (size_t j = 0; j < 16 && (i + j) < length; j++) {
      if (j == 8) {
        result += " ";
      }
      result += fmt::format(" {:02X}", data[i + j]);
    }
    result += "\n";
  }
  return result;
}

CmdShowInterfaceTransceiverEeprom::RetType
CmdShowInterfaceTransceiverEeprom::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedIfs,
    const ObjectArgType& args) {
  if (queriedIfs.size() != 1 || args.size() != 3) {
    return "Usage: show interface <port> transceiver eeprom <page> <offset> <length>\n"
           "  page:   EEPROM page number (hex 0x10 or decimal 16, -1 for lower page 0)\n"
           "  offset: byte offset within the page (0-255)\n"
           "  length: number of bytes to read (1-128)\n"
           "\nExamples:\n"
           "  show interface eth1/18/5 transceiver eeprom -1 0 128      # Lower page 0\n"
           "  show interface eth1/18/5 transceiver eeprom 0x10 128 128   # Page 0x10 upper\n"
           "  show interface eth1/18/5 transceiver eeprom 0x11 128 128   # Page 0x11 upper\n";
  }

  const auto& portName = queriedIfs[0];

  // Parse page, offset, length - support both hex (0x prefix) and decimal
  int page, offset, length;
  try {
    // std::stoi with base 0 auto-detects decimal and hex (0x prefix)
    page = std::stoi(args[0], nullptr, 0);
    offset = std::stoi(args[1], nullptr, 0);
    length = std::stoi(args[2], nullptr, 0);
  } catch (const std::exception& ex) {
    return fmt::format("Error: Invalid numeric argument: {}\n", ex.what());
  }

  if (length < 1 || length > 128) {
    return "Error: length must be between 1 and 128\n";
  }

  // Create thrift clients
  auto agent = utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  auto qsfpService =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  // 1. Get all port info to find port ID for the given port name
  std::map<int32_t, PortInfoThrift> portEntries;
  agent->sync_getAllPortInfo(portEntries);

  int32_t portId = -1;
  for (const auto& [id, info] : portEntries) {
    if (*info.name() == portName) {
      portId = id;
      break;
    }
  }
  if (portId < 0) {
    return fmt::format("Error: Port '{}' not found\n", portName);
  }

  // 2. Get port status to find transceiver ID
  std::map<int32_t, PortStatus> portStatusEntries;
  std::vector<int32_t> portIds = {portId};
  agent->sync_getPortStatus(portStatusEntries, portIds);

  auto* statusPtr = folly::get_ptr(portStatusEntries, portId);
  if (!statusPtr || !statusPtr->transceiverIdx().has_value()) {
    return fmt::format("Error: No transceiver found for port '{}'\n", portName);
  }
  int32_t transceiverId = *statusPtr->transceiverIdx()->transceiverId();

  // 3. Read the specified EEPROM region
  ReadRequest req;
  TransceiverIOParameters param;
  req.ids() = {transceiverId};
  param.offset() = offset;
  param.length() = length;
  if (page >= 0) {
    param.page() = page;
  }
  req.parameter() = param;

  std::map<int32_t, ReadResponse> resp;
  try {
    qsfpService->sync_readTransceiverRegister(resp, req);
  } catch (const std::exception& ex) {
    return fmt::format("Error reading EEPROM: {}\n", ex.what());
  }

  auto* respPtr = folly::get_ptr(resp, transceiverId);
  if (!respPtr) {
    return fmt::format(
        "Error: No response for transceiver {}\n", transceiverId);
  }

  // Format output
  std::string output;
  output += fmt::format("Port: {}\n", portName);
  output += fmt::format("Transceiver ID: {}\n", transceiverId);
  if (page >= 0) {
    output += fmt::format(
        "Page: 0x{:02X}  Offset: {}  Length: {}\n\n", page, offset, length);
  } else {
    output +=
        fmt::format("Page: lower  Offset: {}  Length: {}\n\n", offset, length);
  }
  output +=
      formatHexDump(respPtr->data()->data(), offset, respPtr->data()->length());

  return output;
}

void CmdShowInterfaceTransceiverEeprom::printOutput(
    const RetType& output,
    std::ostream& out) {
  out << output;
}

std::string_view CmdShowInterfaceTransceiverEepromTraits::description() {
  return "Reads and hex-dumps a contiguous byte range from the EEPROM of the transceiver on the given interface. Takes a page (hex like 0x10 or decimal; -1 for the lower page), a byte offset within the page (0-255), and a length (1-128). Use it to inspect specific optic registers during low-level debugging.";
}

CmdShowInterfaceTransceiverEeprom::RetType
CmdShowInterfaceTransceiverEeprom::sampleModel() {
  return R"(Port: eth1/1/1
Transceiver ID: 0
Page: lower  Offset: 0  Length: 128

  0x0000: 11 07 02 00 00 00 00 00  00 00 00 00 00 00 00 00
  0x0010: 00 00 00 00 00 00 2A B6  00 00 82 55 00 00 00 00
  0x0020: 00 00 21 7C 26 37 1D AF  1D 67 5C 21 5C 21 5C 21
  0x0030: 5C 21 1D 73 1C 02 21 80  23 C2 00 00 00 00 00 00
  0x0040: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
  0x0050: 00 00 00 00 00 00 00 AA  AA 00 00 00 00 01 00 00
  0x0060: 00 00 FF 00 00 00 00 00  00 00 00 00 00 0A 00 00
  0x0070: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 03
)";
}

// Explicit template instantiation
template void CmdHandler<
    CmdShowInterfaceTransceiverEeprom,
    CmdShowInterfaceTransceiverEepromTraits>::run();

} // namespace facebook::fboss
