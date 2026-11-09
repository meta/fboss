// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/types.h"
#include "fboss/lib/phy/ExternalPhy.h"
#include "fboss/lib/phy/ExternalPhyPortStatsUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/mdio/Mdio.h"
#include "fboss/mka_service/if/gen-cpp2/mka_structs_types.h"

#include <folly/Synchronized.h>
#include <folly/futures/Future.h>
#include <map>
#include <optional>
#include <vector>

DECLARE_bool(init_pim_xphys);

namespace facebook {
namespace fboss {

class MultiPimPlatformSystemContainer;
class MultiPimPlatformPimContainer;
class PlatformMapping;
class TransceiverInfo;
class PhySnapshotManager;

class PhyManager {
 public:
  using PublishPhyCb = std::function<void(
      std::string&&,
      std::optional<phy::PhyInfo>&&,
      std::optional<HwPortStats>&&)>;

  explicit PhyManager(const PlatformMapping* platformMapping);
  virtual ~PhyManager();

  GlobalXphyID getGlobalXphyIDbyPortID(PortID portID) const;
  virtual phy::PhyIDInfo getPhyIDInfo(GlobalXphyID xphyID) const = 0;
  virtual GlobalXphyID getGlobalXphyID(
      const phy::PhyIDInfo& phyIDInfo) const = 0;

  /*
   * This function initializes all the PHY objects for a given chassis. The PHY
   * objects are kept per slot, per MDIO controller, per phy address. This
   * needs to be defined by inheriting classes.
   */
  virtual bool initExternalPhyMap(bool warmboot) = 0;

  /*
   * A virtual function for the ExternalPhy obejcts. The sub-class needs to
   * implement this function. The implementation will be different for
   * Minipack and Yamp. If the Phy code is in same process then that should
   * called PhyManager function otherwise it should be a thrift call to port
   * service process
   */
  virtual void initializeSlotPhys(PimID pimID, bool warmboot) = 0;

  /*
   * Per-xphy initialization function for platforms using XPHY_LEVEL threading.
   * This MUST be overridden by platforms that set XPHY_LEVEL threading model.
   * Platforms using PIM_LEVEL threading (default) should use
   * initializeSlotPhys().
   */
  virtual void initializeXphy(GlobalXphyID xphyID, bool warmboot) {
    throw FbossError(
        "initializeXphy() must be implemented when using XPHY_LEVEL threading. "
        "Override this method in your platform-specific PhyManager. xphyID=",
        xphyID);
  }

  virtual MultiPimPlatformSystemContainer* getSystemContainer() = 0;

  int getNumOfSlot() const {
    return numOfSlot_;
  }

  virtual int getPimStartNum() = 0;

  /*
   * This function returns the ExternalPhy object for the giving global xphy id
   */
  phy::ExternalPhy* getExternalPhy(GlobalXphyID xphyID);

  /*
   * This function returns the ExternalPhy object for the giving software port
   * id
   */
  phy::ExternalPhy* getExternalPhy(PortID portID) {
    return getExternalPhy(getGlobalXphyIDbyPortID(portID));
  }

  // Key is the xphy identifier, value is the stats
  void publishPhyIOStatsToFb303() const;

  phy::PhyPortConfig getDesiredPhyPortConfig(
      PortID portId,
      cfg::PortProfileID portProfileId,
      std::optional<TransceiverInfo> transceiverInfo);

  // Returns the original PortPinConfig from PlatformMapping, preserving
  // the JSON pin order. Used by the SAI XPHY path to send pinConfigs to
  // SwitchState in NPU-pin order (vs the std::map-sorted order produced by
  // ExternalPhyConfig::getPinConfigs()).
  phy::PortPinConfig getDesiredPortPinConfig(
      PortID portId,
      cfg::PortProfileID portProfileId,
      std::optional<TransceiverInfo> transceiverInfo);

  phy::PhyPortConfig getHwPhyPortConfig(PortID portId, bool readFromHw = false);

  virtual void programOnePort(
      PortID portId,
      cfg::PortProfileID portProfileId,
      const std::optional<TransceiverInfo>& transceiverInfo,
      bool needResetDataPath);

  // Return programmed profile id for a specific port
  std::optional<cfg::PortProfileID> getProgrammedProfile(PortID portID);
  // Return programmed port speed for a specific port
  std::optional<cfg::PortSpeed> getProgrammedSpeed(PortID portID);

  // Macsec is only supported on SAI, so only SaiPhyManager will override this.
  virtual void sakInstallTx(const mka::MKASak& /* sak */) {
    throw FbossError("sakInstallTx must be implemented by defired phyManager");
  }
  virtual void sakInstallRx(
      const mka::MKASak& /* sak */,
      const mka::MKASci& /* sci */) {
    throw FbossError("sakInstallRx must be implemented by defired phyManager");
  }
  virtual void sakDeleteRx(
      const mka::MKASak& /* sak */,
      const mka::MKASci& /* sci */) {
    throw FbossError("sakDeleteRx must be implemented by defired phyManager");
  }
  virtual void sakDelete(const mka::MKASak& /* sak */) {
    throw FbossError("sakDelete must be implemented by defired phyManager");
  }
  virtual mka::MKASakHealthResponse sakHealthCheck(
      const mka::MKASak /*sak*/) const {
    throw FbossError(
        "sakHealthCheck must be implemented by defired phyManager");
  }

  virtual mka::MacsecPortStats getMacsecPortStats(
      std::string /* portName */,
      mka::MacsecDirection /* direction */,
      bool /*readFromHw*/) {
    throw FbossError(
        "Attempted to call getMacsecPortStats from non-SaiPhyManager");
  }
  virtual mka::MacsecFlowStats getMacsecFlowStats(
      std::string /* portName */,
      mka::MacsecDirection /* direction */,
      bool /* readFromHw */) {
    throw FbossError(
        "Attempted to call getMacsecFlowStats from non-SaiPhyManager");
  }
  virtual mka::MacsecSaStats getMacsecSecureAssocStats(
      std::string /* portName */,
      mka::MacsecDirection /* direction */,
      bool /* readFromHw */) {
    throw FbossError(
        "Attempted to call getMacsecSecureAssocStats from non-SaiPhyManager");
  }

  virtual std::map<std::string, MacsecStats> getAllMacsecPortStats(
      bool /* readFromHw */) {
    throw FbossError(
        "Attempted to call getAllMacsecPortStats from non-SaiPhyManager");
  }

  virtual std::map<std::string, MacsecStats> getMacsecPortStats(
      const std::vector<std::string>& /* portName */,
      bool /* readFromHw */) {
    throw FbossError(
        "Attempted to call getMacsecPortStats from non-SaiPhyManager");
  }

  virtual std::optional<HwPortStats> getHwPortStats(
      const std::string& /* portName */) const {
    return std::nullopt;
  }

  virtual std::string listHwObjects(
      std::vector<HwObjectType>& /* hwObjects */,
      bool /* cached */) {
    return "";
  }

  virtual bool getSdkState(const std::string& /* fileName */) {
    throw FbossError("Attempted to call getSdkState from non-SaiPhyManager");
  }

  virtual bool setupMacsecState(
      const std::vector<std::string>& /* portList */,
      bool /* macsecDesired */,
      bool /* dropUnencrypted */) {
    throw FbossError(
        "Attempted to call setupMacsecState from non-SaiPhyManager");
  }

  enum class XphyThreadingModel {
    PIM_LEVEL,
    XPHY_LEVEL,
  };

  folly::EventBase* getPimEventBase(PimID pimID) const;
  virtual folly::EventBase* getXphyEventBase(const GlobalXphyID& xphyID) const;

  // Get all xphy IDs for a given PIM
  std::vector<GlobalXphyID> getXphyIDsForPim(const PimID& pimID) const;

  virtual void
  setPortPrbs(PortID portID, phy::Side side, const phy::PortPrbsState& prbs);

  virtual phy::PortPrbsState getPortPrbs(PortID portID, phy::Side side);

  virtual std::vector<phy::PrbsLaneStats> getPortPrbsStats(
      PortID portID,
      phy::Side side);
  void clearPortPrbsStats(PortID portID, phy::Side side);
  std::vector<PortID> getPortsSupportingFeature(
      phy::ExternalPhy::Feature feature) const;
  std::vector<PortID> getXphyPorts() const;
  std::set<GlobalXphyID> getXphysSupportingFeature(
      phy::ExternalPhy::Feature feature) const;

  // Number of XPHYs that were actually created. A PhyManager is constructed
  // for every platform of an XPHY capable family, but XPHYs are only created
  // for the PIMs that have them, so this is legitimately 0 on configurations
  // like Elbert 8x16Q where no PIM carries an XPHY.
  size_t getNumXphys() const;

  // This is to provide the to-be cached warmboot state, which should include
  // the current portToCacheInfo_ map, so that during warmboot, we can use that
  // to recover the already programmed lane vector information from the cached
  // warmboot state.
  folly::dynamic getWarmbootState() const;

  void restoreFromWarmbootState(const folly::dynamic& phyWarmbootState);

  virtual void updateAllXphyPortsStats();

  // The following two functions return whether the future job of xphy or prbs
  // stats collection is done.
  // NOTE: The following two functions are only used in testing.
  virtual bool isXphyStatsCollectionDone(PortID portID) const;
  bool isPrbsStatsCollectionDone(PortID portID) const;

  void publishXphyInfoSnapshots(PortID portID) const;
  void updateXphyInfo(PortID portID, phy::PhyInfo&& phyInfo);
  std::optional<phy::PhyInfo> getXphyInfo(PortID portID) const;

  // returns the default TX settings for phy ports on the given phy.
  // for most platforms we can just leave this as null
  virtual std::optional<phy::TxSettings> getDefaultTxSettings() {
    return std::nullopt;
  }

  virtual bool shouldInitializePimXphy(PimID pim) const;

  virtual phy::PhyInfo getPhyInfo(PortID /* swPort */) {
    return phy::PhyInfo{};
  }

  virtual std::string getPortInfoStr(PortID /* swPort */) {
    return "getPortInfoStr Not implemented on this platform";
  }

  const std::string& getPortName(PortID portID) const;

  virtual void setPortLoopbackState(
      PortID /* swPort */,
      phy::PortComponent /* component */,
      bool /* setLoopback */) {}

  virtual void setPortAdminState(
      PortID /* swPort */,
      phy::PortComponent /* component */,
      bool /* setAdminUp */) {}

  virtual void gracefulExit() {}

  // True for SAI-based xphys (SaiPhyManager), where each xphy keeps its own
  // warm boot state and flag, written only on gracefulExit().
  virtual bool isSaiPhyManager() const {
    return false;
  }

  void setPublishPhyCb(PublishPhyCb publishPhyCb) {
    publishPhyCb_ = publishPhyCb;
  }

  XphyThreadingModel getXphyThreadingModel() const {
    return xphyThreadingModel_;
  }

 protected:
  struct PortCacheInfo {
    // PhyManager is in the middle of changing its apis to accept PortID instead
    // of asking users to get all three Pim/MDIO Controller/PHY id.
    // Using a global PortID will make it easy for the communication b/w
    // wedge_agent and qsfp_service.
    // As for PhyManager, we need to use the GlobalXphyID to locate the exact
    // ExternalPhy so that we can call ExternalPhy apis to program the xphy.
    // This map will cache the two global ID: PortID and GlobalXphyID
    GlobalXphyID xphyID;
    std::optional<cfg::PortSpeed> speed;
    std::optional<cfg::PortProfileID> profile;
    // Based on current ExternalPhy design, it's hard to get which lanes that
    // a SW port is using. Because all the ExternalPhy are using lanes to
    // program the configs directly instead of a software port. Since we always
    // use PhyManager to programOnePort(), we can cache the LaneID list of both
    // system and line sides in PhyManager. And then for all the following xphy
    // related logic like, programming prbs, getting stats, we can just use this
    // cached info to pass in the xphy lanes directly.
    std::vector<LaneID> systemLanes;
    std::vector<LaneID> lineLanes;
  };
  using PortCacheRLockedPtr = folly::Synchronized<PortCacheInfo>::RLockedPtr;
  using PortCacheWLockedPtr = folly::Synchronized<PortCacheInfo>::WLockedPtr;
  PortCacheRLockedPtr getRLockedCache(PortID portID) const;
  PortCacheWLockedPtr getWLockedCache(PortID portID) const;

  const PlatformMapping* getPlatformMapping() const {
    return platformMapping_;
  }

  void setupPimEventMultiThreading(PimID pimID);

  template <typename LockedPtr>
  phy::ExternalPhy* getExternalPhyLocked(const LockedPtr& lockedCache) {
    return getExternalPhy(lockedCache->xphyID);
  }

  void setXphyThreadingModel(XphyThreadingModel model) {
    xphyThreadingModel_ = model;
  }

  // Return true if the new portConfig is different from the cache
  // Otherwise return false when there're speed/profile/lane changes
  bool setPortToPortCacheInfoLocked(
      const PortCacheWLockedPtr& lockedCache,
      PortID portID,
      cfg::PortProfileID profileID,
      const std::optional<TransceiverInfo>& transceiverInfo,
      const phy::PhyPortConfig& portConfig);

  void setPortToExternalPhyPortStats(
      PortID portID,
      std::unique_ptr<ExternalPhyPortStatsUtils> stats);

  template <typename LockedPtr>
  GlobalXphyID getGlobalXphyIDbyPortIDLocked(
      const LockedPtr& lockedCache) const {
    return lockedCache->xphyID;
  }

  template <typename LockedPtr>
  phy::PhyPortConfig getHwPhyPortConfigLocked(
      const LockedPtr& lockedCache,
      PortID portID,
      bool readFromHw = false) {
    if (lockedCache->systemLanes.empty() || lockedCache->lineLanes.empty()) {
      throw FbossError(
          "Port:", portID, " has not program yet. Can't find the cached info");
    }
    auto* xphy = getExternalPhyLocked(lockedCache);
    return xphy->getConfigOnePort(
        lockedCache->systemLanes,
        lockedCache->lineLanes,
        lockedCache->profile.value_or(cfg::PortProfileID::PROFILE_DEFAULT),
        readFromHw);
  }

  // Number of slot in the platform
  int numOfSlot_{0};

  // Since we're planning to allow PhyManager to use SW PortID to change
  // the xphy config for a FBOSS port, and current PlatformMapping has this
  // PortID : XPHY chip = 1 : 1 relationship, we should consider to use a
  // simplified map (2D vs 3D externalPhyMap_).
  // Besides there's no strong use case we need to deal with all the ports from
  // the same MDIO.
  // This design is also easier to let user to create one random XPHY for the
  // system(i.e. hw_test) without using vectors like externalPhyMap_ and
  // being cautious about the order of the mdio and phy.
  using PimXphyMap = std::map<GlobalXphyID, std::unique_ptr<phy::ExternalPhy>>;
  // Technically we can just use 1D map to store all the XPHY as we have
  // a unique xphy id for each one. But because we might need some pim-level
  // operations, like initializing all phys in the same slot(initializeSlotPhys)
  // or supporting multi-threading per pim. So we divide the whole xphy map
  // based on PimID
  using XphyMap = std::map<PimID, PimXphyMap>;
  XphyMap xphyMap_;

  // Xphy port related stats and prbs stats
  struct PortStatsInfo {
    std::unique_ptr<ExternalPhyPortStatsUtils> stats;
    std::optional<folly::Future<folly::Unit>> ongoingStatCollection;
    std::optional<folly::Future<folly::Unit>> ongoingPrbsStatCollection;
  };
  using PortStatsRLockedPtr = folly::Synchronized<PortStatsInfo>::RLockedPtr;
  using PortStatsWLockedPtr = folly::Synchronized<PortStatsInfo>::WLockedPtr;
  PortStatsRLockedPtr getRLockedStats(PortID portID) const;
  PortStatsWLockedPtr getWLockedStats(PortID portID) const;

 private:
  virtual void createExternalPhy(
      const phy::PhyIDInfo& phyIDInfo,
      MultiPimPlatformPimContainer* pimContainer) = 0;

  virtual std::unique_ptr<ExternalPhyPortStatsUtils> createExternalPhyPortStats(
      PortID portID) = 0;

  using PortToCacheInfo = std::unordered_map<
      PortID,
      std::unique_ptr<folly::Synchronized<PortCacheInfo>>>;
  PortToCacheInfo setupPortToCacheInfo(const PlatformMapping* platformMapping);

  // PortToCacheInfo is a heavily used cache while stats should only be used
  // when the scheduler function kicks in and also some platforms might take
  // longer than the other to collect stats. Split PortCacheInfo and
  // PortStatsInfo into two const map so that each map doesn't affect the other
  // map performance.
  using PortToStatsInfo = std::unordered_map<
      PortID,
      std::unique_ptr<folly::Synchronized<PortStatsInfo>>>;
  PortToStatsInfo setupPortToStatsInfo(const PlatformMapping* platformMapping);

  // Update PortStatsInfo::stats
  void updatePortStats(
      PortID portID,
      const PimID& pimID,
      const GlobalXphyID& xphyID,
      phy::ExternalPhy* xphy,
      const PortStatsWLockedPtr& wLockedStats,
      folly::EventBase* pimEvb);
  void updatePrbsStats(
      PortID portID,
      phy::ExternalPhy* xphy,
      const PortStatsWLockedPtr& wLockedStats,
      folly::EventBase* pimEvb);

  const PlatformMapping* platformMapping_;

  // For PhyManager programming xphy, each pim can program their xphys at the
  // same time without worrying affecting other pim's operation.
  struct PimEventMultiThreading {
    PimID pim;
    std::unique_ptr<folly::EventBase> eventBase;
    std::unique_ptr<std::thread> thread;

    explicit PimEventMultiThreading(PimID pimID);
    ~PimEventMultiThreading();
  };
  std::unordered_map<PimID, std::unique_ptr<PimEventMultiThreading>>
      pimToThread_;

  // Threading model - defaults to PIM_LEVEL for backward compatibility
  XphyThreadingModel xphyThreadingModel_{XphyThreadingModel::PIM_LEVEL};

  // In the constructor function, we create this portToCacheInfo_ based on the
  // PlatformMapping, which should have all xphy ports. But their default
  // PortCacheInfo has empty `systemLanes` and `lineLanes` as they're not
  // programmed on xphy yet.
  // As a controlling port can subsume the subsumed ports' lanes to reach a
  // higher speed, therefore, a port can be "deleted" by clearing systemLanes
  // and lineLanes. And both systemLanes and lineLanes are important for
  // ExternalPhy functions.
  // Thus, we need to make the whole PortCacheInfo as synchronized to protect
  // multi-threading accessing the cache info.
  const PortToCacheInfo portToCacheInfo_;
  const PortToStatsInfo portToStatsInfo_;

  static constexpr auto kXphySnapshotIntervalSeconds = 10;
  std::unique_ptr<PhySnapshotManager> xphySnapshotManager_;

  PublishPhyCb publishPhyCb_;
};

} // namespace fboss
} // namespace facebook
