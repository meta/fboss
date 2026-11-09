/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"

#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/phy/PhyManager.h"
#include "fboss/qsfp_service/PortManager.h"
#include "fboss/qsfp_service/QsfpServer.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManagerInit.h"

#include <thrift/lib/cpp2/server/ThriftServer.h>

DEFINE_bool(
    setup_thrift,
    false,
    "Setup a thrift handler. Primarily useful for inspecting HW state,"
    "say for debugging things via a shell");

namespace facebook::fboss {

WedgeManager* HwQsfpEnsemble::getWedgeManager() {
  return static_cast<WedgeManager*>(
      qsfpServiceHandler_->getTransceiverManager());
}
void HwQsfpEnsemble::init() {
  // Since TransceiverManager::TransceiverManager() can decide whether
  // it should use cold boot and warm boot logic based on the warm boot
  // related files.
  // Eventually we should expect qsfp_service to use cold boot by default if
  // can_warm_boot flag is not set, but because this warm boot support needs
  // several push to fully roll out. We'll keep this setting force cold boot
  // file logic for now.
  // TODO(joseph5wu) Remove the specifically setting force cold boot file logic
  // once we roll out can_warm_boot everywhere
  if (!checkFileExists(TransceiverManager::warmBootFlagFileName())) {
    // Force a cold boot
    createDir(FLAGS_qsfp_service_volatile_dir);
    auto fd = createFile(TransceiverManager::forceColdBootFileName());
    close(fd);
  }

  auto [tcvrManager, portManager] = createQsfpManagers();
  std::tie(server_, qsfpServiceHandler_) =
      setupThriftServer(std::move(tcvrManager), std::move(portManager));
}

HwQsfpEnsemble::~HwQsfpEnsemble() {
  if (FLAGS_setup_thrift) {
    doServerLoop(server_, qsfpServiceHandler_);
  }
}

void HwQsfpEnsemble::setupForWarmboot() {
  // Leave TransceiverManager::gracefulExit() to handle setting up the correct
  // warm boot files.
  qsfpServiceHandler_->gracefulExit();
  if (isSaiPlatform()) {
    if (auto* portManager = qsfpServiceHandler_->getPortManager()) {
      portManager->releasePhyManager();
    } else {
      getWedgeManager()->releasePhyManager();
    }
  }
}

PhyManager* HwQsfpEnsemble::getPhyManager() {
  return qsfpServiceHandler_->getPhyManager();
}

const PlatformMapping* HwQsfpEnsemble::getPlatformMapping() const {
  return getWedgeManager()->getPlatformMapping();
}

phy::ExternalPhy* HwQsfpEnsemble::getExternalPhy(PortID port) {
  auto phyManager = getPhyManager();
  return phyManager->getExternalPhy(port);
}

bool HwQsfpEnsemble::isXphyPlatform() const {
  static const std::set<PlatformType> xphyPlatforms = {
      PlatformType::PLATFORM_MINIPACK,
      PlatformType::PLATFORM_YAMP,
      PlatformType::PLATFORM_FUJI,
      PlatformType::PLATFORM_ELBERT};

  return xphyPlatforms.find(getWedgeManager()->getPlatformType()) !=
      xphyPlatforms.end();
}

bool HwQsfpEnsemble::isSaiPlatform() const {
  static const std::set<PlatformType> saiPlatforms = {
      PlatformType::PLATFORM_ELBERT,
      PlatformType::PLATFORM_LADAKH800BCLS,
      PlatformType::PLATFORM_LEH800BCLS};

  return saiPlatforms.find(getWedgeManager()->getPlatformType()) !=
      saiPlatforms.end();
}

} // namespace facebook::fboss
