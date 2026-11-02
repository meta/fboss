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

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/switch_asics/Chenab2Asic.h"
#include "fboss/agent/hw/switch_asics/ChenabAsic.h"
#include "fboss/agent/hw/switch_asics/EbroAsic.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/switch_asics/Jericho2Asic.h"
#include "fboss/agent/hw/switch_asics/Jericho3Asic.h"
#include "fboss/agent/hw/switch_asics/Ramon3Asic.h"
#include "fboss/agent/hw/switch_asics/RamonAsic.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/types.h"
#include "fboss/lib/config/agent/InterfaceConfigUtils.h"
#include "fboss/lib/config/agent/PortConfigUtils.h"
#include "fboss/lib/config/agent/VlanConfigUtils.h"

#include <folly/MacAddress.h>
#include <vector>

DECLARE_bool(nodeZ);
DECLARE_bool(hyper_port);

namespace facebook::fboss {
class PortMap;
class MultiSwitchPortMap;
class SwSwitch;
class TestEnsembleIf;
} // namespace facebook::fboss

namespace facebook::fboss::utility {

const std::map<cfg::PortType, cfg::PortLoopbackMode>& kDefaultLoopbackMap();

folly::MacAddress kLocalCpuMac();

folly::MacAddress kLocalCpuMac();

std::string getLocalCpuMacStr();

bool isEnabledPortWithSubnet(
    const cfg::Port& port,
    const cfg::SwitchConfig& config);

std::vector<std::string> getLoopbackIps(SwitchID switchId);

/*
 * Correct Tomahawk6 connection handles that were written for a different COM-E
 * variant. Keeps the configured assignment when every configured handle is
 * already on the bus, so a config whose switch id order does not follow BDF
 * order is left alone. Throws when nothing was discovered, or when the count
 * disagrees with the config and the addresses therefore cannot be mapped to
 * switch ids. Takes the addresses as an argument so it is testable off-box.
 */
void applyTomahawk6PciAddrs(
    const std::vector<std::string>& pciAddrs,
    std::map<SwitchID, cfg::SwitchInfo>& switchIdToSwitchInfo);

/*
 * applyTomahawk6PciAddrs() against the addresses on this box's PCI bus.
 */
void applyTomahawk6PciAddrsFromBus(
    std::map<SwitchID, cfg::SwitchInfo>& switchIdToSwitchInfo);

int getMaxRdsw(std::optional<PlatformType> platformType = std::nullopt);
int getMaxEdsw();

cfg::DsfNode dsfNodeConfig(
    const HwAsic& firstAsic,
    int64_t otherSwitchId = 4,
    const std::optional<PlatformType> platformType = std::nullopt,
    const std::optional<int> clusterId = std::nullopt,
    const std::string& switchNamePrefix = "hwTestSwitch");

std::vector<cfg::Port>::iterator findCfgPort(
    cfg::SwitchConfig& cfg,
    PortID portID);
std::vector<cfg::Port>::iterator findCfgPortIf(
    cfg::SwitchConfig& cfg,
    PortID portID);

std::unordered_map<PortID, cfg::PortProfileID> getSafeProfileIDs(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::map<PortID, std::vector<PortID>>&
        controllingPortToSubsidaryPorts,
    bool supportsAddRemovePort,
    std::optional<std::vector<PortID>> masterLogicalPortIds = std::nullopt);

cfg::SwitchConfig onePortPerInterfaceConfigImpl(
    const SwSwitch* swSwitch,
    const std::vector<PortID>& ports,
    bool interfaceHasSubnet,
    bool setInterfaceMac,
    int baseIntfId,
    bool enableFabricPorts,
    cfg::InterfaceType intfType);

cfg::SwitchConfig onePortPerInterfaceConfigImpl(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::vector<PortID>& ports,
    bool supportsAddRemovePort,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap,
    bool interfaceHasSubnet,
    bool setInterfaceMac,
    int baseIntfId,
    bool enableFabricPorts,
    const std::optional<std::map<SwitchID, cfg::SwitchInfo>>&
        switchIdToSwitchInfo,
    const std::optional<std::map<SwitchID, const HwAsic*>>& hwAsicTable,
    const std::optional<PlatformType> platformType,
    cfg::InterfaceType intfType);

cfg::SwitchConfig onePortPerInterfaceConfigImpl(
    const TestEnsembleIf* ensemble,
    const std::vector<PortID>& ports,
    bool interfaceHasSubnet,
    bool setInterfaceMac,
    int baseIntfId,
    bool enableFabricPorts,
    cfg::InterfaceType intfType);

cfg::SwitchConfig onePortPerInterfaceConfig(
    const SwSwitch* swSwitch,
    const std::vector<PortID>& ports,
    bool interfaceHasSubnet = true,
    bool setInterfaceMac = true,
    int baseIntfId = kBaseVlanId,
    bool enableFabricPorts = false,
    const std::optional<cfg::InterfaceType>& intfType = std::nullopt);

cfg::SwitchConfig onePortPerInterfaceConfig(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::vector<PortID>& ports,
    bool supportsAddRemovePort,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap,
    bool interfaceHasSubnet = true,
    bool setInterfaceMac = true,
    int baseIntfId = kBaseVlanId,
    bool enableFabricPorts = false,
    const std::optional<std::map<SwitchID, cfg::SwitchInfo>>&
        switchIdToSwitchInfo = std::nullopt,
    const std::optional<std::map<SwitchID, const HwAsic*>>& hwAsicTable =
        std::nullopt,
    const std::optional<PlatformType> platformType = std::nullopt,
    const std::optional<cfg::InterfaceType>& intfType = std::nullopt);

// Simplified overload with platformType in a convenient position
cfg::SwitchConfig onePortPerInterfaceConfig(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::vector<PortID>& ports,
    bool supportsAddRemovePort,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap,
    PlatformType platformType);

cfg::SwitchConfig onePortPerInterfaceConfig(
    const TestEnsembleIf* ensemble,
    const std::vector<PortID>& ports,
    bool interfaceHasSubnet = true,
    bool setInterfaceMac = true,
    int baseIntfId = kBaseVlanId,
    bool enableFabricPorts = false,
    const std::optional<cfg::InterfaceType>& intfType = std::nullopt);

// Describes one aggregate port to build an L3 interface over. How the
// aggregate's L3 interface is bound (PORT binds it to the aggregate port
// itself, VLAN to a vlan the members share) is a property of the ASIC, passed
// once per config via intfType below, not per aggregate.
struct AggregatePortInfo {
  AggregatePortID id;
  std::vector<PortID> memberPorts;
  cfg::LacpPortRate rate{cfg::LacpPortRate::FAST};
  double minLinkPercentage{1.0};
};

// One L3 interface per aggregate port, plus one per port that is not an
// aggregate member. The aggregate port analogue of onePortPerInterfaceConfig.
cfg::SwitchConfig oneAggregatePortPerInterfaceConfig(
    const SwSwitch* swSwitch,
    const std::vector<PortID>& ports,
    const std::vector<AggregatePortInfo>& aggregatePorts,
    bool interfaceHasSubnet = true,
    bool setInterfaceMac = true,
    int baseIntfId = kBaseVlanId,
    bool enableFabricPorts = false);

cfg::SwitchConfig oneAggregatePortPerInterfaceConfig(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::vector<PortID>& ports,
    bool supportsAddRemovePort,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap,
    const std::vector<AggregatePortInfo>& aggregatePorts,
    cfg::InterfaceType intfType,
    bool interfaceHasSubnet = true,
    bool setInterfaceMac = true,
    int baseIntfId = kBaseVlanId,
    bool enableFabricPorts = false,
    const std::optional<std::map<SwitchID, cfg::SwitchInfo>>&
        switchIdToSwitchInfo = std::nullopt,
    const std::optional<std::map<SwitchID, const HwAsic*>>& hwAsicTable =
        std::nullopt,
    const std::optional<PlatformType> platformType = std::nullopt);

cfg::SwitchConfig
oneL3IntfTwoPortConfig(const SwSwitch* sw, PortID port1, PortID port2);

cfg::SwitchConfig oneL3IntfTwoPortConfig(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    PortID port1,
    PortID port2,
    bool supportsAddRemovePort,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap =
        kDefaultLoopbackMap(),
    const std::optional<PlatformType> platformType = std::nullopt);

cfg::SwitchConfig oneL3IntfNPortConfig(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::vector<PortID>& ports,
    bool supportsAddRemovePort,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap =
        kDefaultLoopbackMap(),
    bool interfaceHasSubnet = true,
    int baseVlanId = kBaseVlanId,
    bool optimizePortProfile = true,
    bool setInterfaceMac = true,
    const std::optional<PlatformType> platformType = std::nullopt);

cfg::SwitchConfig multiplePortsPerIntfConfig(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::vector<PortID>& ports,
    bool supportsAddRemovePort,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap =
        kDefaultLoopbackMap(),
    bool interfaceHasSubnet = true,
    bool setInterfaceMac = true,
    const int baseVlanId = kBaseVlanId,
    const int portsPerVlan = 1,
    bool enableFabricPorts = false,
    const std::optional<std::map<SwitchID, cfg::SwitchInfo>>&
        switchIdToSwitchInfo = std::nullopt,
    const std::optional<std::map<SwitchID, const HwAsic*>>& hwAsicTable =
        std::nullopt,
    const std::optional<PlatformType> platformType = std::nullopt,
    cfg::InterfaceType intfType = cfg::InterfaceType::VLAN);

cfg::SwitchConfig genPortVlanCfg(
    const PlatformMapping* platformMapping,
    const HwAsic* asic,
    const std::vector<PortID>& ports,
    const std::map<PortID, VlanID>& port2vlan,
    const std::vector<VlanID>& vlans,
    const std::map<cfg::PortType, cfg::PortLoopbackMode> lbModeMap,
    bool supportsAddRemovePort,
    bool optimizePortProfile = true,
    bool enableFabricPorts = false,
    const std::optional<std::map<SwitchID, cfg::SwitchInfo>>&
        switchIdToSwitchInfo = std::nullopt,
    const std::optional<std::map<SwitchID, const HwAsic*>>& hwAsicTable =
        std::nullopt,
    const std::optional<PlatformType> platformType = std::nullopt);

void populateSwitchInfo(
    cfg::SwitchConfig& config,
    const std::map<SwitchID, cfg::SwitchInfo>& switchIdToSwitchInfo,
    const std::map<SwitchID, const HwAsic*>& hwAsicTable,
    const std::optional<PlatformType> platformType = std::nullopt);

cfg::SwitchInfo generateSwitchInfo(
    SwitchID switchId,
    const cfg::Range64& portIdRange,
    const std::string& connectionHandle,
    const HwAsic* asic);

cfg::SwitchConfig twoL3IntfConfig(
    SwSwitch* swSwitch,
    PortID port1,
    PortID port2,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap =
        kDefaultLoopbackMap(),
    cfg::InterfaceType intfType = cfg::InterfaceType::VLAN);
cfg::SwitchConfig twoL3IntfConfig(
    const PlatformMapping* platformMapping,
    const std::vector<const HwAsic*>& asics,
    bool supportsAddRemovePort,
    PortID port1,
    PortID port2,
    const std::map<cfg::PortType, cfg::PortLoopbackMode>& lbModeMap =
        kDefaultLoopbackMap(),
    cfg::InterfaceType intfType = cfg::InterfaceType::VLAN);

/*
 * Currently we rely on port max speed to set the PortProfileID in the default
 * port config. This can be expensive as if the Hardware comes up with ports
 * using speeds different than max speed, the system will try to reconfigure
 * the port group or the port speed. But most of our tests don't really care
 * about which speed we use.
 * Therefore, introducing this static PortToDefaultProfileIDMap so that
 * when HwTest::SetUp() finish initializng the HwSwitchEnsemble, we can use
 * the SwState to collect the port and current profile id and then update
 * this map. Using this SwState, which represents the state of the Hardware w/o
 * any config applied yet, the port config can truely represent the default
 * state of the Hardware.
 */
std::unordered_map<PortID, cfg::PortProfileID>& getPortToDefaultProfileIDMap();

bool isRswPlatform(PlatformType type);

/*
 * Functions to get uplinks and downlinks return a pair of vectors, which is a
 * lot to write out, so we define a simple type that's descriptive and saves a
 * few keystrokes.
 */
using UplinkDownlinkPair = std::pair<std::vector<PortID>, std::vector<PortID>>;

UplinkDownlinkPair getRswUplinkDownlinkPorts(
    const cfg::SwitchConfig& config,
    const int ecmpWidth);

UplinkDownlinkPair getRtswUplinkDownlinkPorts(
    const cfg::SwitchConfig& config,
    const int ecmpWidth);

UplinkDownlinkPair getAllUplinkDownlinkPorts(
    PlatformType platformType,
    const cfg::SwitchConfig& config,
    const int ecmpWidth = 4,
    const bool mmu_lossless = false);

void configurePortGroup(
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    cfg::SwitchConfig& config,
    cfg::PortSpeed speed,
    std::vector<PortID> allPortsInGroup);

void configurePortGroupsForMaxSpeed(
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    cfg::SwitchConfig& config);

std::vector<PortID> getAllPortsInGroup(
    const PlatformMapping* platformMapping,
    PortID portID);

void removeSubsumedPorts(
    cfg::SwitchConfig& config,
    const cfg::PlatformPortConfig& profile,
    bool supportsAddRemovePort);

std::map<int, std::vector<uint8_t>> getOlympicQosMaps(
    const cfg::SwitchConfig& config);

bool checkConfigHasAclEntry(
    const cfg::SwitchConfig& config,
    std::string aclName);

void configurePortProfile(
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    cfg::SwitchConfig& config,
    cfg::PortProfileID profileID,
    std::vector<PortID> allPortsInGroup,
    PortID controllingPortID);
void setupMultipleEgressPoolAndQueueConfigs(
    cfg::SwitchConfig& config,
    const std::vector<int>& losslessQueueIds,
    const uint64_t mmuSizeBytes);

bool isSaiConfig(const cfg::SwitchConfig& config);

void modifyPlatformConfig(
    cfg::PlatformConfig& config,
    const std::function<void(std::string&)>& modifyYamlFunc,
    const std::function<void(std::map<std::string, std::string>&)>&
        modifyMapFunc);

void runCintScript(TestEnsembleIf* ensemble, const std::string& cintStr);

std::string
genInterfaceAddress(int ipDecimal, bool isV4, int host, int subnetMask);

} // namespace facebook::fboss::utility
