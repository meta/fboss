/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/platforms/wedge/WedgePlatform.h"

#include <folly/Memory.h>

#include "fboss/agent/SysError.h"
#include "fboss/agent/hw/bcm/BcmAPI.h"
#include "fboss/agent/hw/bcm/BcmConfig.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmWarmBootHelper.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/platforms/wedge/WedgePort.h"
#include "fboss/agent/platforms/wedge/WedgePortMapping.h"
#include "fboss/agent/state/Port.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"
#include "fboss/lib/usb/UsbError.h"
#include "fboss/lib/usb/WedgeI2CBus.h"

DEFINE_string(
    fabric_location,
    "",
    "Provides location of fabric , LEFT or RIGHT");

using folly::MacAddress;
using std::make_unique;
using std::string;

namespace facebook::fboss {

WedgePlatform::WedgePlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping,
    folly::MacAddress localMac)
    : BcmPlatform(
          std::move(productInfo),
          std::move(platformMapping),
          localMac) {}

void WedgePlatform::initImpl(uint32_t hwFeaturesDesired) {
  if (getAsic()->isSupported(HwAsic::Feature::HSDK)) {
    BcmAPI::initHSDK(loadYamlConfig());
  } else {
    BcmAPI::init(loadConfig());
  }
  hw_.reset(new BcmSwitch(this, hwFeaturesDesired));
}

WedgePlatform::~WedgePlatform() {
  BcmAPI::shutdown();
  hw_.reset();
}

void WedgePlatform::initPorts() {
  portMapping_ = createPortMapping();
}

WedgePlatform::BcmPlatformPortMap WedgePlatform::getPlatformPortMap() {
  if (portMapping_ == nullptr) {
    throw FbossError(
        "Platform port mapping is empty, need to initPorts() first");
  }

  BcmPlatformPortMap mapping;
  for (const auto& kv : *portMapping_) {
    bcm_port_t id = kv.first;
    mapping[id] = kv.second.get();
  }
  return mapping;
}

void WedgePlatform::onHwInitialized(HwSwitchCallback* sw) {
  // could populate with initial ports here, but should get taken care
  // of through state changes sent to the stateUpdated method.
  initLEDs();
  // Make sure the initial status of the LEDs reflects the current port
  // settings.
  for (const auto& entry : *portMapping_) {
    // As some platform allows add/remove port at the first time applying the
    // config, we should skip those ports which doesn't exist in hw.
    if (!hw_->getPortTable()->getBcmPortIf(entry.first)) {
      continue;
    }
    bool up = hw_->isPortUp(entry.first);
    entry.second->linkStatusChanged(up, true);
  }
  /*
   * In multiswitch mode, the platform is part hw agent
   * binary and has no access to swswitch. Skip state
   * observer registration. The state updates will be
   * propagated to platform as part of oper delta sync
   * from sw agent to hw agent
   */
  if (sw) {
    sw->registerStateObserver(this, "WedgePlatform");
  }
}

void WedgePlatform::stateUpdated(const StateDelta& delta) {
  updatePorts(delta);
}

void WedgePlatform::updatePorts(const StateDelta& delta) {
  for (const auto& entry : delta.getPortsDelta()) {
    const auto newPort = entry.getNew();
    if (newPort) {
      getPort(newPort->getID())->portChanged(newPort);
    }
  }
}

HwSwitch* WedgePlatform::getHwSwitch() const {
  return hw_.get();
}

void WedgePlatform::onUnitCreate(int unit) {
  warmBootHelper_ = std::make_unique<BcmWarmBootHelper>(
      unit, getDirectoryUtil()->getWarmBootDir());
}

void WedgePlatform::onUnitAttach(int /*unit*/) {}

void WedgePlatform::preWarmbootStateApplied() {}

std::unique_ptr<BaseWedgeI2CBus> WedgePlatform::getI2CBus() {
  return make_unique<WedgeI2CBus>();
}

TransceiverIdxThrift WedgePlatform::getPortMapping(
    PortID portId,
    cfg::PortProfileID /* profileID */) const {
  return getPort(portId)->getTransceiverMapping();
}

WedgePort* WedgePlatform::getPort(PortID id) const {
  return portMapping_->getPort(id);
}
std::vector<WedgePort*> WedgePlatform::getPortsByTransceiverID(
    TransceiverID id) const {
  return portMapping_->getPortsByTransceiverID(id);
}
PlatformPort* WedgePlatform::getPlatformPort(const PortID port) const {
  return getPort(port);
}

std::map<std::string, std::string> WedgePlatform::loadConfig() {
  auto cfg = config();
  if (cfg) {
    return *cfg->thrift.platform()->chip()->bcm()->config();
  }
  return BcmConfig::loadDefaultConfig();
}

std::string WedgePlatform::loadYamlConfig() {
  auto cfg = config();
  if (!cfg) {
    // TODO(josep5wu) Need to revisit whether we should generate a default
    // yaml config just like WedgePlatform::loadConfig()
    throw FbossError("Failed to get agent config");
  }
  if (auto yamlConfig = cfg->thrift.platform()->chip()->bcm()->yamlConfig()) {
    return *yamlConfig;
  }
  throw FbossError("Failed to get bcm yaml config from agent config");
}

void WedgePlatform::stateChanged(const StateDelta& delta) {
  updatePorts(delta);
}

void WedgePlatform::stopThreads() {
  if (hw_) {
    hw_->stopHighFrequencyStatsThread();
  }
}

} // namespace facebook::fboss
