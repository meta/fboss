// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <string_view>
#include <vector>
// Forward declarations only: this header is included widely, and the enums
// are used here by value without naming an enumerator.
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types_fwd.h"

namespace facebook::fboss {

class HostInfo;

// Where one service's config lives and how a rollback applies it. The
// service's current path is intentionally absent: it is resolved only after an
// operation has determined that this domain is needed.
struct ConfigDomainInfo {
  cli::ServiceType service;
  std::string name; // "Agent" / "BGP" (diff section headers, logs)
  std::string sessionPath; // staged edits (~/.fboss2/...)
  std::string gitRelPath; // path within the /etc/coop git repo
  std::string desiredPath; // CLI-owned, git-tracked config
  // Minimum action used when a rollback changes this domain. rollback()
  // promotes this to the highest level recorded by the commits being undone.
  cli::ConfigActionLevel rollbackActionLevel;
};

/**
 * One service's config, as ConfigSession sees it. ConfigSession owns the
 * session, the metadata and the git history; everything that depends on which
 * service a config belongs to is behind this interface.
 */
class ConfigDomain {
 public:
  virtual ~ConfigDomain() = default;

  virtual const ConfigDomainInfo& info() const = 0;

  // The config file the running service reports that it reads.
  virtual std::string queryCurrentPath(const HostInfo& hostInfo) const = 0;

  // True when the service cannot run without a config, so a missing, empty or
  // unreadable config is an error. False when it starts from defaults.
  virtual bool requiresConfig() const = 0;

  // The text in the form serialize() writes. Throws if it does not parse.
  virtual std::string normalize(const std::string& json) const = 0;

  // Compares two config texts by value, not by bytes. Throws if either does
  // not parse.
  virtual bool equal(std::string_view a, std::string_view b) const = 0;

  // The in-memory copy that commands edit.
  virtual bool loaded() const = 0;
  virtual void load(const std::string& json) = 0;
  virtual void unload() = 0;
  virtual std::string serialize() const = 0;

  // Decides how apply() will reach the service at this level. Throws if it
  // cannot. apply() runs it before it reloads or restarts anything.
  virtual void prepare(
      cli::ConfigActionLevel level,
      const HostInfo& hostInfo) = 0;

  // Makes the service pick up its on-disk config at the given level and
  // returns the systemd units that were restarted or reloaded. Returns only
  // once the service can take further commands.
  virtual std::vector<std::string> apply(
      cli::ConfigActionLevel level,
      const HostInfo& hostInfo) = 0;

  // What apply() does at this level, e.g. "warmboot" or "restart".
  virtual std::string actionName(cli::ConfigActionLevel level) const = 0;
};

} // namespace facebook::fboss
