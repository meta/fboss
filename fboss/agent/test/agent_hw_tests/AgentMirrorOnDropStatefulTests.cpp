// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/packet/chenab/ChenabModHeader.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropTestBase.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PortTestUtils.h"

#include <fmt/format.h>
#include <folly/Range.h>

namespace facebook::fboss {

class AgentMirrorOnDropStatefulTest : public AgentMirrorOnDropTestBase {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::MIRROR_ON_DROP_STATEFUL};
  }

 protected:
  struct CapturedModPacket {
    ChenabModOuterHeader outer;
    ChenabModPacket packet;
  };

  static constexpr auto kSnooperPacketType =
      utility::packetSnooperReceivePacketType::PACKET_TYPE_CHENAB_MOD;
  static constexpr size_t kTruncateSize = 128;
  static constexpr uint8_t kSharedBufferDropMirrorReason = 8;
  static constexpr uint8_t kPipelineDropMirrorReason = 0;
  static constexpr uint16_t kDefaultRouteDropTrapId = 0x130;
  static constexpr uint16_t kAclDropTrapId = 0x1eb;

  cfg::MirrorOnDropReport makeReport(const std::string& name, int truncateSize)
      const {
    cfg::MirrorOnDropReport report;
    report.name() = name;
    report.localSrcPort() = kMirrorSrcPort;
    report.collectorIp() = kCollectorIp_.str();
    report.collectorPort() = kMirrorDstPort;
    report.mtu() = 1500;
    report.truncateSize() = truncateSize;
    report.dscp() = 0;
    report.mirrorPort().ensure().tunnel().ensure().srcIp() = kSwitchIp_.str();
    return report;
  }

  void setupMirrorOnDrop(const std::string& name, int truncateSize) {
    const auto injectionPort = masterLogicalInterfacePortIds()[0];
    const auto collectorPort = masterLogicalInterfacePortIds()[1];
    auto config = getAgentEnsemble()->getCurrentConfig();
    addDropPacketAcl(&config, injectionPort);
    applyMirrorOnDropConfig(
        config, makeReport(name, truncateSize), collectorPort);
  }

  std::optional<ChenabModPacket> waitForModPacket(
      utility::SwSwitchPacketSnooper& snooper,
      uint32_t timeoutSeconds = 1) {
    auto captured = waitForCapturedModPacket(snooper, timeoutSeconds);
    if (!captured) {
      return std::nullopt;
    }
    return std::move(captured->packet);
  }

  std::optional<CapturedModPacket> waitForCapturedModPacket(
      utility::SwSwitchPacketSnooper& snooper,
      uint32_t timeoutSeconds = 1) {
    auto packet = snooper.waitForPacket(timeoutSeconds);
    if (!packet) {
      return std::nullopt;
    }
    // The snooper parsed this same frame before adding it to its queue.
    folly::io::Cursor cursor{packet->get()};
    ChenabModOuterHeader outer;
    auto modPacket = parseChenabModPacket(cursor, outer);
    return CapturedModPacket{std::move(outer), std::move(modPacket)};
  }

  std::optional<ChenabTacNewEntry> waitForNewEntry(
      utility::SwSwitchPacketSnooper& snooper,
      const std::vector<uint8_t>& expectedPacket) {
    ChenabModOuterHeader outer;
    return waitForNewEntry(snooper, expectedPacket, outer);
  }

  std::optional<ChenabTacNewEntry> waitForNewEntry(
      utility::SwSwitchPacketSnooper& snooper,
      const std::vector<uint8_t>& expectedPacket,
      ChenabModOuterHeader& outer) {
    std::optional<ChenabTacNewEntry> result;
    WITH_RETRIES_N_TIMED(10, std::chrono::seconds(1), {
      auto captured = waitForCapturedModPacket(snooper);
      if (captured.has_value()) {
        if (auto* entry = std::get_if<ChenabTacNewEntry>(&captured->packet);
            entry && matchesTruncatedPacket(*entry, expectedPacket)) {
          result = *entry;
          outer = captured->outer;
        }
      }
      EXPECT_EVENTUALLY_TRUE(result.has_value());
    });
    return result;
  }

  std::optional<ChenabTacNewEntry> waitForNewEntry(
      utility::SwSwitchPacketSnooper& snooper,
      const folly::IPAddressV6& destination) {
    std::optional<ChenabTacNewEntry> result;
    WITH_RETRIES_N_TIMED(10, std::chrono::seconds(1), {
      auto captured = waitForCapturedModPacket(snooper);
      if (captured) {
        if (const auto* entry =
                std::get_if<ChenabTacNewEntry>(&captured->packet)) {
          auto sample = folly::IOBuf::wrapBuffer(
              entry->truncatedPacket.data(), entry->truncatedPacket.size());
          folly::io::Cursor cursor{sample.get()};
          const EthHdr ethHeader{cursor};
          if (ethHeader.getEtherType() ==
              static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6)) {
            const IPv6Hdr ipHeader{cursor};
            if (ipHeader.nextHeader ==
                static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP)) {
              UDPHeader udpHeader;
              udpHeader.parse(&cursor);
              if (ipHeader.srcAddr == kPacketSrcIp_ &&
                  ipHeader.dstAddr == destination &&
                  udpHeader.srcPort == kPacketSrcPort &&
                  udpHeader.dstPort == kPacketDstPort) {
                result = *entry;
              }
            }
          }
        }
      }
      EXPECT_EVENTUALLY_TRUE(result.has_value());
    });
    return result;
  }

  std::vector<uint8_t> expectedTruncatedPacket(const TxPacket& packet) const {
    folly::io::Cursor cursor{packet.buf()};
    utility::EthFrame frame{cursor};
    frame.stripVlans();
    auto untagged = frame.toIOBuf();
    folly::io::Cursor untaggedCursor{untagged.get()};
    std::vector<uint8_t> expected(kTruncateSize);
    untaggedCursor.pull(expected.data(), expected.size());
    return expected;
  }

  bool matchesTruncatedPacket(
      const ChenabTacNewEntry& entry,
      const std::vector<uint8_t>& expected) const {
    return entry.truncatedPacket == expected;
  }

  uint16_t getExpectedModPacketIngressPortId(PortID injectionPort) {
    std::optional<uint32_t> hwLogicalPortId;
    WITH_RETRIES({
      hwLogicalPortId = getSw()->getHwLogicalPortId(injectionPort);
      EXPECT_EVENTUALLY_TRUE(hwLogicalPortId.has_value());
    });
    if (!hwLogicalPortId) {
      throw FbossError(
          "Missing hardware logical port ID for port ", injectionPort);
    }
    // TAC reports its low ten bits, the SDK local port ID.
    return static_cast<uint16_t>(*hwLogicalPortId & 0x3ff);
  }

  folly::IPAddressV6 flowDestination(size_t offset) const {
    CHECK_LT(offset, 0xffff);
    return folly::IPAddressV6{
        fmt::format("2401:3333:3333:3333:3333:3333:3333:{:x}", offset + 1)};
  }

  static constexpr size_t kLifecycleFlowCount = 4;
  struct FlowReportingState {
    std::array<std::vector<uint8_t>, kLifecycleFlowCount> expectedPackets;
    std::array<std::optional<ChenabTacNewEntry>, kLifecycleFlowCount>
        firstReports;
    std::array<size_t, kLifecycleFlowCount> firstReportIpPayloadLengths{};
    std::array<bool, kLifecycleFlowCount> evicted{};
  };

  void sendDistinctFlowsAndDuplicates(
      size_t payloadSize,
      FlowReportingState& flows) {
    constexpr int kDuplicatePacketCount = 32;
    const auto injectionPort = masterLogicalInterfacePortIds()[0];
    std::array<std::unique_ptr<TxPacket>, kLifecycleFlowCount> firstPackets;
    for (size_t i = 0; i < kLifecycleFlowCount; ++i) {
      firstPackets[i] =
          sendPackets(1, injectionPort, flowDestination(i), 0, payloadSize);
      flows.expectedPackets[i] = expectedTruncatedPacket(*firstPackets[i]);
    }
    // No receive wait between the first packets and their duplicate bursts.
    for (size_t i = 0; i < kLifecycleFlowCount; ++i) {
      auto duplicate = sendPackets(
          kDuplicatePacketCount,
          injectionPort,
          flowDestination(i),
          0,
          payloadSize);
      ASSERT_EQ(
          PktUtil::hexDump(firstPackets[i]->buf()),
          PktUtil::hexDump(duplicate->buf()))
          << "Flow " << i;
    }
  }

  void collectFlowReports(
      utility::SwSwitchPacketSnooper& snooper,
      FlowReportingState& flows,
      uint16_t ingressLocalPort) {
    while (snooper.receivedPacket()) {
      const auto captured = waitForCapturedModPacket(snooper);
      if (!captured) {
        continue;
      }
      if (const auto* entry =
              std::get_if<ChenabTacNewEntry>(&captured->packet)) {
        // A new-entry report contains the first dropped packet of a flow.
        // Match its sample against the distinct flows we injected.
        const auto match = std::find_if(
            flows.expectedPackets.begin(),
            flows.expectedPackets.end(),
            [&](const auto& expected) {
              return matchesTruncatedPacket(*entry, expected);
            });
        if (match != flows.expectedPackets.end()) {
          // Stateful MOD must report this flow only once before eviction,
          // even though we sent byte-identical duplicates.
          const auto flow = std::distance(flows.expectedPackets.begin(), match);
          EXPECT_FALSE(flows.firstReports[flow].has_value())
              << "Duplicate new-entry report for flow " << flow;
          verifyNewEntryReport(*entry, captured->outer, ingressLocalPort);
          if (!flows.firstReports[flow]) {
            flows.firstReports[flow] = *entry;
            flows.firstReportIpPayloadLengths[flow] =
                captured->outer.ipPayloadLength;
          }
        }
      } else if (
          const auto* aggregate =
              std::get_if<ChenabTacAggregate>(&captured->packet)) {
        // An aggregate follows its new-entry report. Match their UID and
        // signature; the eviction bit confirms that the flow was flushed.
        for (size_t i = 0; i < kLifecycleFlowCount; ++i) {
          if (flows.firstReports[i] &&
              aggregate->metadata.uid == flows.firstReports[i]->metadata.uid &&
              aggregate->metadata.signatureHash ==
                  flows.firstReports[i]->metadata.signatureHash) {
            verifyAggregateReport(
                *aggregate,
                captured->outer,
                *flows.firstReports[i],
                flows.firstReportIpPayloadLengths[i]);
            flows.evicted[i] = flows.evicted[i] || aggregate->evicted;
          }
        }
      }
    }
  }

  void verifyFirstReports(
      utility::SwSwitchPacketSnooper& snooper,
      FlowReportingState& flows) {
    const auto ingressLocalPort =
        getExpectedModPacketIngressPortId(masterLogicalInterfacePortIds()[0]);

    // Require a first report for every injected flow, retaining any eviction
    // reports received while those first reports are being collected.
    WITH_RETRIES_N_TIMED(10, std::chrono::seconds(1), {
      collectFlowReports(snooper, flows, ingressLocalPort);
      EXPECT_EVENTUALLY_TRUE(
          std::all_of(
              flows.firstReports.begin(),
              flows.firstReports.end(),
              [](const auto& report) { return report.has_value(); }));
    });
    ASSERT_TRUE(
        std::all_of(
            flows.firstReports.begin(),
            flows.firstReports.end(),
            [](const auto& report) { return report.has_value(); }));
  }

  void verifyFlowEvictions(
      utility::SwSwitchPacketSnooper& snooper,
      FlowReportingState& flows) {
    const auto ingressLocalPort =
        getExpectedModPacketIngressPortId(masterLogicalInterfacePortIds()[0]);

    // Wait for every flow's eviction before testing that it can report again.
    WITH_RETRIES_N_TIMED(20, std::chrono::seconds(1), {
      collectFlowReports(snooper, flows, ingressLocalPort);
      EXPECT_EVENTUALLY_TRUE(
          std::all_of(
              flows.evicted.begin(), flows.evicted.end(), [](bool value) {
                return value;
              }));
    });
    ASSERT_TRUE(
        std::all_of(flows.evicted.begin(), flows.evicted.end(), [](bool value) {
          return value;
        }));
  }

  void verifyReReportMetadata(
      const FlowReportingState& flows,
      const FlowReportingState& reReportedFlows) {
    for (size_t i = 0; i < kLifecycleFlowCount; ++i) {
      ASSERT_TRUE(flows.firstReports[i].has_value());
      ASSERT_TRUE(reReportedFlows.firstReports[i].has_value());
      EXPECT_NE(flows.expectedPackets[i], reReportedFlows.expectedPackets[i]);
      EXPECT_EQ(
          reReportedFlows.firstReports[i]->metadata.signatureHash,
          flows.firstReports[i]->metadata.signatureHash);
      // TAC timestamps are big-endian; array ordering is chronological.
      EXPECT_LT(
          flows.firstReports[i]->metadata.timestamp,
          reReportedFlows.firstReports[i]->metadata.timestamp);
    }
  }

  void verifyModEncapsulation(const ChenabModOuterHeader& outer) {
    EXPECT_EQ(
        outer.srcMac,
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()));
    EXPECT_EQ(outer.dstMac, kCollectorNextHopMac_);
    EXPECT_EQ(outer.srcIp, kSwitchIp_);
    EXPECT_EQ(outer.dstIp, kCollectorIp_);
    EXPECT_EQ(outer.dscp, 0);
    EXPECT_EQ(outer.ttl, 64);
  }

  void verifyNewEntryReport(
      const ChenabTacNewEntry& entry,
      const ChenabModOuterHeader& outer,
      uint16_t ingressLocalPort) {
    verifyModEncapsulation(outer);
    EXPECT_GT(entry.packetLength, kTruncateSize);
    EXPECT_EQ(entry.truncatedPacket.size(), kTruncateSize);
    EXPECT_EQ(entry.ingressLocalPort, ingressLocalPort);
    EXPECT_EQ(entry.metadata.mirrorReason, kPipelineDropMirrorReason);
    EXPECT_EQ(entry.metadata.trapId, kAclDropTrapId);
  }

  void verifyAggregateReport(
      const ChenabTacAggregate& aggregate,
      const ChenabModOuterHeader& outer,
      const ChenabTacNewEntry& firstReport,
      size_t firstReportIpPayloadLength) {
    verifyModEncapsulation(outer);
    EXPECT_GT(aggregate.packetCount, 0);
    EXPECT_EQ(
        aggregate.byteCount,
        static_cast<uint64_t>(aggregate.packetCount) *
            firstReport.packetLength);
    // Aggregates have no sample: their 12-byte counter body replaces the
    // new-entry body's eight fixed bytes and truncated sample.
    constexpr size_t kAggregateBodyBytes = 12;
    constexpr size_t kNewEntryBodyBytes = 8;
    EXPECT_EQ(
        outer.ipPayloadLength,
        firstReportIpPayloadLength - firstReport.truncatedPacket.size() -
            kNewEntryBodyBytes + kAggregateBodyBytes);
  }
  void verifyModExport(
      bool expected,
      size_t flowIndex,
      const folly::MacAddress& collectorMac = kCollectorNextHopMac_) {
    const auto injectionPort = masterLogicalInterfacePortIds()[0];
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        kSnooperPacketType);
    snooper.ignoreUnclaimedRxPkts();
    auto packet = sendPackets(1, injectionPort, flowDestination(flowIndex));
    const auto sample = expectedTruncatedPacket(*packet);
    if (expected) {
      ChenabModOuterHeader outer;
      ASSERT_TRUE(waitForNewEntry(snooper, sample, outer).has_value());
      EXPECT_EQ(outer.dstMac, collectorMac);
      EXPECT_EQ(outer.dstIp, kCollectorIp_);
      return;
    }
    // Match only this fresh flow; queued aggregates and older samples do not
    // indicate that the removed collector is still exporting new drops.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      if (const auto captured = waitForCapturedModPacket(snooper)) {
        if (const auto* entry =
                std::get_if<ChenabTacNewEntry>(&captured->packet)) {
          EXPECT_FALSE(matchesTruncatedPacket(*entry, sample));
        }
      }
    }
  }

  void verifyPipelineDrop(uint16_t expectedTrapId) {
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        kSnooperPacketType);
    snooper.ignoreUnclaimedRxPkts();
    const auto injectionPort = masterLogicalInterfacePortIds()[0];
    auto packet = sendPackets(1, injectionPort, flowDestination(0));
    auto entry = waitForNewEntry(snooper, expectedTruncatedPacket(*packet));
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->metadata.mirrorReason, kPipelineDropMirrorReason);
    EXPECT_EQ(entry->metadata.trapId, expectedTrapId);
    resetMirrorOnDropFlows();
  }
};

TEST_F(AgentMirrorOnDropStatefulTest, DefaultRouteDrop) {
  const auto collectorPort = masterLogicalInterfacePortIds()[1];
  auto setup = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    applyMirrorOnDropConfig(
        config, makeReport("default-route-drop", kTruncateSize), collectorPort);
  };
  auto verify = [&]() { verifyPipelineDrop(kDefaultRouteDropTrapId); };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMirrorOnDropStatefulTest, MmuDrop) {
  constexpr int kPriority = 2;
  const auto injectionPort = masterLogicalInterfacePortIds()[0];
  const auto collectorPort = masterLogicalInterfacePortIds()[1];
  const auto txOffPort = masterLogicalInterfacePortIds()[2];
  auto setup = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();
    configureMmuDropBuffers(config, injectionPort, kPriority);
    applyMirrorOnDropConfig(
        config, makeReport("mmu-drop", kTruncateSize), collectorPort);
    const auto nextHopMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    setupEcmpTraffic(txOffPort, flowDestination(0), nextHopMac);
    waitForStateUpdates(getSw());
  };
  auto verifyDrop = [&]() {
    utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), txOffPort, false);
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        kSnooperPacketType);
    snooper.ignoreUnclaimedRxPkts();
    const auto destination = flowDestination(0);
    sendPackets(10000, injectionPort, destination, kPriority);
    auto entry = waitForNewEntry(snooper, destination);
    utility::setCreditWatchdogAndPortTx(getAgentEnsemble(), txOffPort, true);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->metadata.mirrorReason, kSharedBufferDropMirrorReason);
  };
  auto verify = [&]() {
    verifyDrop();
    resetMirrorOnDropFlows();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMirrorOnDropStatefulTest, FlowReportingLifecycle) {
  auto setup = [&]() {
    setupMirrorOnDrop("flow-reporting-lifecycle", kTruncateSize);
  };
  auto verifyLifecycle = [&](size_t payloadSize) {
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        kSnooperPacketType);
    snooper.ignoreUnclaimedRxPkts();

    FlowReportingState flows;
    // Each distinct dropped flow must report once, despite duplicate packets.
    sendDistinctFlowsAndDuplicates(payloadSize, flows);
    verifyFirstReports(snooper, flows);
    // Aggregate eviction reports confirm that the flows can report again.
    verifyFlowEvictions(snooper, flows);

    // Payload length is not part of the flow key; changed samples distinguish
    // fresh reports from queued reports without changing the flow signature.
    FlowReportingState reReportedFlows;
    sendDistinctFlowsAndDuplicates(payloadSize + 1, reReportedFlows);
    verifyFirstReports(snooper, reReportedFlows);
    // Re-reports retain the flow signature and carry newer timestamps.
    verifyReReportMetadata(flows, reReportedFlows);
  };
  auto verify = [&]() {
    constexpr size_t kPayloadSize = 512;
    // Exercise the restored graph before resetting any flows on warm boot.
    verifyLifecycle(kPayloadSize);
    resetMirrorOnDropFlows();
    // Keep the flow tuples unchanged, but distinguish samples from queued
    // reports emitted before the reset.
    verifyLifecycle(kPayloadSize + 2);
    resetMirrorOnDropFlows();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMirrorOnDropStatefulTest, CollectorLifecycle) {
  const auto collectorPort = masterLogicalInterfacePortIds()[1];
  const auto alternateCollectorPort = masterLogicalInterfacePortIds()[2];
  const folly::MacAddress updatedCollectorMac{"02:44:44:44:44:44"};
  constexpr auto kReportName = "collector-lifecycle";
  auto setup = [&]() { setupMirrorOnDrop(kReportName, kTruncateSize); };
  auto verify = [&]() {
    size_t flowIndex{0};
    auto verifyCollectorResolution = [&](bool resolved) {
      WITH_RETRIES({
        auto report = getProgrammedState()->getMirrorOnDropReports()->getNodeIf(
            kReportName);
        ASSERT_NE(report, nullptr);
        EXPECT_EVENTUALLY_TRUE(report->isResolved() == resolved);
      });
    };
    auto verifyCollectorUnavailable = [&]() {
      // Collector resolution loss must suppress reports for fresh flows.
      verifyCollectorResolution(false);
      verifyModExport(false, flowIndex++);
    };
    auto restoreCollector = [&](PortID port, const folly::MacAddress& mac) {
      setupEcmpTraffic(port, kCollectorIp_, mac);
      WITH_RETRIES({
        auto report = getProgrammedState()->getMirrorOnDropReports()->getNodeIf(
            kReportName);
        ASSERT_NE(report, nullptr);
        EXPECT_EVENTUALLY_TRUE(report->isResolved());
        EXPECT_EVENTUALLY_TRUE(report->getResolvedCollectorMac() == mac);
        EXPECT_EVENTUALLY_TRUE(
            report->getResolvedEgressPort() == PortDescriptor(port).toThrift());
      });
      // Fresh reports must reach the collector with its updated destination
      // MAC.
      verifyModExport(true, flowIndex++, mac);
    };

    // Establish that a resolved collector receives a fresh dropped flow.
    verifyCollectorResolution(true);
    verifyModExport(true, flowIndex++);

    // Route loss stops exports; restoring the route resumes reporting.
    unprogramEcmpTraffic(kCollectorIp_);
    verifyCollectorUnavailable();
    restoreCollector(collectorPort, kCollectorNextHopMac_);

    // An unresolved adjacency also stops exports until it is restored.
    unresolveEcmpTraffic(collectorPort);
    verifyCollectorUnavailable();
    restoreCollector(collectorPort, kCollectorNextHopMac_);

    // Change a live adjacency's MAC, then move the collector route between
    // existing ports; exports must follow the newly resolved destination.
    restoreCollector(collectorPort, updatedCollectorMac);
    restoreCollector(alternateCollectorPort, updatedCollectorMac);
    restoreCollector(collectorPort, kCollectorNextHopMac_);

    // Removing MOD stops reporting without removing the collector port.
    auto config = getAgentEnsemble()->getCurrentConfig();
    auto reports = *config.mirrorOnDropReports();
    config.mirrorOnDropReports()->clear();
    applyNewConfig(config);
    waitForStateUpdates(getSw());
    EXPECT_EQ(getProgrammedState()->getMirrorOnDropReports()->numNodes(), 0);
    verifyModExport(false, flowIndex++);
    // Restoring MOD re-enables exports to the still-resolved collector.
    *config.mirrorOnDropReports() = std::move(reports);
    applyNewConfig(config);
    verifyCollectorResolution(true);
    verifyModExport(true, flowIndex++);

    // TODO(01303391): Test an unresolved collector across warm boot once
    // Chenab supports restoring telemetry after MOD teardown.
    resetMirrorOnDropFlows();
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
