/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <iomanip>

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/tracer/SaiTracer.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;
using std::string;
using std::vector;

namespace facebook::fboss {

string oidAttr(const sai_attribute_t* attr_list, int i) {
  return to<string>(
      "s_a[",
      i,
      "].value.oid=",
      SaiTracer::getInstance()->getVariable(attr_list[i].value.oid));
}

void oidListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_object_id_t), attr_list[i].value.objlist.count);

  string prefix = to<string>("s_a", "[", i, "].value.objlist.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.objlist.count));
  if (logEntry && attr_list[i].value.objlist.list) {
    attrLines.push_back(
        to<string>(prefix, "list=(sai_object_id_t*)(list_", listIndex, ")"));
    for (int j = 0; j < std::min(attr_list[i].value.objlist.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "]=",
              SaiTracer::getInstance()->getVariable(
                  attr_list[i].value.objlist.list[j])));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void aclEntryActionSaiObjectIdAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclaction.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclaction.enable));
  attrLines.push_back(
      to<string>(
          prefix,
          "parameter.oid=",
          SaiTracer::getInstance()->getVariable(
              attr_list[i].value.aclaction.parameter.oid)));
}

void aclEntryActionSaiObjectIdListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry) {
  uint32_t objectListCount =
      attr_list[i].value.aclaction.parameter.objlist.count;

  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_object_id_t), objectListCount);
  string prefix = to<string>("s_a", "[", i, "].value.aclaction.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclaction.enable));
  attrLines.push_back(
      to<string>(prefix, "parameter.objlist.count=", objectListCount));
  if (logEntry && attr_list[i].value.aclaction.parameter.objlist.list) {
    attrLines.push_back(
        to<string>(
            prefix,
            "parameter.objlist.list=(sai_object_id_t*)(list_",
            listIndex,
            ")"));
    for (int j = 0; j < std::min(objectListCount, listLimit); ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "parameter.objlist.list[",
              j,
              "]=",
              SaiTracer::getInstance()->getVariable(
                  attr_list[i].value.aclaction.parameter.objlist.list[j])));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "parameter.objlist.list=NULL"));
  }
}

void aclEntryActionU8Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclaction.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclaction.enable));
  attrLines.push_back(
      to<string>(
          prefix, "parameter.u8=", attr_list[i].value.aclaction.parameter.u8));
}

void aclEntryActionBoolAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclaction.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclaction.enable));
  attrLines.push_back(
      to<string>(
          prefix,
          "parameter.booldata=",
          attr_list[i].value.aclaction.parameter.booldata));
}

void aclEntryActionU32Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclaction.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclaction.enable));
  attrLines.push_back(
      to<string>(
          prefix,
          "parameter.u32=",
          attr_list[i].value.aclaction.parameter.u32));
}

void aclEntryFieldSaiObjectIdAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));
  attrLines.push_back(
      to<string>(
          prefix,
          "data.oid=",
          SaiTracer::getInstance()->getVariable(
              attr_list[i].value.aclfield.data.oid)));
  attrLines.push_back(
      to<string>(prefix, "mask.u32=", attr_list[i].value.aclfield.mask.u32));
}

void aclEntryFieldIpV4Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));
  attrLines.push_back(
      to<string>(prefix, "data.ip4=", attr_list[i].value.aclfield.data.ip4));
  attrLines.push_back(
      to<string>(prefix, "mask.ip4=", attr_list[i].value.aclfield.mask.ip4));
}

void aclEntryFieldIpV6Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));

  // The underlying implementation of sai_ip6_t is uint8_t[16]
  attrLines.push_back(
      to<string>(
          "// data=",
          facebook::fboss::fromSaiIpAddress(
              attr_list[i].value.aclfield.data.ip6)
              .str()));
  for (int j = 0; j < 16; ++j) {
    attrLines.push_back(
        to<string>(
            prefix,
            "data.ip6[",
            j,
            "]=",
            attr_list[i].value.aclfield.data.ip6[j]));
  }

  attrLines.push_back(
      to<string>(
          "// mask=",
          facebook::fboss::fromSaiIpAddress(
              attr_list[i].value.aclfield.mask.ip6)
              .str()));
  for (int j = 0; j < 16; ++j) {
    attrLines.push_back(
        to<string>(
            prefix,
            "mask.ip6[",
            j,
            "]=",
            attr_list[i].value.aclfield.mask.ip6[j]));
  }
}

void aclEntryFieldU8Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));
  attrLines.push_back(
      to<string>(prefix, "data.u8=", attr_list[i].value.aclfield.data.u8));
  attrLines.push_back(
      to<string>(prefix, "mask.u8=", attr_list[i].value.aclfield.mask.u8));
}

void aclEntryFieldU16Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));
  attrLines.push_back(
      to<string>(prefix, "data.u16=", attr_list[i].value.aclfield.data.u16));
  attrLines.push_back(
      to<string>(prefix, "mask.u16=", attr_list[i].value.aclfield.mask.u16));
}

void aclEntryFieldU32Attr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));
  attrLines.push_back(
      to<string>(prefix, "data.u32=", attr_list[i].value.aclfield.data.u32));
  attrLines.push_back(
      to<string>(prefix, "mask.u32=", attr_list[i].value.aclfield.mask.u32));
}

void aclEntryFieldMacAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));

  attrLines.push_back(to<string>("mac=", prefix, "data.mac"));
  for (int j = 0; j < 6; ++j) {
    attrLines.push_back(
        to<string>(
            "mac[",
            j,
            "]=",
            static_cast<const uint8_t*>(
                attr_list[i].value.aclfield.data.mac)[j]));
  }

  attrLines.push_back(to<string>("mac=", prefix, "mask.mac"));
  for (int j = 0; j < 6; ++j) {
    attrLines.push_back(
        to<string>(
            "mac[",
            j,
            "]=",
            static_cast<const uint8_t*>(
                attr_list[i].value.aclfield.mask.mac)[j]));
  }
}

void aclEntryFieldU8ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1,
      sizeof(sai_uint32_t),
      attr_list[i].value.aclfield.data.u8list.count);

  string prefix = to<string>("s_a", "[", i, "].value.aclfield.");
  attrLines.push_back(
      to<string>(prefix, "enable=", attr_list[i].value.aclfield.enable));

  prefix = to<string>("s_a", "[", i, "].value.aclfield.data.u8list.");
  attrLines.push_back(
      to<string>(
          prefix, "count=", attr_list[i].value.aclfield.data.u8list.count));
  if (logEntry && attr_list[i].value.aclfield.data.u8list.list) {
    attrLines.push_back(
        to<string>(prefix, "list=(sai_uint8_t*)(list_", listIndex, ")"));
    for (int j = 0;
         j < std::min(attr_list[i].value.aclfield.data.u8list.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "]=",
              attr_list[i].value.aclfield.data.u8list.list[j]));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }

  prefix = to<string>("s_a", "[", i, "].value.aclfield.mask.u8list.");
  attrLines.push_back(
      to<string>(
          prefix, "count=", attr_list[i].value.aclfield.mask.u8list.count));
  if (logEntry && attr_list[i].value.aclfield.mask.u8list.list) {
    attrLines.push_back(
        to<string>(prefix, "list=(sai_uint8_t*)(list_", listIndex + 1, ")"));
    for (int j = 0;
         j < std::min(attr_list[i].value.aclfield.mask.u8list.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "]=",
              attr_list[i].value.aclfield.mask.u8list.list[j]));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void systemPortConfigAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.sysportconfig.");
  attrLines.push_back(
      to<string>(prefix, "port_id=", attr_list[i].value.sysportconfig.port_id));
  attrLines.push_back(
      to<string>(
          prefix,
          "attached_switch_id=",
          attr_list[i].value.sysportconfig.attached_switch_id));
  attrLines.push_back(
      to<string>(
          prefix,
          "attached_core_index=",
          attr_list[i].value.sysportconfig.attached_core_index));

  attrLines.push_back(
      to<string>(
          prefix,
          "attached_core_port_index=",
          attr_list[i].value.sysportconfig.attached_core_port_index));
  attrLines.push_back(
      to<string>(prefix, "speed=", attr_list[i].value.sysportconfig.speed));
  attrLines.push_back(
      to<string>(prefix, "num_voq=", attr_list[i].value.sysportconfig.num_voq));
}

void systemPortConfigListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1,
      sizeof(sai_uint32_t),
      attr_list[i].value.sysportconfiglist.count);

  string prefix = to<string>("s_a", "[", i, "].value.sysportconfiglist.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.sysportconfiglist.count));
  if (logEntry && attr_list[i].value.sysportconfiglist.list) {
    attrLines.push_back(
        to<string>(
            prefix, "list=(sai_system_port_config_t*)(list_", listIndex, ")"));
    for (int j = 0;
         j < std::min(attr_list[i].value.sysportconfiglist.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "port_id=",
              attr_list[i].value.sysportconfiglist.list[j].port_id));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "attached_switch_id=",
              attr_list[i].value.sysportconfiglist.list[j].attached_switch_id));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "attached_core_index=",
              attr_list[i]
                  .value.sysportconfiglist.list[j]
                  .attached_core_index));

      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "attached_core_port_index=",
              attr_list[i]
                  .value.sysportconfiglist.list[j]
                  .attached_core_port_index));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "speed=",
              attr_list[i].value.sysportconfiglist.list[j].speed));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "num_voq=",
              attr_list[i].value.sysportconfiglist.list[j].num_voq));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
void latchStatusAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.latchstatus.");
  attrLines.push_back(
      to<string>(
          prefix,
          "current_status=",
          attr_list[i].value.latchstatus.current_status));
  attrLines.push_back(
      to<string>(prefix, "changed=", attr_list[i].value.latchstatus.changed));
}

void portLaneLatchStatusListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1,
      sizeof(sai_uint32_t),
      attr_list[i].value.portlanelatchstatuslist.count);

  string prefix = to<string>("s_a", "[", i, "].value.portlanelatchstatuslist.");
  attrLines.push_back(
      to<string>(
          prefix, "count=", attr_list[i].value.portlanelatchstatuslist.count));
  if (logEntry && attr_list[i].value.portlanelatchstatuslist.list) {
    attrLines.push_back(
        to<string>(
            prefix,
            "list=(sai_port_lane_latch_status_t*)(list_",
            listIndex,
            ")"));
    for (int j = 0; j <
         std::min(attr_list[i].value.portlanelatchstatuslist.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "lane=",
              attr_list[i].value.portlanelatchstatuslist.list[j].lane));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.",
              "current_status=",
              attr_list[i]
                  .value.portlanelatchstatuslist.list[j]
                  .value.current_status));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.",
              "changed=",
              attr_list[i]
                  .value.portlanelatchstatuslist.list[j]
                  .value.changed));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
void portFrequencyOffsetPpmListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1,
      sizeof(sai_uint32_t),
      attr_list[i].value.portfrequencyoffsetppmlist.count);

  string prefix =
      to<string>("s_a", "[", i, "].value.portfrequencyoffsetppmlist.");
  attrLines.push_back(
      to<string>(
          prefix,
          "count=",
          attr_list[i].value.portfrequencyoffsetppmlist.count));
  if (logEntry && attr_list[i].value.portfrequencyoffsetppmlist.list) {
    attrLines.push_back(
        to<string>(
            prefix,
            "list=(sai_port_frequency_offset_ppm_values_t*)(list_",
            listIndex,
            ")"));
    for (int j = 0; j <
         std::min(attr_list[i].value.portfrequencyoffsetppmlist.count,
                  listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "lane=",
              attr_list[i].value.portfrequencyoffsetppmlist.list[j].lane));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].ppm=",
              attr_list[i].value.portfrequencyoffsetppmlist.list[j].ppm));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void portSnrListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1,
      sizeof(sai_uint32_t),
      attr_list[i].value.portsnrlist.count);

  string prefix = to<string>("s_a", "[", i, "].value.portsnrlist.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.portsnrlist.count));
  if (logEntry && attr_list[i].value.portsnrlist.list) {
    attrLines.push_back(
        to<string>(
            prefix, "list=(sai_port_snr_values_t*)(list_", listIndex, ")"));
    for (int j = 0;
         j < std::min(attr_list[i].value.portsnrlist.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].",
              "lane=",
              attr_list[i].value.portsnrlist.list[j].lane));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].snr=",
              attr_list[i].value.portsnrlist.list[j].snr));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}
#endif

std::string boolAttr(const sai_attribute_t* attr_list, int i) {
  return to<string>(
      "s_a[",
      i,
      "].value.booldata=",
      attr_list[i].value.booldata ? "true" : "false");
}

string s8Attr(const sai_attribute_t* attr_list, int i) {
  return to<string>("s_a[", i, "].value.s8=", attr_list[i].value.s8);
}

string u8Attr(const sai_attribute_t* attr_list, int i) {
  return to<string>("s_a[", i, "].value.u8=", attr_list[i].value.u8);
}

string s16Attr(const sai_attribute_t* attr_list, int i) {
  return to<string>("s_a[", i, "].value.s16=", attr_list[i].value.s16);
}

string u16Attr(const sai_attribute_t* attr_list, int i) {
  return to<string>("s_a[", i, "].value.u16=", attr_list[i].value.u16);
}

string s32Attr(const sai_attribute_t* attr_list, int i) {
  return to<string>("s_a[", i, "].value.s32=", attr_list[i].value.s32);
}

string u32Attr(const sai_attribute_t* attr_list, int i) {
  return to<string>("s_a[", i, "].value.u32=", attr_list[i].value.u32);
}

string u64Attr(const sai_attribute_t* attr_list, int i) {
  return to<string>("s_a[", i, "].value.u64=", attr_list[i].value.u64);
}

void s8ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool nullable) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_int8_t), attr_list[i].value.s8list.count);

  string prefix = to<string>("s_a", "[", i, "].value.s8list.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.s8list.count));

  // Attribute SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO uses s8list as a char
  // array. If the list count is 0, we'll replace it with NULL.
  if (nullable && attr_list[i].value.s8list.count == 0) {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  } else {
    if (attr_list[i].value.s8list.list) {
      attrLines.push_back(
          to<string>(prefix, "list=(sai_int8_t*)(list_", listIndex, ")"));
      for (int j = 0; j < std::min(attr_list[i].value.s8list.count, listLimit);
           ++j) {
        attrLines.push_back(
            to<string>(
                prefix, "list[", j, "]=", attr_list[i].value.s8list.list[j]));
      }
    } else {
      attrLines.push_back(to<string>(prefix, "list=NULL"));
    }
  }
}

void s32ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_int32_t), attr_list[i].value.s32list.count);

  string prefix = to<string>("s_a", "[", i, "].value.s32list.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.s32list.count));
  attrLines.push_back(
      to<string>(prefix, "list=(sai_int32_t*)(list_", listIndex, ")"));
  if (logEntry && attr_list[i].value.s32list.list) {
    for (int j = 0; j < std::min(attr_list[i].value.s32list.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix, "list[", j, "]=", attr_list[i].value.s32list.list[j]));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void u32ListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_uint32_t), attr_list[i].value.u32list.count);

  string prefix = to<string>("s_a", "[", i, "].value.u32list.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.u32list.count));
  if (logEntry && attr_list[i].value.u32list.list) {
    attrLines.push_back(
        to<string>(prefix, "list=(sai_uint32_t*)(list_", listIndex, ")"));
    for (int j = 0; j < std::min(attr_list[i].value.u32list.count, listLimit);
         ++j) {
      attrLines.push_back(
          to<string>(
              prefix, "list[", j, "]=", attr_list[i].value.u32list.list[j]));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void segmentListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_ip6_t), attr_list[i].value.segmentlist.count);

  string prefix = to<string>("s_a", "[", i, "].value.segmentlist.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.segmentlist.count));
  if (logEntry && attr_list[i].value.segmentlist.list) {
    attrLines.push_back(
        to<string>(prefix, "list=(sai_ip6_t*)(list_", listIndex, ")"));
    for (int j = 0;
         j < std::min(attr_list[i].value.segmentlist.count, listLimit);
         ++j) {
      folly::IPAddressV6 ip6 = facebook::fboss::fromSaiIpAddress(
          attr_list[i].value.segmentlist.list[j]);
      attrLines.push_back(to<string>("// segment[", j, "]=", ip6.str()));
      for (int k = 0; k < 16; ++k) {
        attrLines.push_back(
            to<string>(
                prefix,
                "list[",
                j,
                "][",
                k,
                "]=",
                attr_list[i].value.segmentlist.list[j][k]));
      }
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void s32RangeAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.s32range.");
  attrLines.push_back(
      to<string>(prefix, "min=", attr_list[i].value.s32range.min));
  attrLines.push_back(
      to<string>(prefix, "max=", attr_list[i].value.s32range.max));
}

void u32RangeAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.u32range.");
  attrLines.push_back(
      to<string>(prefix, "min=", attr_list[i].value.u32range.min));
  attrLines.push_back(
      to<string>(prefix, "max=", attr_list[i].value.u32range.max));
}

void mapListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry) {
  // Make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_map_t), attr_list[i].value.maplist.count);

  string prefix = to<string>("s_a", "[", i, "].value.maplist.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.maplist.count));
  attrLines.push_back(
      to<string>(prefix, "list=(sai_map_t*)(list_", listIndex, ")"));
  if (logEntry && attr_list[i].value.maplist.list) {
    for (int j = 0; j < std::min(attr_list[i].value.maplist.count, listLimit);
         ++j) {
      // Key
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key=",
              attr_list[i].value.maplist.list[j].key));
      // Value
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value=",
              attr_list[i].value.maplist.list[j].value));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void qosMapListAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    std::vector<std::string>& attrLines,
    bool logEntry) {
  // First make sure we have enough lists for use
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_qos_map_t), attr_list[i].value.qosmap.count);

  string prefix = to<string>("s_a", "[", i, "].value.qosmap.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.qosmap.count));
  attrLines.push_back(
      to<string>(prefix, "list=(sai_qos_map_t*)(list_", listIndex, ")"));

  // TODO(zecheng): Find a way to know the type of key and value.
  // For now we can only set every single field
  if (logEntry && attr_list[i].value.qosmap.list) {
    for (int j = 0; j < std::min(attr_list[i].value.qosmap.count, listLimit);
         ++j) {
      // Key
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.tc=",
              attr_list[i].value.qosmap.list[j].key.tc));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.dscp=",
              attr_list[i].value.qosmap.list[j].key.dscp));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.dot1p=",
              attr_list[i].value.qosmap.list[j].key.dot1p));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.prio=",
              attr_list[i].value.qosmap.list[j].key.prio));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.pg=",
              attr_list[i].value.qosmap.list[j].key.pg));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.queue_index=",
              attr_list[i].value.qosmap.list[j].key.queue_index));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.color=(sai_packet_color_t)",
              attr_list[i].value.qosmap.list[j].key.color));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].key.mpls_exp=",
              attr_list[i].value.qosmap.list[j].key.mpls_exp));

      // Value
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.tc=",
              attr_list[i].value.qosmap.list[j].value.tc));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.dscp=",
              attr_list[i].value.qosmap.list[j].value.dscp));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.dot1p=",
              attr_list[i].value.qosmap.list[j].value.dot1p));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.prio=",
              attr_list[i].value.qosmap.list[j].value.prio));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.pg=",
              attr_list[i].value.qosmap.list[j].value.pg));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.queue_index=",
              attr_list[i].value.qosmap.list[j].value.queue_index));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.color=(sai_packet_color_t)",
              attr_list[i].value.qosmap.list[j].value.color));
      attrLines.push_back(
          to<string>(
              prefix,
              "list[",
              j,
              "].value.mpls_exp=",
              attr_list[i].value.qosmap.list[j].value.mpls_exp));
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}

void charDataAttr(
    const sai_attribute_t* attr_list,
    int i,
    vector<string>& attrLines) {
  string prefix = to<string>("s_a[", i, "].value.");
  for (int j = 0; j < 32; ++j) {
    std::ostringstream outStringStream;
    SaiTracer::getInstance()->printHex(
        outStringStream, attr_list[i].value.chardata[j]);
    attrLines.push_back(
        to<string>(prefix, "chardata[", j, "]=", outStringStream.str()));
  }
}

void macAddressAttr(
    const sai_attribute_t* attr_list,
    int i,
    vector<string>& attrLines) {
  // The underlying type of sai_mac_t is uint8_t[6]
  // TODO(zecheng): Create helper function to handle this
  attrLines.push_back(to<string>("mac=s_a[", i, "].value.mac"));
  for (int j = 0; j < 6; ++j) {
    attrLines.push_back(
        to<string>(
            "mac[",
            j,
            "]=",
            static_cast<const uint8_t*>(attr_list[i].value.mac)[j]));
  }
}

void ipAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
  string prefix = to<string>("s_a", "[", i, "].value.ipaddr.");
  folly::IPAddress ipAddress =
      facebook::fboss::fromSaiIpAddress(attr_list[i].value.ipaddr);
  attrLines.push_back(to<string>("// ", ipAddress.str()));

  if (attr_list[i].value.ipaddr.addr_family == SAI_IP_ADDR_FAMILY_IPV4) {
    attrLines.push_back(
        to<string>(prefix, "addr_family=SAI_IP_ADDR_FAMILY_IPV4"));
    attrLines.push_back(
        to<string>(prefix, "addr.ip4=", attr_list[i].value.ipaddr.addr.ip4));
  } else if (attr_list[i].value.ipaddr.addr_family == SAI_IP_ADDR_FAMILY_IPV6) {
    attrLines.push_back(
        to<string>(prefix, "addr_family=SAI_IP_ADDR_FAMILY_IPV6"));

    // Underlying type of sai_ip6_t is uint8_t[16]
    for (int j = 0; j < 16; ++j) {
      attrLines.push_back(
          to<string>(
              prefix,
              "addr.ip6[",
              j,
              "]=",
              attr_list[i].value.ipaddr.addr.ip6[j]));
    }
  }
}

void u8ArrGenericAttr(
    const uint8_t* array,
    uint arrLength,
    int i,
    std::vector<std::string>& attrLines,
    const std::string& attrName) {
  std::ostringstream outStringStream;
  outStringStream << to<string>("s_a", "[", i, "].value.", attrName, "={");
  for (int j = 0; j < arrLength - 1; j += 32) {
    outStringStream << "\n";
    uint k_end = j + 32;
    for (int k = j; k < std::min(k_end, arrLength - 1); ++k) {
      SaiTracer::getInstance()->printHex(outStringStream, array[k]);
      outStringStream << ", ";
    }
  }
  SaiTracer::getInstance()->printHex(outStringStream, array[arrLength - 1]);
  outStringStream << "}";
  attrLines.push_back(outStringStream.str());
}

void prbsRxStateAttr(
    const sai_attribute_t* attr_list,
    int i,
    std::vector<std::string>& attrLines) {
#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
  string prefix = to<string>("s_a", "[", i, "].value.rx_state.");
  attrLines.push_back(
      to<string>(prefix, "rx_status=", attr_list[i].value.rx_state.rx_status));
  attrLines.push_back(
      to<string>(
          prefix, "error_count=", attr_list[i].value.rx_state.error_count));
#endif
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
void jsonAttr(
    const sai_attribute_t* attr_list,
    int i,
    uint32_t listIndex,
    vector<string>& attrLines,
    bool logEntry) {
  // sai_json_t contains sai_s8_list_t json member
  // Access via attr_list[i].value.json.json
  uint32_t listLimit = SaiTracer::getInstance()->checkListCount(
      listIndex + 1, sizeof(sai_int8_t), attr_list[i].value.json.json.count);

  string prefix = to<string>("s_a", "[", i, "].value.json.json.");
  attrLines.push_back(
      to<string>(prefix, "count=", attr_list[i].value.json.json.count));

  if (attr_list[i].value.json.json.list) {
    attrLines.push_back(
        to<string>(prefix, "list=(sai_int8_t*)(list_", listIndex, ")"));
    if (logEntry) {
      // Print the JSON string as a readable comment first
      uint32_t strLen = std::min(attr_list[i].value.json.json.count, listLimit);
      std::string jsonStr(
          reinterpret_cast<const char*>(attr_list[i].value.json.json.list),
          strLen);
      attrLines.push_back(to<string>("// JSON: ", jsonStr));

      // Also output the actual byte values for replay
      for (int j = 0; j < strLen; ++j) {
        attrLines.push_back(
            to<string>(
                prefix,
                "list[",
                j,
                "]=",
                attr_list[i].value.json.json.list[j]));
      }
    }
  } else {
    attrLines.push_back(to<string>(prefix, "list=NULL"));
  }
}
#endif

std::string toCapital(const std::string& input) {
  std::string output;
  for (int i = 0; i < input.length(); ++i) {
    if (isupper(input[i])) {
      output += "_";
      output += input[i];
    } else {
      output += toupper(input[i]);
    }
  }
  return output;
}

std::string attrNameToEnum(
    const std::string& objType,
    const std::string& attrName) {
  return to<string>("SAI", toCapital(objType), "_ATTR", toCapital(attrName));
}

} // namespace facebook::fboss
