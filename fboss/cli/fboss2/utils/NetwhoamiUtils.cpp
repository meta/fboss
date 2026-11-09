// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/utils/NetwhoamiUtils.h"
#include "neteng/netwhoami/lib/cpp/Client.h"
#include "neteng/netwhoami/lib/cpp/Recover.h"

namespace facebook::fboss {

std::optional<facebook::netwhoami::NetWhoAmI> getNetWhoAmI(
    const HostInfo& hostInfo) {
  try {
    if (hostInfo.isLocalHost()) {
      return netwhoami::recoverWhoAmI();
    } else {
      auto netwhoamiClient = netwhoami::srclient();
      facebook::netwhoami::NetWhoAmI whoami;
      netwhoamiClient->sync_fetch_one(whoami, hostInfo.getName());
      return whoami;
    }
  } catch (std::exception& ex) {
    std::cerr << "Error fetching netwhoami " << ex.what() << std::endl;
    return std::nullopt;
  }
}

bool isDsfRole(Role role) {
  return role == Role::FDSW || role == Role::SDSW || role == Role::EDSW ||
      role == Role::RDSW;
}

bool isNetosNative(const HostInfo& hostInfo) {
  const auto whoami = getNetWhoAmI(hostInfo);
  return whoami && whoami->current().has_value() &&
      whoami->current()->os_variant() == netwhoami::OsVariant::NETOS;
}

} // namespace facebook::fboss
