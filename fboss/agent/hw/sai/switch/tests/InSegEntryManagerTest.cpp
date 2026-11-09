// Copyright 2004-present Facebook. All Rights Reserved.
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiInSegEntryManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/tests/ManagerTestBase.h"

#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

namespace {
void processInSegEntryDelta(
    SaiInSegEntryManager& manager,
    const ThriftMapDelta<LabelForwardingInformationBase>& delta,
    const std::shared_ptr<SwitchState>& state) {
  DeltaFunctions::forEachChanged(
      delta,
      [&manager, &state](auto removed, auto added) {
        manager.processChangedInSegEntry(removed, added, state);
      },
      [&manager, &state](auto added) {
        manager.processAddedInSegEntry(added, state);
      },
      [&manager](auto removed) { manager.processRemovedInSegEntry(removed); });
}
} // namespace

class InSegEntryManagerTest : public ManagerTestBase {
 public:
  void SetUp() override {
    setupStage = SetupStage::PORT | SetupStage::VLAN | SetupStage::INTERFACE |
        SetupStage::NEIGHBOR;
    ManagerTestBase::SetUp();
  }

  LabelNextHopEntry getLabelNextHopEntryWithNextHops(
      int fromNextHop,
      int toNextHop,
      LabelForwardingAction::LabelForwardingType type) {
    LabelNextHopSet nexthops;
    for (auto nextHopIndex = fromNextHop;
         nextHopIndex < toNextHop && nextHopIndex < testInterfaces.size();
         nextHopIndex++) {
      nexthops.emplace(makeMplsNextHop(
          testInterfaces[nextHopIndex],
          getActionForNextHop(type, nextHopIndex)));
    }
    return LabelNextHopEntry(
        std::move(nexthops), AdminDistance::MAX_ADMIN_DISTANCE);
  }

  LabelForwardingAction getActionForNextHop(
      LabelForwardingAction::LabelForwardingType type,
      int nextHopIndex) {
    switch (type) {
      case LabelForwardingAction::LabelForwardingType::PUSH:
        return LabelForwardingAction{
            type, {100 + nextHopIndex, 200 + nextHopIndex, 300 + nextHopIndex}};

      case LabelForwardingAction::LabelForwardingType::SWAP:
        return LabelForwardingAction{type, 1000 + nextHopIndex};

      case LabelForwardingAction::LabelForwardingType::PHP:
      case LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP:
      case LabelForwardingAction::LabelForwardingType::NOOP:
        break;
    }
    return LabelForwardingAction{type};
  }

  void addEntryToLabelForwardingInformationBase(
      LabelForwardingInformationBase* fib,
      Label label,
      int nextHopBegin,
      int nextHopEnd,
      LabelForwardingAction::LabelForwardingType actionType) {
    auto node =
        std::make_shared<LabelForwardingEntry>(LabelForwardingEntry::makeThrift(
            label,
            ClientID(ClientID::OPENR),
            getLabelNextHopEntryWithNextHops(
                nextHopBegin, nextHopEnd, actionType)));
    MultiLabelForwardingInformationBase::resolve(node);
    fib->addNode(node);
  }

  void removeEntryFromLabelForwardingInformationBase(
      LabelForwardingInformationBase* fib,
      Label label) {
    fib->removeNode(label.label());
  }

  void verifyLabelForwardingEntry(
      Label label,
      int nextHopBegin,
      int nextHopEnd,
      LabelForwardingAction::LabelForwardingType type) {
    const auto* inSegEntryHandle =
        saiManagerTable->inSegEntryManager().getInSegEntryHandle(label);
    ASSERT_NE(inSegEntryHandle, nullptr);
    ASSERT_NE(inSegEntryHandle->inSegEntry, nullptr);
    ASSERT_NE(inSegEntryHandle->nextHopGroupHandle(), nullptr);

    auto adapterKey = inSegEntryHandle->inSegEntry->adapterKey();
    EXPECT_EQ(adapterKey.label(), static_cast<int32_t>(label.value()));
    auto switchId = saiManagerTable->switchManager().getSwitchSaiId();
    EXPECT_EQ(adapterKey.switchId(), switchId);

    auto attributes = inSegEntryHandle->inSegEntry->attributes();
    EXPECT_EQ(
        GET_ATTR(InSeg, PacketAction, attributes), SAI_PACKET_ACTION_FORWARD);
    EXPECT_EQ(GET_ATTR(InSeg, NumOfPop, attributes), 1);
    auto nexHopId =
        inSegEntryHandle->nextHopGroupHandle()->nextHopGroup->adapterKey();
    EXPECT_EQ(GET_OPT_ATTR(InSeg, NextHopId, attributes), nexHopId);

    verifyNextHopGroup(nexHopId, nextHopBegin, nextHopEnd, type);
  }

  void verifyNextHopGroup(
      NextHopGroupSaiId nextHopGroupId,
      int nextHopBegin,
      int nextHopEnd,
      LabelForwardingAction::LabelForwardingType type) {
    auto& nextHopGroupApi = saiApiTable->nextHopGroupApi();
    auto& nextHopApi = saiApiTable->nextHopApi();
    SaiNextHopGroupTraits::Attributes::NextHopMemberList memberList{};
    auto members = nextHopGroupApi.getAttribute(nextHopGroupId, memberList);
    EXPECT_EQ(members.size(), nextHopEnd - nextHopBegin);

    std::unordered_map<
        folly::IPAddress,
        SaiNextHopGroupMemberTraits::Attributes::NextHopId>
        gotNextHops;
    for (const auto& member : members) {
      auto nextHopId = nextHopGroupApi.getAttribute(
          NextHopGroupMemberSaiId(member),
          SaiNextHopGroupMemberTraits::Attributes::NextHopId{});
      folly::IPAddress ip = nextHopApi.getAttribute(
          NextHopSaiId(nextHopId), SaiIpNextHopTraits::Attributes::Ip{});
      gotNextHops.emplace(ip, nextHopId);
    }

    for (auto nextHopIndex = nextHopBegin;
         nextHopIndex < nextHopEnd && nextHopIndex < testInterfaces.size();
         nextHopIndex++) {
      auto itr =
          gotNextHops.find(testInterfaces[nextHopIndex].remoteHosts[0].ip);
      EXPECT_TRUE(itr != gotNextHops.end());
      if (itr == gotNextHops.end()) {
        continue;
      }
      auto action = getActionForNextHop(type, nextHopIndex);
      auto memberId = itr->second.value();

      if (type == LabelForwardingAction::LabelForwardingType::SWAP ||
          type == LabelForwardingAction::LabelForwardingType::PUSH) {
        // verify mpls next hop
        std::vector<sai_label_id_t> expectedStack;
        if (type == LabelForwardingAction::LabelForwardingType::SWAP) {
          expectedStack.push_back(
              static_cast<sai_label_id_t>(action.swapWith().value()));
        } else if (type == LabelForwardingAction::LabelForwardingType::PUSH) {
          for (auto label : action.pushStack().value()) {
            expectedStack.push_back(static_cast<sai_label_id_t>(label));
          }
        }

        SaiMplsNextHopTraits::Attributes::LabelStack labelStackAttribute;
        verifyNextHop(
            static_cast<NextHopSaiId>(memberId),
            SAI_NEXT_HOP_TYPE_MPLS,
            expectedStack);
      } else {
        // verify ip next hop
        verifyNextHop(
            static_cast<NextHopSaiId>(memberId), SAI_NEXT_HOP_TYPE_IP, {});
      }
    }
  }

  void verifyNextHop(
      NextHopSaiId id,
      sai_next_hop_type_t nextHopType,
      const std::vector<sai_label_id_t>& stack) {
    auto& nextHopApi = saiApiTable->nextHopApi();
    EXPECT_EQ(
        nextHopType,
        nextHopApi.getAttribute(id, SaiIpNextHopTraits::Attributes::Type{}));
    if (nextHopType == SAI_NEXT_HOP_TYPE_MPLS) {
      auto labelStack = nextHopApi.getAttribute(
          id, SaiMplsNextHopTraits::Attributes::LabelStack{});
      EXPECT_EQ(labelStack, stack);
    }
  }
};

TEST_F(InSegEntryManagerTest, createInSegEntry) {
  LabelForwardingInformationBase empty{};
  LabelForwardingInformationBase fib{};

  // setup
  addEntryToLabelForwardingInformationBase(
      &fib,
      100 /* label */,
      0 /* begin next hop id */,
      4 /* end next hop id */,
      LabelForwardingAction::LabelForwardingType::SWAP);
  ThriftMapDelta<LabelForwardingInformationBase> delta(&empty, &fib);
  processInSegEntryDelta(
      saiManagerTable->inSegEntryManager(), delta, programmedState);

  // verify
  verifyLabelForwardingEntry(
      100 /* label */,
      0 /* begin next hop id */,
      4 /* end next hop id */,
      LabelForwardingAction::LabelForwardingType::SWAP);
  const auto* preWarmBootHandle =
      saiManagerTable->inSegEntryManager().getInSegEntryHandle(100);
  auto preWarmBootAdapterKey = preWarmBootHandle->inSegEntry->adapterKey();
  auto preWarmBootAttributes = preWarmBootHandle->inSegEntry->attributes();

  ManagerTestBase::pseudoWarmBootExitAndStoreReload();

  // verify pre and post matches
  auto postStoreReloadHandle =
      saiStore->get<SaiInSegTraits>().get(preWarmBootAdapterKey);
  ASSERT_NE(postStoreReloadHandle, nullptr);
  auto postStoreReloadAttributes = postStoreReloadHandle->attributes();
  EXPECT_EQ(preWarmBootAttributes, postStoreReloadAttributes);
}

TEST_F(InSegEntryManagerTest, changeInSegEntry) {
  LabelForwardingInformationBase empty{};
  LabelForwardingInformationBase fib0{};

  // add
  addEntryToLabelForwardingInformationBase(
      &fib0,
      100 /* label */,
      0 /* begin next hop id */,
      4 /* end next hop id */,
      LabelForwardingAction::LabelForwardingType::SWAP);
  ThriftMapDelta<LabelForwardingInformationBase> delta0(&empty, &fib0);
  processInSegEntryDelta(
      saiManagerTable->inSegEntryManager(), delta0, programmedState);
  LabelForwardingInformationBase fib1{};
  // change
  addEntryToLabelForwardingInformationBase(
      &fib1,
      100 /* label */,
      1 /* begin next hop id */,
      5 /* end next hop id */,
      LabelForwardingAction::LabelForwardingType::PUSH);
  ThriftMapDelta<LabelForwardingInformationBase> delta1(&fib0, &fib1);
  processInSegEntryDelta(
      saiManagerTable->inSegEntryManager(), delta1, programmedState);

  // verify
  verifyLabelForwardingEntry(
      100 /* label */,
      1 /* begin next hop id */,
      5 /* end next hop id */,
      LabelForwardingAction::LabelForwardingType::PUSH);

  const auto* preWarmBootHandle =
      saiManagerTable->inSegEntryManager().getInSegEntryHandle(100);
  auto preWarmBootAdapterKey = preWarmBootHandle->inSegEntry->adapterKey();
  auto preWarmBootAttributes = preWarmBootHandle->inSegEntry->attributes();

  ManagerTestBase::pseudoWarmBootExitAndStoreReload();

  // verify pre and post matches
  auto postStoreReloadHandle =
      saiStore->get<SaiInSegTraits>().get(preWarmBootAdapterKey);
  ASSERT_NE(postStoreReloadHandle, nullptr);
  auto postStoreReloadAttributes = postStoreReloadHandle->attributes();
  EXPECT_EQ(preWarmBootAttributes, postStoreReloadAttributes);
}

TEST_F(InSegEntryManagerTest, removeInSegEntry) {
  LabelForwardingInformationBase empty{};
  LabelForwardingInformationBase fib{};

  // add
  addEntryToLabelForwardingInformationBase(
      &fib,
      100 /* label */,
      0 /* begin next hop id */,
      4 /* end next hop id */,
      LabelForwardingAction::LabelForwardingType::SWAP);
  ThriftMapDelta<LabelForwardingInformationBase> delta0(&empty, &fib);
  processInSegEntryDelta(
      saiManagerTable->inSegEntryManager(), delta0, programmedState);
  // remove
  ThriftMapDelta<LabelForwardingInformationBase> delta1(&fib, &empty);
  processInSegEntryDelta(
      saiManagerTable->inSegEntryManager(), delta1, programmedState);

  const auto* preWarmBootHandle =
      saiManagerTable->inSegEntryManager().getInSegEntryHandle(100);
  ASSERT_EQ(preWarmBootHandle, nullptr);

  ManagerTestBase::pseudoWarmBootExitAndStoreReload();

  const auto* postStoreReloadHandle =
      saiManagerTable->inSegEntryManager().getInSegEntryHandle(100);
  ASSERT_EQ(postStoreReloadHandle, nullptr);
}

} // namespace facebook::fboss
