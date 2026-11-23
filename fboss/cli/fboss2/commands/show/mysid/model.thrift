package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowMySidModel {
  1: list<MySidEntryModel> mySidEntries;
}

struct MySidEntryModel {
  1: string prefix;
  2: string type;
  3: list<string> nextHops;
  4: list<string> resolvedNextHops;
  5: list<string> backupResolvedNextHops;
}
