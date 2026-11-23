// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/api/Srv6Api.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)

#include <folly/IPAddressV6.h>

using namespace facebook::fboss;

class Srv6ManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE;
    ManagerTestBase::SetUp();
  }

  SaiSrv6SidListTraits::CreateAttributes makeCreateAttributes(
      sai_int32_t type = SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED,
      const std::optional<std::vector<std::array<uint8_t, 16>>>& segmentList =
          std::nullopt) {
    return SaiSrv6SidListTraits::CreateAttributes{
        type, segmentList, std::nullopt};
  }

  SaiSrv6SidListTraits::AdapterHostKey makeAdapterHostKey(
      sai_int32_t type = SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED,
      const std::optional<std::vector<std::array<uint8_t, 16>>>& segmentList =
          std::nullopt,
      RouterInterfaceSaiId rifId = RouterInterfaceSaiId{42},
      folly::IPAddress ip = folly::IPAddress("10.0.0.1")) {
    return SaiSrv6SidListTraits::AdapterHostKey{type, segmentList, rifId, ip};
  }
};

TEST_F(Srv6ManagerTest, addSrv6SidList) {
  auto key = makeAdapterHostKey();
  auto attrs = makeCreateAttributes();
  auto handle =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
  EXPECT_NE(handle, nullptr);
  EXPECT_NE(handle->managedSidList->getSidList(), nullptr);
}

TEST_F(Srv6ManagerTest, addSrv6SidListWithSegments) {
  auto segments = toSaiIp6List(
      {folly::IPAddressV6("2001:db8::1"),
       folly::IPAddressV6("2001:db8::2"),
       folly::IPAddressV6("2001:db8::3")});
  auto key = makeAdapterHostKey(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED, segments);
  auto attrs = makeCreateAttributes(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED, segments);
  auto handle =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
  EXPECT_NE(handle, nullptr);
  EXPECT_NE(handle->managedSidList->getSidList(), nullptr);

  auto& srv6Api = saiApiTable->srv6Api();
  auto gotType = srv6Api.getAttribute(
      handle->managedSidList->getSidList()->adapterKey(),
      SaiSrv6SidListTraits::Attributes::Type{});
  EXPECT_EQ(gotType, SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);

  auto gotSegments = srv6Api.getAttribute(
      handle->managedSidList->getSidList()->adapterKey(),
      SaiSrv6SidListTraits::Attributes::SegmentList{});
  EXPECT_EQ(gotSegments, segments);
}

TEST_F(Srv6ManagerTest, reuseSrv6SidList) {
  auto key = makeAdapterHostKey();
  auto attrs = makeCreateAttributes();
  auto handle1 =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
  auto handle2 =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
  // Same key should return the same handle
  EXPECT_EQ(handle1, handle2);
  EXPECT_EQ(
      handle1->managedSidList->getSidList(),
      handle2->managedSidList->getSidList());
}

TEST_F(Srv6ManagerTest, getSrv6SidListHandle) {
  auto key = makeAdapterHostKey();
  auto attrs = makeCreateAttributes();
  // Must hold the shared_ptr to keep the RefMap entry alive
  auto holder =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
  auto* handle =
      saiManagerTable->srv6SidListManager().getSrv6SidListHandle(key);
  EXPECT_NE(handle, nullptr);
  EXPECT_NE(handle->managedSidList->getSidList(), nullptr);
}

TEST_F(Srv6ManagerTest, getNonexistentSrv6SidList) {
  auto key = makeAdapterHostKey();
  auto* handle =
      saiManagerTable->srv6SidListManager().getSrv6SidListHandle(key);
  EXPECT_EQ(handle, nullptr);
}

TEST_F(Srv6ManagerTest, addDifferentSidLists) {
  auto key1 = makeAdapterHostKey(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
  auto attrs1 = makeCreateAttributes(SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED);
  auto key2 = makeAdapterHostKey(SAI_SRV6_SIDLIST_TYPE_INSERT_RED);
  auto attrs2 = makeCreateAttributes(SAI_SRV6_SIDLIST_TYPE_INSERT_RED);

  auto handle1 =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key1, attrs1);
  auto handle2 =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key2, attrs2);
  EXPECT_NE(handle1, handle2);
  EXPECT_NE(handle1->managedSidList->getSidList(), nullptr);
  EXPECT_NE(handle2->managedSidList->getSidList(), nullptr);
  EXPECT_NE(
      handle1->managedSidList->getSidList()->adapterKey(),
      handle2->managedSidList->getSidList()->adapterKey());
}

TEST_F(Srv6ManagerTest, refCountRelease) {
  auto key = makeAdapterHostKey();
  auto attrs = makeCreateAttributes();
  std::weak_ptr<SaiSrv6SidListHandle> weakHandle;
  {
    auto handle =
        saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
    weakHandle = handle;
    EXPECT_EQ(handle.use_count(), 1);
  }
  // After releasing the only reference, the handle should be expired
  EXPECT_TRUE(weakHandle.expired());
}

TEST_F(Srv6ManagerTest, recreateAfterRelease) {
  auto key = makeAdapterHostKey();
  auto attrs = makeCreateAttributes();
  sai_object_id_t firstAdapterKey;
  {
    auto handle =
        saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
    firstAdapterKey = handle->managedSidList->getSidList()->adapterKey();
  }
  // Entry released; creating again should produce a fresh object
  auto handle =
      saiManagerTable->srv6SidListManager().addOrReuseSrv6SidList(key, attrs);
  EXPECT_NE(handle, nullptr);
  EXPECT_NE(handle->managedSidList->getSidList(), nullptr);
  // New SAI object gets a different adapter key
  EXPECT_NE(
      handle->managedSidList->getSidList()->adapterKey(), firstAdapterKey);
}

#endif
