/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiMacsecManager.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/CounterUtils.h"
#include "fboss/agent/hw/sai/api/SaiDefaultAttributeValues.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include <fmt/core.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/FbossError.h"

extern "C" {
#include <sai.h>
}

namespace {
using namespace facebook::fboss;
constexpr sai_uint8_t kSectagOffset = 12;
// Vendor recommended replay protection window size for 400Gbps link
constexpr sai_int32_t kReplayProtectionWindow = 4096;

static std::array<uint8_t, 12> kDefaultSaltValue{
    0x9d,
    0x00,
    0x29,
    0x02,
    0x48,
    0xde,
    0x86,
    0xa2,
    0x1c,
    0x66,
    0xfa,
    0x6d};
const MacsecShortSecureChannelId kDefaultSsciValue =
    MacsecShortSecureChannelId(0x01000000);

// ACL priority values for MacSec (lower number has higher priority)
constexpr int kMacsecDefaultAclPriority = 4;
constexpr int kMacsecAclPriority = 3;
constexpr int kMacsecLldpAclPriority = 2;
constexpr int kMacsecMkaAclPriority = 1;

std::shared_ptr<AclEntry> makeAclEntry(int priority, const std::string& name) {
  return std::make_shared<AclEntry>(priority, name);
}

const std::shared_ptr<AclEntry> createMacsecAclEntry(
    int priority,
    const std::string& entryName,
    MacsecFlowSaiId flowId,
    std::optional<folly::MacAddress> mac,
    std::optional<cfg::EtherType> etherType) {
  auto entry = std::make_shared<AclEntry>(priority, entryName);
  if (mac.has_value()) {
    entry->setDstMac(*mac);
  }
  if (etherType.has_value()) {
    entry->setEtherType(*etherType);
  }
  auto macsecAction = MatchAction();
  auto macsecFlowAction = cfg::MacsecFlowAction();
  macsecFlowAction.action() = cfg::MacsecFlowPacketAction::MACSEC_FLOW;
  macsecFlowAction.flowId() = flowId;
  macsecAction.setMacsecFlow(macsecFlowAction);
  entry->setActionType(cfg::AclActionType::PERMIT);
  entry->setAclAction(macsecAction);

  return entry;
}

const std::shared_ptr<AclEntry> createMacsecControlAclEntry(
    int priority,
    const std::string& entryName,
    std::optional<folly::MacAddress> mac,
    std::optional<cfg::EtherType> etherType) {
  auto entry = std::make_shared<AclEntry>(priority, entryName);
  if (mac.has_value()) {
    entry->setDstMac(*mac);
  }
  if (etherType.has_value()) {
    entry->setEtherType(*etherType);
  }

  auto macsecAction = MatchAction();
  auto macsecFlowAction = cfg::MacsecFlowAction();
  macsecFlowAction.action() = cfg::MacsecFlowPacketAction::FORWARD;
  macsecFlowAction.flowId() = 0;
  macsecAction.setMacsecFlow(macsecFlowAction);
  entry->setActionType(cfg::AclActionType::PERMIT);
  entry->setAclAction(macsecAction);

  return entry;
}

const std::shared_ptr<AclEntry> createMacsecRxDefaultAclEntry(
    int priority,
    const std::string& entryName,
    cfg::MacsecFlowPacketAction action) {
  auto entry = std::make_shared<AclEntry>(priority, entryName);

  auto macsecAction = MatchAction();
  auto macsecFlowAction = cfg::MacsecFlowAction();
  macsecFlowAction.action() = action;
  macsecFlowAction.flowId() = 0;
  macsecAction.setMacsecFlow(macsecFlowAction);
  if (action == cfg::MacsecFlowPacketAction::DROP) {
    entry->setActionType(cfg::AclActionType::DENY);
  } else {
    entry->setActionType(cfg::AclActionType::PERMIT);
  }

  // Add traffic counter for packet hitting default ACL
  auto counter = cfg::TrafficCounter();
  counter.name() =
      folly::to<std::string>(entryName, ".", priority, ".def.pkts");
  counter.types() = {cfg::CounterType::PACKETS};
  macsecAction.setTrafficCounter(counter);
  entry->setAclAction(macsecAction);

  return entry;
}

void fillHwPortStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    mka::MacsecPortStats& portStats) {
  for (auto counterIdAndValue : counterId2Value) {
    auto [counterId, value] = counterIdAndValue;
    switch (counterId) {
      case SAI_MACSEC_PORT_STAT_PRE_MACSEC_DROP_PKTS:
        portStats.preMacsecDropPkts() = value;
        break;
      case SAI_MACSEC_PORT_STAT_CONTROL_PKTS:
        portStats.controlPkts() = value;
        break;
      case SAI_MACSEC_PORT_STAT_DATA_PKTS:
        portStats.dataPkts() = value;
        break;
    }
  }
}
mka::MacsecFlowStats fillFlowStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    sai_macsec_direction_t direction) {
  mka::MacsecFlowStats flowStats{};
  flowStats.directionIngress() = direction == SAI_MACSEC_DIRECTION_INGRESS;
  for (auto counterIdAndValue : counterId2Value) {
    auto [counterId, value] = counterIdAndValue;
    switch (counterId) {
      case SAI_MACSEC_FLOW_STAT_UCAST_PKTS_UNCONTROLLED:
        flowStats.ucastUncontrolledPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_UCAST_PKTS_CONTROLLED:
        flowStats.ucastControlledPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_MULTICAST_PKTS_UNCONTROLLED:
        flowStats.mcastUncontrolledPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_MULTICAST_PKTS_CONTROLLED:
        flowStats.mcastControlledPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_BROADCAST_PKTS_UNCONTROLLED:
        flowStats.bcastUncontrolledPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_BROADCAST_PKTS_CONTROLLED:
        flowStats.bcastControlledPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_CONTROL_PKTS:
        flowStats.controlPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_PKTS_UNTAGGED:
        flowStats.untaggedPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_OTHER_ERR:
        flowStats.otherErrPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_OCTETS_UNCONTROLLED:
        flowStats.octetsUncontrolled() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_OCTETS_CONTROLLED:
        flowStats.octetsControlled() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_OUT_OCTETS_COMMON:
        flowStats.outCommonOctets() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_OUT_PKTS_TOO_LONG:
        flowStats.outTooLongPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_IN_TAGGED_CONTROL_PKTS:
        flowStats.inTaggedControlledPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_IN_PKTS_NO_TAG:
        flowStats.inNoTagPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_IN_PKTS_BAD_TAG:
        flowStats.inBadTagPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_IN_PKTS_NO_SCI:
        flowStats.noSciPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_IN_PKTS_UNKNOWN_SCI:
        flowStats.unknownSciPkts() = value;
        break;
      case SAI_MACSEC_FLOW_STAT_IN_PKTS_OVERRUN:
        flowStats.overrunPkts() = value;
        break;
    }
  }
  return flowStats;
}

mka::MacsecSaStats fillSaStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    sai_macsec_direction_t direction) {
  mka::MacsecSaStats saStats{};
  saStats.directionIngress() = direction == SAI_MACSEC_DIRECTION_INGRESS;
  for (auto counterIdAndValue : counterId2Value) {
    auto [counterId, value] = counterIdAndValue;
    switch (counterId) {
      case SAI_MACSEC_SA_STAT_OCTETS_ENCRYPTED:
        saStats.octetsEncrypted() = value;
        break;
      case SAI_MACSEC_SA_STAT_OCTETS_PROTECTED:
        saStats.octetsProtected() = value;
        break;
      case SAI_MACSEC_SA_STAT_OUT_PKTS_ENCRYPTED:
        saStats.outEncryptedPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_OUT_PKTS_PROTECTED:
        saStats.outProtectedPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_UNCHECKED:
        saStats.inUncheckedPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_DELAYED:
        saStats.inDelayedPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_LATE:
        saStats.inLatePkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_INVALID:
        saStats.inInvalidPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_NOT_VALID:
        saStats.inNotValidPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_NOT_USING_SA:
        saStats.inNoSaPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_UNUSED_SA:
        saStats.inUnusedSaPkts() = value;
        break;
      case SAI_MACSEC_SA_STAT_IN_PKTS_OK:
        saStats.inOkPkts() = value;
        break;
    }
  }
  return saStats;
}
} // namespace

namespace facebook::fboss {

SaiMacsecManager::SaiMacsecManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable)
    : saiStore_(saiStore), managerTable_(managerTable) {}

SaiMacsecManager::~SaiMacsecManager() {
  // Collect the list of port and direction before calling
  // removeBasicMacsecState because that function will modify macsecHandles_
  // and related data structure
  std::vector<std::tuple<PortID, sai_macsec_direction_t>> portAndDirectionList;
  for (const auto& macsec : macsecHandles_) {
    auto direction = macsec.first;
    for (const auto& port : macsec.second->ports) {
      auto portId = port.first;
      std::tuple<PortID, sai_macsec_direction_t> portAndDir;
      std::get<sai_macsec_direction_t>(portAndDir) = direction;
      std::get<PortID>(portAndDir) = portId;

      portAndDirectionList.push_back(portAndDir);
    }
  }

  for (auto& portAndDir : portAndDirectionList) {
    auto direction = std::get<sai_macsec_direction_t>(portAndDir);
    auto portId = std::get<PortID>(portAndDir);
    // Remove ACL entries, ACL table, Macsec vPort, Macsec pipeline objects
    removeMacsecState(portId, direction);
  }
}

MacsecSaiId SaiMacsecManager::addMacsec(
    sai_macsec_direction_t direction,
    bool physicalBypassEnable) {
  auto handle = getMacsecHandle(direction);
  if (handle) {
    throw FbossError(
        "Attempted to add macsec for direction that already has a macsec pipeline: ",
        direction,
        " SAI id: ",
        handle->macsec->adapterKey());
  }

  SaiMacsecTraits::CreateAttributes attributes = {
      direction, physicalBypassEnable};
  SaiMacsecTraits::AdapterHostKey key{direction};

  auto& macsecStore = saiStore_->get<SaiMacsecTraits>();
  auto saiMacsecObj = macsecStore.setObject(key, attributes);
  auto macsecHandle = std::make_unique<SaiMacsecHandle>();
  macsecHandle->macsec = std::move(saiMacsecObj);
  macsecHandles_.emplace(direction, std::move(macsecHandle));
  return macsecHandles_[direction]->macsec->adapterKey();
}

void SaiMacsecManager::removeMacsec(sai_macsec_direction_t direction) {
  auto itr = macsecHandles_.find(direction);
  if (itr == macsecHandles_.end()) {
    throw FbossError(
        "Attempted to remove non-existent macsec pipeline for direction: ",
        direction);
  }
  macsecHandles_.erase(itr);
  XLOG(DBG2) << "removed macsec pipeline for direction " << direction;
}

const SaiMacsecHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecHandle(sai_macsec_direction_t direction) const {
  return getMacsecHandleImpl(direction);
}
SaiMacsecHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecHandle(sai_macsec_direction_t direction) {
  return getMacsecHandleImpl(direction);
}

SaiMacsecHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecHandleImpl(sai_macsec_direction_t direction) const {
  auto itr = macsecHandles_.find(direction);
  if (itr == macsecHandles_.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiMacsecHandle for direction " << direction;
  }
  return itr->second.get();
}

std::shared_ptr<SaiMacsecFlow> SaiMacsecManager::createMacsecFlow(
    sai_macsec_direction_t direction) {
  SaiMacsecFlowTraits::CreateAttributes attributes = {
      direction,
  };
  SaiMacsecFlowTraits::AdapterHostKey key{direction};

  auto& store = saiStore_->get<SaiMacsecFlowTraits>();

  return store.setObject(key, attributes);
}

const SaiMacsecFlow* SaiMacsecManager::getMacsecFlow(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction) const {
  return getMacsecFlowImpl(linePort, secureChannelId, direction);
}
SaiMacsecFlow* SaiMacsecManager::getMacsecFlow(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction) {
  return getMacsecFlowImpl(linePort, secureChannelId, direction);
}

SaiMacsecFlow* SaiMacsecManager::getMacsecFlowImpl(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction) const {
  auto macsecScHandle =
      getMacsecSecureChannelHandle(linePort, secureChannelId, direction);
  if (!macsecScHandle) {
    throw FbossError(
        "Attempted to get macsecFlow for non-existent SC ",
        secureChannelId,
        "for port ",
        linePort,
        " direction ",
        direction);
  }
  return macsecScHandle->flow.get();
}

MacsecPortSaiId SaiMacsecManager::addMacsecPort(
    PortID linePort,
    sai_macsec_direction_t direction) {
  auto macsecPort = getMacsecPortHandle(linePort, direction);
  if (macsecPort) {
    throw FbossError(
        "Attempted to add macsecPort for port/direction that already exists: ",
        linePort,
        ",",
        direction,
        " SAI id: ",
        macsecPort->port->adapterKey());
  }
  auto macsecHandle = getMacsecHandle(direction);
  auto portHandle = managerTable_->portManager().getPortHandle(linePort);
  if (!portHandle) {
    throw FbossError(
        "Attempted to add macsecPort for linePort that doesn't exist: ",
        linePort);
  }

  SaiMacsecPortTraits::CreateAttributes attributes{
      portHandle->port->adapterKey(), direction};
  SaiMacsecPortTraits::AdapterHostKey key{
      portHandle->port->adapterKey(), direction};

  auto& store = saiStore_->get<SaiMacsecPortTraits>();
  auto saiObj = store.setObject(key, attributes);
  auto macsecPortHandle = std::make_unique<SaiMacsecPortHandle>();
  macsecPortHandle->port = std::move(saiObj);

  macsecHandle->ports.emplace(linePort, std::move(macsecPortHandle));
  return macsecHandle->ports[linePort]->port->adapterKey();
}

const SaiMacsecPortHandle* FOLLY_NULLABLE SaiMacsecManager::getMacsecPortHandle(
    PortID linePort,
    sai_macsec_direction_t direction) const {
  return getMacsecPortHandleImpl(linePort, direction);
}
SaiMacsecPortHandle* FOLLY_NULLABLE SaiMacsecManager::getMacsecPortHandle(
    PortID linePort,
    sai_macsec_direction_t direction) {
  return getMacsecPortHandleImpl(linePort, direction);
}

void SaiMacsecManager::removeMacsecPort(
    PortID linePort,
    sai_macsec_direction_t direction) {
  auto macsecHandle = getMacsecHandle(direction);
  if (!macsecHandle) {
    throw FbossError(
        "Attempted to remove macsecPort for direction that has no macsec pipeline obj ",
        direction);
  }
  auto itr = macsecHandle->ports.find(linePort);
  if (itr == macsecHandle->ports.end()) {
    throw FbossError(
        "Attempted to remove non-existent macsec port for lineport, direction: ",
        linePort,
        ", ",
        direction);
  }
  macsecHandle->ports.erase(itr);
  XLOG(DBG2) << "removed macsec port for lineport, direction: " << linePort
             << ", " << direction;
}

SaiMacsecPortHandle* FOLLY_NULLABLE SaiMacsecManager::getMacsecPortHandleImpl(
    PortID linePort,
    sai_macsec_direction_t direction) const {
  auto macsecHandle = getMacsecHandle(direction);
  if (!macsecHandle) {
    throw FbossError(
        "Attempted to get macsecPort for direction that has no macsec pipeline obj ",
        direction);
  }

  auto itr = macsecHandle->ports.find(linePort);
  if (itr == macsecHandle->ports.end()) {
    return nullptr;
  }

  CHECK(itr->second.get())
      << "Invalid null SaiMacsecPortHandle for linePort, direction: "
      << linePort << ", " << direction;

  return itr->second.get();
}

MacsecSCSaiId SaiMacsecManager::addMacsecSecureChannel(
    PortID linePort,
    sai_macsec_direction_t direction,
    MacsecSecureChannelId secureChannelId,
    bool xpn64Enable) {
  auto handle =
      getMacsecSecureChannelHandle(linePort, secureChannelId, direction);
  if (handle) {
    throw FbossError(
        "Attempted to add macsecSC for secureChannelId:direction that already exists: ",
        secureChannelId,
        ":",
        direction,
        " SAI id: ",
        handle->secureChannel->adapterKey());
  }

  auto macsecPort = getMacsecPortHandle(linePort, direction);
  if (!macsecPort) {
    throw FbossError(
        "Attempted to add macsecSC for linePort that doesn't exist: ",
        linePort);
  }

  // create a new flow for the SC
  auto flow = createMacsecFlow(direction);
  auto flowId = flow->adapterKey();

  XLOG(DBG2) << "For ScId " << secureChannelId << " Created Flow " << flowId;

  std::optional<bool> secureChannelEnable;
  std::optional<sai_int32_t> replayProtectionWindow;

  secureChannelEnable = true;

  if (direction == SAI_MACSEC_DIRECTION_INGRESS) {
    replayProtectionWindow = kReplayProtectionWindow;
  }

  SaiMacsecSCTraits::CreateAttributes attributes{
      secureChannelId,
      direction,
      flowId,
#if SAI_API_VERSION >= SAI_VERSION(1, 7, 1)
      xpn64Enable ? SAI_MACSEC_CIPHER_SUITE_GCM_AES_XPN_256
                  : SAI_MACSEC_CIPHER_SUITE_GCM_AES_256, /* ciphersuite */
#endif
      secureChannelEnable,
      replayProtectionWindow.has_value(),
      replayProtectionWindow,
      kSectagOffset};
  SaiMacsecSCTraits::AdapterHostKey secureChannelKey{
      secureChannelId, direction};

  auto& store = saiStore_->get<SaiMacsecSCTraits>();
  auto saiObj = store.setObject(secureChannelKey, attributes);
  auto scHandle = std::make_unique<SaiMacsecSecureChannelHandle>();
  scHandle->secureChannel = std::move(saiObj);
  scHandle->flow = std::move(flow);
  macsecPort->secureChannels.emplace(secureChannelId, std::move(scHandle));
  return macsecPort->secureChannels[secureChannelId]
      ->secureChannel->adapterKey();
}

const SaiMacsecSecureChannelHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecSecureChannelHandle(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction) const {
  return getMacsecSecureChannelHandleImpl(linePort, secureChannelId, direction);
}
SaiMacsecSecureChannelHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecSecureChannelHandle(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction) {
  return getMacsecSecureChannelHandleImpl(linePort, secureChannelId, direction);
}

void SaiMacsecManager::removeMacsecSecureChannel(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction) {
  auto portHandle = getMacsecPortHandle(linePort, direction);
  if (!portHandle) {
    throw FbossError(
        "Attempted to remove SC for non-existent linePort: ", linePort);
  }
  auto itr = portHandle->secureChannels.find(secureChannelId);
  if (itr == portHandle->secureChannels.end()) {
    throw FbossError(
        "Attempted to remove non-existent macsec SC for linePort:secureChannelId:direction: ",
        linePort,
        ":",
        secureChannelId,
        ":",
        direction);
  }

  // The objects needs to be removed in this order because of the dependency.
  // ACL entry contans reference to Flow
  // SC contains reference to Flow
  // So first we need to remove ACL entry, then Flow and then SC

  // Remove the ACL entry for this SC
  removeScAcls(linePort, direction);
  // Remove the SC
  portHandle->secureChannels[secureChannelId]->secureChannel.reset();
  // No object refers to Flow so it is safe to remove it now
  portHandle->secureChannels[secureChannelId]->flow.reset();
  // Remove the SC from secureChannel map
  portHandle->secureChannels.erase(itr);

  XLOG(DBG2) << "removed macsec SC for linePort:secureChannelId:direction: "
             << linePort << ":" << secureChannelId << ":" << direction;
}

SaiMacsecSecureChannelHandle* FOLLY_NULLABLE
SaiMacsecManager::getMacsecSecureChannelHandleImpl(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction) const {
  auto portHandle = getMacsecPortHandle(linePort, direction);
  if (!portHandle) {
    return nullptr;
  }
  auto itr = portHandle->secureChannels.find(secureChannelId);
  if (itr == portHandle->secureChannels.end()) {
    return nullptr;
  }

  CHECK(itr->second.get())
      << "Invalid null SaiMacsecSCHandle for secureChannelId:direction: "
      << secureChannelId << ":" << direction;
  return itr->second.get();
}

MacsecSASaiId SaiMacsecManager::addMacsecSecureAssoc(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction,
    uint8_t assocNum,
    SaiMacsecSak secureAssociationKey,
    SaiMacsecSalt salt,
    SaiMacsecAuthKey authKey,
    MacsecShortSecureChannelId shortSecureChannelId) {
  auto handle =
      getMacsecSecureAssoc(linePort, secureChannelId, direction, assocNum);
  if (handle) {
    throw FbossError(
        "Attempted to add macsecSA for secureChannelId:assocNum that already exists: ",
        secureChannelId,
        ":",
        assocNum,
        " SAI id: ",
        handle->adapterKey());
  }
  auto scHandle =
      getMacsecSecureChannelHandle(linePort, secureChannelId, direction);
  if (!scHandle) {
    throw FbossError(
        "Attempted to add macsecSA for non-existent sc for lineport, secureChannelId, direction: ",
        secureChannelId,
        ", ",
        linePort,
        ", ",
        " direction: ");
  }

  SaiMacsecSATraits::CreateAttributes attributes{
      scHandle->secureChannel->adapterKey(),
      assocNum,
      authKey,
      direction,
#if SAI_API_VERSION >= SAI_VERSION(1, 7, 1)
      shortSecureChannelId,
#endif
      secureAssociationKey,
      salt,
      std::nullopt /* minimumXpn */
  };
  SaiMacsecSATraits::AdapterHostKey key{
      scHandle->secureChannel->adapterKey(), assocNum, direction};

  auto& store = saiStore_->get<SaiMacsecSATraits>();
  auto saiObj = store.setObject(key, attributes);

  scHandle->secureAssocs.emplace(assocNum, std::move(saiObj));
  scHandle->latestSaAn = assocNum;
  return scHandle->secureAssocs[assocNum]->adapterKey();
}

const SaiMacsecSecureAssoc* FOLLY_NULLABLE
SaiMacsecManager::getMacsecSecureAssoc(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction,
    uint8_t assocNum) const {
  return getMacsecSecureAssocImpl(
      linePort, secureChannelId, direction, assocNum);
}
SaiMacsecSecureAssoc* FOLLY_NULLABLE SaiMacsecManager::getMacsecSecureAssoc(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction,
    uint8_t assocNum) {
  return getMacsecSecureAssocImpl(
      linePort, secureChannelId, direction, assocNum);
}

void SaiMacsecManager::removeMacsecSecureAssoc(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction,
    uint8_t assocNum,
    bool skipHwUpdate) {
  auto scHandle =
      getMacsecSecureChannelHandle(linePort, secureChannelId, direction);
  if (!scHandle) {
    throw FbossError(
        "Attempted to remove SA for non-existent secureChannelId: ",
        secureChannelId);
  }
  auto saHandle = scHandle->secureAssocs.find(assocNum);
  if (saHandle == scHandle->secureAssocs.end()) {
    throw FbossError(
        "Attempted to remove non-existent SA for secureChannelId, assocNum:  ",
        secureChannelId,
        ", ",
        assocNum);
  }

  // If hardware skip is required then make the SA object owned by adapter so
  // that later when the code tries to delete it then the Sai Store does not
  // make SAI driver call to delete it from hardware
  if (skipHwUpdate) {
    saHandle->second->setOwnedByAdapter(true);
  }

  scHandle->secureAssocs.erase(saHandle);
  XLOG(DBG2) << "removed macsec SA for secureChannelId:assocNum: "
             << secureChannelId << ":" << assocNum;
}

SaiMacsecSecureAssoc* FOLLY_NULLABLE SaiMacsecManager::getMacsecSecureAssocImpl(
    PortID linePort,
    MacsecSecureChannelId secureChannelId,
    sai_macsec_direction_t direction,
    uint8_t assocNum) const {
  auto scHandle =
      getMacsecSecureChannelHandle(linePort, secureChannelId, direction);
  if (!scHandle) {
    return nullptr;
  }
  auto itr = scHandle->secureAssocs.find(assocNum);
  if (itr == scHandle->secureAssocs.end()) {
    return nullptr;
  }
  CHECK(itr->second.get())
      << "Invalid null std::shared_ptr<SaiMacsecSA> for secureChannelId:assocNum: "
      << secureChannelId << ":" << assocNum;
  return itr->second.get();
}

void SaiMacsecManager::setupMacsec(
    PortID linePort,
    const mka::MKASak& sak,
    const mka::MKASci& sci,
    sai_macsec_direction_t direction) {
  auto portHandle = managerTable_->portManager().getPortHandle(linePort);
  if (!portHandle) {
    throw FbossError(
        "Failed to get portHandle for non-existent linePort: ", linePort);
  }

  // Setup basic Macsec state: Macsec pipeline, Macsec vPort, ACL table,
  // default ACL rules for this line port in the given direction
  setupMacsecState(linePort, sak.dropUnencrypted().value(), direction);
  XLOG(DBG2) << "Set up basic Macsec state for linePort: " << linePort;

  // Create the egress flow and ingress sc
  std::string sciKeyString =
      folly::to<std::string>(*sci.macAddress(), ".", *sci.port());

  // First convert sci mac address string and the port id to a uint64
  folly::MacAddress mac = folly::MacAddress(*sci.macAddress());
  auto scIdentifier = MacsecSecureChannelId(mac.u64NBO() | *sci.port());

  // Step2/3: Create flow and SC if it they do not exist
  auto secureChannelHandle =
      getMacsecSecureChannelHandle(linePort, scIdentifier, direction);
  if (!secureChannelHandle) {
    auto scSaiId =
        addMacsecSecureChannel(linePort, direction, scIdentifier, true);
    XLOG(DBG2) << "For SCI: " << sciKeyString << ", created macsec "
               << direction << " SC " << scSaiId;
    secureChannelHandle =
        getMacsecSecureChannelHandle(linePort, scIdentifier, direction);
  }
  CHECK(secureChannelHandle);
  auto flowId = secureChannelHandle->flow->adapterKey();

  // Step4: Create the Macsec SA now
  // Input macsec key is a string which needs to be converted to 32 bytes
  // array for passing to sai

  auto hexStringToBytes = [](const std::string& str,
                             uint8_t* byteList,
                             uint8_t dataLen) {
    for (int i = 0; i < str.length(); i += 2) {
      std::string byteAsStr = str.substr(i, 2);
      uint8_t byte = (uint8_t)strtol(byteAsStr.c_str(), nullptr, 16);
      if (i / 2 >= dataLen) {
        XLOG(ERR) << fmt::format(
            "Key string (length: {:d}) can't fit in key byte array (length: {:d})",
            str.length(),
            dataLen);
        break;
      }
      byteList[i / 2] = byte;
    }
  };

  // keyHex is a hex string: a 32-byte AES-256 key is 64 hex chars. The prior
  // check compared the hex char count against the byte count, so a 32-char
  // (16-byte) key was accepted and only the low half of the 256-bit key was
  // populated. Require the exact hex length before decoding.
  if (sak.keyHex()->size() != 64) {
    XLOG(ERR) << "Macsec key must be exactly 64 hex chars (32 bytes)";
    return;
  }
  std::array<uint8_t, 32> key{0};
  hexStringToBytes(sak.keyHex().value(), key.data(), 32);

  // Input macsec keyid (auth key) is provided in string which needs to be
  // converted to 16 byte array for passing to sai. A 16-byte key id is 32 hex
  // chars.
  if (sak.keyIdHex()->size() != 32) {
    XLOG(ERR) << "Macsec key Id must be exactly 32 hex chars (16 bytes)";
    return;
  }

  std::array<uint8_t, 16> keyId{0};
  hexStringToBytes(sak.keyIdHex().value(), keyId.data(), 16);

  auto secureAssoc = getMacsecSecureAssoc(
      linePort, scIdentifier, direction, *sak.assocNum() % 4);
  if (!secureAssoc) {
    // Add the new SA now
    auto secureAssocSaiId = addMacsecSecureAssoc(
        linePort,
        scIdentifier,
        direction,
        *sak.assocNum() % 4,
        key,
        kDefaultSaltValue,
        keyId,
        kDefaultSsciValue);
    XLOG(DBG2) << "For SCI: " << sciKeyString << ", created macsec "
               << direction << " SA " << secureAssocSaiId;
  }

  // // Step5: Create the ACL table if it does not exist
  // Right now we're just using one entry per table, so we're using the same
  // format for table and entry name
  std::string aclName = getAclName(linePort, direction);
  auto aclTable = managerTable_->aclTableManager().getAclTableHandle(aclName);
  CHECK_NOTNULL(aclTable);
  auto aclTableId = aclTable->aclTable->adapterKey();

  // Step6: Create ACL entry (if does not exist)
  auto aclEntryHandle = managerTable_->aclTableManager().getAclEntryHandle(
      aclTable, kMacsecAclPriority, aclName);
  if (!aclEntryHandle) {
    if (direction == SAI_MACSEC_DIRECTION_INGRESS) {
      // For the ingress direction create one ACL rule to match MACSEC packets
      // (ether-type = 0x88E5) with Action as MacSec Flow. Create another lower
      // priority ACL rule to match rest of the packets with Action as Drop (if
      // dropUnencrypted is true) otherwise Forward
      cfg::EtherType ethTypeMacsec{cfg::EtherType::MACSEC};
      auto aclEntry = createMacsecAclEntry(
          kMacsecAclPriority,
          aclName,
          flowId,
          std::nullopt /* dstMac */,
          ethTypeMacsec);

      auto aclEntryId = managerTable_->aclTableManager().addAclEntry(
          aclEntry, aclName, nullptr /*state*/);
      XLOG(DBG2) << "For SCI: " << sciKeyString << ", created macsec "
                 << direction << " ACL entry " << aclEntryId;
    } else {
      // For egress direction create the ACL rule to match all data packets with
      // Action as MacSec Flow
      auto aclEntry = createMacsecAclEntry(
          kMacsecAclPriority,
          aclName,
          flowId,
          std::nullopt /* dstMac */,
          std::nullopt /* etherType */);

      auto aclEntryId = managerTable_->aclTableManager().addAclEntry(
          aclEntry, aclName, nullptr /*state*/);
      XLOG(DBG2) << "For SCI: " << sciKeyString << ", created macsec "
                 << direction << " ACL entry " << aclEntryId;
    }
  }

  // Step7: Bind ACL table to the line port
  if (direction == SAI_MACSEC_DIRECTION_INGRESS) {
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::IngressMacSecAcl{aclTableId});
  } else {
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::EgressMacSecAcl{aclTableId});
  }
  XLOG(DBG2) << "To linePort " << linePort << ", bound macsec " << direction
             << " ACL table " << aclTableId;
}

std::string SaiMacsecManager::getAclName(
    facebook::fboss::PortID port,
    sai_macsec_direction_t direction) const {
  return folly::to<std::string>(
      "macsec-",
      direction == SAI_MACSEC_DIRECTION_INGRESS ? "ing" : "egr",
      "-port",
      port);
}

/*
 * setMacsecState
 *
 * This function sets up the Macsec state. If Macsec is desired then it will
 * call function to set the Macsec default objects. If Macsec is not desired
 * then it will call function to delete the default Macsec objects
 */
void SaiMacsecManager::setMacsecState(
    PortID linePort,
    bool macsecDesired,
    bool dropUnencrypted) {
  if (!macsecDesired) {
    // If no Macsec is needed then unbind ACL table from port, remove the
    // default ACL rules (MKA, LLDP, Default data packet), remove ACL table.
    // Then remove Macsec port and Macsec (pipeline)
    removeMacsecState(linePort, SAI_MACSEC_DIRECTION_INGRESS);
    removeMacsecState(linePort, SAI_MACSEC_DIRECTION_EGRESS);
  } else {
    // If Macsec is needed then add Macsec (Pipeline), Macsec port, Create ACL
    // table, create ACL rules (MKA, LLDP and default data packet rules as per
    // dropEncrypted), bind ACL table to port
    setupMacsecState(linePort, dropUnencrypted, SAI_MACSEC_DIRECTION_INGRESS);
    setupMacsecState(linePort, dropUnencrypted, SAI_MACSEC_DIRECTION_EGRESS);
  }
  XLOG(DBG2) << "For Port " << linePort << "Basic Macsec state "
             << (macsecDesired ? "Setup" : "Deletion") << " Successful";
}

/*
 * setupMacsecPipeline
 *
 * A helper function to create the Macsec pipeline object for a given direction.
 * Please note that the Macsec enabled per port. So if we create the Macsec
 * pipeline object in one direction then for other direction we need to create
 * the Macsec pipeline object with bypass as enabled, otherwise the traffic will
 * stop in other direction
 */
void SaiMacsecManager::setupMacsecPipeline(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // Get the reverse direction because we need to find the macsec handle of
  // other direction also
  auto reverseDirection =
      (direction == SAI_MACSEC_DIRECTION_INGRESS
           ? SAI_MACSEC_DIRECTION_EGRESS
           : SAI_MACSEC_DIRECTION_INGRESS);

  if (auto dirMacsecHandle = getMacsecHandle(direction)) {
    // If the macsec handle already exist for this direction then check the
    // current bypass state. If current macsec's bypass_enable is True then
    // update the attribute to make it False
    //    otherwise no issue
    auto bypassEnable = GET_OPT_ATTR(
        Macsec, PhysicalBypass, dirMacsecHandle->macsec->attributes());

    if (bypassEnable) {
      dirMacsecHandle->macsec->setOptionalAttribute(
          SaiMacsecTraits::Attributes::PhysicalBypass{false});
      XLOG(DBG2) << "For lineport: " << linePort
                 << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                               : "Egress")
                 << ", set macsec pipeline object w/ ID: " << dirMacsecHandle
                 << ", bypass enable as False";
    }
  } else {
    // If the macsec handle does not exist for this direction then create the
    // macsec handle with bypass_enable as False for this direction
    auto macsecId = addMacsec(direction, false);
    XLOG(DBG2) << "For "
               << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                             : "Egress")
               << ", created macsec pipeline object w/ ID: " << macsecId
               << ", bypass enable as False";
  }

  // Also check if the reverse direction macsec handle exist. If it does not
  // exist then we need to create reverse direction macsec handle with
  // bypass_enable as True
  if (!getMacsecHandle(reverseDirection)) {
    auto macsecId = addMacsec(reverseDirection, true);
    XLOG(DBG2) << "For "
               << (reverseDirection == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                                    : "Egress")
               << ", created macsec pipeline object w/ ID: " << macsecId
               << ", bypass enable as True";
  }
}

/*
 * setupMacsecPort
 *
 * The helper function to add Macsec vport in a given direction
 */
void SaiMacsecManager::setupMacsecPort(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // Check if the macsec port exists for lineport. If not present then create
  // Macsec vPort
  if (!getMacsecPortHandle(linePort, direction)) {
    auto macsecPortId = addMacsecPort(linePort, direction);
    XLOG(DBG2) << "For lineport: " << linePort << ", created macsec "
               << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                             : "Egress")
               << " port " << macsecPortId;
  }
}

/*
 * setupAclTable
 *
 * The helper function to add ACL table on a port in a given direction. Each
 * table has default control packet rules which does not change later, these
 * rules are also created by this helper function
 */
void SaiMacsecManager::setupAclTable(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // Right now we're just using one entry per table, so we're using the same
  // format for table and entry name
  std::string aclName = getAclName(linePort, direction);
  auto aclTable = managerTable_->aclTableManager().getAclTableHandle(aclName);
  if (!aclTable) {
    state::AclTableFields aclTableFields{};
    aclTableFields.id() = aclName;
    // TODO(saranicholas): set appropriate table priority
    aclTableFields.priority() = 0;
    auto table = std::make_shared<AclTable>(std::move(aclTableFields));
    auto aclTableId = managerTable_->aclTableManager().addAclTable(
        table,
        direction == SAI_MACSEC_DIRECTION_INGRESS
            ? cfg::AclStage::INGRESS_MACSEC
            : cfg::AclStage::EGRESS_MACSEC,
        nullptr /*state*/);
    XLOG(DBG2) << "For linePort: " << linePort << ", created "
               << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                             : "Egress")
               << " ACL table with sai ID " << aclTableId;
    aclTable = managerTable_->aclTableManager().getAclTableHandle(aclName);
    CHECK_NOTNULL(aclTable);

    // Create couple of control packet rules for MKA and LLDP
    setupAclControlPacketRules(linePort, direction);
  }
}

/*
 * setupAclControlPacketRules
 *
 * Helper function to create the control packet ACL rules for LLDP and MKA PDU
 */
void SaiMacsecManager::setupAclControlPacketRules(
    PortID linePort,
    sai_macsec_direction_t direction) {
  std::string aclName = getAclName(linePort, direction);

  // Create ACL Rule for MKA PDU
  cfg::EtherType ethTypeMka{cfg::EtherType::EAPOL};
  auto aclEntry = createMacsecControlAclEntry(
      kMacsecMkaAclPriority, aclName, std::nullopt /* dstMac */, ethTypeMka);
  auto aclEntryId = managerTable_->aclTableManager().addAclEntry(
      aclEntry, aclName, nullptr /*state*/);
  XLOG(DBG2) << "For linePort: " << linePort << ", direction "
             << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                           : "Egress")
             << " ACL entry for MKA " << aclEntryId;

  // Create ACL Rule for LLDP packets
  cfg::EtherType ethTypeLldp{cfg::EtherType::LLDP};
  aclEntry = createMacsecControlAclEntry(
      kMacsecLldpAclPriority, aclName, std::nullopt /* dstMac */, ethTypeLldp);
  aclEntryId = managerTable_->aclTableManager().addAclEntry(
      aclEntry, aclName, nullptr /*state*/);
  XLOG(DBG2) << "For linePort: " << linePort << ", direction "
             << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                           : "Egress")
             << " ACL entry for LLDP " << aclEntryId;
}

/*
 * setupDropUnencryptedRule
 *
 * Helper function to create the default data packet ACL rule as per
 * dropUnencrypted value specified. In case the rule already exists and
 * dropUnencrypted value has to change then this function will first delete the
 * ACL rule and then add a new one
 */
void SaiMacsecManager::setupDropUnencryptedRule(
    PortID linePort,
    bool dropUnencrypted,
    sai_macsec_direction_t direction) {
  std::string aclName = getAclName(linePort, direction);
  auto aclTable = managerTable_->aclTableManager().getAclTableHandle(aclName);
  CHECK_NOTNULL(aclTable);

  // Create the default lower priority ACL rule for inital data packets as
  // per dropUnencrypted value
  auto aclEntryHandle = managerTable_->aclTableManager().getAclEntryHandle(
      aclTable, kMacsecDefaultAclPriority, aclName);

  // If aclEntryHandle exists for default data packet then it could be that the
  // dropUnencrypted value has changed
  if (aclEntryHandle) {
    auto aclAttrs = aclEntryHandle->aclEntry->attributes();
    auto pktAction = GET_OPT_ATTR(AclEntry, ActionPacketAction, aclAttrs);

    if ((pktAction.getData() == SAI_PACKET_ACTION_FORWARD && dropUnencrypted) ||
        (pktAction.getData() == SAI_PACKET_ACTION_DROP && !dropUnencrypted)) {
      // Default data packet action needs to change soo first delete this
      // entry
      auto aclEntry = makeAclEntry(kMacsecDefaultAclPriority, aclName);
      managerTable_->aclTableManager().removeAclEntry(aclEntry, aclName);
      XLOG(DBG2) << "For linePort: " << linePort << ", direction "
                 << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                               : "Egress")
                 << " Removed default data packet entry";
      aclEntryHandle = nullptr;
    }
  }

  // If Acl Entry for the default data packet does not exist then add it
  if (!aclEntryHandle) {
    cfg::MacsecFlowPacketAction action = dropUnencrypted
        ? cfg::MacsecFlowPacketAction::DROP
        : cfg::MacsecFlowPacketAction::FORWARD;

    auto aclEntry = createMacsecRxDefaultAclEntry(
        kMacsecDefaultAclPriority, aclName, action);
    auto aclEntryId = managerTable_->aclTableManager().addAclEntry(
        aclEntry, aclName, nullptr /*state*/);
    XLOG(DBG2) << "For linePort " << linePort << " direction "
               << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                             : "Egress")
               << ", created macsec default packet rule "
               << (dropUnencrypted ? "Drop" : "Forward") << " ACL entry "
               << aclEntryId;
  }
}

/*
 * setupMacsecState
 *
 * This function:
 * 1. Creates Macsec (pipeline) object
 * 2. Create Macsec port (vPort) on the line port
 * 3. Create ACL table
 * 4. Create default ACL rules for LLDP, MKA and data packet as per
 *    dropUnencrypted value
 * 5. Bind ACL table to line port
 */
void SaiMacsecManager::setupMacsecState(
    PortID linePort,
    bool dropUnencrypted,
    sai_macsec_direction_t direction) {
  auto portHandle = managerTable_->portManager().getPortHandle(linePort);
  if (!portHandle) {
    throw FbossError(
        "Failed to get portHandle for non-existent linePort: ", linePort);
  }

  // 1. Creates Macsec (pipeline) object
  setupMacsecPipeline(linePort, direction);

  // 2. Create Macsec port (vPort) on the line port
  setupMacsecPort(linePort, direction);

  // 3. Create the ACL table if it does not exist
  setupAclTable(linePort, direction);

  // Get the table handle and throw if it is not found
  std::string aclName = getAclName(linePort, direction);
  auto aclTable = managerTable_->aclTableManager().getAclTableHandle(aclName);
  if (!aclTable) {
    throw FbossError(
        fmt::format(
            "For linePort: {}, {:s} ACL table Not Found",
            static_cast<int>(linePort),
            (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                       : "Egress")));
  }

  // 4. Create default data packet rule as per dropUnencrypted value
  setupDropUnencryptedRule(linePort, dropUnencrypted, direction);

  // 5. Finally bind the ACL table to the line port (no need to do any prior
  // check for this)
  AclTableSaiId aclTableId = aclTable->aclTable->adapterKey();
  if (direction == SAI_MACSEC_DIRECTION_INGRESS) {
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::IngressMacSecAcl{aclTableId});
  } else {
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::EgressMacSecAcl{aclTableId});
  }

  XLOG(DBG2) << "To linePort " << linePort << ", bound macsec "
             << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                           : "Egress")
             << " ACL table " << aclTableId;
}

/*
 * removeMacsecPipeline
 *
 * Helper function to remove the Macsec pipeline object from the port. After
 * this the port will send/receive plaintext packets
 */
void SaiMacsecManager::removeMacsecPipeline(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // If Macsec (pipeline) object exists then delete it
  if (getMacsecHandle(direction)) {
    removeMacsec(direction);
    XLOG(DBG2) << "For lineport: " << linePort << ", deleted macsec "
               << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                             : "Egress")
               << " Pipeline object";
  }
}

/*
 * removeMacsecVport
 *
 * Helper function to remove Macsec vPort from a line port. The line port still
 * remains in Macsec mode after removal of Macsec vPort
 */
void SaiMacsecManager::removeMacsecVport(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // If the macsec vPort exists for lineport then delete it
  if (getMacsecPortHandle(linePort, direction)) {
    // Put the port in Macsec bypass mode before deleting it
    auto macsecHandle = getMacsecHandle(direction);
    macsecHandle->macsec->setOptionalAttribute(
        SaiMacsecTraits::Attributes::PhysicalBypass{true});
    XLOG(DBG2) << "For "
               << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress"
                                                             : "Egress")
               << ", set macsec pipeline object w/ ID: " << macsecHandle
               << ", bypass enable as True";

    // Delete Macsec vPort
    removeMacsecPort(linePort, direction);
    XLOG(DBG2) << "For lineport: " << linePort << ", deleted macsec "
               << (direction == SAI_MACSEC_DIRECTION_INGRESS ? "Ingress vPort"
                                                             : "Egress vPort");
  }
}

/*
 * removeAclTable
 *
 * Helper function to delete the ACL table from the port ingress/egress stage
 */
void SaiMacsecManager::removeAclTable(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // Delete the default control packet and data packet rules in ACL table
  std::string aclTableName = getAclName(linePort, direction);
  auto aclTable =
      managerTable_->aclTableManager().getAclTableHandle(aclTableName);

  if (aclTable) {
    // Finally delete the ACL table
    state::AclTableFields aclTableFields{};
    aclTableFields.priority() = 0;
    aclTableFields.id() = aclTableName;
    auto table = std::make_shared<AclTable>(std::move(aclTableFields));
    managerTable_->aclTableManager().removeAclTable(
        table,
        direction == SAI_MACSEC_DIRECTION_INGRESS
            ? cfg::AclStage::INGRESS_MACSEC
            : cfg::AclStage::EGRESS_MACSEC,
        nullptr /*state*/);
    XLOG(DBG2) << "Removed ACL table " << aclTableName;
  }
}

/*
 * removeAclControlPacketRules
 *
 * Helper function to remove the control packet (MKA and LLDP) related ACL rule
 * from ACL table
 */
void SaiMacsecManager::removeAclControlPacketRules(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // Delete the default control packet and data packet rules in ACL table
  std::string aclTableName = getAclName(linePort, direction);
  auto aclTable =
      managerTable_->aclTableManager().getAclTableHandle(aclTableName);

  if (aclTable) {
    // Delete the control packet rules:
    // ACL table LLDP rule
    auto aclEntryHandle = managerTable_->aclTableManager().getAclEntryHandle(
        aclTable, kMacsecLldpAclPriority, aclTableName);
    if (aclEntryHandle) {
      auto aclEntry = makeAclEntry(kMacsecLldpAclPriority, aclTableName);
      managerTable_->aclTableManager().removeAclEntry(aclEntry, aclTableName);
      XLOG(DBG2) << "Removed LLDP ACL entry from ACL table " << aclTableName;
    }

    // MKA rule
    aclEntryHandle = managerTable_->aclTableManager().getAclEntryHandle(
        aclTable, kMacsecMkaAclPriority, aclTableName);
    if (aclEntryHandle) {
      auto aclEntry = makeAclEntry(kMacsecMkaAclPriority, aclTableName);
      managerTable_->aclTableManager().removeAclEntry(aclEntry, aclTableName);
      XLOG(DBG2) << "Removed MKA ACL entry from ACL table " << aclTableName;
    }
  }
}

/*
 * removeDropUnencryptedRule
 *
 * Helper function to remove the default data packet dropUnencrypted ACL rule.
 */
void SaiMacsecManager::removeDropUnencryptedRule(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // Delete the default control packet and data packet rules in ACL table
  std::string aclTableName = getAclName(linePort, direction);
  auto aclTable =
      managerTable_->aclTableManager().getAclTableHandle(aclTableName);

  if (aclTable) {
    // Remove the entry for default packet rule
    auto aclEntryHandle = managerTable_->aclTableManager().getAclEntryHandle(
        aclTable, kMacsecDefaultAclPriority, aclTableName);
    if (aclEntryHandle) {
      auto aclEntry = makeAclEntry(kMacsecDefaultAclPriority, aclTableName);
      managerTable_->aclTableManager().removeAclEntry(aclEntry, aclTableName);
      XLOG(DBG2) << "Removed default packet ACL entry from ACL table "
                 << aclTableName;
    }
  }
}

/*
 * removeMacsecState
 *
 * This function:
 * 1. Unbind ACL table from line Port
 * 2. Delete default ACL rules for LLDP, MKA and data packets
 * 3. Delete ACL table
 * 4. Remove Macsec vPort
 * 5. Remove Macsec (pipeline) object
 */
void SaiMacsecManager::removeMacsecState(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // 1. unbind acl tables from the line port
  auto portHandle = managerTable_->portManager().getPortHandle(linePort);
  if (direction == SAI_MACSEC_DIRECTION_INGRESS) {
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::IngressMacSecAcl{SAI_NULL_OBJECT_ID});
  } else {
    portHandle->port->setOptionalAttribute(
        SaiPortTraits::Attributes::EgressMacSecAcl{SAI_NULL_OBJECT_ID});
  }
  XLOG(DBG2) << "removeMacsecState: Unbound ACL table from line port "
             << linePort;

  // Remove the ACL related to SC (if any)
  removeScAcls(linePort, direction);

  // Delete the default control packet and data packet rules in ACL table
  std::string aclTableName = getAclName(linePort, direction);
  auto aclTable =
      managerTable_->aclTableManager().getAclTableHandle(aclTableName);

  if (aclTable) {
    // 2. Delete the control packet rules
    removeAclControlPacketRules(linePort, direction);

    // 2. Remove the entry for default packet rule
    removeDropUnencryptedRule(linePort, direction);

    // 3. Finally delete the ACL table
    removeAclTable(linePort, direction);
  }

  if (getMacsecHandle(direction)) {
    // 4. If the macsec vPort exists for lineport then delete it
    removeMacsecVport(linePort, direction);

    // 5. If Macsec (pipeline) object exists then delete it
    removeMacsecPipeline(linePort, direction);
  }
}

/* Perform the following:
  1. Delete the secure association
  2. If we have no more secure associations on this secure channel:
    * Delete the secure channel
    * Delete the macsec flow for the channel
    * Delete the acl entry for the channel
*/
void SaiMacsecManager::deleteMacsec(
    PortID linePort,
    const mka::MKASak& sak,
    const mka::MKASci& sci,
    sai_macsec_direction_t direction,
    bool skipHwUpdate) {
  std::string sciString =
      folly::to<std::string>(*sci.macAddress(), ".", *sci.port());
  // TODO(ccpowers): Break this back out into a helper method
  auto mac = folly::MacAddress(*sci.macAddress());
  auto scIdentifier = MacsecSecureChannelId(mac.u64NBO() | *sci.port());

  auto assocNum = *sak.assocNum() % 4;

  auto secureChannelHandle =
      getMacsecSecureChannelHandle(linePort, scIdentifier, direction);
  if (!secureChannelHandle) {
    throw FbossError(
        "Secure Channel not found when deleting macsec keys for SCI ",
        sciString,
        " for port: ",
        linePort);
  }

  auto secureAssoc =
      getMacsecSecureAssoc(linePort, scIdentifier, direction, assocNum);
  if (!secureAssoc) {
    throw FbossError(
        "Secure Association not found when deleting macsec keys for SCI ",
        sciString,
        "for port: ",
        linePort);
  }

  removeMacsecSecureAssoc(
      linePort, scIdentifier, direction, assocNum, skipHwUpdate);

  // If there's no SA's on this SC, remove the SC
  if (secureChannelHandle->secureAssocs.empty() && !skipHwUpdate) {
    removeMacsecSecureChannel(linePort, scIdentifier, direction);
  }
}

std::optional<MacsecSASaiId> SaiMacsecManager::getMacsecSaAdapterKey(
    PortID linePort,
    sai_macsec_direction_t direction,
    const mka::MKASci& sci,
    uint8_t assocNum) {
  auto mac = folly::MacAddress(*sci.macAddress());
  auto scIdentifier = MacsecSecureChannelId(mac.u64NBO() | *sci.port());

  auto secureChannelHandle =
      getMacsecSecureChannelHandle(linePort, scIdentifier, direction);
  if (!secureChannelHandle) {
    return std::nullopt;
  }

  auto secureAssoc =
      getMacsecSecureAssoc(linePort, scIdentifier, direction, assocNum);
  if (!secureAssoc) {
    return std::nullopt;
  }
  return secureAssoc->adapterKey();
}

void SaiMacsecManager::removeScAcls(
    PortID linePort,
    sai_macsec_direction_t direction) {
  // delete acl entries
  std::string aclName = getAclName(linePort, direction);
  auto aclTable = managerTable_->aclTableManager().getAclTableHandle(aclName);
  if (aclTable) {
    auto aclEntryHandle = managerTable_->aclTableManager().getAclEntryHandle(
        aclTable, kMacsecAclPriority, aclName);
    if (aclEntryHandle) {
      auto aclEntry = makeAclEntry(kMacsecAclPriority, aclName);
      managerTable_->aclTableManager().removeAclEntry(aclEntry, aclName);
      XLOG(DBG2) << "removeScAcls: Removed ACL entry from ACL table "
                 << aclName;
    }
  }
}

namespace {
void updateMacsecPortStats(
    const std::optional<mka::MacsecFlowStats>& prev,
    const mka::MacsecFlowStats& cur,
    mka::MacsecPortStats& portStats) {
  *portStats.inBadOrNoMacsecTagDroppedPkts() +=
      (utility::CounterPrevAndCur(
           prev ? *prev->inNoTagPkts() : 0L, *cur.inNoTagPkts())
           .incrementFromPrev() +
       utility::CounterPrevAndCur(
           prev ? *prev->inBadTagPkts() : 0L, *cur.inBadTagPkts())
           .incrementFromPrev());
  *portStats.inNoSciDroppedPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->noSciPkts() : 0L, *cur.noSciPkts())
          .incrementFromPrev();
  *portStats.inUnknownSciPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->unknownSciPkts() : 0L, *cur.unknownSciPkts())
          .incrementFromPrev();
  *portStats.inOverrunDroppedPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->overrunPkts() : 0L, *cur.overrunPkts())
          .incrementFromPrev();

  *portStats.outTooLongDroppedPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->outTooLongPkts() : 0L, *cur.untaggedPkts())
          .incrementFromPrev();
  *portStats.noMacsecTagPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->untaggedPkts() : 0L, *cur.untaggedPkts())
          .incrementFromPrev();
}

void updateMacsecPortStats(
    const std::optional<mka::MacsecSaStats>& prev,
    const mka::MacsecSaStats& cur,
    mka::MacsecPortStats& portStats) {
  *portStats.octetsEncrypted() +=
      utility::CounterPrevAndCur(
          prev ? *prev->octetsEncrypted() : 0L, *cur.octetsEncrypted())
          .incrementFromPrev();
  *portStats.inDelayedPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->inDelayedPkts() : 0L, *cur.inDelayedPkts())
          .incrementFromPrev();
  *portStats.inLateDroppedPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->inLatePkts() : 0L, *cur.inLatePkts())
          .incrementFromPrev();
  *portStats.inNotValidDroppedPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->inNotValidPkts() : 0L, *cur.inNotValidPkts())
          .incrementFromPrev();
  *portStats.inInvalidPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->inInvalidPkts() : 0L, *cur.inInvalidPkts())
          .incrementFromPrev();
  *portStats.inNoSaDroppedPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->inNoSaPkts() : 0L, *cur.inNoSaPkts())
          .incrementFromPrev();
  *portStats.inUnusedSaPkts() +=
      utility::CounterPrevAndCur(
          prev ? *prev->inUnusedSaPkts() : 0L, *cur.inUnusedSaPkts())
          .incrementFromPrev();
}
} // namespace
void SaiMacsecManager::updateStats(PortID port, HwPortStats& portStats) {
  for (const auto& dirAndMacsec : macsecHandles_) {
    const auto& [direction, macsec] = dirAndMacsec;
    auto pitr = macsec->ports.find(port);
    if (pitr == macsec->ports.end()) {
      continue;
    }
    auto& macsecPort = *pitr;
    if (!portStats.macsecStats()) {
      portStats.macsecStats() = MacsecStats{};
    }
    auto& macsecStats = *portStats.macsecStats();
    auto& macsecPortStats = direction == SAI_MACSEC_DIRECTION_INGRESS
        ? *macsecStats.ingressPortStats()
        : *macsecStats.egressPortStats();
    // Record flow, SA stats into a new map. We will then use this to
    // updated macsecStats flow stats map. This will take care of
    // removing any flows that have been deleted since last stat
    // collection
    std::vector<MacsecSciFlowStats> newFlowStats;
    std::vector<MacsecSaIdSaStats> newSaStats;
    auto& flowStats = direction == SAI_MACSEC_DIRECTION_INGRESS
        ? *macsecStats.ingressFlowStats()
        : *macsecStats.egressFlowStats();
    auto& saStats = direction == SAI_MACSEC_DIRECTION_INGRESS
        ? *macsecStats.rxSecureAssociationStats()
        : *macsecStats.txSecureAssociationStats();
    for (const auto& macsecSc : macsecPort.second->secureChannels) {
      MacsecSecureChannelId secureChannelId = macsecSc.first;
      mka::MKASci sci;
      sci.macAddress() =
          folly::MacAddress::fromNBO((secureChannelId >> 16) << 16).toString();
      sci.port() = secureChannelId & 0xFF;
      for (const auto& macsecSa : macsecSc.second->secureAssocs) {
        macsecSa.second->updateStats<SaiMacsecSATraits>();
        mka::MKASecureAssociationId saId;
        uint8_t assocNum = macsecSa.first;
        saId.sci() = sci;
        saId.assocNum() = assocNum;

        // Find saStats for a given sa
        auto findSaStats = [](const std::vector<MacsecSaIdSaStats>& saStats,
                              mka::MKASecureAssociationId sa)
            -> std::optional<mka::MacsecSaStats> {
          for (const auto& saStat : saStats) {
            if (saStat.saId().value() == sa) {
              return saStat.saStats().value();
            }
          }
          return std::nullopt;
        };

        auto prevSaStats = findSaStats(saStats, saId);
        auto curSaStats = fillSaStats(macsecSa.second->getStats(), direction);
        updateMacsecPortStats(prevSaStats, curSaStats, macsecPortStats);

#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
        // Fill in the Current Packet Number for ingress and egress SA
        // Roll up these XPN to port level stats. The port level XPN reflects
        // latest installed SA's XPN
        auto saHandle = macsecSa.second->adapterKey();
        auto currXpn = SaiApiTable::getInstance()->macsecApi().getAttribute(
            MacsecSASaiId(saHandle),
            SaiMacsecSATraits::Attributes::CurrentXpn());
        if (direction == SAI_MACSEC_DIRECTION_INGRESS) {
          curSaStats.inCurrentXpn() = currXpn;
          if (macsecSc.second->latestSaAn == assocNum) {
            *macsecPortStats.inCurrentXpn() = currXpn;
          }
        } else {
          curSaStats.outCurrentXpn() = currXpn;
          *macsecPortStats.outCurrentXpn() = currXpn;
        }
#endif

        MacsecSaIdSaStats singleSaStats;
        singleSaStats.saId() = saId;
        singleSaStats.saStats() = curSaStats;

        newSaStats.push_back(singleSaStats);
      }
      macsecSc.second->flow->updateStats<SaiMacsecFlowTraits>();
      auto curFlowStats =
          fillFlowStats(macsecSc.second->flow->getStats(), direction);

      // Find flowStats for a given sci
      auto findFlowStats =
          [](const std::vector<MacsecSciFlowStats>& flowStats,
             mka::MKASci sci) -> std::optional<mka::MacsecFlowStats> {
        for (const auto& flowStat : flowStats) {
          if (flowStat.sci().value() == sci) {
            return flowStat.flowStats().value();
          }
        }
        return std::nullopt;
      };

      auto prevFlowStats = findFlowStats(flowStats, sci);
      updateMacsecPortStats(prevFlowStats, curFlowStats, macsecPortStats);

      MacsecSciFlowStats singleFlowStats;
      singleFlowStats.sci() = sci;
      singleFlowStats.flowStats() = curFlowStats;
      newFlowStats.push_back(singleFlowStats);
    }
    flowStats = std::move(newFlowStats);
    saStats = std::move(newSaStats);
    // Port stats
    macsecPort.second->port->updateStats<SaiMacsecPortTraits>();
    fillHwPortStats(macsecPort.second->port->getStats(), macsecPortStats);

    // ACL counters for default rule on ingress and egress
    std::string aclTblName = getAclName(port, direction);
    auto aclTable =
        managerTable_->aclTableManager().getAclTableHandle(aclTblName);
    if (aclTable) {
      // Lambda to get the ACL counter value
      auto getAclCounter =
          [this, &aclTable, &aclTblName](int prio) -> uint64_t {
        auto aclEntryHandle =
            managerTable_->aclTableManager().getAclEntryHandle(
                aclTable, prio, aclTblName);
        if (aclEntryHandle) {
          auto aclEntryId = aclEntryHandle->aclEntry->adapterKey();
          auto aclCounterIdGot =
              SaiApiTable::getInstance()
                  ->aclApi()
                  .getAttribute(
                      AclEntrySaiId(aclEntryId),
                      SaiAclEntryTraits::Attributes::ActionCounter())
                  .getData();
          auto counterPackets =
              SaiApiTable::getInstance()->aclApi().getAttribute(
                  AclCounterSaiId(aclCounterIdGot),
                  SaiAclCounterTraits::Attributes::CounterPackets());
          return counterPackets;
        }
        XLOG(ERR) << fmt::format(
            "ACL entry does not exist for priority {:d}", prio);
        return 0;
      };
      auto defaultAclCount = getAclCounter(kMacsecDefaultAclPriority);
      XLOG(DBG5) << fmt::format(
          "ACL counter Macsec default = {:d}", defaultAclCount);

      if (direction == SAI_MACSEC_DIRECTION_INGRESS) {
        auto& macsecAclStats = *macsecStats.ingressAclStats();
        macsecAclStats.defaultAclStats() = defaultAclCount;
      } else {
        auto& macsecAclStats = *macsecStats.egressAclStats();
        macsecAclStats.defaultAclStats() = defaultAclCount;
      }
    }
  }
}
} // namespace facebook::fboss
