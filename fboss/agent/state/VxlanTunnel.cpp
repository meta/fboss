// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/state/VxlanTunnel.h"

namespace facebook::fboss {

template struct ThriftStructNode<VxlanTunnel, state::VxLanTunnelFields>;

} // namespace facebook::fboss
