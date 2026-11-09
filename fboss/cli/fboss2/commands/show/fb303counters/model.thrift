package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowFb303CountersModel {
  1: list<Fb303CounterEntry> counters;
}

struct Fb303CounterEntry {
  // Process the counter came from. For --service agent on a multi-switch
  // platform this distinguishes swagent from each hwagent<N>; elsewhere it is
  // just the service name.
  1: string source;
  2: string name;
  3: i64 value;
}
