package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

include "fboss/platform/reboot_cause_finder/if/reboot_cause_config.thrift"

// The records reboot_cause_finder wrote on this switch.
//
// `records`: Newest boot first, by bootTimeMs. `causeStatus` is unset on
// records written before the finder recorded it.
//
// `skippedCount`: Records that could not be read or parsed.
//
// `warnings`: Why, one per problem.
struct ShowRebootHistoryModel {
  1: list<reboot_cause_config.RebootCauseRecord> records;
  2: i32 skippedCount;
  3: list<string> warnings;
}
