/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/SolConsoleCheck.h"

#include <utility>

#include <folly/ScopeGuard.h>

#include "fboss/platform/helpers/ExpectSession.h"

namespace facebook::fboss::platform::platform_checks {

namespace {

// The quotes keep the echoed command line from matching the prompt itself.
constexpr auto kSetPrompt = "PS1='FIXMYFBOSS''_READY# '";
constexpr auto kPrompt = "FIXMYFBOSS_READY# ";
// sol.sh's escape sequence: Ctrl-L, then x.
constexpr auto kSolExit =
    "\x0c"
    "x";
constexpr std::chrono::seconds kShellTimeout{30};
// Printed by sol.sh once it is attached to the console.
constexpr auto kSolBanner = "Terminate the connection";
constexpr std::chrono::seconds kSolStartTimeout{5};
// The console prints its login prompt only in response to a key press.
constexpr auto kEnter = "\r";
constexpr std::chrono::seconds kEnterInterval{2};

} // namespace

CheckResult SolConsoleCheck::run() {
  auto session = host().spawnInteractive();
  if (!session) {
    return makeError("Cannot open a shell on " + host().name());
  }
  std::string transcript;
  CheckResult result;
  try {
    result = runConsole(*session, transcript);
  } catch (const std::exception& ex) {
    result =
        makeError(std::string("Lost the BMC console session: ") + ex.what());
  }
  result.details() = transcript;
  return result;
}

CheckResult SolConsoleCheck::runConsole(
    ExpectSession& terminal,
    std::string& transcript) {
  auto expect = [&](const std::string& pattern, std::chrono::seconds timeout) {
    bool found = terminal.expect(pattern, timeout);
    transcript += terminal.getOutput();
    return found;
  };

  terminal.sendLine(kSetPrompt);
  if (!expect(kPrompt, kShellTimeout)) {
    return makeError("No shell prompt from " + host().name() + " within 30s");
  }
  terminal.sendLine("sol.sh");
  // The console is shared: never leave it attached. Runs once, never throws.
  bool detached = false;
  auto detach = [&] {
    if (std::exchange(detached, true)) {
      return;
    }
    try {
      terminal.send(kSolExit);
    } catch (const std::exception&) {
      // The session is broken; dropping it ends sol.sh on the BMC as well.
    }
  };
  SCOPE_EXIT {
    detach();
  };
  // Keys sent before sol.sh is attached are lost: switching the terminal to
  // raw mode discards pending input. Proceed without the banner anyway, in
  // case a BMC version words it differently.
  expect(kSolBanner, kSolStartTimeout);
  bool gotLogin = false;
  auto deadline = std::chrono::steady_clock::now() + promptTimeout_;
  while (!gotLogin && std::chrono::steady_clock::now() < deadline) {
    // Repeated in case a key press is lost.
    terminal.send(kEnter);
    gotLogin = expect("login:", kEnterInterval);
  }
  detach();
  expect(kPrompt, std::chrono::seconds(5));

  if (gotLogin) {
    return makeOK();
  }
  return makeProblem(
      "sol.sh did not show the x86 login prompt within " +
          std::to_string(promptTimeout_.count()) + "s",
      RemediationType::MANUAL_REMEDIATION,
      "Check that the x86 is powered on and its serial console is enabled");
}

} // namespace facebook::fboss::platform::platform_checks
