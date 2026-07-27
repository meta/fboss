package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowIntStatusModel {
  1: list<InterfaceStatus> interfaces;
}

struct InterfaceStatus {
  1: string name;
  2: string description;
  3: string status;
  4: optional i32 vlan;
  5: string speed;
  6: string vendor;
  7: string mpn;
  8: i32 portId;
}
