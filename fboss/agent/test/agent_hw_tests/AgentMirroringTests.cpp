// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <boost/range/combine.hpp>
#include <folly/IPAddress.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/ErspanParser.h"
#include "fboss/agent/test/utils/MirrorTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

namespace {
const std::string kMirrorAcl = "mirror_acl";

// Each merged test exercises both IPv4 and IPv6 in a single setup()/verify().
// To let both families' mirror sessions coexist (a port direction can hold only
// one mirror, and v4/v6 ERSPAN tunnels can't share a port), each family gets
// its own mirror session name, its own traffic port, and its own
// mirror-to/sflow-to port. Each family uses two ports (traffic at index 0,
// mirror-to at index 1); IPv4 keeps the base constants while IPv6 is offset by
// kV6PortIndexOffset so the two families never collide. The offset equals the
// number of ports a family consumes (2), so the two families pack into the
// first four ports -- this matters on platforms like meru800bia that expose
// only four hyper ports.
constexpr uint8_t kV6PortIndexOffset{2};

// Returns the v6 mirror session name for a given v4 base name (e.g.
// "ingress_erspan" -> "ingress_erspan_v6"). The base name still drives the
// ingress/egress + erspan/span logic; only the session name differs so the two
// families' sessions are distinct entities in SwitchState.
std::string v6MirrorName(const std::string& baseMirrorName) {
  return baseMirrorName + "_v6";
}

const std::string kMirrorAclV6 = kMirrorAcl + "_v6";
} // namespace

namespace facebook::fboss {

class AgentMirroringTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    // Program BOTH families' mirror sessions (and their per-family port/acl
    // config) here so they are the warmboot baseline and survive warm boot.
    // Each derived fixture decides what to add for each family (span vs erspan,
    // port vs acl, truncate) via addMirrorConfigForTest<AddrT>().
    addMirrorConfigForTest<folly::IPAddressV4>(&config, ensemble);
    addMirrorConfigForTest<folly::IPAddressV6>(&config, ensemble);
    return config;
  }

  // Adds the per-family mirror config to the baseline. Implemented in derived
  // fixtures. Dispatches to the templated implementation so a single virtual
  // can serve both families.
  virtual void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const = 0;

  template <typename AddrT>
  void addMirrorConfigForTest(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble) const {
    addMirrorConfigForTestImpl(
        cfg, ensemble, std::is_same_v<AddrT, folly::IPAddressV4>);
  }

  // Per-family port index helpers. IPv4 uses the base index, IPv6 offsets it so
  // the two families occupy disjoint ports.
  template <typename AddrT>
  uint8_t trafficPortIndex() const {
    return std::is_same_v<AddrT, folly::IPAddressV4>
        ? utility::kTrafficPortIndex
        : utility::kTrafficPortIndex + kV6PortIndexOffset;
  }

  template <typename AddrT>
  uint8_t mirrorToPortIndex(
      uint8_t baseIndex = utility::kMirrorToPortIndex,
      bool isUpdate = false) const {
    // The update test moves each family's mirror to the OTHER family's original
    // (now-freed) mirror-to port: v4 -> v6's slot, v6 -> v4's slot. This keeps
    // every port index within the small hyper-port count (meru800bia exposes
    // only 4 hyper ports) while still exercising a real mirror-to port change.
    if (isUpdate) {
      return std::is_same_v<AddrT, folly::IPAddressV4>
          ? static_cast<uint8_t>(
                utility::kMirrorToPortIndex + kV6PortIndexOffset)
          : utility::kMirrorToPortIndex;
    }
    return std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseIndex
        : static_cast<uint8_t>(baseIndex + kV6PortIndexOffset);
  }

  PortID portForIndex(const AgentEnsemble& ensemble, uint8_t index) const {
    if (FLAGS_hyper_port) {
      return ensemble.masterLogicalPortIds({cfg::PortType::HYPER_PORT})[index];
    }
    return ensemble.masterLogicalPortIds(
        {cfg::PortType::INTERFACE_PORT})[index];
  }

  template <typename AddrT>
  PortID getMirrorToPort(
      const AgentEnsemble& ensemble,
      uint8_t baseIndex = utility::kMirrorToPortIndex,
      bool isUpdate = false) const {
    return portForIndex(
        ensemble, mirrorToPortIndex<AddrT>(baseIndex, isUpdate));
  }

  template <typename AddrT>
  PortID getTrafficPort(const AgentEnsemble& ensemble) const {
    return portForIndex(ensemble, trafficPortIndex<AddrT>());
  }

  // Adds the per-family mirror session. mirrorToPortIndex is the
  // family-agnostic base index (offset applied internally for v6 via
  // addMirrorConfig).
  template <typename AddrT>
  void addMirrorSession(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      const std::string& baseMirrorName,
      bool truncate,
      uint8_t baseMirrorToPortIndex = utility::kMirrorToPortIndex) const {
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseMirrorName
        : v6MirrorName(baseMirrorName);
    utility::addMirrorConfig<AddrT>(
        cfg,
        ensemble,
        mirrorName,
        truncate,
        utility::kDscpDefault,
        mirrorToPortIndex<AddrT>(baseMirrorToPortIndex));
  }

  template <typename AddrT>
  void addPortMirrorConfig(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      const std::string& baseMirrorName) const {
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseMirrorName
        : v6MirrorName(baseMirrorName);
    auto trafficPort = getTrafficPort<AddrT>(ensemble);
    auto portConfig = utility::findCfgPort(*cfg, trafficPort);
    if (baseMirrorName == utility::kIngressSpan ||
        baseMirrorName == utility::kIngressErspan) {
      portConfig->ingressMirror() = mirrorName;
    } else if (
        baseMirrorName == utility::kEgressSpan ||
        baseMirrorName == utility::kEgressErspan) {
      portConfig->egressMirror() = mirrorName;
    } else {
      throw FbossError("Invalid mirror name ", baseMirrorName);
    }
  }

  template <typename AddrT>
  void sendPackets(int count, size_t payloadSize = 1) {
    auto params = utility::getMirrorTestParams<AddrT>();
    auto vlanId = getVlanIDForTx();
    const auto dstMac = getMacForFirstInterfaceWithPortsForTesting(
        getAgentEnsemble()->getProgrammedState());
    const auto srcMac = utility::MacAddressGenerator().get(dstMac.u64HBO() + 1);

    std::vector<uint8_t> payload(payloadSize, 0xff);
    auto trafficPort = getTrafficPort<AddrT>(*getAgentEnsemble());
    auto oldPacketStats = getLatestPortStats(trafficPort);
    auto oldOutPkts = *oldPacketStats.outUnicastPkts_() +
        *oldPacketStats.outMulticastPkts_() +
        *oldPacketStats.outBroadcastPkts_();
    auto i = 0;
    while (i < count) {
      auto pkt = utility::makeUDPTxPacket(
          getSw(),
          vlanId,
          srcMac,
          dstMac,
          params.senderIp,
          params.receiverIp,
          srcL4Port_,
          dstL4Port_,
          0,
          255,
          payload);
      sendPacketSwitchedAsync(std::move(pkt));
      i++;
    }
    WITH_RETRIES({
      auto portStats = getLatestPortStats(trafficPort);
      auto newOutPkt = *portStats.outUnicastPkts_() +
          *portStats.outMulticastPkts_() + *portStats.outBroadcastPkts_();
      EXPECT_EVENTUALLY_GE(newOutPkt, oldOutPkts);
      EXPECT_EVENTUALLY_EQ(newOutPkt - oldOutPkts, count);
    });
  }

  template <typename AddrT>
  RoutePrefix<AddrT> getMirrorRoutePrefix(const folly::IPAddress dip) const {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return RoutePrefix<AddrT>{
          folly::IPAddressV4{dip.str()}, static_cast<uint8_t>(dip.bitCount())};
    } else {
      return RoutePrefix<AddrT>{
          folly::IPAddressV6{dip.str()}, static_cast<uint8_t>(dip.bitCount())};
    }
  }

  template <typename AddrT>
  void resolveMirror(
      const std::string& mirrorName,
      uint8_t baseMirrorToPortIndex = utility::kMirrorToPortIndex,
      bool isUpdate = false) {
    std::set<cfg::PortType> ecmpPortTypes;
    if (FLAGS_hyper_port) {
      ecmpPortTypes = {cfg::PortType::HYPER_PORT};
    } else {
      ecmpPortTypes = {cfg::PortType::INTERFACE_PORT};
    }
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        RouterID(0),
        ecmpPortTypes);
    PortID trafficPort = getTrafficPort<AddrT>(*getAgentEnsemble());
    PortID mirrorToPort = getMirrorToPort<AddrT>(
        *getAgentEnsemble(), baseMirrorToPortIndex, isUpdate);
    // These ports are chosen by index into masterLogicalPortIds(), which
    // AgentEnsemble interleaves across dies on multi-die ASICs, while the ECMP
    // helper orders its nexthops from a sorted SwitchState map. The two lists
    // hold the same ports in different orders, so the index only lines up on
    // single-die ASICs, which keep the index assertion unchanged. Where it does
    // not line up, assert the precondition the rest of this function relies on
    // instead: two distinct ports, each usable as a nexthop. ip() looks the
    // port up via nhop(PortDescriptor), which throws when the port has no
    // nexthop.
    if (checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumDies() > 1) {
      ASSERT_NE(trafficPort, mirrorToPort);
      ASSERT_NO_THROW(ecmpHelper.ip(PortDescriptor(trafficPort)))
          << "no ECMP nexthop for traffic port " << trafficPort;
      ASSERT_NO_THROW(ecmpHelper.ip(PortDescriptor(mirrorToPort)))
          << "no ECMP nexthop for mirror-to port " << mirrorToPort;
    } else {
      EXPECT_EQ(
          trafficPort,
          ecmpHelper.nhop(trafficPortIndex<AddrT>()).portDesc.phyPortID());
      EXPECT_EQ(
          mirrorToPort,
          ecmpHelper
              .nhop(mirrorToPortIndex<AddrT>(baseMirrorToPortIndex, isUpdate))
              .portDesc.phyPortID());
    }
    // Route this family's injected traffic out its OWN traffic port. The
    // packets from sendPackets<AddrT>() have dst == receiverIp from
    // getMirrorTestParams<AddrT>(), which matches the family's default route.
    // Resolving + programming that default route via the family's traffic port
    // (not the first ECMP nexthop, which is always the v4 traffic port at index
    // 0) ensures v4 traffic egresses the v4 traffic port and v6 traffic
    // egresses the v6 traffic port. The two families use distinct default
    // prefixes (0.0.0.0/0 vs ::/0) so they never clash.
    boost::container::flat_set<PortDescriptor> trafficNhopPorts{
        PortDescriptor(trafficPort)};
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper.resolveNextHops(in, trafficNhopPorts);
    });
    {
      // The PortDescriptor overload of programRoutes does not substitute a
      // default prefix for an empty list, so pass the family's default route
      // (0.0.0.0/0 for v4, ::/0 for v6) explicitly.
      RoutePrefix<AddrT> defaultPrefix{AddrT(), 0};
      auto wrapper = getSw()->getRouteUpdater();
      ecmpHelper.programRoutes(&wrapper, trafficNhopPorts, {defaultPrefix});
    }
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      boost::container::flat_set<PortDescriptor> nhopPorts{
          PortDescriptor(mirrorToPort)};
      return utility::EcmpSetupAnyNPorts<AddrT>(
                 in,
                 getSw()->needL2EntryForNeighbor(),
                 RouterID(0),
                 ecmpPortTypes)
          .resolveNextHops(in, nhopPorts);
    });
    getSw()->getUpdateEvb()->runInFbossEventBaseThreadAndWait([] {});
    auto mirror = getSw()->getState()->getMirrors()->getNodeIf(mirrorName);
    if (mirror) {
      auto dip = mirror->getDestinationIp();
      if (dip.has_value()) {
        auto prefix = getMirrorRoutePrefix<AddrT>(dip.value());
        boost::container::flat_set<PortDescriptor> nhopPorts{
            PortDescriptor(mirrorToPort)};
        auto wrapper = getSw()->getRouteUpdater();
        ecmpHelper.programRoutes(&wrapper, nhopPorts, {prefix});
      }
    }
  }

  // Resolves both families' mirrors. Called from setup() so both are part of
  // the warmboot baseline.
  void resolveMirrors(
      const std::string& baseMirrorName,
      uint8_t baseMirrorToPortIndex = utility::kMirrorToPortIndex,
      bool isUpdate = false) {
    resolveMirror<folly::IPAddressV4>(
        baseMirrorName, baseMirrorToPortIndex, isUpdate);
    resolveMirror<folly::IPAddressV6>(
        v6MirrorName(baseMirrorName), baseMirrorToPortIndex, isUpdate);
  }

  template <typename AddrT>
  void addAclMirrorConfig(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      const std::string& baseMirrorName) const {
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseMirrorName
        : v6MirrorName(baseMirrorName);
    std::string aclEntryName =
        std::is_same_v<AddrT, folly::IPAddressV4> ? kMirrorAcl : kMirrorAclV6;
    auto aclEntry = cfg::AclEntry();
    aclEntry.name() = aclEntryName;
    aclEntry.actionType() = cfg::AclActionType::PERMIT;
    auto trafficPort = getTrafficPort<AddrT>(ensemble);
    aclEntry.srcPort() = static_cast<uint16_t>(trafficPort);
    /*
     * * Where is the test running?
     *
     * SDK - sai
     * ASIC - th4 and th5 for now. It still needs to be validated on other
     * vendors.
     *
     * * How does adding the srcPort qualifier help?
     *
     * Adding the srcPort qualifier is important because all the ports are in
     * loopback mode during these agent tests. Without specifying a srcPort,
     * packets sent out on the monitor port will loop back and hit the ACL
     * again, which causes a packet loop. The qualifier helps prevent this
     * issue. Using a per-family srcPort also keeps the v4 and v6 ACLs from
     * matching each other's traffic.
     *
     *
     * * What happens to the original packet injected from CPU?
     * The receiverIp (aka dstIP in the callee function) from the sendPackets
     * function are not resolved so the original packet is intended to be
     * dropped. However, there is still an Acl match and we configure the mirror
     * action depending on the test.
     *
     * * What happens to the mirrored packet once it is mirrored after ACL hit?
     * For mirrored packets after an ACL hit, the behavior depends on the type
     * of test. In ingress tests, the ERSPAN test uses an IP address to send
     * packets that hit the ACL. The IP address is resolved, and the packets are
     * sent to the port corresponding to that address. In the SPAN test, all
     * packets matching the ACL are forwarded to the monitor port. SPAN can be
     * configured for a single port, multiple ports, or an entire VLAN, but this
     * test is specifically verifying the single port ACL match. In egress
     * tests, packets that are about to be sent out are compared to the ACL, and
     * if they match, they are forwarded to the monitor port.
     *
     */
    utility::addAclEntry(cfg, aclEntry, utility::kDefaultAclTable());
    auto counterName = aclEntryName + "_counter";
    utility::addAclMirrorAction(
        cfg, aclEntryName, counterName, mirrorName, isIngress());
  }

  void verifyMirrorProgrammed(
      apache::thrift::Client<utility::AgentHwTestCtrl>* client,
      const state::MirrorFields& fields) {
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_TRUE(client->sync_isMirrorProgrammed(fields)); });
  }

  template <typename AddrT>
  void verifyPortMirrorProgrammed(const std::string& mirrorName) {
    auto mirror = getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
    EXPECT_NE(mirror, nullptr);
    auto fields = mirror->toThrift();

    auto scope = getAgentEnsemble()->scopeResolver().scope(mirror);
    for (auto switchID : scope.switchIds()) {
      auto client = getAgentEnsemble()->getHwAgentTestClient(switchID);
      verifyMirrorProgrammed(client.get(), fields);
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(client->sync_isPortMirrored(
            getTrafficPort<AddrT>(*getAgentEnsemble()),
            mirrorName,
            isIngress()));
      });
    }
  }

  void verifyAclMirrorProgrammed(
      const std::string& aclName,
      const std::string& mirrorName) {
    auto mirror = getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
    EXPECT_NE(mirror, nullptr);
    auto fields = mirror->toThrift();

    auto scope = getAgentEnsemble()->scopeResolver().scope(mirror);
    for (auto switchID : scope.switchIds()) {
      auto client = getAgentEnsemble()->getHwAgentTestClient(switchID);
      verifyMirrorProgrammed(client.get(), fields);
      WITH_RETRIES({
        EXPECT_EVENTUALLY_TRUE(
            client->sync_isAclEntryMirrored(aclName, mirrorName, isIngress()));
      });
    }
  }

  template <typename AddrT>
  void verify(
      const std::string& mirrorName,
      uint8_t baseMirrorToPortIndex = utility::kMirrorToPortIndex,
      bool isUpdate = false,
      int payloadSize = 500) {
    PortID trafficPort = getTrafficPort<AddrT>(*getAgentEnsemble());
    PortID mirrorToPort = getMirrorToPort<AddrT>(
        *getAgentEnsemble(), baseMirrorToPortIndex, isUpdate);
    WITH_RETRIES({
      auto ingressMirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
      ASSERT_NE(ingressMirror, nullptr);
      EXPECT_EVENTUALLY_EQ(ingressMirror->isResolved(), true);
    });
    auto trafficPortPktStatsBefore = getLatestPortStats(trafficPort);
    auto mirrorPortPktStatsBefore = getLatestPortStats(mirrorToPort);

    auto trafficPortPktsBefore = *trafficPortPktStatsBefore.outUnicastPkts_();
    auto mirroredPortPktsBefore = *mirrorPortPktStatsBefore.outUnicastPkts_();

    this->sendPackets<AddrT>(1, payloadSize);

    WITH_RETRIES({
      auto trafficPortPktStatsAfter = getLatestPortStats(trafficPort);
      auto trafficPortPktsAfter = *trafficPortPktStatsAfter.outUnicastPkts_();
      EXPECT_EVENTUALLY_EQ(1, trafficPortPktsAfter - trafficPortPktsBefore);
    });

    auto expectedMirrorPackets = 1;
    WITH_RETRIES({
      auto mirrorPortPktStatsAfter = getLatestPortStats(mirrorToPort);
      auto mirroredPortPktsAfter = *mirrorPortPktStatsAfter.outUnicastPkts_();
      EXPECT_EVENTUALLY_EQ(
          mirroredPortPktsAfter - mirroredPortPktsBefore,
          expectedMirrorPackets);
    });
  }

  // Sends + verifies traffic for a single family. Wrapped in SCOPED_TRACE by
  // the caller so failures are attributable to v4 vs v6.
  template <typename AddrT>
  void verifyPortMirrorFamily(const std::string& baseMirrorName) {
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseMirrorName
        : v6MirrorName(baseMirrorName);
    this->verify<AddrT>(mirrorName);
    this->verifyPortMirrorProgrammed<AddrT>(mirrorName);
  }

  void testPortMirror(const std::string& baseMirrorName) {
    auto setup = [=, this]() { this->resolveMirrors(baseMirrorName); };
    auto verify = [=, this]() {
      {
        SCOPED_TRACE("v4");
        this->verifyPortMirrorFamily<folly::IPAddressV4>(baseMirrorName);
      }
      {
        SCOPED_TRACE("v6");
        this->verifyPortMirrorFamily<folly::IPAddressV6>(baseMirrorName);
      }
    };
    this->verifyAcrossWarmBoots(setup, verify);
  }

  // Re-adds a single family's mirror session at an updated mirror-to port +
  // dscp and re-resolves it. Used in setup() only.
  template <typename AddrT>
  void updatePortMirrorSession(
      cfg::SwitchConfig* cfg,
      const std::string& baseMirrorName) {
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseMirrorName
        : v6MirrorName(baseMirrorName);
    // Test that these port mirror attributes can be changed:
    // - PortID from which mirrored packets will be sent
    // - DSCP value
    // Note that DSCP may be ignored by SPAN tests with local mirroring.
    utility::addMirrorConfig<AddrT>(
        cfg,
        *getAgentEnsemble(),
        mirrorName,
        false /* truncate */,
        48 /* dscp */,
        mirrorToPortIndex<AddrT>(
            utility::kMirrorToPortIndex, /*isUpdate*/ true));
  }

  void testUpdatePortMirror(const std::string& baseMirrorName) {
    auto setup = [=, this]() {
      this->resolveMirrors(baseMirrorName);
      auto cfg = initialConfig(*getAgentEnsemble());
      cfg.mirrors()->clear();
      updatePortMirrorSession<folly::IPAddressV4>(&cfg, baseMirrorName);
      updatePortMirrorSession<folly::IPAddressV6>(&cfg, baseMirrorName);
      this->applyNewConfig(cfg);
      this->resolveMirrors(
          baseMirrorName, utility::kMirrorToPortIndex, /*isUpdate*/ true);
    };
    auto verify = [=, this]() {
      {
        SCOPED_TRACE("v4");
        this->verify<folly::IPAddressV4>(
            baseMirrorName, utility::kMirrorToPortIndex, /*isUpdate*/ true);
        this->verifyPortMirrorProgrammed<folly::IPAddressV4>(baseMirrorName);
      }
      {
        SCOPED_TRACE("v6");
        auto v6Name = v6MirrorName(baseMirrorName);
        this->verify<folly::IPAddressV6>(
            v6Name, utility::kMirrorToPortIndex, /*isUpdate*/ true);
        this->verifyPortMirrorProgrammed<folly::IPAddressV6>(v6Name);
      }
    };
    this->verifyAcrossWarmBoots(setup, verify);
  }

  template <typename AddrT>
  void verifyAclMirrorFamily(const std::string& baseMirrorName) {
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseMirrorName
        : v6MirrorName(baseMirrorName);
    auto aclName =
        std::is_same_v<AddrT, folly::IPAddressV4> ? kMirrorAcl : kMirrorAclV6;
    this->verify<AddrT>(mirrorName);
    this->verifyAclMirrorProgrammed(aclName, mirrorName);
  }

  void testAclMirror(const std::string& baseMirrorName) {
    auto setup = [=, this]() { this->resolveMirrors(baseMirrorName); };
    auto verify = [=, this]() {
      {
        SCOPED_TRACE("v4");
        this->verifyAclMirrorFamily<folly::IPAddressV4>(baseMirrorName);
      }
      {
        SCOPED_TRACE("v6");
        this->verifyAclMirrorFamily<folly::IPAddressV6>(baseMirrorName);
      }
    };
    this->verifyAcrossWarmBoots(setup, verify);
  }

  void testUpdateAclMirror(const std::string& baseMirrorName) {
    auto setup = [=, this]() {
      this->resolveMirrors(baseMirrorName);
      auto cfg = initialConfig(*getAgentEnsemble());
      cfg.mirrors()->clear();
      utility::addMirrorConfig<folly::IPAddressV4>(
          &cfg,
          *getAgentEnsemble(),
          baseMirrorName,
          false /* truncate */,
          48 /* dscp */,
          mirrorToPortIndex<folly::IPAddressV4>());
      utility::addMirrorConfig<folly::IPAddressV6>(
          &cfg,
          *getAgentEnsemble(),
          v6MirrorName(baseMirrorName),
          false /* truncate */,
          48 /* dscp */,
          mirrorToPortIndex<folly::IPAddressV6>());
      this->applyNewConfig(cfg);
    };
    auto verify = [=, this]() {
      {
        SCOPED_TRACE("v4");
        this->verifyAclMirrorFamily<folly::IPAddressV4>(baseMirrorName);
      }
      {
        SCOPED_TRACE("v6");
        this->verifyAclMirrorFamily<folly::IPAddressV6>(baseMirrorName);
      }
    };
    this->verifyAcrossWarmBoots(setup, verify);
  }

  void testRemoveMirror(const std::string& baseMirrorName) {
    auto setup = [=, this]() {
      const auto& ensemble = *getAgentEnsemble();
      this->resolveMirrors(baseMirrorName);

      // Re-apply a clean baseline with NO mirror config, removing both
      // families' mirrors.
      auto config = utility::onePortPerInterfaceConfig(
          ensemble.getSw(),
          ensemble.masterLogicalPortIds(),
          true /*interfaceHasSubnet*/);

      this->applyNewConfig(config);
    };
    auto verify = [=, this]() {
      {
        SCOPED_TRACE("v4");
        auto mirror =
            getProgrammedState()->getMirrors()->getNodeIf(baseMirrorName);
        EXPECT_EQ(mirror, nullptr);
      }
      {
        SCOPED_TRACE("v6");
        auto mirror = getProgrammedState()->getMirrors()->getNodeIf(
            v6MirrorName(baseMirrorName));
        EXPECT_EQ(mirror, nullptr);
      }
      // if mirror is expected to be null then we should not pass
      // it as an argument to scopeResolver->scope
    };
    this->verifyAcrossWarmBoots(setup, verify);
  }

  template <typename AddrT>
  void verifyTruncateFamily(const std::string& baseMirrorName) {
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? baseMirrorName
        : v6MirrorName(baseMirrorName);
    auto mirrorToPort = getMirrorToPort<AddrT>(*getAgentEnsemble());
    auto statsBefore = getLatestPortStats(mirrorToPort);
    this->verify<AddrT>(
        mirrorName,
        utility::kMirrorToPortIndex,
        /*isUpdate*/ false,
        8000 /*payloadSize*/);
    WITH_RETRIES({
      auto statsAfter = getLatestPortStats(mirrorToPort);

      auto outBytes = (*statsAfter.outBytes_() - *statsBefore.outBytes_());
      // TODO: on TH3 for v6 packets, 254 bytes are mirrored which is a single
      // MMU cell. but for v4 packets, 234 bytes are mirrored. need to
      // investigate this behavior.
      EXPECT_EVENTUALLY_LE(
          outBytes, 1500 /* payload is truncated to ethernet MTU of 1500 */);
    });
  }

  void testPortMirrorWithLargePacket(const std::string& baseMirrorName) {
    auto setup = [=, this]() { this->resolveMirrors(baseMirrorName); };
    auto verify = [=, this]() {
      {
        SCOPED_TRACE("v4");
        this->verifyTruncateFamily<folly::IPAddressV4>(baseMirrorName);
      }
      {
        SCOPED_TRACE("v6");
        this->verifyTruncateFamily<folly::IPAddressV6>(baseMirrorName);
      }
    };
    this->verifyAcrossWarmBoots(setup, verify);
  }

  virtual bool isIngress() const = 0;
  const RouterID kRid{0};
  const uint16_t srcL4Port_{1234};
  const uint16_t dstL4Port_{4321};
};

class AgentIngressPortSpanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    // SPAN has no tunnel IP; a single SPAN session serves both families, so no
    // v6-specific ERSPAN feature is required.
    return {ProductionFeature::INGRESS_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressSpan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressSpan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressSpan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressSpan);
    }
  }

  bool isIngress() const override {
    return true;
  }
};

class AgentIngressPortErspanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    // Merged test exercises both families; require v6 ERSPAN so the whole test
    // is skipped where v6 ERSPAN is unsupported.
    return {
        ProductionFeature::INGRESS_MIRRORING,
        ProductionFeature::ERSPANV6_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan);
    }
  }

  bool isIngress() const override {
    return true;
  }
};

class AgentIngressPortErspanMirroringTruncateTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::INGRESS_MIRRORING,
        ProductionFeature::MIRROR_PACKET_TRUNCATION,
        ProductionFeature::ERSPANV6_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan, true /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan, true /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan);
    }
  }

  bool isIngress() const override {
    return true;
  }
};

class AgentIngressAclSpanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::INGRESS_MIRRORING,
        ProductionFeature::INGRESS_ACL_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressSpan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressSpan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressSpan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressSpan);
    }
  }

  bool isIngress() const override {
    return true;
  }
};

class AgentIngressAclErspanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::INGRESS_MIRRORING,
        ProductionFeature::INGRESS_ACL_MIRRORING,
        ProductionFeature::ERSPANV6_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan);
    }
  }

  bool isIngress() const override {
    return true;
  }
};

class AgentEgressPortSpanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    // SPAN has no tunnel IP; a single SPAN session serves both families.
    return {ProductionFeature::EGRESS_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressSpan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressSpan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressSpan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressSpan);
    }
  }

  bool isIngress() const override {
    return false;
  }
};

class AgentEgressPortErspanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::EGRESS_MIRRORING,
        ProductionFeature::ERSPANV6_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressErspan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressErspan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressErspan, false /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressErspan);
    }
  }

  bool isIngress() const override {
    return false;
  }
};

class AgentEgressPortErspanMirroringTruncateTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::MIRROR_PACKET_TRUNCATION,
        ProductionFeature::EGRESS_MIRRORING,
        ProductionFeature::EGRESS_MIRROR_PACKET_TRUNCATION,
        ProductionFeature::ERSPANV6_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressErspan, true /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressErspan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressErspan, true /* truncate */);
      this->addPortMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressErspan);
    }
  }

  bool isIngress() const override {
    return false;
  }
};

class AgentEgressAclSpanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::EGRESS_MIRRORING,
        ProductionFeature::EGRESS_ACL_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressSpan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressSpan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressSpan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressSpan);
    }
  }

  bool isIngress() const override {
    return false;
  }
};

class AgentEgressAclErspanMirroringTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::EGRESS_MIRRORING,
        ProductionFeature::EGRESS_ACL_MIRRORING,
        ProductionFeature::ERSPANV6_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressErspan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kEgressErspan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressErspan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kEgressErspan);
    }
  }

  bool isIngress() const override {
    return false;
  }
};

TEST_F(AgentIngressPortSpanMirroringTest, SpanPortMirror) {
  this->testPortMirror(utility::kIngressSpan);
}

TEST_F(AgentIngressPortSpanMirroringTest, UpdateSpanPortMirror) {
  this->testUpdatePortMirror(utility::kIngressSpan);
}

TEST_F(AgentIngressAclSpanMirroringTest, RemoveSpanMirror) {
  this->testRemoveMirror(utility::kIngressSpan);
}

TEST_F(AgentIngressPortErspanMirroringTest, ErspanPortMirror) {
  this->testPortMirror(utility::kIngressErspan);
}

TEST_F(AgentIngressAclSpanMirroringTest, RemoveErspanMirror) {
  this->testRemoveMirror(utility::kIngressErspan);
}

TEST_F(AgentIngressPortErspanMirroringTest, UpdateErspanPortMirror) {
  this->testUpdatePortMirror(utility::kIngressErspan);
}

TEST_F(AgentIngressAclSpanMirroringTest, SpanAclMirror) {
  this->testAclMirror(utility::kIngressSpan);
}

TEST_F(AgentIngressAclSpanMirroringTest, UpdateSpanAclMirror) {
  this->testUpdateAclMirror(utility::kIngressSpan);
}

TEST_F(AgentIngressAclErspanMirroringTest, ErspanAclMirror) {
  this->testAclMirror(utility::kIngressErspan);
}

TEST_F(AgentIngressAclErspanMirroringTest, UpdateErspanAclMirror) {
  this->testUpdateAclMirror(utility::kIngressErspan);
}

TEST_F(AgentIngressPortErspanMirroringTruncateTest, TruncatePortErspanMirror) {
  this->testPortMirrorWithLargePacket(utility::kIngressErspan);
}

TEST_F(AgentEgressPortSpanMirroringTest, SpanPortMirror) {
  this->testPortMirror(utility::kEgressSpan);
}

TEST_F(AgentEgressPortErspanMirroringTest, ErspanPortMirror) {
  this->testPortMirror(utility::kEgressErspan);
}

TEST_F(AgentEgressAclSpanMirroringTest, SpanAclMirror) {
  this->testAclMirror(utility::kEgressSpan);
}

TEST_F(AgentEgressAclErspanMirroringTest, ErspanAclMirror) {
  this->testAclMirror(utility::kEgressErspan);
}

TEST_F(AgentEgressPortErspanMirroringTruncateTest, TruncatePortErspanMirror) {
  this->testPortMirrorWithLargePacket(utility::kEgressErspan);
}

class AgentErspanIngressSamplingTest
    : public AgentIngressPortErspanMirroringTruncateTest {
 public:
  using Base = AgentIngressPortErspanMirroringTruncateTest;
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    // This fixture is IPv6-only: the sample-packet feature allows only ONE
    // sample mirror globally, so we program a single v6 sample session. Require
    // the v6 sampling feature (and v6 ERSPAN inherited from the base) so the
    // test is skipped where v6 sampling/ERSPAN is unsupported.
    auto features = Base::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::ERSPANv6_SAMPLING);
    return features;
  }

  // The sampling config does not attach the mirror to a port directly (that is
  // done via configureSflowSampling), so addMirrorConfigForTestImpl only
  // programs the v6 mirror destination. This fixture is IPv6-only: a single
  // sample mirror is allowed globally, so we never program a v4 sample session.
  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      return;
    }
    this->addMirrorSession<folly::IPAddressV6>(
        cfg, ensemble, utility::kIngressErspan, false /* truncate */);
  }

  template <typename AddrT>
  void configureSamping(cfg::SwitchConfig* config, int sampleRate) {
    auto ensemble = this->getAgentEnsemble();
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? utility::kIngressErspan
        : v6MirrorName(utility::kIngressErspan);
    utility::configureSflowSampling(
        *config,
        mirrorName,
        {this->getTrafficPort<AddrT>(*ensemble)},
        sampleRate);
  }

  template <typename AddrT>
  void configureTrapAcl(cfg::SwitchConfig* config) {
    auto ensemble = this->getAgentEnsemble();
    auto asic = checkSameAndGetAsicForTesting(ensemble->getL3Asics());
    if (asic->isSupported(HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER)) {
      utility::configureTrapAcl(
          asic, *config, this->getMirrorToPort<AddrT>(*ensemble));
    } else {
      utility::configureTrapAcl(
          asic, *config, std::is_same_v<AddrT, folly::IPAddressV4>);
    }
  }

  bool isIngress() const override {
    return true;
  }

  // Verifies sampled-traffic mirroring for a single family. Wrapped in
  // SCOPED_TRACE by the caller.
  template <typename AddrT>
  void verifySamplingFamily(int sampleRate) {
    auto agentEnsemble = this->getAgentEnsemble();
    auto trafficPort = this->getTrafficPort<AddrT>(*agentEnsemble);
    auto mirrorToPort = this->getMirrorToPort<AddrT>(*agentEnsemble);
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? utility::kIngressErspan
        : v6MirrorName(utility::kIngressErspan);
    WITH_RETRIES({
      auto ingressMirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
      ASSERT_NE(ingressMirror, nullptr);
      EXPECT_EVENTUALLY_EQ(ingressMirror->isResolved(), true);
    });
    auto trafficPortPktStatsBefore = this->getLatestPortStats(trafficPort);
    auto mirrorPortPktStatsBefore = this->getLatestPortStats(mirrorToPort);

    auto trafficPortPktsBefore = *trafficPortPktStatsBefore.outUnicastPkts_();
    auto mirroredPortPktsBefore = *mirrorPortPktStatsBefore.outUnicastPkts_();

    auto totalPackets = 300 * sampleRate;
    this->sendPackets<AddrT>(totalPackets, 64);

    //  The probability that there are less than 200 packets with 300000 packets
    //  sampled at 1/1000. is 3*10^-10, and the probability that there are more
    //  than 400 packets is 1.6*10^-8.
    WITH_RETRIES({
      auto trafficPortPktStatsAfter = this->getLatestPortStats(trafficPort);
      auto trafficPortPktsAfter = *trafficPortPktStatsAfter.outUnicastPkts_();
      EXPECT_EVENTUALLY_EQ(
          totalPackets, trafficPortPktsAfter - trafficPortPktsBefore);
    });

    auto expectedMirrorPackets = 200; /* expect at least 200 packets */
    WITH_RETRIES({
      auto mirrorPortPktStatsAfter = this->getLatestPortStats(mirrorToPort);
      auto mirroredPortPktsAfter = *mirrorPortPktStatsAfter.outUnicastPkts_();
      EXPECT_EVENTUALLY_GE(
          mirroredPortPktsAfter - mirroredPortPktsBefore,
          expectedMirrorPackets);
    });
  }

  // Verifies the ERSPAN shim of a single sampled packet for a single family.
  // Wrapped in SCOPED_TRACE by the caller.
  template <typename AddrT>
  void verifySamplePacketFormatFamily() {
    auto agentEnsemble = this->getAgentEnsemble();
    auto mirrorToPort = this->getMirrorToPort<AddrT>(*agentEnsemble);
    auto trafficPort = this->getTrafficPort<AddrT>(*agentEnsemble);
    auto mirrorName = std::is_same_v<AddrT, folly::IPAddressV4>
        ? utility::kIngressErspan
        : v6MirrorName(utility::kIngressErspan);

    auto platformMapping =
        this->getAgentEnsemble()->getSw()->getPlatformMapping();
    auto platformPortEntry = platformMapping->getPlatformPort(trafficPort);
    auto chips = platformMapping->getChips();
    auto profileID = this->getAgentEnsemble()
                         ->getProgrammedState()
                         ->getPorts()
                         ->getNode(trafficPort)
                         ->getProfileID();
    // Below is CHENAB specific computation
    // ingress label port is calculated based on the lane as follows
    // ingress label port = ((module + 1) << 4 | split)
    // module is first lane / 8 and split is 0 or 1 depending on first lane
    auto lanes = utility::getHwPortLanes(
        platformPortEntry, profileID, chips, [](auto, auto lane) {
          return lane;
        });
    auto module = lanes[0] / 8;
    auto split = lanes[0] % 8;
    auto ingressLabelPort = ((module + 1) << 4 | split);

    WITH_RETRIES({
      auto ingressMirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
      ASSERT_NE(ingressMirror, nullptr);
      EXPECT_EVENTUALLY_EQ(ingressMirror->isResolved(), true);
    });

    WITH_RETRIES({
      utility::SwSwitchPacketSnooper snooper(
          this->getSw(), "snooper", mirrorToPort);
      snooper.ignoreUnclaimedRxPkts();
      this->sendPackets<AddrT>(1000, 1000);
      auto buf = snooper.waitForPacket(1);
      if (buf.has_value()) {
        // Intentionally dumping to develop deep packet inspection
        XLOG(INFO) << PktUtil::hexDump(buf.value().get());
        auto ensemble = this->getAgentEnsemble();
        auto asic = checkSameAndGetAsicForTesting(ensemble->getL3Asics());
        if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
          // Bytes before the GRE header: outer Ethernet (14) + outer IPv4 (20)
          // or IPv6 (40). Assumes untagged Ethernet and no IPv4 options, which
          // the mirror packets this test generates satisfy.
          constexpr uint32_t kOuterHeaderBytes =
              14 + (std::is_same_v<AddrT, folly::IPAddressV4> ? 20 : 40);
          folly::io::Cursor cursor(buf.value().get());
          cursor += kOuterHeaderBytes; // skip outer Ethernet + IP to GRE header
          auto gre = cursor.readBE<uint32_t>(); // read gre proto
          EXPECT_EQ(gre, 0x8949);
          auto port = cursor.readBE<uint16_t>(); // ingress label port
          EXPECT_EQ(port, ingressLabelPort);
          auto opcode = cursor.readBE<uint8_t>(); // opcode
          EXPECT_EQ(opcode, 0x1); // v1 erspan
          cursor.readBE<uint8_t>(); // padding
          cursor.readBE<uint8_t>(); // flags
          cursor.readBE<uint8_t>(); // packet type

          // Cross-validate the raw wire bytes against the sFlow collector's
          // ERSPAN parser: the datagram the ASIC emitted must decode exactly as
          // the collector decodes it off its raw GRE socket, which delivers the
          // packet starting at the GRE header. This ties
          // ChenabAsic::getGreProtocol() and the mirror-header layout to the
          // collector. parseChenabErspanSample() has an internal (nettools) and
          // an open-source implementation behind one header.
          folly::io::Cursor greCursor(buf.value().get());
          greCursor += kOuterHeaderBytes; // skip outer Ethernet + IP to GRE
          auto params = utility::getMirrorTestParams<AddrT>();
          auto sample = utility::parseChenabErspanSample(greCursor);
          EXPECT_EQ(
              sample.ingressLabelPort, static_cast<uint32_t>(ingressLabelPort));
          EXPECT_EQ(sample.srcAddr, folly::IPAddress(params.senderIp));
          EXPECT_EQ(sample.dstAddr, folly::IPAddress(params.receiverIp));
          EXPECT_EQ(sample.ipProtocol, 17); // UDP
        }
      }
      EXPECT_EVENTUALLY_TRUE(buf.has_value());
    });
  }
};

TEST_F(AgentErspanIngressSamplingTest, ErspanIngressSampling) {
  auto kSampleRate = 1000;
  // IPv6-only: the sample-packet feature allows only ONE sample mirror across
  // all ports, so we configure a single v6 sample session.
  auto setup = [=, this]() {
    auto config = this->initialConfig(*this->getAgentEnsemble());
    this->configureSamping<folly::IPAddressV6>(&config, kSampleRate);
    this->applyNewConfig(config);
    this->resolveMirror<folly::IPAddressV6>(
        v6MirrorName(utility::kIngressErspan));
  };
  auto verify = [=, this]() {
    SCOPED_TRACE("v6");
    this->verifySamplingFamily<folly::IPAddressV6>(kSampleRate);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentErspanIngressSamplingTest, SamplePacketFormat) {
  // IPv6-only: the sample-packet feature allows only ONE sample mirror across
  // all ports, so we configure a single v6 sample session + trap ACL. No config
  // is applied in verify() so this exercises warm-boot correctness (this is the
  // test that originally exposed the warm-boot bug).
  auto setup = [=, this]() {
    auto config = this->initialConfig(*this->getAgentEnsemble());
    this->configureSamping<folly::IPAddressV6>(&config, 1000);
    this->configureTrapAcl<folly::IPAddressV6>(&config);
    this->applyNewConfig(config);
    this->resolveMirror<folly::IPAddressV6>(
        v6MirrorName(utility::kIngressErspan));
  };
  auto verify = [=, this]() {
    SCOPED_TRACE("v6");
    this->verifySamplePacketFormatFamily<folly::IPAddressV6>();
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

/*
 * Test that warmboot succeeds when an ACL references a mirror that has become
 * unresolved (e.g., because the route to the mirror destination was withdrawn).
 *
 * When a mirror is unresolved, BcmMirror removes the mirror action from the
 * HW ACL entry. But the SwitchState ACL still references the mirror by name.
 * Without the fix in BcmFieldProcessorUtils::isActionStateSame(), this would
 * cause a CHECK failure during warmboot because the SW expected action count
 * (which incorrectly included the unresolved mirror) wouldn't match the HW
 * actual action count.
 */
class AgentAclMirrorWarmbootOnUnresolvedTest : public AgentMirroringTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::INGRESS_MIRRORING,
        ProductionFeature::INGRESS_ACL_MIRRORING,
        ProductionFeature::ERSPANV6_MIRRORING};
  }

  void addMirrorConfigForTestImpl(
      cfg::SwitchConfig* cfg,
      const AgentEnsemble& ensemble,
      bool isV4) const override {
    if (isV4) {
      this->addMirrorSession<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV4>(
          cfg, ensemble, utility::kIngressErspan);
    } else {
      this->addMirrorSession<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan, false /* truncate */);
      this->addAclMirrorConfig<folly::IPAddressV6>(
          cfg, ensemble, utility::kIngressErspan);
    }
  }

  bool isIngress() const override {
    return true;
  }

  // Resolves a family's mirror, then unresolves it by withdrawing the route to
  // its destination. Runs in setup() so the warmboot baseline has the mirror
  // unresolved-but-referenced-by-acl for this family.
  template <typename AddrT>
  void resolveThenUnresolve(const std::string& mirrorName) {
    // First resolve the mirror so it gets programmed in HW
    this->resolveMirror<AddrT>(mirrorName);

    // Verify mirror is resolved
    WITH_RETRIES({
      auto mirror =
          this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
      ASSERT_NE(mirror, nullptr);
      EXPECT_EVENTUALLY_TRUE(mirror->isResolved());
    });

    // Now unresolve the mirror by removing the route to its destination.
    // This causes BcmMirror to remove the mirror action from the HW ACL
    // entry, but the SwitchState ACL still references the mirror by name.
    auto mirror =
        this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
    ASSERT_NE(mirror, nullptr);
    auto dip = mirror->getDestinationIp();
    ASSERT_TRUE(dip.has_value());
    auto prefix = this->getMirrorRoutePrefix<AddrT>(dip.value());

    std::set<cfg::PortType> ecmpPortTypes;
    if (FLAGS_hyper_port) {
      ecmpPortTypes = {cfg::PortType::HYPER_PORT};
    } else {
      ecmpPortTypes = {cfg::PortType::INTERFACE_PORT};
    }
    utility::EcmpSetupAnyNPorts<AddrT> ecmpHelper(
        this->getProgrammedState(),
        this->getSw()->needL2EntryForNeighbor(),
        RouterID(0),
        ecmpPortTypes);
    auto wrapper = this->getSw()->getRouteUpdater();
    ecmpHelper.unprogramRoutes(&wrapper, {prefix});

    // Verify mirror is now unresolved
    WITH_RETRIES({
      auto m = this->getProgrammedState()->getMirrors()->getNodeIf(mirrorName);
      ASSERT_NE(m, nullptr);
      EXPECT_EVENTUALLY_FALSE(m->isResolved());
    });
  }
};

TEST_F(
    AgentAclMirrorWarmbootOnUnresolvedTest,
    WarmbootWithUnresolvedAclMirror) {
  auto setup = [=, this]() {
    this->resolveThenUnresolve<folly::IPAddressV4>(utility::kIngressErspan);
    this->resolveThenUnresolve<folly::IPAddressV6>(
        v6MirrorName(utility::kIngressErspan));
  };

  auto verify = [=, this]() {
    // If we get here without crashing, the fix works. The agent survived
    // warmboot with an unresolved mirror referenced by an ACL.
    {
      SCOPED_TRACE("v4");
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(
          utility::kIngressErspan);
      EXPECT_NE(mirror, nullptr);
      // Mirror should still be unresolved after warmboot
      EXPECT_FALSE(mirror->isResolved());
    }
    {
      SCOPED_TRACE("v6");
      auto mirror = this->getProgrammedState()->getMirrors()->getNodeIf(
          v6MirrorName(utility::kIngressErspan));
      EXPECT_NE(mirror, nullptr);
      EXPECT_FALSE(mirror->isResolved());
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
