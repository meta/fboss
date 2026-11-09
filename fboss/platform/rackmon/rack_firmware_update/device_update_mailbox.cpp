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
 * Mailbox-Based Device Firmware Update Tool
 * ==========================================
 *
 * This tool updates firmware on BBU (Battery Backup Unit) devices using a
 * mailbox-based protocol over Modbus/RTU. Supported vendors include Panasonic,
 * Delta, and AEI.
 *
 * MAILBOX PROTOCOL OVERVIEW
 * --------------------------
 * The mailbox protocol uses standard Modbus registers as a communication
 * channel between the host and the device firmware update bootloader.
 *
 * Register Layout:
 *   - Control Register: Command and status flags
 *   - Data Registers: Firmware data blocks
 *   - Status Register: Update progress and errors
 *
 * FIRMWARE UPDATE PROTOCOL
 * ------------------------
 * The firmware update process follows these steps:
 *
 * 1. UNLOCK ENGINEERING MODE
 *    - Write unlock sequence to control register
 *    - Device enters engineering/bootloader mode
 *    - Normal operation is suspended
 *
 * 2. ENTER BOOT MODE
 *    - Write boot mode command
 *    - Device bootloader takes control
 *    - Ready to receive firmware data
 *    - May retry up to 5 times if device is busy
 *
 * 3. FIRMWARE PARSING
 *    - Parse .bin firmware file
 *    - Extract firmware blocks
 *    - Calculate total blocks and checksums
 *
 * 4. FIRMWARE TRANSFER
 *    - Split firmware into blocks (vendor-specific size):
 *      * Panasonic: 64 bytes per block
 *      * Delta: 64 bytes per block
 *      * AEI: 128 bytes per block
 *    - For each block:
 *      * Write block number to control register
 *      * Write block data to data registers
 *      * Write block checksum
 *      * Wait for device acknowledgment
 *      * Display progress percentage
 *
 * 5. REQUEST VERIFY FIRMWARE
 *    - Write verify command to control register
 *    - Device calculates CRC of received firmware
 *    - Device compares with expected CRC
 *    - Poll status register for verification result
 *
 * 6. WAIT FOR VERIFICATION
 *    - Verification can take 60+ seconds
 *    - Poll status register every second
 *    - Status codes:
 *      * 0x0006: Verification in progress
 *      * 0x00AA: Verification complete, success
 *      * Other: Verification failed
 *
 * 7. EXIT BOOT MODE
 *    - Write exit boot mode command
 *    - Device reboots with new firmware
 *    - Returns to normal operation
 *
 * VENDOR-SPECIFIC PARAMETERS
 * ---------------------------
 * Different vendors use different register addresses and block sizes:
 *
 * Panasonic:
 *   - Control Register: 0x0100
 *   - Data Registers: 0x0101-0x0120
 *   - Block Size: 64 bytes
 *
 * Delta:
 *   - Control Register: 0x0200
 *   - Data Registers: 0x0201-0x0220
 *   - Block Size: 64 bytes
 *
 * Delta Power Tether:
 *   - Control Register: 0x0200
 *   - Data Registers: 0x0201-0x0220
 *   - Block Size: 64 bytes
 *   - Special handling for power tether mode
 *
 * HPR (High Power Rectifier) variants:
 *   - hpr_panasonic: Panasonic registers, 64-byte blocks
 *   - hpr_pmm_panasonic: PMM-controlled Panasonic
 *   - hpr_pmm_delta: PMM-controlled Delta
 *   - hpr_pmm_aei: PMM-controlled AEI, 128-byte blocks
 *
 * ERROR HANDLING
 * --------------
 * - All register operations include retry logic
 * - Timeouts trigger retries (up to 5 attempts)
 * - Boot mode entry failures retry with backoff
 * - Block write failures retry current block
 * - Verification failures abort update
 * - Exit boot mode is always attempted (even on error)
 *
 * MONITORING CONTROL
 * ------------------
 * During firmware update, rackmon monitoring is paused to prevent
 * interference with the update process. For PMM-controlled devices,
 * the PMM monitoring is also paused. Monitoring is automatically
 * resumed after update completion or on error.
 *
 * PMM (Power Management Module) CONTROL
 * --------------------------------------
 * Some devices are controlled through a PMM. The tool automatically:
 * - Detects if device is PMM-controlled
 * - Pauses PMM monitoring before update
 * - Resumes PMM monitoring after update
 * - Logs warnings if PMM control fails (non-critical)
 *
 * USAGE
 * -----
 * List devices:
 *   device_update_mailbox --list-devices
 *
 * Update firmware:
 *   device_update_mailbox --addr <address> --vendor <vendor> --firmware <file>
 *
 * Example:
 *   device_update_mailbox --addr 64 --vendor delta \
 *       --firmware DPST3000GBA_IMG_APP_FB_S10604_S10600.bin
 */

#include "fboss/platform/rackmon/rack_firmware_update/MailboxFirmwareUpdater.h"
#include "fboss/platform/rackmon/rack_firmware_update/RackmonClient.h"
#include "fboss/platform/rackmon/rack_firmware_update/UpdaterExceptions.h"
#include "fboss/platform/rackmon/rack_firmware_update/UpdaterUtils.h"

#include <fmt/format.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>
#include <iostream>

DEFINE_uint32(addr, 0, "Device address (required)");
DEFINE_string(
    vendor,
    "",
    "Vendor type: panasonic, delta, delta_power_tether, hpr_panasonic, "
    "hpr_pmm_panasonic, hpr_pmm_delta, hpr_pmm_aei (required)");
DEFINE_string(firmware, "", "Path to firmware file (.bin format) (required)");
DEFINE_uint32(
    block_size,
    0,
    "Override block size in bytes (optional, uses vendor default if not set)");
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

  if (FLAGS_vendor.empty()) {
    std::cerr << "Error: --vendor is required" << std::endl;
    std::cerr << "Valid vendors: panasonic, delta, delta_power_tether, "
                 "hpr_panasonic, hpr_pmm_panasonic, hpr_pmm_delta, hpr_pmm_aei"
              << std::endl;
    return 1;
  }

  if (FLAGS_firmware.empty()) {
    std::cerr << "Error: --firmware is required" << std::endl;
    return 1;
  }

  try {
    std::cout << "Mailbox-Based Device Firmware Update Tool" << std::endl;
    std::cout << fmt::format("Device Address: 0x{:02X}", FLAGS_addr)
              << std::endl;
    std::cout << "Vendor: " << FLAGS_vendor << std::endl;
    std::cout << "Firmware File: " << FLAGS_firmware << std::endl;

    // Get vendor parameters
    VendorParams params = VendorParams::getByName(FLAGS_vendor);

    // Override block size if provided
    if (FLAGS_block_size > 0) {
      std::cout << fmt::format(
                       "Overriding block size: {} bytes", FLAGS_block_size)
                << std::endl;
      params.blockSize = FLAGS_block_size;
    }

    std::cout << fmt::format("Block Size: {} bytes", params.blockSize)
              << std::endl;
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
    MailboxFirmwareUpdater updater(
        client, static_cast<uint16_t>(FLAGS_addr), params);

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
