// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/show/interface/transceiver/eeprom/CmdShowInterfaceTransceiverEeprom.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

namespace facebook::fboss {

class CmdShowInterfaceTransceiverEepromTestFixture : public CmdHandlerTestBase {
};

TEST_F(CmdShowInterfaceTransceiverEepromTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowInterfaceTransceiverEepromTraits::description().empty());
  EXPECT_FALSE(CmdShowInterfaceTransceiverEeprom::sampleModel().empty());
}

} // namespace facebook::fboss
