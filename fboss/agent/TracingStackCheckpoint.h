// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>

// Keeps the guard itself out of the trace. It only takes effect in builds
// instrumented with XRay.
#if defined(__clang__)
#define FBOSS_TRACING_NEVER_INSTRUMENT [[clang::xray_never_instrument]]
#else
#define FBOSS_TRACING_NEVER_INSTRUMENT
#endif

namespace facebook::fboss {

// Scoped guard for a blocking RPC. An RPC that is cut short, for example by an
// exception, can leave a function tracer's per-thread shadow call stack, such
// as XRay's, unbalanced. Restoring a checkpoint taken before the call
// rebalances it. The guard does nothing unless a collector defining the
// checkpoint entry points is linked in.
class TracingStackCheckpoint {
 public:
  FBOSS_TRACING_NEVER_INSTRUMENT TracingStackCheckpoint() noexcept;
  FBOSS_TRACING_NEVER_INSTRUMENT ~TracingStackCheckpoint();
  // Deleting the moves also deletes the copies.
  TracingStackCheckpoint(TracingStackCheckpoint&&) = delete;
  TracingStackCheckpoint& operator=(TracingStackCheckpoint&&) = delete;

 private:
  // Tracked apart from the token, which may be any value, including 0.
  bool captured_{false};
  uint64_t token_{0};
};

} // namespace facebook::fboss
