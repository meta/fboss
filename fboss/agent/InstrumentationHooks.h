// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

namespace facebook::fboss {

// Start and stop optional test-coverage collection. Each does nothing unless a
// collector is linked in; see InstrumentationHookEntryPoints.h.
void startCoverageCollection() noexcept;
void stopCoverageCollection() noexcept;

} // namespace facebook::fboss
