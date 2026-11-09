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
#include "fboss/cli/fboss2/session/FbossServiceUtil.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/PortMap.h"

namespace facebook::bgp::thrift {
class BgpConfig;
}

namespace facebook::fboss::cfg {
class AgentConfig;
}

namespace facebook::fboss {

class ConfigFileManager;

/**
 * ConfigSession manages configuration editing sessions for the fboss2 CLI.
 *
 * OVERVIEW:
 * ConfigSession provides a session-based workflow for editing FBOSS agent
 * configuration. It maintains a session file that can be edited and then
 * atomically committed to the system configuration with Git version control.
 *
 * SINGLETON PATTERN:
 * ConfigSession is typically accessed via getInstance(), which currently
 * returns a per-UNIX-user singleton. The singleton is created on first
 * access and persists across CLI commands until commit() is called.
 *
 * TYPICAL USAGE:
 * Most CLI commands follow this pattern:
 *   1. Get the singleton: auto& session = ConfigSession::getInstance();
 *   2. Access the config: auto& config = session.getAgentConfig();
 *   3. Modify the config: config.sw()->ports()[0].name() = "eth0";
 *   4. Save changes: session.saveConfig();
 *
 * The changes are saved to the session file (currently ~/.fboss2/agent.conf)
 * but are NOT applied to the running system until explicitly committed.
 *
 * COMMIT FLOW:
 * To apply session changes to the system:
 *   1. User runs: fboss2 config session commit
 *   2. ConfigSession::commit() is called, which:
 *      a. Atomically writes the session config to /etc/coop/cli/agent.conf
 *      b. Updates the config path reported by the affected service
 *      c. Creates a Git commit with the updated agent.conf and metadata
 *      d. Calls reloadConfig() on wedge_agent (or restarts it for
 *         AGENT_RESTART changes)
 *   3. The session file is cleared (ready for next edit session)
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
 * CONFIGURATION FILES:
 * - Session file: ~/.fboss2/agent.conf (per-user, temporary edits)
 * - Current config: discovered lazily from each service's `config` option
 * - CLI config: /etc/coop/cli/agent.conf (actual config file, Git-versioned)
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

  // Static path getters - can be called without creating a session instance.
  // These are useful for checking if session files exist without triggering
  // session initialization.
  static std::string getSessionDir();
  static std::string getSessionConfigPathStatic();
  static std::string getSessionMetadataPathStatic();
  // ~/.fboss2/bgp_config.json — staged BGP edits (used by `config session
  // clear` without instantiating a session).
  static std::string getBgpSessionConfigPathStatic();

  // All per-session staged files under ~/.fboss2 that `config session clear`
  // should remove: every config domain's staged file plus the session
  // metadata. Static so callers can clear a session without getInstance()
  // (which would create one). A new config domain adds one entry here rather
  // than a new block in the clear command.
  static std::vector<std::string> stagedSessionFilePaths();

  // Get the path to the session config file (~/.fboss2/agent.conf)
  std::string getSessionConfigPath() const;

  // Get the legacy default agent config path under the system config directory.
  std::string getSystemConfigPath() const;

  // Get the validated COOP config path reported by a running service.
  // Discovery is lazy and falls back to the last committed path for recovery.
  std::string getCurrentConfigPath(cli::ServiceType service) const;

  // Get the path to the CLI config directory (/etc/coop/cli)
  std::string getCliConfigDir() const;

  // Get the path to the actual CLI config file (/etc/coop/cli/agent.conf)
  std::string getCliConfigPath() const;

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

  // Describes one config "domain" managed by a session. The service's current
  // path is intentionally absent: it is resolved only after an operation has
  // determined that this domain is needed.
  struct ConfigDomain {
    cli::ServiceType service; // AGENT / BGP -- feeds applyServiceActions()
    std::string name; // "Agent" / "BGP" (diff section headers, logs)
    std::string sessionPath; // staged edits (~/.fboss2/...)
    std::string gitRelPath; // path within the /etc/coop git repo
    std::string desiredPath; // CLI-owned, git-tracked config
    // Minimum action used when a rollback changes this domain: HITLESS reloads
    // the agent; SERVICE_RESTART restarts bgpd. rollback() promotes this to the
    // highest level recorded by the commits being undone (see
    // rolledBackActionLevels()).
    cli::ConfigActionLevel rollbackActionLevel;
  };

  // The config domains this session manages (agent + BGP), in a stable order
  // (agent first). Public so `config session diff` can share the same list.
  std::vector<ConfigDomain> configDomains() const;

  // Staged content for a domain, or nullopt if no session edit is staged.
  // Throws if the session file exists but cannot be read. Public so `config
  // session diff` shares the same "is it staged + its content" primitive that
  // commit()/rollback() use.
  std::optional<std::string> readStagedContent(
      const ConfigDomain& domain) const;

  // Atomically commit the session to /etc/coop/cli/agent.conf and create a git
  // commit. For HITLESS changes, also calls reloadConfig() on the agent.
  // For AGENT_RESTART changes, restarts the agent via systemd.
  // Returns CommitResult with git commit SHA and action level.
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

  // Check if an agent config session exists (~/.fboss2/agent.conf staged).
  bool sessionExists() const;

  // Check if any committable session is staged: either an agent config session
  // (agent.conf) or a protocol session persisted outside agent.conf (e.g. a
  // BGP++ session at ~/.fboss2/bgp_config.json recorded via
  // recordServiceAction()). The commit path uses this rather than
  // sessionExists() so a BGP-only change can be committed.
  bool hasActiveSession() const;

  // Get the parsed agent configuration
  cfg::AgentConfig& getAgentConfig();
  const cfg::AgentConfig& getAgentConfig() const;

  // Get the PortMap for port-to-interface lookups
  utils::PortMap& getPortMap();
  const utils::PortMap& getPortMap() const;

  // Regenerate the cached PortMap from the current in-memory agentConfig_.
  // Call this after mutating the config (e.g. adding a port) so that
  // subsequent getPortMap() lookups reflect the change.
  void rebuildPortMap();

  // Serialize the given service's typed config (AGENT -> agentConfig_,
  // BGP -> bgpConfig_) to that domain's staged session file, and record the
  // command + bump the service's required action level (if the new level is
  // higher than the current one). One generic entry point for every service.
  void saveConfig(cli::ServiceType service, cli::ConfigActionLevel actionLevel);
  // Save the configuration for AGENT service with HITLESS action level.
  void saveConfig();

  // Record that a service requires an action for a config change that is
  // persisted OUTSIDE the agent config file (e.g. BGP++ writes its own
  // ~/.fboss2/bgp_config.json). This updates the action level for the service
  // in the shared session metadata and persists it, WITHOUT rewriting
  // agent.conf. A subsequent `config session commit` then applies the recorded
  // action (e.g. restart bgpd).
  void recordServiceAction(
      cli::ServiceType service,
      cli::ConfigActionLevel actionLevel);

  // ==================== BGP configuration ====================
  // ConfigSession owns the BGP config as a typed bgp::thrift::BgpConfig,
  // exactly the way it owns cfg::AgentConfig for the agent. It is BGP-*aware*
  // but agnostic about the config's internal structure: getBgpConfig() exposes
  // the WHOLE typed config (router_id, ASNs, peers, peer_groups, networks, ...)
  // and commands mutate whichever typed fields they need -- mirroring how
  // interface commands mutate getAgentConfig().sw()->ports(). ConfigSession has
  // no notion of "global" vs "peer" vs "peer-group".

  // Typed, mutable view of the entire BGP config. Lazily seeded from the staged
  // ~/.fboss2/bgp_config.json, else the path reported by bgpd, else schema
  // defaults. Mirrors getAgentConfig().
  bgp::thrift::BgpConfig& getBgpConfig();
  const bgp::thrift::BgpConfig& getBgpConfig() const;

  // Convenience wrapper over saveConfig(BGP, SERVICE_RESTART): persists the
  // typed BGP config to ~/.fboss2/bgp_config.json and records that bgpd must be
  // restarted on the next `config session commit`. Mirrors the no-arg
  // saveConfig() for the agent.
  void saveBgpConfig();

  // ~/.fboss2/bgp_config.json (staged BGP edits)
  std::string getBgpSessionConfigPath() const;
  // /etc/coop/bgpcpp/bgpcpp.conf (CLI-owned, Git-versioned desired config)
  std::string getBgpSystemConfigPath() const;
  // Whether a BGP session is staged (~/.fboss2/bgp_config.json exists)
  bool bgpSessionExists() const;

  // Get the Git instance for this config session
  // Used to access the Git repository for history, rollback, etc.
  Git& getGit();
  const Git& getGit() const;

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

  // Get the systemd service name for a service type
  static std::string getServiceName(cli::ServiceType service);

  // Get the list of commands executed in this session
  const std::vector<std::string>& getCommands() const;

 protected:
  // Constructor for testing with custom paths
  ConfigSession(
      std::string sessionConfigDir,
      std::string systemConfigDir,
      SessionInit init = SessionInit::CreateIfAbsent);

  // Constructor for testing with custom paths and mock FbossServiceUtil
  ConfigSession(
      std::string sessionConfigDir,
      std::string systemConfigDir,
      std::unique_ptr<FbossServiceUtil> fbossServiceUtil);

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

  // Apply actions (restart or reload) to all services based on their action
  // levels. For the restart levels, restarts the service. For HITLESS, reloads
  // the config.
  // Returns a map of service type to list of actual systemd service names.
  std::map<cli::ServiceType, std::vector<std::string>> applyServiceActions(
      const std::map<cli::ServiceType, cli::ConfigActionLevel>& actions,
      const HostInfo& hostInfo);

 protected:
  // Service orchestration for systemd operations
  std::unique_ptr<FbossServiceUtil> fbossServiceUtil_;

 private:
  std::string sessionConfigDir_; // Typically ~/.fboss2
  std::string systemConfigDir_; // Typically /etc/coop
  bool readOnly_{false};
  mutable std::map<cli::ServiceType, std::string> currentConfigPaths_;
  std::string username_;

  // Git instance for version control operations
  std::unique_ptr<Git> git_;

  // Lazy-initialized configuration and port map. agentConfig_ is null until
  // loadConfig(AGENT) populates it (null == "not loaded"), which is why it is
  // a pointer -- that also keeps the heavy generated type out of this header.
  std::unique_ptr<cfg::AgentConfig> agentConfig_;
  std::unique_ptr<utils::PortMap> portMap_;

  // Typed view of the entire BGP config, mirroring agentConfig_: null until
  // loadConfig(BGP) populates it.
  std::unique_ptr<bgp::thrift::BgpConfig> bgpConfig_;

  // /etc/coop/bgpcpp (directory holding the bgpd daemon's config)
  std::string getBgpSystemConfigDir() const;
  // ==================== Per-domain primitives ====================
  // Shared building blocks used by commit()/rollback() so both the agent and
  // BGP domains go through identical logic (see ConfigDomain /
  // configDomains()). readStagedContent() is declared public above.

  // Remove a domain's staged session file and drop its in-memory cache so the
  // next access re-seeds from disk. Called after a successful commit.
  void clearStagedDomain(const ConfigDomain& domain);
  // Compare two serialized configs for a domain by deserializing each into its
  // typed thrift struct (cfg::AgentConfig / bgp::thrift::BgpConfig) and using
  // struct equality. This is a SEMANTIC comparison, so formatting-only
  // differences (whitespace, key ordering, integer-vs-string map keys, a
  // raw-seeded file vs a round-tripped one) do not count as a change. Falls
  // back to a byte comparison when either side is empty or fails to parse.
  bool domainContentEqual(
      const ConfigDomain& domain,
      std::string_view a,
      std::string_view b) const;

  // git relative path of the bgpd config tracked in the /etc/coop repo.
  static constexpr auto kBgpGitRelPath = "bgpcpp/bgpcpp.conf";
  // git relative path of the agent config tracked in the /etc/coop repo.
  static constexpr auto kAgentGitRelPath = "cli/agent.conf";
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

  // Path to the system metadata file (in the Git repo)
  std::string getSystemMetadataPath() const;

  // Path to the session metadata file (in the user's home directory)
  std::string getMetadataPath() const;

  // Load/save metadata (action levels and commands) from disk
  void loadMetadata();
  void saveMetadata();

  // Agent actions require the running agent's multi-switch state; actions for
  // other services do not.
  virtual void ensureFbossServiceUtil(
      const HostInfo& hostInfo,
      bool needsAgentState);

  // Initialize shared session state without loading either service config.
  void initializeSession(SessionInit init);
  // Load one service's typed config, resolving its current path only when no
  // staged config exists.
  void loadConfig(cli::ServiceType service);

  ConfigDomain getConfigDomain(cli::ServiceType service) const;
  ConfigFileManager fileManagerFor(const ConfigDomain& domain) const;
  void ensureDomainBaseline(
      const ConfigDomain& domain,
      const std::string& currentContent);

  std::string validateCurrentConfigPath(
      cli::ServiceType service,
      const std::string& path) const;
  std::optional<std::string> readCommittedCurrentConfigPath(
      cli::ServiceType service) const;

  // Initialize the Git repository if needed
  void initializeGit();

  bool fbossServiceUtilHasAgentState_{false};
};

} // namespace facebook::fboss
