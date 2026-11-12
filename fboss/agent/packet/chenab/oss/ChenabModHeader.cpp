// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/packet/chenab/ChenabModHeader.h"

#include "fboss/agent/packet/HdrParseError.h"

namespace facebook::fboss {

// NOLINTNEXTLINE(clang-diagnostic-missing-noreturn)
ChenabTacEvent parseChenabTacEvent(folly::io::Cursor&) {
  throw HdrParseError("Mirror-on-drop packet decoding is unavailable in OSS");
}

// NOLINTNEXTLINE(clang-diagnostic-missing-noreturn)
ChenabModPacket parseChenabModPacket(folly::io::Cursor&) {
  throw HdrParseError("Mirror-on-drop packet decoding is unavailable in OSS");
}

// NOLINTNEXTLINE(clang-diagnostic-missing-noreturn)
ChenabModPacket parseChenabModPacket(
    folly::io::Cursor&,
    ChenabModOuterHeader&) {
  throw HdrParseError("Mirror-on-drop packet decoding is unavailable in OSS");
}

// NOLINTNEXTLINE(clang-diagnostic-missing-noreturn)
ChenabModOuterHeader parseChenabModOuterHeader(folly::io::Cursor) {
  throw HdrParseError("Mirror-on-drop packet decoding is unavailable in OSS");
}

} // namespace facebook::fboss
