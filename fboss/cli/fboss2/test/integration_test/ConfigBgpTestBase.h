// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

/**
 * Shared fixture + helpers for BGP config integration tests.
 *
 * BGP config is not pushed into the agent's running config like the
 * SwitchConfig-backed commands. Instead the CLI stages a BGP++
 * thrift-compatible JSON file at ~/.fboss2/bgp_config.json; `config session
 * commit` promotes it to /etc/coop/bgpcpp/bgpcpp.conf and restarts the bgpd
 * daemon. These helpers let the global-attribute tests and the
 * session-lifecycle tests share the staging, commit, daemon-state, and
 * system-config plumbing.
 */

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/transport/TTransportException.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

class ConfigBgpTestBase : public Fboss2IntegrationTest {
 protected:
  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    if (bgpDaemonActiveState() != "active") {
      GTEST_SKIP() << "skipping because bgp is not active";
    }
    // systemd "active" is a point sample: on an image where bgpd cannot hold
    // its boot config it flaps activating -> active -> failed, and a single
    // sample can land inside the brief active window (seen on fboss-sim,
    // where the sibling tests' samples all landed on inactive and skipped).
    // Require the daemon to actually answer its thrift RPC before running,
    // and skip otherwise — the same skip the rest of the flap cycle produces.
    try {
      readRunningBgpConfigViaRpc(std::chrono::seconds(10));
    } catch (const std::exception& ex) {
      GTEST_SKIP() << "skipping because bgpd is active but not answering its "
                   << "thrift RPC (flapping unit?): " << ex.what();
    }
    // Snapshot the config bgpd runs with; TearDown restores it, so committed
    // test objects never outlive the test regardless of where it failed.
    haveBgpSnapshot_ = folly::readFile(kBgpDaemonConfigPath, bgpSnapshot_);
    if (haveBgpSnapshot_) {
      bgpSnapshotConfig_ = parseBgpConfig(bgpSnapshot_);
    }
  }

  void TearDown() override {
    restoreBgpConfigSnapshot();
    Fboss2IntegrationTest::TearDown();
  }

  // Staged session file written by the CLI (~/.fboss2/bgp_config.json).
  std::string bgpSessionPath() const {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): HOME is read-only in practice
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
      throw std::runtime_error("HOME environment variable not set");
    }
    return std::string(home) + "/.fboss2/bgp_config.json";
  }

  // System config consumed by the bgpd daemon: the stable path bgpd is started
  // with (--config). After the first commit this is a symlink to the promoted
  // bgpcpp/bgpcpp.conf; before it, it is the plain file the bgp++ RPM installs.
  // Reading it (rather than the promoted path directly) means the fixture does
  // not require a commit to have happened, so these tests can run first on a
  // freshly imaged box.
  std::string systemBgpConfigPath() const {
    return "/etc/coop/bgpcpp.conf";
  }

  // Remove any pre-existing staged BGP session so each test starts clean.
  void clearBgpSession() const {
    std::remove(bgpSessionPath().c_str());
  }

  folly::dynamic readBgpSessionConfig() const {
    std::string content;
    if (!folly::readFile(bgpSessionPath().c_str(), content)) {
      throw std::runtime_error(
          "Failed to read BGP session config at " + bgpSessionPath());
    }
    return folly::parseJson(content);
  }

  folly::dynamic readSystemBgpConfig() const {
    std::string content;
    if (!folly::readFile(systemBgpConfigPath().c_str(), content)) {
      const int promotedErrno = errno;
      // On a box where no BGP commit ever happened, the promoted
      // bgpcpp/bgpcpp.conf doesn't exist yet; the effective config is the
      // plain file the RPM installed at /etc/coop/bgpcpp.conf (a symlink to
      // the promoted file afterwards, so it is always safe to read).
      if (promotedErrno != ENOENT) {
        throw std::runtime_error(
            "Failed to read system BGP config at " + systemBgpConfigPath() +
            ": " + folly::errnoStr(promotedErrno));
      }
      if (!folly::readFile(kBgpDaemonConfigPath, content)) {
        throw std::runtime_error(
            "Failed to read system BGP config: " + systemBgpConfigPath() +
            " does not exist and reading " + kBgpDaemonConfigPath +
            " failed: " + folly::errnoStr(errno));
      }
    }
    return folly::parseJson(content);
  }

  // The running config as reported by the bgpd daemon itself, over its
  // TBgpService::getRunningConfig thrift RPC. Unlike readSystemBgpConfig()
  // (the promoted file on disk, which only proves what the CLI wrote), this
  // proves the daemon parsed and adopted the config after the post-commit
  // restart. Mirrors Fboss2IntegrationTest::getRunningConfig() for the agent.
  //
  // Retries on connection errors: systemd reports bgpd "active" as soon as
  // the process starts, but the thrift server only binds its port a few
  // seconds later — an RPC issued right after a commit-triggered restart
  // would otherwise race that window and get ECONNREFUSED.
  folly::dynamic readRunningBgpConfigViaRpc(
      std::chrono::seconds timeout = std::chrono::seconds(90)) const {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    HostInfo hostInfo("localhost");
    std::string configStr;
    while (true) {
      try {
        auto client = utils::createClient<apache::thrift::Client<
            facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
        client->sync_getRunningConfig(configStr);
        break;
      } catch (const std::exception& ex) {
        // A NOT_OPEN transport exception (connect refused / connection
        // closed) is the expected bind-window race; any other RPC error is
        // worth surfacing even though we keep retrying until the deadline.
        // The refused connect reaches us as a channel-level
        // TTransportException wrapping the socket error, so the inner errno
        // is gone but the NOT_OPEN type survives.
        const auto* tex =
            dynamic_cast<const apache::thrift::transport::TTransportException*>(
                &ex);
        if (tex == nullptr ||
            tex->getType() !=
                apache::thrift::transport::TTransportException::NOT_OPEN) {
          XLOG(WARN) << "BGP getRunningConfig failed with an unexpected "
                        "error (not a connection refused error): "
                     << ex.what();
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          throw;
        }
        // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
    return folly::parseJson(configStr);
  }

  // Root of the git repo that versions both the agent config (cli/agent.conf)
  // and the BGP config (bgpcpp/bgpcpp.conf).
  std::string coopRepoDir() const {
    return "/etc/coop";
  }

  // Current git HEAD sha of the /etc/coop repo (trimmed; "" on failure).
  // runCmd uses folly::Subprocess, which does not search PATH, so git is
  // invoked by full path. -c safe.directory mirrors the CLI's Git class:
  // /etc/coop is typically owned by another user (e.g. coop), which git
  // otherwise rejects as dubious ownership.
  std::string gitHead() const {
    auto r = runCmd(
        {"/usr/bin/git",
         "-c",
         "safe.directory=" + coopRepoDir(),
         "-C",
         coopRepoDir(),
         "rev-parse",
         "HEAD"});
    std::string s = r.stdout;
    while (!s.empty() &&
           (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
      s.pop_back();
    }
    return s;
  }

  // True if bgpcpp/bgpcpp.conf is tracked (present in the tree) at the given
  // revision. `git cat-file -e <sha>:<path>` exits 0 iff the blob exists.
  bool bgpTrackedAtRevision(const std::string& sha) const {
    auto r = runCmd(
        {"/usr/bin/git",
         "-c",
         "safe.directory=" + coopRepoDir(),
         "-C",
         coopRepoDir(),
         "cat-file",
         "-e",
         sha + ":bgpcpp/bgpcpp.conf"});
    return r.exitCode == 0;
  }

  // Stage a `config protocol bgp global <attr> <value>` change (no commit).
  // Returns the staged session JSON.
  folly::dynamic runBgpGlobal(const std::string& attr, const std::string& value)
      const {
    clearBgpSession();
    XLOG(INFO) << "Running: config protocol bgp global " << attr << " "
               << value;
    auto result = runCli({"config", "protocol", "bgp", "global", attr, value});
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    // Handler error strings are printed to stdout with exit code 0, so guard
    // against silent failures by rejecting an "Error" prefix.
    EXPECT_THAT(result.stdout, ::testing::Not(::testing::HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // Reset bgpd's systemd start-limit burst counter. Tests commit several
  // times in quick succession, and each commit restarts bgpd; without this,
  // systemd's default StartLimitBurst (5 starts / 10s) refuses a rapid restart
  // with "start limit hit" — a test artifact, not a product issue.
  void resetBgpDaemonLimit() const {
    runCmd({"/usr/bin/systemctl", "reset-failed", "bgpd"});
  }

  // Run `config session commit` and return the short SHA parsed from
  // "Config session committed successfully as <sha>". Empty string on failure.
  std::string commitAndGetSha() const {
    resetBgpDaemonLimit();
    auto r = runCli({"config", "session", "commit"});
    EXPECT_EQ(r.exitCode, 0) << "stderr=" << r.stderr;
    const std::string marker = "as ";
    auto pos = r.stdout.find(marker);
    if (pos == std::string::npos) {
      return "";
    }
    pos += marker.size();
    auto end = r.stdout.find_first_of(" .\n", pos);
    return r.stdout.substr(pos, end - pos);
  }

  // Stage a global change, commit it, wait for bgpd to come back, and return
  // bgpd's own running config as reported over its getRunningConfig RPC.
  //
  // The returned config is the daemon's adopted view, which is strictly
  // stronger than the promoted file on disk (readSystemBgpConfig): a value
  // present here proves the commit promoted /etc/coop/bgpcpp/bgpcpp.conf AND
  // that bgpd parsed and adopted it after the commit-triggered restart, not
  // merely that the CLI wrote the file. The running config shares the same
  // BgpConfig JSON schema as the promoted file, so callers assert the same
  // field paths against it.
  //
  // A no-op commit (staged value already current) yields no SHA and is fine:
  // the caller's assertions on the returned config still verify the value;
  // commit/restart mechanics are covered by ConfigBgpSessionTest.
  folly::dynamic setAndCommit(const std::string& attr, const std::string& value)
      const {
    discardSession();
    runBgpGlobal(attr, value);
    commitAndGetSha();
    EXPECT_TRUE(waitForBgpDaemonActive())
        << "bgpd did not return active after commit; state="
        << bgpDaemonActiveState();
    return readRunningBgpConfigViaRpc();
  }

  // Trailing-whitespace-trimmed stdout of a `systemctl` query for bgpd.
  std::string systemctlValue(const std::string& property) const {
    // Full path: folly::Subprocess (used by runCmd) does not search PATH.
    auto r = runCmd(
        {"/usr/bin/systemctl", "show", "bgpd", "-p", property, "--value"});
    std::string s = r.stdout;
    while (!s.empty() &&
           (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
      s.pop_back();
    }
    return s;
  }

  // "active" / "inactive" / "failed" / ... (trimmed).
  std::string bgpDaemonActiveState() const {
    auto r = runCmd({"/usr/bin/systemctl", "is-active", "bgpd"});
    std::string s = r.stdout;
    while (!s.empty() &&
           (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
      s.pop_back();
    }
    return s;
  }

  // bgpd main PID per systemd ("0" if not running).
  std::string bgpDaemonMainPid() const {
    return systemctlValue("MainPID");
  }

  // Poll until bgpd reports active or the timeout elapses.
  bool waitForBgpDaemonActive(
      std::chrono::seconds timeout = std::chrono::seconds(60)) const {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (bgpDaemonActiveState() == "active") {
        return true;
      }
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return bgpDaemonActiveState() == "active";
  }

 private:
  // The stable path bgpd reads (--config): a plain file on a device the CLI
  // never committed on, a symlink into bgpcpp/ afterwards. Reading follows
  // the symlink, so the snapshot is always bgpd's effective config.
  static constexpr auto kBgpDaemonConfigPath = "/etc/coop/bgpcpp.conf";

  // Deserialized view of a serialized BgpConfig; nullopt if unparseable.
  static std::optional<bgp::thrift::BgpConfig> parseBgpConfig(
      const std::string& content) {
    try {
      return apache::thrift::SimpleJSONSerializer::deserialize<
          bgp::thrift::BgpConfig>(content);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  // True when `content` matches the snapshot: by thrift struct equality when
  // both sides parse (formatting-only differences from a commit round-trip
  // don't count), else by bytes.
  bool matchesBgpSnapshot(const std::string& content) const {
    auto current = parseBgpConfig(content);
    if (bgpSnapshotConfig_ && current) {
      return *current == *bgpSnapshotConfig_;
    }
    return content == bgpSnapshot_;
  }

  void restoreBgpConfigSnapshot() {
    if (!haveBgpSnapshot_) {
      return; // SetUp skipped or there was no config to protect
    }
    discardSession();
    clearBgpSession();
    std::string current;
    if (folly::readFile(kBgpDaemonConfigPath, current) &&
        matchesBgpSnapshot(current)) {
      return; // semantically unchanged; skip the restart
    }
    // Write the real file, never through the bgpd.conf symlink.
    std::string target = std::filesystem::exists(systemBgpConfigPath())
        ? systemBgpConfigPath()
        : std::string(kBgpDaemonConfigPath);
    EXPECT_TRUE(folly::writeFile(bgpSnapshot_, target.c_str()))
        << "failed to restore BGP config snapshot to " << target;
    resetBgpDaemonLimit();
    runCmd({"/usr/bin/systemctl", "restart", "bgpd"});
    EXPECT_TRUE(waitForBgpDaemonActive())
        << "bgpd did not return active after snapshot restore; state="
        << bgpDaemonActiveState();
  }

  bool haveBgpSnapshot_{false};
  // Raw bytes, written back verbatim on restore; the typed view drives the
  // changed/unchanged decision.
  std::string bgpSnapshot_;
  std::optional<bgp::thrift::BgpConfig> bgpSnapshotConfig_;
};

} // namespace facebook::fboss
