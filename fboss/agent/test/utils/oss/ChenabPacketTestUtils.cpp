// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/test/utils/PacketTestUtils.h"

namespace facebook::fboss::utility {

bool isChenabMirrorOnDropPacket(folly::io::Cursor&) {
  // The Chenab MOD decoder is not available in OSS builds.
  return false;
}

} // namespace facebook::fboss::utility
