// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/led_service/BspLedManager.h"

namespace facebook::fboss {

class M4061nhpLedManager : public BspLedManager {
 public:
  M4061nhpLedManager();
  ~M4061nhpLedManager() override = default;

  // Forbidden copy and move constructors and assignment operators
  M4061nhpLedManager(M4061nhpLedManager const&) = delete;
  M4061nhpLedManager& operator=(M4061nhpLedManager const&) = delete;
  M4061nhpLedManager(M4061nhpLedManager&&) = delete;
  M4061nhpLedManager& operator=(M4061nhpLedManager&&) = delete;
};

} // namespace facebook::fboss
