// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/led_service/FsdbSwitchStateSubscriber.h"
#include "fboss/fsdb/if/FsdbModel.h" // NOLINT(misc-include-cleaner)

namespace {
const thriftpath::RootThriftPath<facebook::fboss::fsdb::FsdbOperStateRoot>
    stateRoot;
} // namespace

namespace facebook::fboss {

std::vector<std::string> FsdbSwitchStateSubscriber::getSwitchStatePath() {
  return stateRoot.agent().switchState().tokens();
}
std::vector<std::string> FsdbSwitchStateSubscriber::getTransceiverStatePath() {
  return stateRoot.qsfp_service().state().tcvrStates().tokens();
}

} // namespace facebook::fboss
