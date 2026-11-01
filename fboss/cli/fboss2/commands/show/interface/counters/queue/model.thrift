package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct InterfaceQueueCountersModel {
  1: list<InterfaceQueueCounters> queueCounters;
}

// Egress counters of one queue of one interface. Counters the hardware does
// not report for the queue are set to STAT_UNINITIALIZED.
struct InterfaceQueueCounters {
  1: string interfaceName;
  2: i16 queueId;
  // Configured queue name, empty when the queue has none.
  3: string queueName;
  4: i64 outPackets;
  5: i64 outBytes;
  6: i64 outDiscardPackets;
  7: i64 outDiscardBytes;
}
