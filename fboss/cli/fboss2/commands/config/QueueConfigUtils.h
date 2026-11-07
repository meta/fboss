/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss::utils {

// Reserved name: `queue-config default` edits SwitchConfig::defaultPortQueues,
// any other name edits a portQueueConfigs entry. Reserving it keeps the two
// disjoint.
inline constexpr auto kDefaultQueueConfigName = "default";

/**
 * Name of a queue config: either a portQueueConfigs key or the reserved
 * `default`.
 */
class QueueConfigName : public BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ QueueConfigName(std::vector<std::string> v);

  const std::string& getName() const {
    return data_[0];
  }

  bool isDefault() const {
    return data_[0] == kDefaultQueueConfigName;
  }
};

// Resolves `name` to the PortQueue list it designates, creating an empty
// portQueueConfigs entry if a named config does not exist yet.
std::vector<cfg::PortQueue>& queueConfigListForWrite(
    cfg::SwitchConfig& switchConfig,
    const QueueConfigName& name);

// Resolves `name` to the PortQueue list it designates, or nullptr if a named
// config does not exist. Callers that must not create an entry on a typo --
// delete, in particular -- use this rather than queueConfigListForWrite.
// `default` always resolves, since defaultPortQueues always exists.
std::vector<cfg::PortQueue>* findQueueConfigList(
    cfg::SwitchConfig& switchConfig,
    const QueueConfigName& name);

// Names of the ports whose Port::portQueueConfigName references `name`, so a
// caller can refuse to remove a queue config that is still bound and would
// leave those ports pointing at nothing.
//
// Always empty for the reserved `default`: no port names it explicitly, and
// every port without a binding falls back to it, so it can never be dangling.
std::vector<std::string> portsUsingQueueConfig(
    const cfg::SwitchConfig& switchConfig,
    const QueueConfigName& name);

/**
 * A queue id plus the attributes to apply to it.
 *
 * Parses command line arguments in the format:
 *   <queue-id> [<attr1> <val1...> [<attr2> <val2...> ...]]
 *
 * Every attribute takes a single value token except rate-limit, which takes
 * two or three (<kbps|pps> [<min>] <max>).
 *
 * For example:
 *   0 reserved-bytes 1000 weight 10 scheduling WEIGHTED_ROUND_ROBIN
 *   0 rate-limit kbps 12500000 12500000
 */
class QueueIdAndAttributes : public BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ QueueIdAndAttributes(std::vector<std::string> v);

  int16_t getQueueId() const {
    return queueId_;
  }

  const std::vector<std::pair<std::string, std::vector<std::string>>>&
  getAttributes() const {
    return attributes_;
  }

  const std::vector<std::string>& getAqmAttributes() const {
    return aqmAttributes_;
  }

 private:
  int16_t queueId_{0};
  std::vector<std::pair<std::string, std::vector<std::string>>> attributes_;
  std::vector<std::string> aqmAttributes_;
};

// Human-readable list of the supported queue attributes. Shared by the CLI help
// text and the parser's error messages so the two cannot drift.
const std::string& validQueueAttrs();

// Shell completion for the "<attr> <value...>" grammar walkQueueAttributes()
// parses, preceded by `minObjects` free-form tokens (the queue id).
std::vector<std::string> completeQueueAttrs(
    const std::vector<std::string>& typed,
    size_t minObjects);

// Walks the `<attr> <value...>` stream starting at v[begin] into
// `attributes`, with `active-queue-management` (or `aqm`) consuming every
// remaining token into `aqmAttributes` -- and throwing when that tail is
// empty, so a trailing bare keyword cannot silently no-op. The one grammar
// both `config qos queue-config` and `config copp queue` parse.
void walkQueueAttributes(
    const std::vector<std::string>& v,
    size_t begin,
    std::vector<std::pair<std::string, std::vector<std::string>>>& attributes,
    std::vector<std::string>& aqmAttributes);

// The stream-type value named in the attribute list, or nullopt when the edit
// does not name one. Lets a caller that requires stream-type on queue
// creation check for it before applying the edit.
std::optional<cfg::StreamType> findStreamTypeAttr(
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        attributes);

// Applies queue configuration attributes onto `queue`. Shared by every command
// that configures cfg::PortQueue attributes -- the `config qos queue-config
// <name|default>` family and `config copp queue` -- which write the same
// struct into different parts of the switch config.
//
// `attributes` maps each attribute to its value tokens: one for the scalars
// (name, reserved-bytes, shared-bytes, max-dynamic-shared-bytes, weight,
// scaling-factor, scheduling, stream-type, buffer-pool-name) and two or three
// for rate-limit (<kbps|pps> [<min>] <max>). `aqmArgs` is the
// active-queue-management sub-arg stream.
//
// bandwidthBurstMinKbits/bandwidthBurstMaxKbits are deliberately absent: the
// SAI scheduler profile is created from a five-attribute tuple that has no
// burst-rate member (SaiSchedulerTraits::AdapterHostKey), so those two fields
// reach the running config and stop there. Therefore the CLI does not expose
// them, and the parser does not accept them.
//
// Merges onto the given queue: fields not named are left untouched, and an AQM
// edit targets the entry matching its congestion-behavior (appending a new one
// so ECN and EARLY_DROP entries coexist rather than clobbering aqms[0]).
//
// Throws std::invalid_argument on an empty edit (both lists empty) or any
// invalid, unknown, or duplicated attribute; the caller is expected to apply
// the mutation transactionally (edit a copy, splice back only on success).
void applyPortQueueConfig(
    cfg::PortQueue& queue,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        attributes,
    const std::vector<std::string>& aqmArgs);

} // namespace facebook::fboss::utils
