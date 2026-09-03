// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/packet/MPLSHdr.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/types.h"

#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"

namespace facebook::fboss {
class TestEnsembleIf;
}

namespace facebook::fboss::utility {

template <typename EcmpSetupHelperT>
class HwEcmpDataPlaneTestUtil {
 public:
  HwEcmpDataPlaneTestUtil(
      TestEnsembleIf* hwSwitchEnsemble,
      std::unique_ptr<EcmpSetupHelperT> helper)
      : ensemble_(hwSwitchEnsemble), helper_(std::move(helper)) {}
  virtual ~HwEcmpDataPlaneTestUtil() {}

  virtual void programRoutes(
      int ecmpWidth,
      const std::vector<NextHopWeight>& weights) = 0;

  void pumpTraffic(int ecmpWidth, bool loopThroughFrontPanel);
  void unresolveNextHop(unsigned int id);
  void resolveNextHopsandClearStats(unsigned int ecmpWidth);
  void shrinkECMP(unsigned int ecmpWidth, bool clearStats = true);
  void expandECMP(unsigned int ecmpWidth, bool clearStats = true);
  EcmpSetupHelperT* ecmpSetupHelper() const {
    return helper_.get();
  }
  TestEnsembleIf* getEnsemble() {
    return ensemble_;
  }
  bool isLoadBalanced(
      int ecmpWidth,
      const std::vector<NextHopWeight>& weights,
      uint8_t deviation);
  bool isLoadBalanced(
      const std::vector<PortDescriptor>& portDescs,
      const std::vector<NextHopWeight>& weights,
      uint8_t deviation);
  bool isTrafficSprayed(
      const std::vector<PortDescriptor>& portDescs,
      const MemberShareBounds& bounds);

  void programLoadBalancer(const cfg::LoadBalancer& lb);

  void programRoutesAndLoadBalancer(
      int ecmpWidth,
      const std::vector<NextHopWeight>& weights,
      const cfg::LoadBalancer& lb) {
    programRoutes(ecmpWidth, weights);
    programLoadBalancer(lb);
  }

  /*
   * When sprayBounds is set, each member is checked against a share of the
   * total rather than against every other member. See MemberShareBounds.
   */
  void pumpTrafficPortAndVerifyLoadBalanced(
      unsigned int ecmpWidth,
      bool loopThroughFrontPanel,
      const std::vector<NextHopWeight>& weights,
      int deviation,
      bool loadBalanceExpected,
      const std::optional<MemberShareBounds>& sprayBounds = std::nullopt);

 protected:
  static constexpr uint64_t kDefaultPacketCount{10000};
  static constexpr uint64_t kDefaultRocePacketCount{200000};

 private:
  virtual void pumpTrafficThroughPort(std::optional<PortID> port) = 0;
  virtual uint64_t getNumPacketsToPump() const {
    return kDefaultPacketCount;
  }

  TestEnsembleIf* ensemble_;
  std::unique_ptr<EcmpSetupHelperT> helper_;
};

template <typename AddrT>
class HwIpEcmpDataPlaneTestUtil
    : public HwEcmpDataPlaneTestUtil<EcmpSetupAnyNPorts<AddrT>> {
 public:
  using EcmpSetupAnyNPortsT = EcmpSetupAnyNPorts<AddrT>;
  using BaseT = HwEcmpDataPlaneTestUtil<EcmpSetupAnyNPortsT>;

  HwIpEcmpDataPlaneTestUtil(TestEnsembleIf* ensemble, RouterID vrf);

  HwIpEcmpDataPlaneTestUtil(
      TestEnsembleIf* ensemble,
      RouterID vrf,
      std::vector<LabelForwardingAction::LabelStack> stacks);
  HwIpEcmpDataPlaneTestUtil(
      TestEnsembleIf* ensemble,
      const std::optional<folly::MacAddress>& nextHopMac,
      RouterID vrf);

  void programRoutes(int ecmpWidth, const std::vector<NextHopWeight>& weights)
      override;
  void programRoutes(
      const boost::container::flat_set<PortDescriptor>& portDescs,
      const std::vector<NextHopWeight>& weights);
  void programRoutesVecHelper(
      const std::vector<PortDescriptor>& portDescs,
      const std::vector<NextHopWeight>& weights);
  /* pump IP traffic */
  void pumpTrafficThroughPort(std::optional<PortID> port) override;

  const std::vector<EcmpNextHop<AddrT>> getNextHops();

 private:
  std::vector<LabelForwardingAction::LabelStack> stacks_;
};

template <typename AddrT>
class HwIpRoCEEcmpDataPlaneTestUtil : public HwIpEcmpDataPlaneTestUtil<AddrT> {
 public:
  using BaseT = HwIpEcmpDataPlaneTestUtil<AddrT>;

  HwIpRoCEEcmpDataPlaneTestUtil(TestEnsembleIf* ensemble, RouterID vrf);

  /* pump IP traffic */
  void pumpTrafficThroughPort(std::optional<PortID> port) override;

 private:
  uint64_t getNumPacketsToPump() const override {
    return BaseT::kDefaultRocePacketCount;
  }
};

template <typename AddrT>
class HwIpRoCEEcmpDestPortDataPlaneTestUtil
    : public HwIpEcmpDataPlaneTestUtil<AddrT> {
 public:
  using BaseT = HwIpEcmpDataPlaneTestUtil<AddrT>;

  HwIpRoCEEcmpDestPortDataPlaneTestUtil(TestEnsembleIf* ensemble, RouterID vrf);

  /* pump IP traffic */
  void pumpTrafficThroughPort(std::optional<PortID> port) override;

 private:
  uint64_t getNumPacketsToPump() const override {
    return BaseT::kDefaultRocePacketCount;
  }
};

template <typename AddrT>
class HwMplsEcmpDataPlaneTestUtil
    : public HwEcmpDataPlaneTestUtil<MplsEcmpSetupAnyNPorts<AddrT>> {
 public:
  using EcmpSetupAnyNPortsT = MplsEcmpSetupAnyNPorts<AddrT>;
  using BaseT = HwEcmpDataPlaneTestUtil<MplsEcmpSetupAnyNPorts<AddrT>>;

  HwMplsEcmpDataPlaneTestUtil(
      TestEnsembleIf* ensemble,
      MPLSHdr::Label topLabel,
      LabelForwardingAction::LabelForwardingType actionType);

  void programRoutes(int ecmpWidth, const std::vector<NextHopWeight>& weights)
      override;
  /* pump MPLS traffic */
  void pumpTrafficThroughPort(std::optional<PortID> port) override;

 private:
  MPLSHdr::Label label_;
};

class HwIpV6FlowLabelEcmpDataPlaneTestUtil
    : public HwIpEcmpDataPlaneTestUtil<folly::IPAddressV6> {
 public:
  using BaseT = HwIpEcmpDataPlaneTestUtil<folly::IPAddressV6>;

  HwIpV6FlowLabelEcmpDataPlaneTestUtil(TestEnsembleIf* ensemble, RouterID vrf);

  void pumpTrafficThroughPort(std::optional<PortID> port) override;
};

class HwSrv6EcmpDataPlaneTestUtil
    : public HwIpEcmpDataPlaneTestUtil<folly::IPAddressV6> {
 public:
  using BaseT = HwIpEcmpDataPlaneTestUtil<folly::IPAddressV6>;

  HwSrv6EcmpDataPlaneTestUtil(TestEnsembleIf* ensemble, RouterID vrf);

  void programRoutes(int ecmpWidth, const std::vector<NextHopWeight>& weights)
      override;
};

// SRv6 ECMP with traffic varying only the IPv6 flow label (fixed 5-tuple), to
// exercise flow-label based hashing over SRv6 encap specifically.
class HwSrv6FlowLabelEcmpDataPlaneTestUtil
    : public HwSrv6EcmpDataPlaneTestUtil {
 public:
  using BaseT = HwSrv6EcmpDataPlaneTestUtil;

  HwSrv6FlowLabelEcmpDataPlaneTestUtil(TestEnsembleIf* ensemble, RouterID vrf);

  void pumpTrafficThroughPort(std::optional<PortID> port) override;
};

using HwIpV4EcmpDataPlaneTestUtil =
    HwIpEcmpDataPlaneTestUtil<folly::IPAddressV4>;
using HwIpV6EcmpDataPlaneTestUtil =
    HwIpEcmpDataPlaneTestUtil<folly::IPAddressV6>;
using HwMplsV4EcmpDataPlaneTestUtil =
    HwMplsEcmpDataPlaneTestUtil<folly::IPAddressV4>;
using HwMplsV6EcmpDataPlaneTestUtil =
    HwMplsEcmpDataPlaneTestUtil<folly::IPAddressV6>;
using HwIpV6RoCEEcmpDataPlaneTestUtil =
    HwIpRoCEEcmpDataPlaneTestUtil<folly::IPAddressV6>;
using HwIpV6RoCEEcmpDestPortDataPlaneTestUtil =
    HwIpRoCEEcmpDestPortDataPlaneTestUtil<folly::IPAddressV6>;
using HwIpV4RoCEEcmpDataPlaneTestUtil =
    HwIpRoCEEcmpDataPlaneTestUtil<folly::IPAddressV4>;

} // namespace facebook::fboss::utility
