/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/RemoteHost.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <folly/system/Shell.h>

#include "fboss/platform/helpers/ExpectSession.h"

namespace facebook::fboss::platform::platform_checks {

namespace {

constexpr auto kSushReason = "fixmyfboss";

// Switches are reprovisioned often, so their host keys churn.
const std::vector<std::string> kSshOptions{
    "-o",
    "ConnectTimeout=10",
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
    "-o",
    "LogLevel=ERROR",
    // Notice a dead connection (e.g. a reset switch) within ~15s.
    "-o",
    "ServerAliveInterval=5",
    "-o",
    "ServerAliveCountMax=3",
};

// Without a password, fail fast instead of prompting for one.
const std::vector<std::string> kKeyOnlyOptions{"-o", "BatchMode=yes"};
const std::vector<std::string> kPasswordOptions{
    "-o",
    "PreferredAuthentications=keyboard-interactive,password",
    "-o",
    "NumberOfPasswordPrompts=1",
};

// Makes the terminal pass bytes through unchanged (no echo, no CRLF
// translation, no line length limit), so output is byte-exact, then replaces
// the login shell with a plain sh without prompts.
constexpr auto kSessionSetup = "stty raw -echo; exec env PS1= PS2= sh";
// The quotes keep an echo of the command itself from matching the marker.
constexpr auto kReadyCommand = "echo __fixmyfboss''_ready__";
constexpr auto kReadyMarker = "__fixmyfboss_ready__";
// Far above the output of any check.
constexpr size_t kMaxOutputBytes = 256 * 1024 * 1024;

// Runs `cmd` in a subshell and prints "<token> <rc> <stdout size> <stderr
// size>\n" followed by the raw stdout and stderr bytes, so they stay separate
// and exact over a single terminal stream.
std::string wrapCommand(const std::string& cmd, const std::string& token) {
  return fmt::format(
      "o=$(mktemp) && e=$(mktemp) && {{ sh -c {} </dev/null >\"$o\" 2>\"$e\"; "
      "r=$?; printf '%s %d %d %d\\n' {} \"$r\" $(wc -c <\"$o\") "
      "$(wc -c <\"$e\"); cat \"$o\" \"$e\"; rm -f \"$o\" \"$e\"; }}",
      folly::shellQuote(cmd),
      token);
}

std::chrono::milliseconds remainingUntil(
    std::chrono::steady_clock::time_point deadline) {
  return std::max(
      std::chrono::milliseconds(0),
      std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()));
}

std::string shellJoin(const std::vector<std::string>& argv) {
  std::vector<std::string> quoted;
  quoted.reserve(argv.size());
  for (const auto& arg : argv) {
    quoted.push_back(folly::shellQuote(arg));
  }
  return folly::join(" ", quoted);
}

std::vector<std::string> concat(
    std::vector<std::string> head,
    const std::vector<std::string>& tail) {
  head.insert(head.end(), tail.begin(), tail.end());
  return head;
}

} // namespace

RemoteHost::Transport RemoteHost::detectTransport() {
  return runLocalCommand({"which", "sush2"}, std::chrono::seconds(5)).ok()
      ? Transport::SUSH2
      : Transport::SSH;
}

RemoteHost::RemoteHost(
    std::string hostname,
    Transport transport,
    std::string user,
    std::optional<std::string> password)
    : hostname_(std::move(hostname)),
      transport_(password ? Transport::SSH : transport),
      user_(std::move(user)),
      askPass_(password ? std::make_unique<SshAskPass>(*password) : nullptr) {}

RemoteHost::~RemoteHost() = default;

CommandResult RemoteHost::run(
    const std::string& cmd,
    std::chrono::seconds timeout) const {
  {
    std::lock_guard lock(sessionMutex_);
    if (auto result = runInSession(cmd, timeout)) {
      return *result;
    }
  }
  // A one-off connection reports why the host is unreachable on stderr.
  return runLocalCommand(execArgv(cmd), timeout);
}

std::optional<CommandResult> RemoteHost::runInSession(
    const std::string& cmd,
    std::chrono::seconds timeout) const {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  // A shell that died while idle can be replaced: nothing was sent to it.
  if (session_ && !session_->isAlive()) {
    session_.reset();
  }
  if (!session_) {
    session_ = startSession(deadline);
  }
  if (!session_) {
    return std::nullopt;
  }
  ExpectSession& session = *session_;

  // Never retried once sent, even partially: the shell executes an incomplete
  // final line when its input closes, and commands such as resets must not
  // run twice.
  auto token = fmt::format("__fixmyfboss_{}__", nextCommandId_++);
  try {
    session.sendLine(wrapCommand(cmd, token));
  } catch (const std::exception&) {
    return endSession(
        255, "Lost connection to " + hostname_ + " while sending: " + cmd);
  }
  if (!session.expect(token + " ", remainingUntil(deadline)) ||
      !session.expect("\n", remainingUntil(deadline))) {
    return abandonSession(cmd, timeout);
  }
  CommandResult result;
  size_t stdoutSize = 0;
  size_t stderrSize = 0;
  // The sizes come from the remote side; implausible ones mean a corrupted
  // reply, and their sum must not overflow.
  if (std::sscanf(
          session.getOutput().c_str(),
          "%d %zu %zu",
          &result.exitCode,
          &stdoutSize,
          &stderrSize) != 3 ||
      stdoutSize > kMaxOutputBytes ||
      stderrSize > kMaxOutputBytes - stdoutSize) {
    return endSession(
        255,
        "Malformed reply from the shell on " + hostname_ +
            " while running: " + cmd + " (reply: '" +
            folly::trimWhitespace(session.getOutput()).str() + "')");
  }
  auto output =
      session.readExactly(stdoutSize + stderrSize, remainingUntil(deadline));
  if (!output) {
    return abandonSession(cmd, timeout);
  }
  result.standardOut = output->substr(0, stdoutSize);
  result.standardErr = output->substr(stdoutSize);
  return result;
}

std::unique_ptr<ExpectSession> RemoteHost::startSession(
    std::chrono::steady_clock::time_point deadline) const {
  XLOG(DBG2) << "Starting shell session on " << hostname_;
  auto session = std::make_unique<ExpectSession>(shellJoin(interactiveArgv()));
  try {
    session->sendLine(kSessionSetup);
    session->sendLine(kReadyCommand);
  } catch (const std::exception&) {
    return nullptr;
  }
  // Login banners and prompts before the marker are discarded.
  if (!session->expect(kReadyMarker, remainingUntil(deadline))) {
    return nullptr;
  }
  return session;
}

CommandResult RemoteHost::abandonSession(
    const std::string& cmd,
    std::chrono::seconds timeout) const {
  bool lost = !session_ || session_->isEof() || !session_->isAlive();
  if (lost) {
    return endSession(
        255, "Lost connection to " + hostname_ + " while running: " + cmd);
  }
  return endSession(
      124, fmt::format("Timed out after {}s: {}", timeout.count(), cmd));
}

CommandResult RemoteHost::endSession(int exitCode, std::string error) const {
  session_.reset();
  return CommandResult{.exitCode = exitCode, .standardErr = std::move(error)};
}

void RemoteHost::copyTo(
    const std::filesystem::path& localPath,
    const std::filesystem::path& hostPath,
    std::chrono::seconds timeout) const {
  auto result = runLocalCommand(copyArgv(localPath, hostPath), timeout);
  if (!result.ok()) {
    throw std::runtime_error(
        "Copying " + localPath.string() + " to " + hostname_ + ":" +
        hostPath.string() + " failed (exit " + std::to_string(result.exitCode) +
        "): " + result.standardErr);
  }
}

std::vector<std::string> RemoteHost::authMethodsArgv() const {
  // Fails at authentication, and ssh then prints the methods the server
  // accepts.
  return concat(
      concat({"ssh"}, kSshOptions),
      {"-o",
       "BatchMode=yes",
       "-o",
       "PubkeyAuthentication=no",
       "-o",
       "GSSAPIAuthentication=no",
       destination(),
       "true"});
}

bool RemoteHost::acceptsPassword(std::chrono::seconds timeout) const {
  return offersPasswordAuth(
      runLocalCommand(authMethodsArgv(), timeout).standardErr);
}

std::unique_ptr<ExpectSession> RemoteHost::spawnInteractive() const {
  return std::make_unique<ExpectSession>(shellJoin(interactiveArgv()));
}

std::vector<std::string> RemoteHost::interactiveArgv() const {
  switch (transport_) {
    case Transport::SSH:
      return concat(sshPrefix("ssh"), {"-tt", destination()});
    case Transport::SUSH2:
      return {"sush2", "--reason", kSushReason, destination()};
  }
  throw std::logic_error("Unknown transport");
}

std::vector<std::string> RemoteHost::execArgv(const std::string& cmd) const {
  switch (transport_) {
    case Transport::SSH:
      return concat(sshPrefix("ssh"), {destination(), cmd});
    case Transport::SUSH2:
      return {"sush2", "-q", "--reason", kSushReason, destination(), "--", cmd};
  }
  throw std::logic_error("Unknown transport");
}

std::vector<std::string> RemoteHost::copyArgv(
    const std::filesystem::path& localPath,
    const std::filesystem::path& hostPath) const {
  auto target = destination() + ":" + hostPath.string();
  switch (transport_) {
    case Transport::SSH:
      return concat(sshPrefix("scp"), {"-r", localPath.string(), target});
    case Transport::SUSH2:
      return {
          "suscp2", "-r", "--reason", kSushReason, localPath.string(), target};
  }
  throw std::logic_error("Unknown transport");
}

std::vector<std::string> RemoteHost::sshPrefix(
    const std::string& program) const {
  auto prefix = askPass_ ? askPass_->envPrefix() : std::vector<std::string>{};
  prefix.push_back(program);
  prefix = concat(std::move(prefix), kSshOptions);
  return concat(
      std::move(prefix), askPass_ ? kPasswordOptions : kKeyOnlyOptions);
}

std::string RemoteHost::destination() const {
  return user_ + "@" + hostname_;
}

bool offersPasswordAuth(const std::string& sshStderr) {
  constexpr std::string_view kDenied = "Permission denied (";
  auto start = sshStderr.find(kDenied);
  if (start == std::string::npos) {
    return false;
  }
  start += kDenied.size();
  auto end = sshStderr.find(')', start);
  if (end == std::string::npos) {
    return false;
  }
  std::vector<folly::StringPiece> methods;
  folly::split(
      ',', folly::StringPiece(sshStderr).subpiece(start, end - start), methods);
  return std::any_of(methods.begin(), methods.end(), [](auto method) {
    return method == "password" || method == "keyboard-interactive";
  });
}

} // namespace facebook::fboss::platform::platform_checks
