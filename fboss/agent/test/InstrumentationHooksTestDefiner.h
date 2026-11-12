// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

namespace facebook::fboss {

// Calls recorded by the test-only definitions of the instrumentation hooks.
struct InstrumentationHookCalls {
  int starts{0};
  int stops{0};
};

// Defined by the test that links the definer.
InstrumentationHookCalls& instrumentationHookCalls();

} // namespace facebook::fboss
