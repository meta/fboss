// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/utils/SessionPortMap.h"

#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss::utils {

SessionPortMap::SessionPortMap()
    : portMap_(ConfigSession::getInstance().getAgentConfig()) {}

} // namespace facebook::fboss::utils
