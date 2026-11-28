// Copyright 2004-present Facebook. All Rights Reserved.
#include "fboss/agent/lldp/LinkNeighbor.h"

#include <folly/Conv.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/String.h>
#include <folly/io/Cursor.h>
#include <folly/logging/xlog.h>
#include <glog/logging.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/lldp/gen-cpp2/lldp_constants.h"

using folly::ByteRange;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::StringPiece;
using folly::io::Cursor;
using std::string;

static uint16_t ETHERTYPE_LLDP = 0x88cc;

namespace facebook::fboss {

enum class LinkNeighbor::LldpTlvType : uint8_t {
  END = 0,
  CHASSIS = 1,
  PORT = 2,
  TTL = 3,
  PORT_DESC = 4,
  SYSTEM_NAME = 5,
  SYSTEM_DESC = 6,
  SYSTEM_CAPS = 7,
  MGMT_ADDR = 8,
  CUSTOM = 127,
};

enum class LinkNeighbor::CdpTlvType : uint16_t {
  DEVICE_ID = 1,
  ADDRESSES = 2,
  PORT_ID = 3,
  CAPABILITIES = 4,
  SOFTWARE_VERSION = 5,
  PLATFORM = 6,
  IP_NETWORK_PREFIX = 7,
  VTP_MGMT_DOMAIN = 9,
  NATIVE_VLAN = 10,
  DUPLEX = 11,
  TRUST_BITMAP = 18,
  UNTRUSTED_PORT = 19,
  SYSTEM_NAME = 20,
  MGMT_ADDRESSES = 22,
  POWER_AVAILABLE = 26,
  SPARE_PAIR_POE = 31,
};

LinkNeighbor::LinkNeighbor() {}

std::string LinkNeighbor::humanReadableChassisId() const {
  auto chassisId = get<lldp_tags::chassisId>()->ref();
  if (get<lldp_tags::chassisIdType>() == lldp::LldpChassisIdType::MAC_ADDRESS) {
    return humanReadableMac(chassisId);
  } else if (
      get<lldp_tags::chassisIdType>() == lldp::LldpChassisIdType::NET_ADDRESS) {
    return humanReadableNetAddr(chassisId);
  }
  return folly::humanify(chassisId);
}

std::string LinkNeighbor::humanReadablePortId() const {
  auto portId = get<lldp_tags::portId>()->ref();
  if (get<lldp_tags::portIdType>() == lldp::LldpPortIdType::MAC_ADDRESS) {
    return humanReadableMac(portId);
  } else if (
      get<lldp_tags::portIdType>() == lldp::LldpPortIdType::NET_ADDRESS) {
    return humanReadableNetAddr(portId);
  }
  return folly::humanify(portId);
}

std::string LinkNeighbor::humanReadableMac(const std::string& data) {
  try {
    ByteRange buf{StringPiece(data)};
    return MacAddress::fromBinary(buf).toString();
  } catch (...) {
    return folly::humanify(data);
  }
}

std::string LinkNeighbor::humanReadableNetAddr(const std::string& data) {
  enum IanaAddressFamily {
    IANA_IPV4 = 1,
    IANA_IPV6 = 2,
  };

  // First byte is the IANA address familiy number.
  try {
    if (data.size() < 1) {
      return folly::humanify(data);
    }
    ByteRange address{StringPiece(data).subpiece(1)};
    if (data[0] == IANA_IPV4) {
      return IPAddressV4::fromBinary(address).str();
    } else if (data[0] == IANA_IPV6) {
      return IPAddressV6::fromBinary(address).str();
    }
  } catch (...) {
    // fall through
  }
  return folly::humanify(data);
}

bool LinkNeighbor::parseLldpPdu(
    PortID srcPort,
    std::optional<VlanID> vlanID,
    folly::MacAddress srcMac,
    uint16_t ethertype,
    folly::io::Cursor* cursor) {
  if (ethertype != ETHERTYPE_LLDP) {
    return false;
  }

  ref<lldp_tags::protocol>() = lldp::LinkProtocol::LLDP;
  ref<lldp_tags::localPort>() = srcPort;
  // TODO(skhare) make localVlan optional
  ref<lldp_tags::localVlan>() = vlanID.has_value() ? vlanID.value() : 0;
  ref<lldp_tags::srcMac>() = srcMac.u64NBO();

  bool chassisIdPresent{false};
  bool portIdPresent{false};
  bool ttlPresent{false};
  try {
    while (true) {
      auto type = parseLldpTlv(cursor);
      if (type == LldpTlvType::END) {
        break;
      } else if (type == LldpTlvType::CHASSIS) {
        chassisIdPresent = true;
      } else if (type == LldpTlvType::PORT) {
        portIdPresent = true;
      } else if (type == LldpTlvType::TTL) {
        ttlPresent = true;
      }
    }
  } catch (const std::exception& ex) {
    // This generally happens if the packet is malformatted and specifies
    // a TLV length longer than the packet itself.
    XLOG(DBG1) << "error parsing LLDP packet on port " << srcPort << " from "
               << srcMac << ": " << folly::exceptionStr(ex);
    return false;
  }

  if (!chassisIdPresent) {
    XLOG(DBG1) << "received bad LLDP packet on port " << srcPort << " from "
               << srcMac << ": missing chassis ID";
    return false;
  }
  if (!portIdPresent) {
    XLOG(DBG1) << "received bad LLDP packet on port " << srcPort << " from "
               << srcMac << ": missing port ID";
    return false;
  }
  if (!ttlPresent) {
    XLOG(DBG1) << "received bad LLDP packet on port " << srcPort << " from "
               << srcMac << ": missing TTL";
    return false;
  }

  return true;
}

LinkNeighbor::LldpTlvType LinkNeighbor::parseLldpTlv(Cursor* cursor) {
  // Parse the type (7 bits) and length (9 bits)
  uint16_t length = cursor->readBE<uint16_t>();
  auto type = LldpTlvType(length >> 9);
  length &= 0x01ff;

  switch (type) {
    case LldpTlvType::END:
      break;
    case LldpTlvType::CHASSIS:
      parseLldpChassis(cursor, length);
      break;
    case LldpTlvType::PORT:
      parseLldpPortId(cursor, length);
      break;
    case LldpTlvType::TTL:
      parseLldpTtl(cursor, length);
      break;
    case LldpTlvType::PORT_DESC:
      ref<lldp_tags::portDescription>() = cursor->readFixedString(length);
      break;
    case LldpTlvType::SYSTEM_NAME:
      ref<lldp_tags::systemName>() = cursor->readFixedString(length);
      break;
    case LldpTlvType::SYSTEM_DESC:
      ref<lldp_tags::systemDescription>() = cursor->readFixedString(length);
      break;
    case LldpTlvType::SYSTEM_CAPS:
      parseLldpSystemCaps(cursor, length);
      break;
    case LldpTlvType::CUSTOM:
      parseLldpOrgSpecific(cursor, length);
      break;
    default:
      // We ignore any other TLV types for now, including
      // LldpTlvType::MGMT_ADDR.
      cursor->skip(length);
      break;
  }
  return type;
}

void LinkNeighbor::parseLldpChassis(Cursor* cursor, uint16_t length) {
  if (length < 1) {
    throw std::out_of_range("LLDP chassis length must be at least 1");
  }

  ref<lldp_tags::chassisIdType>() =
      static_cast<lldp::LldpChassisIdType>(cursor->read<uint8_t>());
  ref<lldp_tags::chassisId>() = cursor->readFixedString(length - 1);
}

void LinkNeighbor::parseLldpPortId(Cursor* cursor, uint16_t length) {
  if (length < 1) {
    throw std::out_of_range("LLDP port length must be at least 1");
  }

  ref<lldp_tags::portIdType>() =
      static_cast<lldp::LldpPortIdType>(cursor->read<uint8_t>());
  ref<lldp_tags::portId>() = cursor->readFixedString(length - 1);
}

void LinkNeighbor::parseLldpTtl(Cursor* cursor, uint16_t length) {
  if (length != 2) {
    throw std::out_of_range("LLDP TTL length must be 2");
  }

  auto ttl = cursor->readBE<uint16_t>();
  setTTL(std::chrono::seconds(ttl));
}

void LinkNeighbor::parseLldpSystemCaps(Cursor* cursor, uint16_t length) {
  if (length != 4) {
    throw std::out_of_range("LLDP capabilities length must be 4");
  }

  ref<lldp_tags::capabilities>() = cursor->readBE<uint16_t>();
  ref<lldp_tags::enabledCapabilities>() = cursor->readBE<uint16_t>();
}

bool LinkNeighbor::parseCdpPdu(
    PortID srcPort,
    std::optional<VlanID> vlanID,
    MacAddress srcMac,
    uint16_t ethertype,
    folly::io::Cursor* cursor) {
  if (ethertype >= 0x600) {
    // This looks like an ethertype field rather than a frame length.
    // We expect CDP packets to be Ethernet v2 frames rather than 802.3.
    return false;
  }

  ref<lldp_tags::protocol>() = lldp::LinkProtocol::CDP;
  ref<lldp_tags::localPort>() = srcPort;
  // TODO(skhare) make localVlan optional
  ref<lldp_tags::localVlan>() = vlanID.has_value() ? vlanID.value() : 0;
  ref<lldp_tags::srcMac>() = srcMac.u64NBO();

  try {
    auto dsap = cursor->read<uint8_t>();
    auto ssap = cursor->read<uint8_t>();

    static const uint8_t SAP_SNAP = 0xaa;
    if (dsap != SAP_SNAP || ssap != SAP_SNAP) {
      return false;
    }

    // Skip over:
    // 1 byte LLC control field
    // 3 byte SNAP vendor code
    // 2 byte SNAP local code
    cursor->skip(6);

    auto cdpVersion = cursor->read<uint8_t>();
    if (cdpVersion != 2) {
      // We only understand CDP version 2 for now
      return false;
    }

    auto ttl = cursor->read<uint8_t>();
    setTTL(std::chrono::seconds(ttl));

    auto checksum = cursor->readBE<uint16_t>();
    // TODO: We don't actually verify the checksum right now.
    (void)checksum;

    // Parse the payload data.
    if (!parseCdpPayload(srcPort, srcMac, cursor)) {
      return false;
    }

    return true;
  } catch (const std::exception& ex) {
    // This generally happens if the packet is malformatted and specifies a TLV
    // length longer than the packet itself.
    XLOG(DBG1) << "error parsing CDP packet on port " << srcPort << " from "
               << srcMac << ": " << folly::exceptionStr(ex);
    return false;
  }
}

bool LinkNeighbor::parseCdpPayload(
    PortID srcPort,
    MacAddress srcMac,
    Cursor* cursor) {
  bool chassisIdPresent{false};
  bool portIdPresent{false};
  while (!cursor->isAtEnd()) {
    auto type = CdpTlvType(cursor->readBE<uint16_t>());
    auto length = cursor->readBE<uint16_t>();

    if (length < 4) {
      XLOG(DBG1) << "received bad CDP packet on port " << srcPort << " from "
                 << srcMac << " too-short TLV: length=" << length
                 << ", type=" << static_cast<int>(type);
      return false;
    }
    length -= 4;

    switch (type) {
      case CdpTlvType::DEVICE_ID:
        ref<lldp_tags::chassisId>() = cursor->readFixedString(length);
        chassisIdPresent = true;
        break;
      case CdpTlvType::PORT_ID:
        ref<lldp_tags::portId>() = cursor->readFixedString(length);
        portIdPresent = true;
        break;
      case CdpTlvType::SYSTEM_NAME:
        ref<lldp_tags::systemName>() = cursor->readFixedString(length);
        break;
      default:
        cursor->skip(length);
        break;
    }
  }

  if (!chassisIdPresent) {
    XLOG(DBG1) << "received bad CDP packet on port " << srcPort << " from "
               << srcMac << ": missing device ID";
    return false;
  }
  if (!portIdPresent) {
    XLOG(DBG1) << "received bad CDP packet on port " << srcPort << " from "
               << srcMac << ": missing port ID";
    return false;
  }

  return true;
}

void LinkNeighbor::parseLldpOrgSpecific(Cursor* cursor, uint16_t length) {
  // Organizationally-specific TLV must have at least OUI (3 bytes) + subtype (1
  // byte)
  if (length < 4) {
    cursor->skip(length);
    return;
  }

  // Read OUI (3 bytes)
  uint8_t oui1 = cursor->read<uint8_t>();
  uint8_t oui2 = cursor->read<uint8_t>();
  uint8_t oui3 = cursor->read<uint8_t>();

  // Check if this is a Facebook OUI (0x48:57:DD)
  if (oui1 == lldp::lldp_constants::FACEBOOK_OUI_BYTE1_ &&
      oui2 == lldp::lldp_constants::FACEBOOK_OUI_BYTE2_ &&
      oui3 == lldp::lldp_constants::FACEBOOK_OUI_BYTE3_) {
    // Read subtype (1 byte)
    uint8_t subtype = cursor->read<uint8_t>();

    // Parse based on Facebook-specific subtype
    switch (subtype) {
      case static_cast<uint8_t>(FacebookLldpSubtype::PORT_DRAIN_STATE):
        // Only parse port drain state TLV if the flag is enabled
        if (FLAGS_lldp_port_drain_state) {
          if (length == PORT_DRAIN_STATE_TLV_LENGTH) {
            setPortDrainState(cursor->read<uint8_t>() != 0);
          } else {
            // Invalid length for this subtype, skip remaining bytes
            cursor->skip(length - 4);
          }
        } else {
          // Flag is disabled, skip the TLV value
          cursor->skip(length - 4);
        }
        break;
      default:
        // Unknown Facebook subtype, skip remaining bytes
        cursor->skip(length - 4);
        break;
    }
  } else {
    // Not a Facebook OUI, skip remaining bytes
    cursor->skip(length - 3);
  }
}

} // namespace facebook::fboss
