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

namespace facebook::fboss::platform::fixmyfboss {

namespace {

// Function-local so that registering from another file's static initializer
// cannot run before the vector is constructed.
std::vector<Extension>& extensions() {
  static std::vector<Extension> extensions;
  return extensions;
}

} // namespace

bool registerExtension(Extension extension) {
  extensions().push_back(std::move(extension));
  return true;
}

std::vector<ReportHook> addExtensionOptions(CLI::App& app) {
  std::vector<ReportHook> hooks;
  for (const auto& extension : extensions()) {
    hooks.push_back(extension(app));
  }
  return hooks;
}

} // namespace facebook::fboss::platform::fixmyfboss
