package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowNextHopGroupsModel {
  1: list<NextHopGroupEntry> nextHopGroups;
}

struct NextHopGroupEntry {
  1: string name;
  2: bool isNamed;
  3: string programmed;
  4: list<string> nextHops;
  5: optional i64 id;
}
