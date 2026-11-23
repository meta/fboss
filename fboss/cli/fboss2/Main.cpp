/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <CLI/App.hpp>
#include "fboss/cli/fboss2/utils/CmdInitUtils.h"

#include <folly/init/Init.h>
#include <folly/logging/Init.h>
#include <exception>

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG0; default:async=true");

namespace facebook::fboss {

namespace {
#ifdef FBOSS2_READ_ONLY
constexpr bool kReadOnly = true;
#else
constexpr bool kReadOnly = false;
#endif
} // namespace

int cliMain(int argc, char* argv[]) {
  int one = 1;
  const folly::Init init(&one, &argv, /* removeFlags = */ false);

  CLI::App app{"FBOSS CLI"};

  utils::initApp(app, kReadOnly);
  utils::postAppInit(argc, argv, app);

  try {
    CLI11_PARSE(app, argc, argv);
  } catch (const std::exception& /* e */) {
    // Errors are already printed to stderr by CmdHandler::run()
    return 1;
  }

  return 0;
}

} // namespace facebook::fboss

int main(int argc, char* argv[]) {
  return facebook::fboss::cliMain(argc, argv);
}
