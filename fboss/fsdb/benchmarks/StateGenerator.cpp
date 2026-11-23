// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/benchmarks/StateGenerator.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"

namespace facebook::fboss::fsdb::test {
state::SystemPortFields fillSystemPortMap(int switchId, int portId) {
  state::SystemPortFields sysPortFields;
  sysPortFields.portId() = portId;
  sysPortFields.switchId() = switchId;
  sysPortFields.portName() =
      folly::to<std::string>("abcd123.a123.a123.abc1:", portId);
  sysPortFields.coreIndex() = 0;
  sysPortFields.corePortIndex() = 0;
  sysPortFields.speedMbps() = 100000;
  sysPortFields.numVoqs() = 8;
  sysPortFields.enabled_DEPRECATED() = true;
  sysPortFields.qosPolicy() = "defaultQosPolicy";
  sysPortFields.scope() = cfg::Scope::LOCAL;
  sysPortFields.shelDestinationEnabled() = false;

  std::vector<PortQueueFields> portQueueFields;
  for (int queueId = 0; queueId < 8; ++queueId) {
    PortQueueFields portQueueFieldsEntry;
    portQueueFieldsEntry.id() = queueId;
    portQueueFieldsEntry.weight() = 0;
    portQueueFieldsEntry.streamType() = "UNICAST";
    portQueueFieldsEntry.scheduling() = "INTERNAL";
    portQueueFieldsEntry.name() = "test";
    portQueueFieldsEntry.maxDynamicSharedBytes() = 20971520;
  }
  sysPortFields.queues() = portQueueFields;

  return sysPortFields;
}

state::NeighborEntryFields fillNeighborEntryFields(
    const std::string& address,
    int interfaceId) {
  state::NeighborEntryFields neighborEntryFields;
  neighborEntryFields.ipaddress() = address;
  neighborEntryFields.mac() = "00:00:00:00:00:00";
  cfg::PortDescriptor portDescriptor;
  portDescriptor.portId() = interfaceId;
  portDescriptor.portType() = cfg::PortDescriptorType::SystemPort;
  neighborEntryFields.portId() = portDescriptor;
  neighborEntryFields.interfaceId() = interfaceId;
  neighborEntryFields.state() = state::NeighborState::Reachable;
  neighborEntryFields.isLocal() = true;
  neighborEntryFields.type() = state::NeighborEntryType::DYNAMIC_ENTRY;
  neighborEntryFields.resolvedSince() = 1234567899;
  neighborEntryFields.noHostRoute() = true;

  return neighborEntryFields;
}

state::NeighborResponseEntryFields fillNeighborResponseEntryFields(
    const std::string& address,
    int interfaceId) {
  state::NeighborResponseEntryFields neighborResponseEntryFields;
  neighborResponseEntryFields.ipAddress() = address;
  neighborResponseEntryFields.mac() = "00:00:00:00:00:00";
  neighborResponseEntryFields.interfaceId() = interfaceId;
  return neighborResponseEntryFields;
}

cfg::NdpConfig fillNdpConfig() {
  cfg::NdpConfig ndpConfig;
  ndpConfig.routerAdvertisementSeconds() = 0;
  ndpConfig.curHopLimit() = 255;
  ndpConfig.routerLifetime() = 1800;
  ndpConfig.prefixValidLifetimeSeconds() = 2592000;
  ndpConfig.prefixPreferredLifetimeSeconds() = 604800;
  ndpConfig.routerAdvertisementManagedBit() = false;
  ndpConfig.routerAdvertisementOtherBit() = false;

  return ndpConfig;
}

state::InterfaceFields fillInterfaceMap(int portId, int numInterfaces) {
  state::InterfaceFields interfaceFields;
  interfaceFields.interfaceId() = portId;
  interfaceFields.vlanId() = 0;
  interfaceFields.name() = folly::to<std::string>("interface ", portId);
  interfaceFields.mac() = 796855659067998208;
  interfaceFields.mtu() = 9000;
  interfaceFields.isVirtual() = false;
  interfaceFields.isStateSyncDisabled() = false;
  interfaceFields.type() = cfg::InterfaceType::SYSTEM_PORT;
  interfaceFields.dhcpV4Relay() = "0.0.0.0";
  interfaceFields.dhcpV6Relay() = "::";
  interfaceFields.scope() = cfg::Scope::LOCAL;
  interfaceFields.portId() = portId;
  interfaceFields.ndpConfig() = fillNdpConfig();

  std::map<std::string, int16_t> intfAddresses;
  std::map<std::string, state::NeighborEntryFields> arpTable;
  std::map<std::string, state::NeighborEntryFields> ndpTable;
  std::map<std::string, state::NeighborResponseEntryFields> arpResponseTable;
  std::map<std::string, state::NeighborResponseEntryFields> ndpResponseTable;

  for (int interfaceId = 0; interfaceId < numInterfaces; ++interfaceId) {
    auto address = folly::to<std::string>("2401:db00:e206:550::", interfaceId);
    intfAddresses.emplace(address, 128);
    arpTable.emplace(address, fillNeighborEntryFields(address, interfaceId));
    ndpTable.emplace(address, fillNeighborEntryFields(address, interfaceId));
    arpResponseTable.emplace(
        address, fillNeighborResponseEntryFields(address, interfaceId));
    ndpResponseTable.emplace(
        address, fillNeighborResponseEntryFields(address, interfaceId));
  }

  interfaceFields.addresses() = intfAddresses;
  interfaceFields.arpTable() = arpTable;
  interfaceFields.ndpTable() = ndpTable;
  interfaceFields.arpResponseTable() = arpResponseTable;
  interfaceFields.ndpResponseTable() = ndpResponseTable;

  return interfaceFields;
}

void StateGenerator::fillSwitchState(
    state::SwitchState* state,
    int numSwitches,
    int numInterfaces) {
  std::map<std::string, std::map<int64_t, state::SystemPortFields>>
      systemPortMaps;
  std::map<std::string, std::map<int32_t, state::InterfaceFields>>
      interfaceMaps;

  for (int switchId = 0; switchId < numSwitches; ++switchId) {
    std::map<int64_t, state::SystemPortFields> systemPortMap;
    std::map<int32_t, state::InterfaceFields> interfaceMap;
    for (int systemPortId = 0; systemPortId < numInterfaces; ++systemPortId) {
      systemPortMap.emplace(
          systemPortId, fillSystemPortMap(switchId, systemPortId));
      interfaceMap.emplace(
          systemPortId, fillInterfaceMap(systemPortId, numInterfaces));
    }
    systemPortMaps.emplace(folly::to<std::string>(switchId), systemPortMap);
    interfaceMaps.emplace(folly::to<std::string>(switchId), interfaceMap);
  }
  state->systemPortMaps() = systemPortMaps;
  state->interfaceMaps() = interfaceMaps;
}

// Update switch state by adding entries to systemPortMaps and interfaceMaps
void StateGenerator::updateSysPorts(
    state::SwitchState* state,
    int numInterfacesToAdd) {
  auto numSwitchIds = state->systemPortMaps()->size();
  for (int switchId = 0; switchId < numSwitchIds; ++switchId) {
    auto& systemPortMap =
        state->systemPortMaps()->at(folly::to<std::string>(switchId));
    auto& interfaceMap =
        state->interfaceMaps()->at(folly::to<std::string>(switchId));
    // Get existing number of interface entries present and add it to
    // numInterfaces so sys port and RIF contains entry for total number of
    // interfaces
    auto numInterfaces = systemPortMap.size();
    auto totalInterfaces = numInterfaces + numInterfacesToAdd;
    for (int systemPortId = numInterfaces; systemPortId < totalInterfaces;
         ++systemPortId) {
      systemPortMap.emplace(
          systemPortId, fillSystemPortMap(switchId, systemPortId));
      interfaceMap.emplace(
          systemPortId, fillInterfaceMap(systemPortId, totalInterfaces));
    }
  }
}

void StateGenerator::updateNeighborTables(
    state::SwitchState* state,
    int numNeighborsToAdd) {
  auto numSwitchIds = state->systemPortMaps()->size();
  for (int switchId = 0; switchId < numSwitchIds; ++switchId) {
    auto& interfaceMap =
        state->interfaceMaps()->at(folly::to<std::string>(switchId));
    // Get existing number of interface entries present and add it to
    // numInterfaces so sys port and RIF contains entry for total number of
    // interfaces
    auto numInterfaces = interfaceMap.size();
    for (int interfaceId = 0; interfaceId < numInterfaces; ++interfaceId) {
      auto arpTable = interfaceMap.at(interfaceId).arpTable();
      auto ndpTable = interfaceMap.at(interfaceId).ndpTable();
      auto arpResponseTable = interfaceMap.at(interfaceId).arpResponseTable();
      auto ndpResponseTable = interfaceMap.at(interfaceId).ndpResponseTable();

      for (int neighborId = numInterfaces;
           neighborId < (numInterfaces + numNeighborsToAdd);
           ++neighborId) {
        auto address =
            folly::to<std::string>("2401:db00:e206:550::", neighborId);
        arpTable->emplace(
            address, fillNeighborEntryFields(address, neighborId));
        ndpTable->emplace(
            address, fillNeighborEntryFields(address, neighborId));
        arpResponseTable->emplace(
            address, fillNeighborResponseEntryFields(address, neighborId));
        ndpResponseTable->emplace(
            address, fillNeighborResponseEntryFields(address, neighborId));
      }
    }
  }
}

void StateGenerator::fillVoqStats(
    AgentStats* stats,
    int nSysPorts,
    int nVoqsPerSysPort,
    int value) {
  stats->sysPortStatsMap()->emplace(0, std::map<std::string, HwSysPortStats>());
  for (int portId = 0; portId < nSysPorts; ++portId) {
    std::string portName = folly::to<std::string>(":eth/", portId, "/1");
    HwSysPortStats sysPortStatsEntry;
    for (int queueId = 0; queueId < nVoqsPerSysPort; queueId++) {
      sysPortStatsEntry.queueOutDiscardBytes_()[queueId] = value;
      sysPortStatsEntry.queueOutBytes_()[queueId] = value;
      sysPortStatsEntry.queueWatermarkBytes_()[queueId] = value;
      sysPortStatsEntry.queueWredDroppedPackets_()[queueId] = value;
      sysPortStatsEntry.queueCreditWatchdogDeletedPackets_()[queueId] = value;
      sysPortStatsEntry.queueLatencyWatermarkNsec_()[queueId] = value;
    }
    sysPortStatsEntry.portName_() = portName;
    sysPortStatsEntry.timestamp_() = 1;
    stats->sysPortStatsMap()->at(0).emplace(
        portName, std::move(sysPortStatsEntry));
  }
}

void StateGenerator::updateVoqStats(AgentStats* stats, int increment) {
  for (auto& [switchIdx, sysPortStatsMap] : *stats->sysPortStatsMap()) {
    for (auto& [portName, sysPortStatsEntry] : sysPortStatsMap) {
      for (int queueId = 0;
           queueId < sysPortStatsEntry.queueOutDiscardBytes_()->size();
           queueId++) {
        sysPortStatsEntry.queueOutDiscardBytes_()[queueId] += increment;
      }
      for (int queueId = 0;
           queueId < sysPortStatsEntry.queueOutBytes_()->size();
           queueId++) {
        sysPortStatsEntry.queueOutBytes_()[queueId] += increment;
      }
      for (int queueId = 0;
           queueId < sysPortStatsEntry.queueWatermarkBytes_()->size();
           queueId++) {
        sysPortStatsEntry.queueWatermarkBytes_()[queueId] += increment;
      }
      for (int queueId = 0;
           queueId < sysPortStatsEntry.queueWredDroppedPackets_()->size();
           queueId++) {
        sysPortStatsEntry.queueWredDroppedPackets_()[queueId] += increment;
      }
      for (int queueId = 0; queueId <
           sysPortStatsEntry.queueCreditWatchdogDeletedPackets_()->size();
           queueId++) {
        sysPortStatsEntry.queueCreditWatchdogDeletedPackets_()[queueId] +=
            increment;
      }
      for (int queueId = 0;
           queueId < sysPortStatsEntry.queueLatencyWatermarkNsec_()->size();
           queueId++) {
        sysPortStatsEntry.queueLatencyWatermarkNsec_()[queueId] += increment;
      }
    }
  }
}

} // namespace facebook::fboss::fsdb::test
