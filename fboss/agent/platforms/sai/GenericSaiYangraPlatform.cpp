/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/GenericSaiYangraPlatform.h"

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/hw/HwSwitchWarmBootHelper.h"
#include "fboss/agent/hw/sai/api/ArsApi.h"
#include "fboss/agent/hw/sai/api/MplsApi.h"
#include "fboss/agent/hw/sai/api/SystemPortApi.h"
#include "fboss/agent/hw/sai/api/VirtualRouterApi.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace facebook::fboss {

GenericSaiYangraPlatform::GenericSaiYangraPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping,
    folly::MacAddress localMac)
    : SaiPlatform(
          std::move(productInfo),
          std::move(platformMapping),
          localMac) {}

GenericSaiYangraPlatform::~GenericSaiYangraPlatform() = default;

void GenericSaiYangraPlatform::setupAsic(
    std::optional<int64_t> switchId,
    const cfg::SwitchInfo& switchInfo,
    std::optional<HwAsic::FabricNodeRole> fabricNodeRole) {
  CHECK(!fabricNodeRole.has_value());
  std::optional<cfg::SdkVersion> sdkVersion;
  auto agentConfig = config();
  if (agentConfig->thrift.sw()->sdkVersion().has_value()) {
    sdkVersion = agentConfig->thrift.sw()->sdkVersion().value();
  }
  asic_ = HwAsic::makeAsic(
      switchId.value_or(0), switchInfo, sdkVersion, std::nullopt);
  asic_->setDefaultStreamType(cfg::StreamType::UNICAST);
}

HwAsic* GenericSaiYangraPlatform::getAsic() const {
  return asic_.get();
}

const std::unordered_map<std::string, std::string>
GenericSaiYangraPlatform::getSaiProfileVendorExtensionValues() const {
  std::unordered_map<std::string, std::string> kv_map;

  // FBOSS does not set SAI_PORT_ATTR_AUTO_NEG_CONFIG_MODE on ports, and
  // the Chenab SAI default is to keep auto-negotiation ON. This KV sets
  // the default to OFF, which complies with the SAI spec (saiport.h) where
  // SAI_PORT_ATTR_AUTO_NEG_MODE defaults to false.
  kv_map.insert(std::make_pair("SAI_KEY_PORT_AUTONEG_DEFAULT_OFF", "1"));

  // Do not drop packets where source MAC equals destination MAC. Sets the
  // SX_TRAP_ID_DISCARD_ING_PACKET_SMAC_DMAC trap action to IGNORE instead
  // of DISCARD. Needed because FBOSS tests forwards such packets legitimately.
  kv_map.insert(std::make_pair("SAI_KEY_NOT_DROP_SMAC_DMAC_EQUAL", "1"));

  // Do not drop packets where source IP equals destination IP. Sets the
  // SX_TRAP_ID_DISCARD_ING_ROUTER_SIP_DIP trap action to IGNORE instead
  // of DISCARD. Required for L3 hairpin forwarding where a host sends a
  // packet with the same source and destination IP, and routing resolves
  // the egress back to the same ingress port.
  kv_map.insert(std::make_pair("SAI_KEY_NOT_DROP_SIP_DIP_EQUAL", "1"));

  // Skip the SX SDK vs kmod version check at SDK init. When unset, the SDK
  // fails create_switch on any SDK/kmod version mismatch (e.g. during a
  // rolling upgrade or when testing against a different kmod). FBOSS manages
  // the kmod lifecycle externally, so a mismatch should warn, not abort.
  // Replaces the former Meta SDK patch 0002 (skip_version_check = TRUE).
  kv_map.insert(std::make_pair("SAI_KEY_SDK_SKIP_VERSION_CHECK", "1"));

  // PG0 is the default priority group — traffic is mapped to it when no
  // specific priority group is configured. It is lossy/best-effort. The
  // SDK has an internal optimization that reclaims PG0 buffer space. Some
  // FBOSS tests create egress buffer pools but no ingress pools; in those
  // cases, rx packets would be dropped if the SDK reclaims PG0 buffers.
  // Disable this so tests work even without an ingress buffer pool.
  kv_map.insert(std::make_pair("SAI_KEY_RECLAIM_PG0_BUFFER_DISABLED", "1"));

  // Deliver trapped packets to the CPU via a callback function
  // (SAI_SWITCH_ATTR_PACKET_EVENT_NOTIFY) instead of through Linux netdev
  // or file descriptor interfaces. This is more efficient for a userspace
  // agent like FBOSS as packets go directly into the packet processing
  // pipeline. Callback mode is mutually exclusive with netdev/fd delivery,
  // so hostif table entries with WILDCARD or TRAP_ID match types are
  // rejected when this is enabled.
  kv_map.insert(std::make_pair("SAI_KEY_TRAP_PACKETS_USING_CALLBACK", "1"));

  // NOTE: Deprecated in favor of SAI_KEY_ROUTE_METADATA_BIT_START/BIT_END.
  kv_map.insert(std::make_pair("SAI_KEY_ROUTE_METADATA_FIELD_SIZE", "5"));

  // For packets sent from CPU with pipeline lookup, map the packet's DSCP
  // to a traffic class (TC) via the switch DSCP-to-TC QoS map, then set
  // the VLAN PCP accordingly. Ensures correct QoS treatment for
  // CPU-originated packets traversing the forwarding pipeline.
  kv_map.insert(
      std::make_pair("SAI_KEY_CPU_PORT_PIPELINE_LOOKUP_L3_TRUST_MODE", "1"));

  // Bitmask (0x7 = bits 0|1|2) excluding CPU ports, default bridge ports,
  // and default hash objects from sai_get_object_key() enumeration.
  // Reduces noise when querying SAI object keys.
  kv_map.insert(std::make_pair("SAI_KEY_GET_OBJECT_KEY_EXCLUDE", "7"));

  // FBOSS manages the sxdkernel driver lifecycle externally. SAI will skip
  // starting/stopping sxdkernel but the PCI communication channel between
  // the ASIC and CPU is still reset to clear any stale PCI state.
  kv_map.insert(std::make_pair("SAI_KEY_EXTERNAL_SXD_DRIVER_MANAGEMENT", "1"));

  // The ASIC requires a port to be admin DOWN before changing its internal
  // loopback mode. This KV makes it seamless for FBOSS tests — SAI will
  // automatically toggle the port admin DOWN, apply the loopback change,
  // then restore admin UP, so FBOSS can just set the loopback attribute
  // without manually managing the admin state.
  kv_map.insert(std::make_pair("SAI_INTERNAL_LOOPBACK_TOGGLE_ENABLED", "1"));

  // Enable SER (System Error Recovery) health events. The SDK health event
  // callback will report SAI_HEALTH_DATA_TYPE_SER with ECC single-bit and
  // double-bit error counts from ASIC memory.
  kv_map.insert(std::make_pair("SAI_KEY_ENABLE_HEALTH_DATA_TYPE_SER", "1"));

  // Configure SDK crash dump storage location and retain 1 dump file.
  utilCreateDir(getDirectoryUtil()->getCrashInfoDir());
  kv_map.insert(
      std::make_pair(
          "SAI_DUMP_STORE_PATH", getDirectoryUtil()->getCrashInfoDir()));
  kv_map.insert(std::make_pair("SAI_DUMP_STORE_AMOUNT", "1"));

  // Enable host interface v2. Only supported on SPC4+ (Chenab). Compared
  // to v1, this enables CPU egress buffer pool management, per-trap
  // priority get/set, direct trap-group-to-CPU-traffic-class mapping, and
  // debug counter integration with CPU traffic class reservation.
  kv_map.insert(std::make_pair("SAI_KEY_HOSTIF_V2_ENABLED", "1"));

  // When PRBS testing is requested on an admin-UP port, SAI will
  // automatically toggle the port admin DOWN, configure PRBS (with retry
  // logic), then restore admin state. Without this, PRBS config requires
  // the port to be manually brought admin DOWN first.
  kv_map.insert(std::make_pair("SAI_KEY_PRBS_ADMIN_TOGGLE_ENABLED", "1"));

  // Reserve bits 0-4 (5 bits) of the ASIC user-token register for route
  // metadata. This range is shared with ACL, tunnel, and FDB metadata.
  kv_map.insert(std::make_pair("SAI_KEY_ROUTE_METADATA_BIT_START", "0"));

  // Preserve PRBS configuration across In-Service Software Upgrade (ISSU).
  // Without this, PRBS state would be lost during SDK upgrade.
  kv_map.insert(std::make_pair("SAI_KEY_PRBS_OVER_ISSU_ENABLED", "1"));

  // For ACL rules that set the Adaptive Routing (AR) packet profile, add a
  // random hash action (SX_ACL_ACTION_HASH_COMMAND_RANDOM) for ECMP. This
  // randomly sprays AR-profiled packets across ECMP members instead of
  // using the standard hash-based selection.
  kv_map.insert(std::make_pair("SAI_KEY_AR_ECMP_RANDOM_SPRAY_ENABLED", "1"));

  // End of the 5-bit route metadata range (bits 0-4).
  kv_map.insert(std::make_pair("SAI_KEY_ROUTE_METADATA_BIT_END", "4"));

  // Include discarded packets in SAI_PORT_STAT_IF_IN_UCAST_PKTS counter.
  // By default, only non-discarded packets are counted. FBOSS needs the
  // aggregate count for accurate traffic visibility.
  kv_map.insert(std::make_pair("SAI_AGGREGATE_UCAST_DROPS", "1"));

  // PFC watchdog performs recovery immediately once recovery timer expires,
  // even if new PFC XOFF frames are still being received. Default behavior
  // would delay recovery until XOFF frames stop, which can cause prolonged
  // traffic stalls.
  kv_map.insert(
      std::make_pair(
          "SAI_KEY_PFC_WD_FORWARD_ACTION_BEHAVIOR",
          "PFC_WD_FORWARD_ACTION_IMMEDIATE_RECOVERY"));

#if SAI_API_VERSION >= SAI_VERSION(1, 17, 0) && defined(CHENAB_SAI_SDK)
  // Disable creation of Linux netdev interfaces for PORT and LAG type
  // host interfaces. FBOSS uses callback-based packet I/O and does not
  // need per-port netdevs. Also passes WITHOUT_SX_NETDEV=1 to the
  // sxdkernel start script.
  kv_map.insert(std::make_pair("SAI_WITHOUT_SX_NETDEV", "1"));
  // Enable PTP-based cable length estimation for link diagnostics.
  kv_map.insert(std::make_pair("SAI_KEY_PTP_CABLE_MEASUREMENT_ENABLE", "1"));
#endif

  // Independent module mode: transceiver programming is handled by an
  // external component (qsfp_service) while the ASIC firmware acts as a
  // relay. Value 1 = INDEPENDENT_MODULE_MODE.
  kv_map.insert(std::make_pair("SAI_INDEPENDENT_MODULE_MODE", "1"));

  // case #1050017
  // When port is transmitting traffic with TC2 and TC7, and PFC XOFF is
  // received for TC2, expectation is that TC7 traffic is not throttled.
  // This KV makes queues with a port parent use SUB_GROUP hierarchy instead
  // of TC hierarchy, isolating per-TC scheduling so PFC on one TC does not
  // throttle other TCs.
  kv_map.insert(std::make_pair("SAI_KEY_NO_HQOS_QUEUE_TO_SUBGROUP", "1"));

  // Disable the SDK sniffer; FBOSS does not consume sniffer logs and the
  // sniffer requires /usr/bin/sx_def_filter which is not staged in the
  // FBOSS image.
  kv_map.insert(std::make_pair("SAI_KEY_SDK_SNIFFER_DISABLE", "1"));

  return kv_map;
}

const std::set<sai_api_t>& GenericSaiYangraPlatform::getSupportedApiList()
    const {
  static auto apis = getDefaultSwitchAsicSupportedApis();
  apis.erase(facebook::fboss::MplsApi::ApiType);
  apis.erase(facebook::fboss::SystemPortApi::ApiType);
  return apis;
}

std::string GenericSaiYangraPlatform::getHwConfig() {
  std::string xml_filename =
      *config()->thrift.platform()->chip().value().asic()->config();
  std::string base_filename =
      xml_filename.substr(0, xml_filename.find(".xml") + 4);
  std::ifstream xml_file(base_filename);
  std::string xml_config(
      (std::istreambuf_iterator<char>(xml_file)),
      std::istreambuf_iterator<char>());
  // std::cout << "Read config from: " << xml_filename << std::endl;
  // std::cout << "Content:" << std::endl << xml_config << std::endl;
  if (xml_filename.find(";disable_lb_filter") != std::string::npos) {
    std::string keyTag = "</issu-enabled>";
    std::string newKeyTag = "<lb_filter_disable>1</lb_filter_disable>";
    int indentSpaces = 8;

    size_t pos = xml_config.find(keyTag);
    if (pos != std::string::npos) {
      xml_config.insert(
          pos + keyTag.length(), std::string(indentSpaces, ' ') + newKeyTag);
    } else {
      std::cerr << "Tag " << keyTag << " not found in XML." << std::endl;
    }
  }
  return xml_config;
}

bool GenericSaiYangraPlatform::isSerdesApiSupported() const {
  return true;
}

std::vector<PortID> GenericSaiYangraPlatform::getAllPortsInGroup(
    PortID /*portID*/) const {
  return {};
}

std::vector<FlexPortMode> GenericSaiYangraPlatform::getSupportedFlexPortModes()
    const {
  return {
      FlexPortMode::ONEX400G,
      FlexPortMode::ONEX100G,
      FlexPortMode::ONEX40G,
      FlexPortMode::FOURX25G,
      FlexPortMode::FOURX10G,
      FlexPortMode::TWOX50G};
}

std::optional<sai_port_interface_type_t>
GenericSaiYangraPlatform::getInterfaceType(
    TransmitterTechnology /*transmitterTech*/,
    cfg::PortSpeed /*speed*/) const {
  return std::nullopt;
}

bool GenericSaiYangraPlatform::supportInterfaceType() const {
  return false;
}

void GenericSaiYangraPlatform::initLEDs() {}

SaiSwitchTraits::CreateAttributes GenericSaiYangraPlatform::getSwitchAttributes(
    bool mandatoryOnly,
    cfg::SwitchType switchType,
    std::optional<int64_t> switchId,
    BootType bootType) {
  auto attributes = SaiPlatform::getSwitchAttributes(
      mandatoryOnly, switchType, switchId, bootType);
  std::get<std::optional<SaiSwitchTraits::Attributes::HwInfo>>(attributes) =
      std::nullopt;
  // disable timeout based discards and retain only buffer discards
  std::get<std::optional<SaiSwitchTraits::Attributes::DisableSllAndHllTimeout>>(
      attributes) = true;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_CHENAB2) {
    std::get<std::optional<SaiSwitchTraits::Attributes::PtpMode>>(attributes) =
        SAI_PORT_PTP_MODE_SINGLE_STEP_TIMESTAMP;
  }
#endif

  return attributes;
}

HwSwitchWarmBootHelper* GenericSaiYangraPlatform::getWarmBootHelper() {
  if (!wbHelper_) {
    wbHelper_ = std::make_unique<HwSwitchWarmBootHelper>(
        getAsic()->getSwitchIndex(),
        getDirectoryUtil()->getWarmBootDir(),
        "sai_adaptor_state_",
        false /* do not create warm boot data file */);
  }
  return wbHelper_.get();
}

} // namespace facebook::fboss
