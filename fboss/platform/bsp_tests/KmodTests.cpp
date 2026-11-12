// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <filesystem>
#include <set>

#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <re2/re2.h>
#include <vector>

#include "fboss/platform/bsp_tests/BspTest.h"
#include "fboss/platform/bsp_tests/utils/KmodUtils.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::bsp_tests {

using platform_manager::BspKmodsFile;

// Test fixture for kmod tests
class KmodTest : public BspTest {
 protected:
  void SetUp() override {
    // Ensure we have a clean environment before each test
    KmodUtils::unloadKmods(*GetRuntimeConfig().kmods());
  }

  void TearDown() override {
    // Clean up after each test
    KmodUtils::unloadKmods(*GetRuntimeConfig().kmods());
  }
};

// Test loading kernel modules
TEST_F(KmodTest, LoadKmods) {
  const auto& kmods = *GetRuntimeConfig().kmods();
  KmodUtils::loadKmods(kmods);

  // Verify modules are loaded
  auto loadedKmods = KmodUtils::getLoadedKmods(kmods);
  EXPECT_FALSE(loadedKmods.empty()) << "No kernel modules were loaded";

  std::set<std::string> expectedKmods;
  for (const auto& kmod : *kmods.bspKmods()) {
    expectedKmods.insert(kmod);
  }
  for (const auto& kmod : *kmods.sharedKmods()) {
    expectedKmods.insert(kmod);
  }

  std::set<std::string> loadedKmodsSet(loadedKmods.begin(), loadedKmods.end());

  std::vector<std::string> missingKmods;
  std::set_difference(
      expectedKmods.begin(),
      expectedKmods.end(),
      loadedKmodsSet.begin(),
      loadedKmodsSet.end(),
      std::back_inserter(missingKmods));
  EXPECT_TRUE(missingKmods.empty())
      << "Not all expected kernel modules were loaded: "
      << folly::join(", ", missingKmods);
}

// Test unloading kernel modules
TEST_F(KmodTest, UnloadKmods) {
  const auto& kmods = *GetRuntimeConfig().kmods();

  KmodUtils::loadKmods(kmods);
  KmodUtils::unloadKmods(kmods);

  auto loadedKmods = KmodUtils::getLoadedKmods(kmods);
  EXPECT_TRUE(loadedKmods.empty()) << "Some kernel modules were not unloaded";
}

// Test fixture for kmod stress tests
class KmodStressTest : public KmodTest {};

// Stress test for loading and unloading modules
TEST_F(KmodStressTest, KmodLoadUnloadStress) {
  const auto& kmods = *GetRuntimeConfig().kmods();

  for (int i = 0; i < 10; i++) {
    XLOG(INFO) << "Stress test iteration " << i + 1;
    KmodUtils::loadKmods(kmods);
    KmodUtils::unloadKmods(kmods);
  }

  auto loadedKmods = KmodUtils::getLoadedKmods(kmods);
  EXPECT_TRUE(loadedKmods.empty())
      << "Some kernel modules were not unloaded after stress test";
}

// Test fbsp-remove script
TEST_F(KmodTest, FbspRemove) {
  const auto& kmods = *GetRuntimeConfig().kmods();

  KmodUtils::loadKmods(kmods);

  try {
    KmodUtils::fbspRemove(GetPlatformManagerConfig());
  } catch (const std::exception& e) {
    FAIL() << "fbsp-remove failed: " << e.what();
  }

  auto loadedKmods = KmodUtils::getLoadedKmods(kmods);
  EXPECT_TRUE(loadedKmods.empty())
      << "Some kernel modules were not unloaded by fbsp-remove. Loaded kmods: "
      << folly::join(", ", loadedKmods);
}

// Test that kmods.json contains entries for all .ko files installed by the BSP
TEST_F(KmodTest, KmodsJsonComplete) {
  // Get the vendor keyword and kernel version using shared helpers
  auto vendor = KmodUtils::getVendorKeyword(GetPlatformManagerConfig());
  ASSERT_FALSE(vendor.empty())
      << "Failed to extract vendor from bspKmodsRpmName: "
      << *GetPlatformManagerConfig().bspKmodsRpmName();
  auto kernelVersion = KmodUtils::getKernelVersion();

  // Build the path to the installed kmod directory:
  // /lib/modules/<kernel-version>/extra/<vendor>/
  auto kmodDir =
      std::filesystem::path("/lib/modules") / kernelVersion / "extra" / vendor;
  ASSERT_TRUE(std::filesystem::exists(kmodDir))
      << "Kmod directory does not exist: " << kmodDir;
  ASSERT_TRUE(std::filesystem::is_directory(kmodDir))
      << "Kmod path is not a directory: " << kmodDir;

  // Collect all kmod names from the raw kmods.json (not the filtered
  // RuntimeConfig version, which excludes platform-specific exceptions)
  const auto& kmods = GetKmodsJson();
  // Normalize dashes to underscores when inserting since modprobe treats
  // them equivalently
  std::set<std::string> kmodsInJson;
  auto normalize = [](std::string s) {
    std::replace(s.begin(), s.end(), '-', '_');
    return s;
  };
  for (const auto& kmod : *kmods.bspKmods()) {
    kmodsInJson.insert(normalize(kmod));
  }
  for (const auto& kmod : *kmods.sharedKmods()) {
    kmodsInJson.insert(normalize(kmod));
  }

  // Iterate through all .ko files in the vendor directory and check each has
  // an entry in kmods.json.
  const re2::RE2 kKoFileRe("(.+)\\.ko");
  std::vector<std::string> missingKmods;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(kmodDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto filename = entry.path().filename().string();
    std::string moduleName;
    if (!re2::RE2::FullMatch(filename, kKoFileRe, &moduleName)) {
      continue;
    }

    if (!kmodsInJson.contains(normalize(moduleName))) {
      missingKmods.push_back(filename);
    }
  }

  EXPECT_TRUE(missingKmods.empty())
      << "The following kernel modules in " << kmodDir
      << " are missing from kmods.json: " << folly::join(", ", missingKmods);
}

} // namespace facebook::fboss::platform::bsp_tests
