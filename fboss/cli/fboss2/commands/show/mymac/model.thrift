package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowMyMacModel {
  1: list<MyMacEntry> myMacs;
}

struct MyMacEntry {
  1: i32 vlanID;
  2: string macAddress;
}
