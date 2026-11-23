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

#include "fboss/agent/TxPacket.h"

#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"

namespace facebook::fboss {

class AgentQosTestBase : public AgentHwTest {
 protected:
  void verifyDscpQueueMapping(
      const std::map<int, std::vector<uint8_t>>& queueToDscp);

  void sendPacket(uint8_t dscp, bool frontPanel);
};

} // namespace facebook::fboss
