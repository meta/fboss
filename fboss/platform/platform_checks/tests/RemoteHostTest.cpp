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

#include <sys/stat.h>

#include <thread>

#include <folly/testing/TestUtil.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace facebook::fboss::platform::platform_checks;
using ::testing::ElementsAre;
using ::testing::IsSupersetOf;

namespace {

constexpr std::chrono::seconds kTimeout{10};

// Stands in for ssh by running the "remote" command with a local shell.
class LoopbackRemoteHost : public RemoteHost {
 public:
  LoopbackRemoteHost() : RemoteHost("loopback", Transport::SSH) {}

  std::vector<std::string> execArgv(const std::string& cmd) const override {
    return {"/bin/sh", "-c", cmd};
  }

  std::vector<std::string> interactiveArgv() const override {
    return {"/bin/sh"};
  }
};

// The remote shell cannot be started, as when the host is unreachable.
class NoShellRemoteHost : public LoopbackRemoteHost {
 public:
  std::vector<std::string> interactiveArgv() const override {
    return {"/bin/false"};
  }
};

} // namespace

TEST(RemoteHostTest, SshPassesCommandAsSingleArgument) {
  RemoteHost host("rsw1", RemoteHost::Transport::SSH);

  auto argv = host.execArgv("echo a b");

  EXPECT_EQ(argv.front(), "ssh");
  EXPECT_THAT(argv, IsSupersetOf({"BatchMode=yes"}));
  EXPECT_THAT(
      std::vector<std::string>(argv.end() - 2, argv.end()),
      ElementsAre("root@rsw1", "echo a b"));
}

TEST(RemoteHostTest, Sush2Exec) {
  RemoteHost host("rsw1-oob", RemoteHost::Transport::SUSH2);

  EXPECT_THAT(
      host.execArgv("uptime"),
      ElementsAre(
          "sush2",
          "-q",
          "--reason",
          "fixmyfboss",
          "root@rsw1-oob",
          "--",
          "uptime"));
}

TEST(RemoteHostTest, InteractiveArgvAllocatesTerminal) {
  RemoteHost ssh("rsw1", RemoteHost::Transport::SSH);
  RemoteHost sush2("rsw1", RemoteHost::Transport::SUSH2);

  auto sshArgv = ssh.interactiveArgv();

  EXPECT_EQ(sshArgv.front(), "ssh");
  EXPECT_THAT(sshArgv, ::testing::Contains("-tt"));
  EXPECT_EQ(sshArgv.back(), "root@rsw1");
  EXPECT_THAT(
      sush2.interactiveArgv(),
      ElementsAre("sush2", "--reason", "fixmyfboss", "root@rsw1"));
}

TEST(RemoteHostTest, CopyArgvTargetsHostPath) {
  RemoteHost ssh("rsw1", RemoteHost::Transport::SSH, "admin");
  RemoteHost sush2("rsw1", RemoteHost::Transport::SUSH2);

  auto sshArgv = ssh.copyArgv("/local/bin", "/tmp/bin");
  auto sush2Argv = sush2.copyArgv("/local/bin", "/tmp/bin");

  EXPECT_EQ(sshArgv.front(), "scp");
  EXPECT_THAT(
      std::vector<std::string>(sshArgv.end() - 2, sshArgv.end()),
      ElementsAre("/local/bin", "admin@rsw1:/tmp/bin"));
  EXPECT_THAT(
      sush2Argv,
      ElementsAre(
          "suscp2",
          "-r",
          "--reason",
          "fixmyfboss",
          "/local/bin",
          "root@rsw1:/tmp/bin"));
}

TEST(RemoteHostTest, RunAndFileAccessGoThroughTransport) {
  LoopbackRemoteHost host;

  auto result = host.run("echo hi; exit 2");

  EXPECT_EQ(result.exitCode, 2);
  EXPECT_EQ(result.standardOut, "hi\n");
  EXPECT_FALSE(host.isLocal());
  EXPECT_TRUE(host.exists("/proc/self"));
  EXPECT_EQ(host.readFile("/nonexistent"), std::nullopt);
}

TEST(RemoteHostTest, FallsBackToOneShotCommandWithoutShell) {
  NoShellRemoteHost host;

  auto result = host.run("echo direct");

  EXPECT_EQ(result.exitCode, 0);
  EXPECT_EQ(result.standardOut, "direct\n");
}

TEST(RemoteHostTest, DoesNotRerunCommandWhenShellDies) {
  folly::test::TemporaryDirectory tmpDir;
  auto counter = tmpDir.path().string() + "/runs";
  LoopbackRemoteHost host;

  auto result = host.run("echo run >> " + counter + "; kill -9 $PPID");

  EXPECT_EQ(result.exitCode, 255);
  EXPECT_EQ(host.readFile(counter), "run\n");
}

// The session tests below run a real shell in a terminal through
// ExpectSession, as with ssh -tt.

TEST(RemoteHostSessionTest, SeparatesStdoutStderrAndExitCode) {
  LoopbackRemoteHost host;

  auto result = host.run("echo out; echo err >&2; exit 7", kTimeout);

  EXPECT_EQ(result.exitCode, 7);
  EXPECT_EQ(result.standardOut, "out\n");
  EXPECT_EQ(result.standardErr, "err\n");
}

TEST(RemoteHostSessionTest, OutputIsByteExact) {
  LoopbackRemoteHost host;

  // NUL, bare LF, CR LF and terminal control characters (^C, ^D).
  auto result = host.run("printf 'a\\000\\nb\\r\\n\\003\\004'", kTimeout);

  EXPECT_EQ(result.standardOut, std::string("a\0\nb\r\n\x03\x04", 8));
}

TEST(RemoteHostSessionTest, LongCommandsAreNotTruncated) {
  LoopbackRemoteHost host;
  // Longer than a terminal's canonical-mode line limit (4096).
  const std::string payload(10000, 'x');

  auto result = host.run("printf %s " + payload, kTimeout);

  EXPECT_EQ(result.standardOut, payload);
}

TEST(RemoteHostSessionTest, ReusesOneShell) {
  LoopbackRemoteHost host;

  auto first = host.run("echo $PPID", kTimeout);
  auto second = host.run("echo $PPID", kTimeout);

  EXPECT_EQ(first.standardOut, second.standardOut);
}

TEST(RemoteHostSessionTest, QuotingIsPreserved) {
  LoopbackRemoteHost host;

  auto result = host.run("echo \"it's\" '$HOME'", kTimeout);

  EXPECT_EQ(result.standardOut, "it's $HOME\n");
}

TEST(RemoteHostSessionTest, TimeoutEndsShellAndNextCommandRestartsIt) {
  LoopbackRemoteHost host;

  auto timedOut = host.run("sleep 30", std::chrono::seconds(1));
  auto next = host.run("echo back", kTimeout);

  EXPECT_EQ(timedOut.exitCode, 124);
  EXPECT_EQ(next.standardOut, "back\n");
}

TEST(RemoteHostSessionTest, ShellDyingWhileIdleIsRestarted) {
  LoopbackRemoteHost host;
  // Kills the session shell shortly after this command has returned.
  host.run("(sleep 0.2; kill -9 $PPID) >/dev/null 2>&1 &", kTimeout);
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::seconds(1));

  auto result = host.run("echo ok", kTimeout);

  EXPECT_EQ(result.exitCode, 0);
  EXPECT_EQ(result.standardOut, "ok\n");
}

TEST(RemoteHostSessionTest, MalformedReplyIsNotReportedAsTimeout) {
  LoopbackRemoteHost host;

  // Writing to the terminal directly bypasses the per-command capture and
  // fakes a status line for the first command (token 0).
  auto result = host.run(
      "printf '__fixmyfboss_0__ garbage\\n' >/dev/tty; sleep 30", kTimeout);

  EXPECT_EQ(result.exitCode, 255);
  EXPECT_THAT(result.standardErr, ::testing::HasSubstr("Malformed reply"));
}

TEST(RemoteHostSessionTest, ImplausibleOutputSizeIsMalformedReply) {
  LoopbackRemoteHost host;

  // A status line whose sizes would overflow when added.
  auto result = host.run(
      "printf '__fixmyfboss_0__ 0 18446744073709551615 2\\n' >/dev/tty; "
      "sleep 30",
      kTimeout);

  EXPECT_EQ(result.exitCode, 255);
  EXPECT_THAT(result.standardErr, ::testing::HasSubstr("Malformed reply"));
}

TEST(RemoteHostTest, PasswordUsesAskPassAndSsh) {
  RemoteHost host("rsw1", RemoteHost::Transport::SUSH2, "root", "s3cret");

  auto argv = host.execArgv("uptime");

  ASSERT_GE(argv.size(), 4);
  EXPECT_EQ(argv[0], "env");
  EXPECT_THAT(argv[1], ::testing::StartsWith("SSH_ASKPASS="));
  EXPECT_EQ(argv[2], "SSH_ASKPASS_REQUIRE=force");
  EXPECT_EQ(argv[3], "ssh");
  EXPECT_THAT(argv, ::testing::Not(::testing::Contains("BatchMode=yes")));
  EXPECT_THAT(argv, ::testing::Not(::testing::Contains("s3cret")));
}

TEST(RemoteHostTest, AuthMethodsProbeOffersNoKeysAndNeverPrompts) {
  RemoteHost host("rsw1", RemoteHost::Transport::SUSH2);

  auto argv = host.authMethodsArgv();

  ASSERT_FALSE(argv.empty());
  EXPECT_EQ(argv.front(), "ssh");
  EXPECT_THAT(
      argv,
      IsSupersetOf({"BatchMode=yes", "PubkeyAuthentication=no", "root@rsw1"}));
}

TEST(RemoteHostTest, OffersPasswordAuth) {
  EXPECT_TRUE(offersPasswordAuth(
      "root@rsw1: Permission denied (publickey,password).\n"));
  EXPECT_TRUE(offersPasswordAuth(
      "root@rsw1: Permission denied (publickey,keyboard-interactive).\n"));
  EXPECT_FALSE(
      offersPasswordAuth("root@rsw1: Permission denied (publickey).\n"));
  EXPECT_FALSE(offersPasswordAuth(
      "ssh: Could not resolve hostname rsw1: Name or service not known\n"));
  EXPECT_FALSE(offersPasswordAuth(""));
}

TEST(SshAskPassTest, ScriptPrintsPassword) {
  SshAskPass askPass("pa ss'word");

  auto result = runLocalCommand({askPass.script().string()}, kTimeout);

  EXPECT_EQ(result.standardOut, "pa ss'word");
}

TEST(SshAskPassTest, WorksUnderARestrictiveUmask) {
  // Would strip the owner execute bit from the helper's creation mode.
  auto oldMask = ::umask(0177);
  SshAskPass askPass("s3cret");
  ::umask(oldMask);

  auto result = runLocalCommand({askPass.script().string()}, kTimeout);

  EXPECT_EQ(result.standardOut, "s3cret");
}
