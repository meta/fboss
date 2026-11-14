// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/led_service/BspLedManager.h"

namespace facebook::fboss {

class M4340nhpLedManager : public BspLedManager {
 public:
  M4340nhpLedManager();
  ~M4340nhpLedManager() override = default;

  // Forbidden copy and move constructors and assignment operators
  M4340nhpLedManager(M4340nhpLedManager const&) = delete;
  M4340nhpLedManager& operator=(M4340nhpLedManager const&) = delete;
  M4340nhpLedManager(M4340nhpLedManager&&) = delete;
  M4340nhpLedManager& operator=(M4340nhpLedManager&&) = delete;
};

} // namespace facebook::fboss
