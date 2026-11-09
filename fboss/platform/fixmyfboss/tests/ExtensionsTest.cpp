/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/Extensions.h"

#include <gtest/gtest.h>

using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::fixmyfboss;

TEST(ExtensionsTest, RegisteredExtensionAddsOptionAndHook) {
  std::optional<std::string> reportedTarget;
  registerExtension([&](CLI::App& app) -> ReportHook {
    app.add_flag("--test-extension");
    return [&](const platform_checks::CheckReport& report) {
      reportedTarget = report.target().value_or("");
    };
  });
  CLI::App app;
  platform_checks::CheckReport report;
  report.target() = "rsw1";

  auto hooks = addExtensionOptions(app);
  for (const auto& hook : hooks) {
    hook(report);
  }

  EXPECT_NE(app.get_option_no_throw("--test-extension"), nullptr);
  EXPECT_EQ(reportedTarget, "rsw1");
}
