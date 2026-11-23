// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "fboss/agent/state/InterfaceVrf.h"
#include "fboss/agent/state/NodeBase-defs.h"

namespace facebook::fboss {

template struct ThriftStructNode<InterfaceVrf, state::InterfaceVrfFields>;

} // namespace facebook::fboss
