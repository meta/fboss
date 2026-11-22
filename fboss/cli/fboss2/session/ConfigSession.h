/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigDomainRegistry.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

class ConfigFileManager;

/**
 * ConfigSession manages configuration editing sessions for the fboss2 CLI.
 *
 * OVERVIEW:
 * ConfigSession provides a session-based workflow for editing the config of
 * FBOSS services (agent, BGP). It maintains one session file per service
 * that can be edited and then atomically committed to the system
 * configuration with Git version control.
 *
 * SINGLETON PATTERN:
 * ConfigSession is typically accessed via getInstance(), which currently
 * returns a per-UNIX-user singleton. The singleton is created on first
 * access and persists across CLI commands until commit() is called.
 *
 * TYPICAL USAGE:
 * Most CLI commands follow this pattern:
 *   1. Get the singleton: auto& session = ConfigSession::getInstance();
 *   2. Access the config:
 *        auto& config = session.config<AgentConfigDomain>();
 *   3. Modify the config: config.sw()->ports()[0].name() = "eth0";
 *   4. Save changes: session.save<AgentConfigDomain>(), or
 *        session.saveConfig(cli::ServiceType::AGENT, level) when the change
 *        needs more than the service's least disruptive action.
 *
 * The changes are saved to the service's session file (~/.fboss2/agent.conf
 * for the agent, ~/.fboss2/bgp_config.json for BGP) but are NOT applied to
 * the running system until explicitly committed.
 *
 * COMMIT FLOW:
 * To apply session changes to the system:
 *   1. User runs: fboss2-dev config session commit
 *   2. ConfigSession::commit() is called, which, for every service whose
 *      staged config differs from its committed one:
 *      a. Atomically writes the staged config to the service's Git-tracked
 *         file (e.g. /etc/coop/cli/agent.conf) and points the path the
 *         service reads at it
 *      b. Applies the recorded action: a config reload for HITLESS, a
 *         restart of the service for SERVICE_RESTART or
 *         DISRUPTIVE_SERVICE_RESTART
 *      c. Creates one Git commit with the updated configs and the metadata
 *   3. The session files are cleared (ready for next edit session)
 *
 * ROLLBACK FLOW:
 * To revert to a previous configuration:
 *   1. User runs: fboss2-dev config rollback [<revision>]
 *   2. ConfigSession::rollback() is called, which:
 *      a. Reads the target revision's agent.conf from Git history
 *      b. Atomically writes it to /etc/coop/cli/agent.conf
 *      c. Creates a new Git commit indicating the rollback
 *      d. Calls wedge_agent to reload the configuration (or restarts
 *         it if necessary)
 *
 * CONFIGURATION FILES (per service; see each ConfigDomain for the paths):
 * - Session file: ~/.fboss2/agent.conf, ~/.fboss2/bgp_config.json (per-user,
 *   temporary edits)
 * - Current config: discovered lazily from each service's `config` option
 * - CLI config: /etc/coop/cli/agent.conf, /etc/coop/bgpcpp/bgpcpp.conf
 *   (actual config files, Git-versioned)
 * - Metadata: /etc/coop/cli/cli_metadata.json (commit metadata, Git-versioned)
 *
 * VERSION CONTROL:
 * The /etc/coop directory is a local Git repository. Each commit() creates
 * a Git commit with the updated config. History is retrieved via git log,
 * and rollback reads from Git history rather than using git revert.
 *
 * THREAD SAFETY:
 * ConfigSession is NOT thread-safe. It is designed for single-threaded CLI
 * command execution. The code is safe in face of concurrent usage from
 * multiple processes.
 */
class ConfigSession {
 public:
  // ReadOnly skips seeding ~/.fboss2 when no session exists; used by commands
  // (history, session diff) that must not stage one.
  enum class SessionInit { CreateIfAbsent, ReadOnly };

  explicit ConfigSession(SessionInit init = SessionInit::CreateIfAbsent);

  virtual ~ConfigSession();

  // Get or create the current config session. Service configuration is not
  // loaded until a command first accesses that service.
  static ConfigSession& getInstance(
      SessionInit init = SessionInit::CreateIfAbsent);

  // Reset the singleton (for testing only).
  // Destroys the current instance so the next getInstance() creates a fresh
  // session, re-reading config from disk.
  static void resetInstance();

  // All per-session staged files under ~/.fboss2 that `config session clear`
  // should remove: every config domain's staged file plus the session
  // metadata. Static so callers can clear a session without getInstance()
  // (which would create one).
  static std::vector<std::string> stagedSessionFilePaths();

  // The file under ~/.fboss2 that holds a service's staged edits.
  std::string stagedPath(cli::ServiceType service) const;

  // Get the validated COOP config path reported by a running service.
  // Discovery is lazy and falls back to the last committed path for recovery.
  std::string getCurrentConfigPath(cli::ServiceType service) const;

  // Result of a commit operation
  struct CommitResult {
    std::string commitSha; // The git commit SHA of the committed config
    // Maps each service to the action level that was applied during commit.
    // Services not in this map had no action taken.
    std::map<cli::ServiceType, cli::ConfigActionLevel> actions;
    // Maps each service to the list of actual systemd service names that were
    // restarted/reloaded (e.g., "fboss_sw_agent", "fboss_hw_agent@0", etc.)
    std::map<cli::ServiceType, std::vector<std::string>> serviceNames;
  };

  // The config domains this session manages (agent, BGP), in a stable
  // order (agent first). Public so `config session diff` can share the same
  // list.
  std::vector<ConfigDomainInfo> configDomains() const;

  // What applying this level means for the service, e.g. "warmboot" for the
  // agent or "restart" for bgpd.
  std::string actionName(cli::ServiceType service, cli::ConfigActionLevel level)
      const;

  // Staged content for a domain, or nullopt if no session edit is staged.
  // Throws if the session file exists but cannot be read. Public so `config
  // session diff` shares the same "is it staged + its content" primitive that
  // commit()/rollback() use.
  std::optional<std::string> readStagedContent(
      const ConfigDomainInfo& domain) const;

  // Atomically commit every service's staged config to its Git-tracked file,
  // apply each service's recorded action (a config reload for HITLESS, a
  // restart otherwise) and create a git commit.
  // Returns CommitResult with the git commit SHA and the actions applied.
  CommitResult commit(const HostInfo& hostInfo);

  // Rebase the session onto the current HEAD.
  // This is needed when someone else has committed changes while this session
  // was in progress. It computes the diff between the base config and the
  // session config, then applies that diff on top of the current HEAD.
  // Throws std::runtime_error if there are conflicts that cannot be resolved.
  void rebase();

  // Rollback to a specific revision (git commit SHA) or to the previous
  // revision Returns the git commit SHA of the new commit created for the
  // rollback
  std::string rollback(const HostInfo& hostInfo);
  std::string rollback(const HostInfo& hostInfo, const std::string& commitSha);

  // True when any service has staged edits.
  bool hasActiveSession() const;

  // The typed config of domain D (e.g. AgentConfigDomain), loaded on first
  // use. Edit it, then call save<D>().
  template <class D>
  auto& config() {
    auto& domain = domains_.get<D>();
    if (!domain.loaded()) {
      loadConfig(domain.info().service);
    }
    return domain.config();
  }
  // Read-only view. Throws unless the non-const overload loaded it first.
  template <class D>
  const auto& config() const {
    const auto& domain = domains_.get<D>();
    if (!domain.loaded()) {
      throwConfigNotLoaded(domain.info().name);
    }
    return domain.config();
  }

  // Stages domain D's config at the least disruptive level that makes its
  // service pick up a change. Use saveConfig() to record a higher level.
  template <class D>
  void save() {
    const auto& info = domains_.get<D>().info();
    saveConfig(info.service, info.minActionLevel);
  }

  // Serialize the given service's typed config to that domain's staged
  // session file, and record the command + bump the service's required action
  // level (if the new level is higher than the current one).
  void saveConfig(cli::ServiceType service, cli::ConfigActionLevel actionLevel);

  // Commits that changed any service's config or the commit metadata, most
  // recent first. This covers every revision rollback() walks.
  std::vector<GitCommit> history(size_t limit = 0) const;
  std::string head() const;
  // Resolves a ref or abbreviated SHA to a full SHA. Throws if it is unknown.
  std::string resolveRevision(const std::string& ref) const;
  // Throws if the path does not exist at the revision.
  std::string contentAt(
      const std::string& revision,
      const std::string& gitRelPath) const;

 protected:
  // Get the path to the CLI config directory (/etc/coop/cli)
  std::string getCliConfigDir() const;

  // Record the action a staged change to this service's config needs. Updates
  // the service's action level in the session metadata and persists it. A
  // later `config session commit` applies the recorded action.
  void recordServiceAction(
      cli::ServiceType service,
      cli::ConfigActionLevel actionLevel);

  // Update the required action level for the current session.
  // Tracks the highest action level across all config commands, per service.
  // Higher action levels take precedence (DISRUPTIVE_SERVICE_RESTART >
  // SERVICE_RESTART > HITLESS).
  void updateRequiredAction(
      cli::ServiceType service,
      cli::ConfigActionLevel actionLevel);

  // Get the current required action level for the session
  cli::ConfigActionLevel getRequiredAction(cli::ServiceType service) const;

  // Reset the required action level to HITLESS (called after successful commit)
  void resetRequiredAction(cli::ServiceType service);

  // Get the list of commands executed in this session
  const std::vector<std::string>& getCommands() const;

  const Git& getGit() const;

  // Constructor for testing with custom paths and caller-supplied config
  // domains.
  ConfigSession(
      std::string sessionConfigDir,
      std::string systemConfigDir,
      ConfigDomainRegistry domains,
      SessionInit init);

  // As above, but does not initialize the session.
  ConfigSession(
      std::string sessionConfigDir,
      std::string systemConfigDir,
      ConfigDomainRegistry domains);

  // Set the singleton instance (for testing only)
  static void setInstance(std::unique_ptr<ConfigSession> instance);

  // Read the command line for the current process from /proc/self/cmdline.
  // Returns the command arguments as a space-separated string,
  // e.g., "config interface eth1/1/1 mtu 9000"
  // Throws runtime_error if the command line cannot be read.
  // Virtual to allow tests to override with mock command lines.
  virtual std::string readCommandLineFromProc() const;

  virtual std::string queryLocalServiceConfigPath(
      cli::ServiceType service) const;

  // Apply each service's action through its config domain: a restart for the
  // restart levels, a reload for HITLESS.
  // Returns a map of service type to list of actual systemd service names.
  std::map<cli::ServiceType, std::vector<std::string>> applyServiceActions(
      const std::map<cli::ServiceType, cli::ConfigActionLevel>& actions,
      const HostInfo& hostInfo);

 private:
  // Out of line so this header needs neither fmt nor <stdexcept>.
  [[noreturn]] static void throwConfigNotLoaded(const std::string& domainName);

  std::string sessionConfigDir_; // Typically ~/.fboss2
  std::string systemConfigDir_; // Typically /etc/coop
  bool readOnly_{false};
  mutable std::map<cli::ServiceType, std::string> currentConfigPaths_;
  std::string username_;

  // Git instance for version control operations
  std::unique_ptr<Git> git_;

  // The per-service configs. Each domain holds its own typed copy, which is
  // loaded on first access.
  ConfigDomainRegistry domains_;

  // ==================== Per-domain primitives ====================
  // Shared building blocks used by commit()/rollback() so both the agent and
  // BGP domains go through identical logic (see configDomains()).
  // readStagedContent() is declared public above.

  // Remove a domain's staged session file and drop its in-memory cache so the
  // next access re-seeds from disk. Called after a successful commit.
  void clearStagedDomain(const ConfigDomainInfo& domain);
  // Compare two serialized configs for a domain by deserializing each into its
  // typed thrift struct and using
  // struct equality. This is a SEMANTIC comparison, so formatting-only
  // differences (whitespace, key ordering, integer-vs-string map keys, a
  // raw-seeded file vs a round-tripped one) do not count as a change. Falls
  // back to a byte comparison when either side is empty or fails to parse.
  bool domainContentEqual(
      const ConfigDomainInfo& domain,
      std::string_view a,
      std::string_view b) const;

  // git relative path of the CLI metadata tracked in the /etc/coop repo.
  static constexpr auto kMetadataGitRelPath = "cli/cli_metadata.json";
  // Like Git::fileAtRevision but returns "" instead of throwing when the path
  // does not exist at that revision (e.g. a pre-BGP commit). Used by
  // rebase/rollback/diff so a missing bgpcpp.conf is treated as empty.
  std::string fileAtRevisionOrEmpty(
      const std::string& revision,
      const std::string& gitRelPath) const;

  // Highest per-service action level recorded in the metadata of every commit
  // a rollback to resolvedSha would undo (i.e. commits in (resolvedSha, HEAD]).
  // Undoing a change needs at least the action level applying it did (e.g. a
  // VLAN membership change requires an agent warmboot in both directions), so
  // rollback() promotes each domain's default action to this level.
  // If resolvedSha is not found in the metadata history, the max over the
  // whole history is returned (conservative).
  std::map<cli::ServiceType, cli::ConfigActionLevel> rolledBackActionLevels(
      const std::string& resolvedSha) const;

  // Track the highest action level required for pending config changes per
  // service. Persisted to disk so it survives across CLI invocations within a
  // session.
  std::map<cli::ServiceType, cli::ConfigActionLevel> requiredActions_;

  // List of commands executed in this session, persisted to disk
  std::vector<std::string> commands_;

  // Git commit SHA that this session is based on (captured when session is
  // created). Used to detect if someone else committed changes while this
  // session was in progress.
  std::string base_;

  // Per-user session paths under ~/.fboss2, usable without an instance;
  // stagedSessionFilePaths() is the public way to reach them.
  static std::string getSessionDir();
  static std::string getSessionMetadataPathStatic();

  // Path to the system metadata file (in the Git repo)
  std::string getSystemMetadataPath() const;

  // Path to the session metadata file (in the user's home directory)
  std::string getMetadataPath() const;

  // Load/save metadata (action levels and commands) from disk
  void loadMetadata();
  void saveMetadata();

  // Initialize shared session state without loading either service config.
  void initializeSession(SessionInit init);
  // Load one service's typed config, resolving its current path only when no
  // staged config exists.
  void loadConfig(cli::ServiceType service);

  ConfigFileManager fileManagerFor(const ConfigDomainInfo& domain) const;
  void ensureDomainBaseline(
      const ConfigDomainInfo& domain,
      const std::string& currentContent);

  std::string validateCurrentConfigPath(
      cli::ServiceType service,
      const std::string& path) const;
  std::optional<std::string> readCommittedCurrentConfigPath(
      cli::ServiceType service) const;

  // Initialize the Git repository if needed
  void initializeGit();
};

} // namespace facebook::fboss
