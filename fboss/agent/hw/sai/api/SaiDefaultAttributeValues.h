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

#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Traits.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <folly/MacAddress.h>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

template <typename T>
struct IsDefaultStdNullOpt : std::false_type {};

template <typename T>
struct StdNullOptDefault {
  std::optional<T> operator()() const {
    return std::nullopt;
  }
};

template <typename T>
struct IsDefaultStdNullOpt<StdNullOptDefault<T>> : std::true_type {};

struct SaiObjectIdDefault {
  sai_object_id_t operator()() const {
    return SAI_NULL_OBJECT_ID;
  }
};

template <typename SaiIntT>
struct SaiIntDefault {
  SaiIntT operator()() const {
    return 0;
  }
};

struct SaiVlanIdDefault {
  uint16_t operator()() const {
#if defined(CHENAB_SAI_SDK)
    return 1;
#else
    return 0;
#endif
  }
};

struct SaiInt1Default {
  sai_uint32_t operator()() const {
    return 1;
  }
};

struct SaiInt96Default {
  sai_uint32_t operator()() const {
    return 96;
  }
};

template <typename SaiIntT>
struct SaiInt100Default {
  SaiIntT operator()() const {
    return 100;
  }
};

template <typename SaiIntT, SaiIntT N>
struct SaiIntValueDefault {
  SaiIntT operator()() const {
    return N;
  }
};

struct SaiBoolDefaultFalse {
  bool operator()() const {
    return false;
  }
};

struct SaiBoolDefaultTrue {
  bool operator()() const {
    return true;
  }
};
struct SaiPortInterfaceTypeDefault {
  sai_port_interface_type_t operator()() const {
    return SAI_PORT_INTERFACE_TYPE_NONE;
  }
};

struct SaiSwitchTypeDefault {
  sai_uint32_t operator()() const {
    return SAI_SWITCH_TYPE_NPU;
  }
};

struct SaiMacAddressDefault {
  folly::MacAddress operator()() const {
    return folly::MacAddress{};
  }
};

struct SaiMacAddressExactMatchMaskDefault {
  folly::MacAddress operator()() const {
    return folly::MacAddress::BROADCAST;
  }
};

template <typename SaiIntRangeT>
struct SaiIntRangeDefault {
  SaiIntRangeT operator()() const {
    return {0, 0};
  }
};

template <typename SaiListT>
struct SaiListDefault {
  SaiListT operator()() const {
    return SaiListT{0, nullptr};
  }
};

struct SaiPointerDefault {
  sai_pointer_t operator()() const {
    return SAI_NULL_OBJECT_ID;
  }
};

// using AclEntryActionSaiObjectIdT = AclEntryAction<sai_object_id_t>;
struct SaiAclEntryActionSaiObjectDefault {
  AclEntryActionSaiObjectIdT operator()() const {
    return AclEntryActionSaiObjectIdT(SAI_NULL_OBJECT_ID);
  }
};

struct SaiAclEntryActionBoolTrue {
  AclEntryActionBool operator()() const {
    return AclEntryActionBool(true);
  }
};

struct SaiAclEntryActionBoolFalse {
  AclEntryActionBool operator()() const {
    return AclEntryActionBool(false);
  }
};

struct SaiAclEntryFieldSaiObjectIdTDefault {
  AclEntryFieldSaiObjectIdT operator()() const {
    return AclEntryFieldSaiObjectIdT(std::make_pair(SAI_NULL_OBJECT_ID, 0));
  }
};

struct SaiPortEyeValuesDefault {
  sai_port_lane_eye_values_t operator()() const {
    return sai_port_lane_eye_values_t{0, 0, 0, 0, 0};
  }
};

struct SaiPortErrStatusDefault {
  sai_port_err_status_list_t operator()() const {
    return sai_port_err_status_list_t{0, nullptr};
  }
};

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
struct SaiPortLaneLatchStatusDefault {
  sai_port_lane_latch_status_t operator()() const {
    return sai_port_lane_latch_status_t{0, {false, false}};
  }
};

struct SaiLatchStatusDefault {
  sai_latch_status_t operator()() const {
    return sai_latch_status_t();
  }
};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
struct SaiRxPPMDefault {
  sai_port_frequency_offset_ppm_values_t operator()() const {
    return sai_port_frequency_offset_ppm_values_t{0, 0};
  }
};
#endif

struct SaiPrbsConfigDefault {
  sai_port_prbs_config_t operator()() const {
    return SAI_PORT_PRBS_CONFIG_DISABLE;
  }
};

#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
struct SaiPrbsRxStateDefault {
  sai_prbs_rx_state_t operator()() const {
    return sai_prbs_rx_state_t();
  }
};
#endif

struct SaiPacketActionDefaultDrop {
  sai_packet_action_t operator()() const {
    return SAI_PACKET_ACTION_DROP;
  }
};

struct SaiHostifUserDefinedTrapDefaultType {
  sai_hostif_user_defined_trap_type_t operator()() const {
    return SAI_HOSTIF_USER_DEFINED_TRAP_TYPE_ACL;
  }
};

struct SaiHostifUserDefinedTrapDefaultPriority {
  sai_uint32_t operator()() const {
    return SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY;
  }
};

struct SaiIpAddressDefault {
  folly::IPAddress operator()() const {
    return folly::IPAddressV6("::");
  }
};

using SaiObjectIdListDefault = SaiListDefault<sai_object_list_t>;
using SaiU32ListDefault = SaiListDefault<sai_u32_list_t>;
using SaiS32ListDefault = SaiListDefault<sai_s32_list_t>;
using SaiS8ListDefault = SaiListDefault<sai_s8_list_t>;

} // namespace facebook::fboss
