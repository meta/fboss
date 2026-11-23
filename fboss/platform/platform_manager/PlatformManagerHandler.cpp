// Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
// All Rights Reserved.

#include "fboss/platform/platform_manager/PlatformManagerHandler.h"

#include "fboss/platform/platform_manager/ExplorationErrors.h"
#include "fboss/platform/platform_manager/PlatformSnapshotBuilder.h"
#include "fboss/platform/platform_manager/Utils.h"

namespace facebook::fboss::platform::platform_manager {
namespace {
// exploreSlot() records a slot as absent before checking presence, and leaves
// it that way if the check throws. A stored absence for such a slot is really
// unknown, so it must not be served as confirmed absence.
// TODO: Record no PresenceInfo when the check throws, then remove this.
bool hasPresenceCheckError(
    const PlatformManagerStatus& pmStatus,
    const std::string& slotPath) {
  const auto& failedDevices = pmStatus.failedDevices();
  if (!failedDevices) {
    return false;
  }
  auto it = failedDevices->find(Utils().createDevicePath(slotPath, "<ABSENT>"));
  if (it == failedDevices->end()) {
    return false;
  }
  return std::any_of(
      it->second.begin(), it->second.end(), [](const auto& explorationError) {
        return *explorationError.errorType() ==
            toExplorationErrorTypeStr(
                ExplorationErrorType::SLOT_PRESENCE_CHECK);
      });
}
} // namespace

PlatformManagerHandler::PlatformManagerHandler(
    const PlatformExplorer& platformExplorer,
    const DataStore& dataStore,
    const PlatformConfig& config)
    : platformExplorer_(platformExplorer),
      dataStore_(dataStore),
      platformConfig_(config) {}

void PlatformManagerHandler::getPlatformSnapshot(PlatformSnapshot& response) {
  response = PlatformSnapshotBuilder(platformConfig_, dataStore_).build();
}

void PlatformManagerHandler::getLastPMStatus(PlatformManagerStatus& pmStatus) {
  pmStatus = platformExplorer_.getPMStatus();
}

void PlatformManagerHandler::getPmUnitInfo(
    PmUnitInfoResponse& pmUnitInfoResp,
    std::unique_ptr<PmUnitInfoRequest> pmUnitInfoReq) {
  auto pmStatus = platformExplorer_.getPMStatus();
  // TODO: 1) Retry on in progress 2) Ensure PM isn't running forever.
  if (*pmStatus.explorationStatus() == ExplorationStatus::IN_PROGRESS) {
    auto error = PlatformManagerError();
    error.errorCode() = PlatformManagerErrorCode::EXPLORATION_NOT_SUCCEEDED;
    error.message() = fmt::format(
        "PM exploration status is {}, not SUCCEEDED",
        static_cast<int>(*pmStatus.explorationStatus()));
    throw error;
  }
  if (hasPresenceCheckError(pmStatus, *pmUnitInfoReq->slotPath())) {
    auto error = PlatformManagerError();
    error.errorCode() = PlatformManagerErrorCode::EXPLORATION_NOT_SUCCEEDED;
    error.message() = fmt::format(
        "Presence check failed for SlotPath {}", *pmUnitInfoReq->slotPath());
    throw error;
  }
  try {
    auto pmUnitInfo =
        platformExplorer_.getPmUnitInfo(*pmUnitInfoReq->slotPath());
    pmUnitInfoResp.pmUnitInfo() = pmUnitInfo;
  } catch (std::exception&) {
    auto error = PlatformManagerError();
    error.errorCode() = PlatformManagerErrorCode::PM_UNIT_NOT_FOUND;
    error.message() = fmt::format(
        "Unable to get PmUnitInfo. Reason: Invalid SlotPath {}. No PmUnit was explored",
        *pmUnitInfoReq->slotPath());
    throw error;
  }
}

void PlatformManagerHandler::getAllPmUnits(PmUnitsResponse& response) {
  response.pmUnits() = dataStore_.getSlotPathToPmUnitInfo();
}

void PlatformManagerHandler::getBspVersion(
    BspVersionResponse& bspVersionResponse) {
  bspVersionResponse.bspBaseName() = *platformConfig_.bspKmodsRpmName();
  bspVersionResponse.bspVersion() = *platformConfig_.bspKmodsRpmVersion();
  bspVersionResponse.kernelVersion() = system_.getHostKernelVersion();
}

void PlatformManagerHandler::getPlatformName(std::string& response) {
  response = *platformConfig_.platformName();
}

void PlatformManagerHandler::getEepromContents(
    EepromContentResponse& response,
    std::unique_ptr<PmUnitInfoRequest> req) {
  std::string slotPath = *req->slotPath();
  if (slotPath == "") {
    slotPath = *platformConfig_.chassisEepromDevicePath();
  }
  try {
    response.eepromContents() =
        dataStore_.getEepromContents(slotPath).getEepromContents();
  } catch (std::exception&) {
    auto error = PlatformManagerError();
    error.errorCode() = PlatformManagerErrorCode::EEPROM_CONTENTS_NOT_FOUND;
    error.message() = fmt::format(
        "Unable to get EepromContents. Reason: Invalid SlotPath {}.", slotPath);
    throw error;
  }
}

void PlatformManagerHandler::getFirmwareVersions(
    FirmwareVersionsResponse& response) {
  auto versions = dataStore_.getFirmwareVersions();
  response.firmwareVersions() =
      std::map<std::string, std::string>(versions.begin(), versions.end());
}
} // namespace facebook::fboss::platform::platform_manager
