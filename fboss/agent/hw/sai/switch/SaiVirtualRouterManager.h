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

#include "fboss/agent/hw/sai/api/VirtualRouterApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/types.h"

#include "folly/MacAddress.h"
#include "folly/container/F14Map.h"

#include <memory>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;
class Vrf;

using SaiVirtualRouter = SaiObject<SaiVirtualRouterTraits>;
using SaiMplsRouterInterface = SaiObject<SaiMplsRouterInterfaceTraits>;

struct SaiVirtualRouterHandle {
  std::shared_ptr<SaiVirtualRouter> virtualRouter;
  std::shared_ptr<SaiMplsRouterInterface> mplsRouterInterface;
};

class SaiVirtualRouterManager {
 public:
  SaiVirtualRouterManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);
  VirtualRouterSaiId addVirtualRouter(const RouterID& routerId);
  void removeVirtualRouter(const RouterID& routerId);
  void addVrf(const std::shared_ptr<Vrf>& vrf);
  void removeVrf(const std::shared_ptr<Vrf>& vrf);
  void changeVrf(
      const std::shared_ptr<Vrf>& oldVrf,
      const std::shared_ptr<Vrf>& newVrf);
  SaiVirtualRouterHandle* getVirtualRouterHandle(const RouterID& routerId);
  const SaiVirtualRouterHandle* getVirtualRouterHandle(
      const RouterID& routerId) const;
  RouterID getRouterID(const sai_object_id_t vrId) const;

 private:
  SaiVirtualRouterHandle* getVirtualRouterHandleImpl(
      const RouterID& routerId) const;

  std::shared_ptr<SaiMplsRouterInterface> createMplsRouterInterface(
      VirtualRouterSaiId vrId);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  folly::F14FastMap<RouterID, std::unique_ptr<SaiVirtualRouterHandle>> handles_;
};

} // namespace facebook::fboss
