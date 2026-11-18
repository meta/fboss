// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiTamManager.h"

namespace facebook::fboss {

void SaiTamManager::addMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& /* report */) {}

void SaiTamManager::removeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& /* report */) {}

void SaiTamManager::changeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& /* oldReport */,
    const std::shared_ptr<MirrorOnDropReport>& /* newReport */) {}

std::vector<PortID> SaiTamManager::getAllMirrorOnDropPortIds() {
  return {};
}

} // namespace facebook::fboss
