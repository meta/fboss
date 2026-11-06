/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/**
 * Delta ORv3 PSU Firmware Update Tool
 * ====================================
 *
 * This tool updates firmware on Delta ORv3 PSUs using the MEI (Modbus
 * Encapsulated Interface) protocol over Modbus/RTU.
 *
 * MEI PROTOCOL OVERVIEW
 * ---------------------
 * The MEI protocol is a standardized Modbus extension (function code 0x2B)
 * that allows vendor-specific commands to be encapsulated within Modbus frames.
 *
 * MEI Frame Structure:
 *   [Device Addr][0x2B][MEI Type][Vendor Data...][CRC]
 *
 * For Delta PSU firmware updates, MEI Type = 0x0E (vendor-specific)
 *
 * FIRMWARE UPDATE PROTOCOL
 * ------------------------
 * The firmware update process follows these steps:
 *
 * 1. AUTHENTICATION (Get Seed & Send Key)
 *    - Send "Get Seed" command (0x01)
 *    - PSU responds with 8-byte random seed
 *    - XOR seed with security key
 *    - Send "Send Key" command (0x02) with XORed result
 *    - PSU validates and grants access
 *
 * 2. FIRMWARE PARSING
 *    - Parse .dflash or .hex firmware file
 *    - Extract firmware blocks and metadata
 *    - Validate file format and checksums
 *
 * 3. ERASE FLASH
 *    - Send "Erase Flash" command (0x03)
 *    - PSU erases target flash memory
 *    - Wait for completion (can take several seconds)
 *
 * 4. FIRMWARE TRANSFER
 *    - Split firmware into 128-byte blocks
 *    - For each block:
 *      * Send "Write Block" command (0x04) with:
 *        - Block number (2 bytes)
 *        - Block data (128 bytes)
 *        - Block checksum (2 bytes)
 *      * PSU writes block and responds with status
 *      * Display progress percentage
 *
 * 5. VERIFY FIRMWARE
 *    - Send "Verify Firmware" command (0x05)
 *    - PSU calculates CRC of written firmware
 *    - PSU compares with expected CRC
 *    - PSU responds with verification result
 *
 * 6. FINALIZE UPDATE
 *    - Send "Finalize" command (0x06)
 *    - PSU commits new firmware
 *    - PSU may reboot to apply changes
 *
 * ERROR HANDLING
 * --------------
 * - All commands include CRC validation
 * - Timeouts trigger retries (up to 5 attempts)
 * - Authentication failures abort immediately
 * - Block write failures retry current block
 * - Verification failures abort update
 *
 * MONITORING CONTROL
 * ------------------
 * During firmware update, rackmon monitoring is paused to prevent
 * interference with the update process. Monitoring is automatically
 * resumed after update completion or on error.
 *
 * USAGE
 * -----
 * List devices:
 *   psu_update_delta_orv3 --list-devices
 *
 * Update firmware:
 *   psu_update_delta_orv3 --addr <address> --key <security_key> --firmware
 * <file>
 *
 * Example:
 *   psu_update_delta_orv3 --addr 232 --key 0x06854137C758A5B6 \
 *       --firmware V3_DFLASH_REV_01_00_07_00.dflash
 */

#include "fboss/platform/rackmon/rack_firmware_update/MEIFirmwareUpdater.h"
#include "fboss/platform/rackmon/rack_firmware_update/RackmonClient.h"
#include "fboss/platform/rackmon/rack_firmware_update/UpdaterExceptions.h"
#include "fboss/platform/rackmon/rack_firmware_update/UpdaterUtils.h"

#include <fmt/format.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>
#include <iostream>

DEFINE_uint32(addr, 0, "Device address (required)");
DEFINE_uint64(key, 0, "Security key for firmware update (required)");
DEFINE_string(
    firmware,
    "",
    "Path to firmware file (.dflash/.hex format) (required)");
DEFINE_bool(list_devices, false, "List all discovered Modbus devices and exit");

using namespace facebook::fboss::platform::rackmon;

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  // Handle --list-devices flag
  if (FLAGS_list_devices) {
    try {
      listAndPrintDevices();
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // Validate arguments
  if (FLAGS_addr == 0) {
    std::cerr << "Error: --addr is required" << std::endl;
    return 1;
  }

  if (FLAGS_key == 0) {
    std::cerr << "Error: --key is required (security key in hex, e.g., "
                 "0x06854137C758A5B6)"
              << std::endl;
    return 1;
  }

  if (FLAGS_firmware.empty()) {
    std::cerr << "Error: --firmware is required" << std::endl;
    return 1;
  }

  try {
    std::cout << "Delta ORv3 PSU Firmware Update Tool" << std::endl;
    std::cout << fmt::format("Device Address: 0x{:02X}", FLAGS_addr)
              << std::endl;
    std::cout << fmt::format("Security Key: 0x{:016X}", FLAGS_key) << std::endl;
    std::cout << "Firmware File: " << FLAGS_firmware << std::endl;
    std::cout << std::endl;

    // ========================================
    // Step 1: Validate Device
    // ========================================
    std::cout << "============================================================"
              << std::endl;
    std::cout << "Step 1: Validate Device" << std::endl;
    std::cout << "============================================================"
              << std::endl;
    std::cout << fmt::format("Device found at address 0x{:x}", FLAGS_addr)
              << std::endl;
    std::cout << std::endl;

    // ========================================
    // Step 2: Firmware Update
    // ========================================
    std::cout << "============================================================"
              << std::endl;
    std::cout << "Step 2: Firmware Update" << std::endl;
    std::cout << "============================================================"
              << std::endl;

    // Create rackmon client
    auto client = std::make_shared<RackmonClient>();

    // Create updater
    MEIFirmwareUpdater updater(
        client, static_cast<uint16_t>(FLAGS_addr), FLAGS_key);

    // Read current version (if possible)
    try {
      std::cout << "Current version: " << updater.readVersion() << std::endl;
    } catch (const std::exception& e) {
      std::cout << "Could not read current version: " << e.what() << std::endl;
    }

    // Perform update with monitoring suppression
    {
      MonitoringGuard guard(
          [&client]() { client->pauseMonitoring(); },
          [&client]() { client->resumeMonitoring(); },
          client.get(), // Pass client for PMM control
          static_cast<uint16_t>(
              FLAGS_addr)); // Pass device address for PMM lookup
      updater.updateFirmware(FLAGS_firmware);
    }

    // Read new version
    try {
      std::cout << "New version: " << updater.readVersion() << std::endl;
    } catch (const std::exception& e) {
      std::cout << "Could not read new version: " << e.what() << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Upgrade Success!" << std::endl;
    return 0;

  } catch (const UpdaterException& e) {
    std::cerr << "Firmware update failed: " << e.what() << std::endl;
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Unexpected error: " << e.what() << std::endl;
    return 1;
  }
}
