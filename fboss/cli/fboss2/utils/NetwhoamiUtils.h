// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once
#include <optional>
#include "configerator/structs/neteng/netwhoami/gen-cpp2/netwhoami_types.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

using facebook::netwhoami::Role;

std::optional<facebook::netwhoami::NetWhoAmI> getNetWhoAmI(
    const HostInfo& hostInfo);

bool isDsfRole(Role role);

bool isNetosNative(const HostInfo& hostInfo);

} // namespace facebook::fboss
