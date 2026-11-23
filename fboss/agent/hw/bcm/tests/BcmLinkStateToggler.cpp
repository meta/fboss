/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/bcm/tests/BcmLinkStateToggler.h"

#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/tests/BcmSwitchEnsemble.h"
#include "fboss/agent/test/TestEnsembleIf.h"

#include <memory>

extern "C" {
#include <bcm/port.h>
}

namespace facebook::fboss {

BcmSwitch* BcmLinkStateToggler::getHw() {
  return static_cast<BcmSwitchEnsemble*>(getHwSwitchEnsemble())->getHwSwitch();
}

void BcmLinkStateToggler::invokeLinkScanIfNeeded(PortID port, bool isUp) {
  auto* platform = dynamic_cast<BcmPlatform*>(getHw()->getPlatform());
  CHECK(platform);
  if (!platform->hasLinkScanCapability()) {
    /* If the platform doesn't support link-scan, mimic linkscan by
     * specifically invoking hwSwitch's link scan callback. */
    bcm_port_info_t portInfo;
    portInfo.linkstatus =
        isUp ? BCM_PORT_LINK_STATUS_UP : BCM_PORT_LINK_STATUS_DOWN;
    getHw()->linkscanCallback(getHw()->getUnit(), port, &portInfo);
  }
}

} // namespace facebook::fboss
