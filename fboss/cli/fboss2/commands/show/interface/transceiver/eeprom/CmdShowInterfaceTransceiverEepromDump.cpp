// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/show/interface/transceiver/eeprom/CmdShowInterfaceTransceiverEepromDump.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <fmt/format.h>
#include <folly/MapUtil.h>

namespace facebook::fboss {

namespace {

// SFF-8024 transceiver identifiers
constexpr uint8_t kIdentifierQsfpDD = 0x18;
constexpr uint8_t kIdentifierOsfp = 0x19;
constexpr uint8_t kIdentifierQsfpPlusCmis = 0x1E;

bool isCmisModule(uint8_t identifier) {
  return identifier == kIdentifierQsfpDD || identifier == kIdentifierOsfp ||
      identifier == kIdentifierQsfpPlusCmis;
}

std::string identifierToString(uint8_t identifier) {
  switch (identifier) {
    case 0x03:
      return "SFP+";
    case 0x0C:
      return "QSFP";
    case 0x0D:
      return "QSFP+";
    case 0x11:
      return "QSFP28";
    case kIdentifierQsfpDD:
      return "QSFP-DD (CMIS)";
    case kIdentifierOsfp:
      return "OSFP (CMIS)";
    case kIdentifierQsfpPlusCmis:
      return "QSFP+ (CMIS)";
    default:
      return fmt::format("Unknown (0x{:02X})", identifier);
  }
}

} // namespace

std::vector<CmdShowInterfaceTransceiverEepromDump::PageReadConfig>
CmdShowInterfaceTransceiverEepromDump::getCmisPages() {
  return {
      {"Page 0x00 Lower (bytes 0-127) - Module state, faults, temp, voltage, LOS/LOL flags",
       -1,
       0,
       128},
      {"Page 0x00 Upper (bytes 128-255) - Cable identity (vendor, PN, SN, date code)",
       0x00,
       128,
       128},
      {"Page 0x02 (bytes 128-255) - Module-rated thresholds (temp, voltage, bias, power)",
       0x02,
       128,
       128},
      {"Page 0x04 (bytes 128-255) - Per-lane DataPath state machine",
       0x04,
       128,
       128},
      {"Page 0x10 (bytes 128-255) - Per-lane optical power and bias (Tx)",
       0x10,
       128,
       128},
      {"Page 0x11 (bytes 128-255) - Per-lane optical power and bias (Rx)",
       0x11,
       128,
       128},
      {"Page 0x13 (bytes 128-255) - Module-reported BER", 0x13, 128, 128},
      {"Page 0x14 (bytes 128-255) - Module-reported SNR", 0x14, 128, 128},
      {"Page 0x20 (bytes 128-255) - Vendor-specific", 0x20, 128, 128},
      {"Page 0x21 (bytes 128-255) - Vendor-specific", 0x21, 128, 128},
  };
}

std::vector<CmdShowInterfaceTransceiverEepromDump::PageReadConfig>
CmdShowInterfaceTransceiverEepromDump::getSff8636Pages() {
  return {
      {"Page 0x00 Lower (bytes 0-127)", -1, 0, 128},
      {"Page 0x00 Upper (bytes 128-255)", 0x00, 128, 128},
      {"Page 0x03 (bytes 128-255)", 0x03, 128, 128},
  };
}

std::string CmdShowInterfaceTransceiverEepromDump::formatHexDump(
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

CmdShowInterfaceTransceiverEepromDump::RetType
CmdShowInterfaceTransceiverEepromDump::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedIfs,
    const utils::Message& /* eepromArgs */) {
  if (queriedIfs.size() != 1) {
    return "Usage: show interface <port> transceiver eeprom dump\n";
  }

  const auto& portName = queriedIfs[0];

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

  // 3. Read identifier byte (byte 0 of lower page 0) to detect module type
  ReadRequest idReq;
  TransceiverIOParameters idParam;
  idReq.ids() = {transceiverId};
  idParam.offset() = 0;
  idParam.length() = 1;
  idReq.parameter() = idParam;

  std::map<int32_t, ReadResponse> idResp;
  qsfpService->sync_readTransceiverRegister(idResp, idReq);

  auto* idRespPtr = folly::get_ptr(idResp, transceiverId);
  if (!idRespPtr) {
    return fmt::format(
        "Error: Could not read identifier for transceiver {}\n", transceiverId);
  }

  if (idRespPtr->data()->length() < 1) {
    return fmt::format(
        "Error: Empty response reading identifier for transceiver {}\n",
        transceiverId);
  }

  uint8_t identifier = idRespPtr->data()->data()[0];
  bool cmis = isCmisModule(identifier);
  auto pages = cmis ? getCmisPages() : getSff8636Pages();

  std::string output;
  output += fmt::format("Port: {}\n", portName);
  output += fmt::format("Transceiver ID: {}\n", transceiverId);
  output += fmt::format(
      "Module Type: {} (identifier: 0x{:02X})\n\n",
      identifierToString(identifier),
      identifier);

  for (const auto& page : pages) {
    ReadRequest req;
    TransceiverIOParameters param;
    req.ids() = {transceiverId};
    param.offset() = page.offset;
    param.length() = page.length;
    if (page.page >= 0) {
      param.page() = page.page;
    }
    req.parameter() = param;

    std::map<int32_t, ReadResponse> resp;
    try {
      qsfpService->sync_readTransceiverRegister(resp, req);
    } catch (const std::exception& ex) {
      output += fmt::format("\n--- {} ---\n", page.name);
      output += fmt::format("  Error: {}\n", ex.what());
      continue;
    }

    auto* respPtr = folly::get_ptr(resp, transceiverId);
    if (!respPtr) {
      output += fmt::format("\n--- {} ---\n", page.name);
      output += "  Error: No response for transceiver\n";
      continue;
    }

    output += fmt::format("\n--- {} ---\n", page.name);
    output += formatHexDump(
        respPtr->data()->data(), page.offset, respPtr->data()->length());
  }

  return output;
}

void CmdShowInterfaceTransceiverEepromDump::printOutput(
    const RetType& output,
    std::ostream& out) {
  out << output;
}

// Explicit template instantiation
template void CmdHandler<
    CmdShowInterfaceTransceiverEepromDump,
    CmdShowInterfaceTransceiverEepromDumpTraits>::run();

} // namespace facebook::fboss
