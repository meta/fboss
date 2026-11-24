package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowPbrModel {
  1: list<PbrPolicyEntry> policies;
}

struct PbrPolicyEntry {
  1: string name;
  2: string defaultNextHopGroup;
  3: bool programmed;
  4: list<PbrClassEntry> classes;
}

struct PbrClassEntry {
  1: string forwardingClass;
  2: string redirectNextHopGroup;
  3: string aclEntry;
}
