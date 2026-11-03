// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/hw/sai/api/SaiVersion.h"

#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/api/Srv6Api.h"
#include "fboss/agent/hw/sai/api/TunnelApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"

#include <memory>
#include <optional>
#include <variant>

namespace facebook::fboss {

class MySid;
class SaiManagerTable;
class SaiPlatform;
class SaiStore;
class SaiSrv6MySidManager;
class SwitchState;
struct SaiNextHopGroupHandle;

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
using SaiMySid = SaiObject<SaiMySidEntryTraits>;

template <typename NextHopTraits>
class ManagedMySidNextHop
    : public detail::SaiObjectEventSubscriber<NextHopTraits> {
 public:
  using PublisherObject = std::shared_ptr<const SaiObject<NextHopTraits>>;
  ManagedMySidNextHop(
      SaiSrv6MySidManager* manager,
      SaiMySidEntryTraits::AdapterHostKey mySidKey,
      std::shared_ptr<ManagedNextHop<NextHopTraits>> managedNextHop);
  void afterCreate(PublisherObject nexthop) override;
  void beforeRemove() override;
  void linkDown() override {}
  sai_object_id_t adapterKey() const;
  using detail::SaiObjectEventSubscriber<NextHopTraits>::isReady;

 private:
  SaiSrv6MySidManager* manager_;
  SaiMySidEntryTraits::AdapterHostKey mySidKey_;
  std::shared_ptr<ManagedNextHop<NextHopTraits>> managedNextHop_;
};

SaiMySidEntryTraits::AdapterHostKey getMySidAdapterHostKey(
    const MySid& mySid,
    SaiManagerTable* managerTable);

struct SaiMySidEntryHandle {
  using NextHopHandle = std::variant<
      std::shared_ptr<SaiNextHopGroupHandle>,
      std::shared_ptr<ManagedMySidNextHop<SaiIpNextHopTraits>>,
      std::shared_ptr<ManagedMySidNextHop<SaiSrv6SidlistNextHopTraits>>>;
  NextHopHandle nexthopHandle;
  std::shared_ptr<SaiObject<SaiSrv6TunnelTraits>> decapTunnel;
  std::shared_ptr<SaiMySid> mySidEntry;
};
#endif

class SaiSrv6MySidManager {
 public:
  SaiSrv6MySidManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  void addMySidEntry(
      const std::shared_ptr<MySid>& mySid,
      const std::shared_ptr<SwitchState>& state);
  void removeMySidEntry(
      const std::shared_ptr<MySid>& mySid,
      const std::shared_ptr<SwitchState>& state);
  void changeMySidEntry(
      const std::shared_ptr<MySid>& oldMySid,
      const std::shared_ptr<MySid>& newMySid,
      const std::shared_ptr<SwitchState>& state);

  std::shared_ptr<SaiObject<SaiMySidEntryTraits>> getMySidObject(
      const SaiMySidEntryTraits::AdapterHostKey& key);
#endif

 private:
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  std::optional<SaiMySidEntryHandle::NextHopHandle> getNextHopHandle(
      const SaiMySidEntryTraits::AdapterHostKey& adapterHostKey,
      const MySid& mySid,
      const std::shared_ptr<SwitchState>& state);
#endif

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  folly::F14FastMap<
      SaiMySidEntryTraits::AdapterHostKey,
      std::unique_ptr<SaiMySidEntryHandle>>
      handles_;
#endif
};

} // namespace facebook::fboss
