// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

// Entry points that an optional test-coverage collector defines. FBOSS code
// calls them only through the wrappers in InstrumentationHooks.h, which skip
// any entry point that no linked library defines.
//
// A binary may link at most one collector. It must be a direct dependency of
// the cpp_binary, with link_whole = True and preferred_linkage = "static", and
// define the entry points against these declarations. A second collector then
// fails the link with a duplicate symbol.
extern "C" {
// Runs before command-line flags and logging are initialized.
void fbossStartCoverageCollection() noexcept;
// Writes out the coverage collected so far. It may run more than once in a
// process, so it must be idempotent, and may run without a prior start or
// while other threads are still running.
void fbossStopCoverageCollection() noexcept;
}
