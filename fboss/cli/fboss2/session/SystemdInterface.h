/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string>
#include <vector>

namespace facebook::fboss {

/**
 * Interface for interacting with systemd services.
 * This class provides methods to manage systemd services and can be
 * subclassed for mocking in unit tests.
 * Uses folly::Subprocess to execute systemctl commands.
 */
class SystemdInterface {
 public:
  SystemdInterface() = default;
  virtual ~SystemdInterface() = default;

  /**
   * Check if a systemd service is enabled.
   * @param serviceName The name of the service (e.g., "fboss_sw_agent")
   * @return true if the service is enabled, false otherwise
   */
  virtual bool isServiceEnabled(const std::string& serviceName);

  /** Return loaded systemd service units matching a systemd glob. */
  virtual std::vector<std::string> getMatchingServices(
      const std::string& pattern);

  /**
   * Stop a systemd service.
   * @param serviceName The name of the service to stop
   * @throws std::runtime_error if the operation fails
   */
  virtual void stopService(const std::string& serviceName);

  /**
   * Start a systemd service.
   * @param serviceName The name of the service to start
   * @throws std::runtime_error if the operation fails
   */
  virtual void startService(const std::string& serviceName);

  /**
   * Restart a systemd service.
   * @param serviceName The name of the service to restart
   * @throws std::runtime_error if the operation fails
   */
  virtual void restartService(const std::string& serviceName);

  /**
   * Check if a systemd service is active.
   * @param serviceName The name of the service to check
   * @return true if the service is active, false otherwise
   */
  virtual bool isServiceActive(const std::string& serviceName);

  /**
   * Wait for a systemd service to become active.
   * @param serviceName The name of the service to wait for
   * @param maxWaitSeconds Maximum time to wait in seconds
   * @param pollIntervalMs Polling interval in milliseconds
   * @throws std::runtime_error if the service doesn't become active within the
   * timeout
   */
  virtual void waitForServiceActive(
      const std::string& serviceName,
      int maxWaitSeconds = 60,
      int pollIntervalMs = 500);

  /**
   * Wait for a systemd service to stop being active.
   *
   * `systemctl stop` already blocks until the unit's stop job completes, so
   * this is a defensive barrier rather than the primary one: it catches the
   * case where the unit was stopped by something other than our own
   * stopService() call, or where the job was still settling.
   *
   * Note this cannot distinguish a clean exit from a SIGKILL after
   * TimeoutStopSec: both land the unit in a not-active state. Callers that
   * care whether the process shut down gracefully must check for whatever
   * artifact a graceful shutdown leaves behind.
   *
   * @param serviceName The name of the service to wait for
   * @param maxWaitSeconds Maximum time to wait in seconds
   * @param pollIntervalMs Polling interval in milliseconds
   * @throws std::runtime_error if the service is still active after the
   * timeout
   */
  virtual void waitForServiceInactive(
      const std::string& serviceName,
      int maxWaitSeconds = 60,
      int pollIntervalMs = 500);
};

} // namespace facebook::fboss
