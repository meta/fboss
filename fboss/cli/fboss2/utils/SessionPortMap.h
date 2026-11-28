// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/cli/fboss2/utils/PortMap.h"

namespace facebook::fboss::utils {

/**
 * A PortMap over the agent config of the current config session.
 *
 * It is built on construction and never refreshed. The PortMap points into
 * the config, so keep the object local to one lookup sequence and construct a
 * new one after adding or removing ports or interfaces.
 */
class SessionPortMap {
 public:
  // Built from ConfigSession::getInstance().config<AgentConfigDomain>().
  SessionPortMap();

  // Not copyable: a copy would duplicate every lookup map of the PortMap, and
  // this object is meant to stay local to one lookup sequence.
  SessionPortMap(const SessionPortMap&) = delete;
  SessionPortMap& operator=(const SessionPortMap&) = delete;
  SessionPortMap(SessionPortMap&&) = default;
  SessionPortMap& operator=(SessionPortMap&&) = default;
  ~SessionPortMap() = default;

  const PortMap& operator*() const {
    return portMap_;
  }
  const PortMap* operator->() const {
    return &portMap_;
  }

 private:
  PortMap portMap_;
};

} // namespace facebook::fboss::utils
