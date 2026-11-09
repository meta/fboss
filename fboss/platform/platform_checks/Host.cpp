/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/Host.h"

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <folly/logging/xlog.h>
#include <folly/system/Shell.h>

namespace facebook::fboss::platform::platform_checks {

CommandResult runLocalCommand(
    const std::vector<std::string>& argv,
    std::chrono::seconds timeout) {
  // coreutils `timeout` is simpler than folly::Subprocess polling and gives the
  // conventional exit code 124 on expiry.
  std::vector<std::string> timedArgv{
      "timeout", "--kill-after=5", std::to_string(timeout.count())};
  timedArgv.insert(timedArgv.end(), argv.begin(), argv.end());
  XLOG(DBG2) << "Running: " << folly::join(" ", timedArgv);

  CommandResult result;
  try {
    folly::Subprocess proc(
        timedArgv,
        folly::Subprocess::Options().pipeStdout().pipeStderr().usePath());
    std::tie(result.standardOut, result.standardErr) = proc.communicate();
    auto returnCode = proc.wait();
    result.exitCode = returnCode.exited() ? returnCode.exitStatus()
                                          : 128 + returnCode.killSignal();
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Failed to run " << folly::join(" ", timedArgv) << ": "
              << ex.what();
    result.exitCode = -1;
    result.standardErr = ex.what();
  }
  return result;
}

namespace {

// Exit codes of a command that never ran: timeout(1)'s 124-127 and ssh's 255.
bool notExecuted(const CommandResult& result) {
  return result.exitCode < 0 || result.exitCode >= 124;
}

} // namespace

CommandResult Host::runQuery(const std::string& cmd) const {
  auto result = run(cmd);
  if (notExecuted(result)) {
    throw HostCommandError(
        "Could not run `" + cmd + "` on " + name() + " (exit " +
        std::to_string(result.exitCode) +
        "): " + folly::trimWhitespace(result.standardErr).str());
  }
  return result;
}

std::optional<std::string> Host::readFile(
    const std::filesystem::path& path) const {
  auto result = runQuery("cat -- " + folly::shellQuote(path.string()));
  if (!result.ok()) {
    return std::nullopt;
  }
  return result.standardOut;
}

bool Host::exists(const std::filesystem::path& path) const {
  return runQuery("test -e " + folly::shellQuote(path.string())).ok();
}

std::vector<std::optional<std::string>> Host::readFiles(
    const std::vector<std::filesystem::path>& paths) const {
  std::vector<std::optional<std::string>> contents(paths.size());
  if (paths.empty()) {
    return contents;
  }
  // Prints "<size>\n<bytes>" per readable file and "-1\n" otherwise.
  std::string script = "t=$(mktemp) || exit 1; for f in";
  for (const auto& path : paths) {
    script += " " + folly::shellQuote(path.string());
  }
  script +=
      "; do if cat -- \"$f\" >\"$t\" 2>/dev/null; then "
      "printf '%d\\n' $(wc -c <\"$t\"); cat \"$t\"; "
      "else echo -1; fi; done; rm -f \"$t\"";
  auto result = runQuery(script);
  if (!result.ok()) {
    return contents;
  }
  folly::StringPiece out(result.standardOut);
  for (auto& content : contents) {
    auto newline = out.find('\n');
    if (newline == folly::StringPiece::npos) {
      break;
    }
    auto size = folly::tryTo<long>(out.subpiece(0, newline));
    out.advance(newline + 1);
    if (size && *size == -1) {
      continue;
    }
    // Anything else means the output is corrupt; parsing further would read
    // file contents as sizes.
    if (!size || *size < 0 || static_cast<size_t>(*size) > out.size()) {
      break;
    }
    content = out.subpiece(0, *size).str();
    out.advance(*size);
  }
  return contents;
}

std::optional<std::string> Host::readTrimmedFile(
    const std::filesystem::path& path) const {
  auto content = readFile(path);
  if (!content) {
    return std::nullopt;
  }
  return folly::trimWhitespace(*content).str();
}

std::vector<std::filesystem::path> Host::listDirectory(
    const std::filesystem::path& path) const {
  std::vector<std::filesystem::path> entries;
  // NUL-separated, since file names may contain newlines. A plain glob loop
  // rather than `find -print0`, which BusyBox (BMCs) may not support.
  auto result = runQuery(
      "cd -- " + folly::shellQuote(path.string()) +
      " || exit 1; for f in * .[!.]* ..?*; do "
      "if [ -e \"$f\" ] || [ -L \"$f\" ]; then printf '%s\\0' \"$f\"; fi; "
      "done");
  if (!result.ok()) {
    return entries;
  }
  std::vector<folly::StringPiece> names;
  folly::split('\0', result.standardOut, names, /*ignoreEmpty=*/true);
  for (const auto& entryName : names) {
    entries.push_back(path / entryName.str());
  }
  return entries;
}

} // namespace facebook::fboss::platform::platform_checks
