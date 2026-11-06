// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/state/NodeBase-defs.h"

namespace facebook::fboss {

template struct ThriftStructNode<Vrf, state::VrfFields>;

} // namespace facebook::fboss
