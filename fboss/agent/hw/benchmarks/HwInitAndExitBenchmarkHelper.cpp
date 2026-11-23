/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/benchmarks/HwInitAndExitBenchmarkHelper.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/DsfStateUpdaterUtil.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/benchmarks/AgentBenchmarks.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestCoppUtils.h"
#include "fboss/agent/hw/test/HwTestProdConfigUtils.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/RouteDistributionGenerator.h"
#include "fboss/agent/test/RouteScaleGenerators.h"
#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/test/utils/FabricTestUtils.h"
#include "fboss/agent/test/utils/VoqTestUtils.h"

#include "fboss/lib/FunctionCallTimeReporter.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"

using namespace facebook::fboss;

namespace facebook::fboss::utility {

std::optional<uint16_t> getUplinksCount(
    PlatformType platformType,
    cfg::PortSpeed uplinkSpeed,
    cfg::PortSpeed downlinkSpeed) {
  using ConfigType = std::tuple<PlatformType, cfg::PortSpeed, cfg::PortSpeed>;
  static const std::map<ConfigType, uint16_t> numUplinksMap = {
      {{PlatformType::PLATFORM_WEDGE100,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::XG},
       4},
      {{PlatformType::PLATFORM_WEDGE100,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::TWENTYFIVEG},
       4},
      {{PlatformType::PLATFORM_WEDGE100,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::FIFTYG},
       4},
      {{PlatformType::PLATFORM_GALAXY_LC,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::FIFTYG},
       16},
      {{PlatformType::PLATFORM_GALAXY_FC,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::FIFTYG},
       16},
      {{PlatformType::PLATFORM_WEDGE400C,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::XG},
       4},
      {{PlatformType::PLATFORM_WEDGE400C,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::TWENTYFIVEG},
       4},
      {{PlatformType::PLATFORM_WEDGE400C,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::FIFTYG},
       4},
      {{PlatformType::PLATFORM_WEDGE400C,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::HUNDREDG},
       4},
      {{PlatformType::PLATFORM_WEDGE400,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::TWENTYFIVEG},
       4},
      {{PlatformType::PLATFORM_WEDGE400,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::FIFTYG},
       4},
      {{PlatformType::PLATFORM_WEDGE400,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::HUNDREDG},
       4},
      {{PlatformType::PLATFORM_YAMP,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::HUNDREDG},
       4},
      {{PlatformType::PLATFORM_MINIPACK,
        cfg::PortSpeed::HUNDREDG,
        cfg::PortSpeed::HUNDREDG},
       4},
      {{PlatformType::PLATFORM_MINIPACK3N,
        cfg::PortSpeed::FOURHUNDREDG,
        cfg::PortSpeed::FOURHUNDREDG},
       64}};

  auto iter = numUplinksMap.find(
      std::make_tuple(platformType, uplinkSpeed, downlinkSpeed));
  if (iter == numUplinksMap.end()) {
    return std::nullopt;
  }
  return iter->second;
}

utility::RouteDistributionGenerator::ThriftRouteChunks getRoutes(
    const AgentEnsemble* ensemble) {
  /*
   * |  Platform   |  Role  |
   * |  TOMAHAWK   |   FSW  |
   * |  TOMAHAWK3  |    UU  |
   * |  EBRO       |   RSW  |
   *
   * The benchmarks are categorized by chip and not by the route scale.
   * Pick the highest scale for that chip. For instance, any TH asic will
   * be categorized as th_atom_init_and_exit_100Gx100G which is applicable
   * for wedge100, wedge100S and Galaxy. Hence, use FSW route scale for TH.
   */
  auto* swSwitch = ensemble->getSw();
  // Before m-mpu agent test, use first Asic for initialization.
  auto switchIds = swSwitch->getHwAsicTable()->getSwitchIDs();
  CHECK_GE(switchIds.size(), 1);
  auto asicType =
      swSwitch->getHwAsicTable()->getHwAsic(*switchIds.cbegin())->getAsicType();

  if (asicType == cfg::AsicType::ASIC_TYPE_CHENAB ||
      asicType == cfg::AsicType::ASIC_TYPE_CHENAB2 ||
      asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK3 ||
      asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK4 ||
      asicType == cfg::AsicType::ASIC_TYPE_EBRO ||
      asicType == cfg::AsicType::ASIC_TYPE_P200 ||
      asicType == cfg::AsicType::ASIC_TYPE_YUBA ||
      asicType == cfg::AsicType::ASIC_TYPE_GARONNE ||
      asicType == cfg::AsicType::ASIC_TYPE_JERICHO2 ||
      asicType == cfg::AsicType::ASIC_TYPE_JERICHO3 ||
      asicType == cfg::AsicType::ASIC_TYPE_RAMON ||
      asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK5 ||
      asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK6 ||
      asicType == cfg::AsicType::ASIC_TYPE_G202X) {
    return utility::HgridUuRouteScaleGenerator(
               swSwitch->getState(), swSwitch->needL2EntryForNeighbor())
        .getThriftRoutes();
  } else if (asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK) {
    return utility::FSWRouteScaleGenerator(
               swSwitch->getState(), swSwitch->needL2EntryForNeighbor())
        .getThriftRoutes();
  } else {
    CHECK(false) << "Invalid asic type for route scale";
  }
}

void initAndExitBenchmarkHelper(
    cfg::PortSpeed uplinkSpeed,
    cfg::PortSpeed downlinkSpeed,
    cfg::SwitchType switchType) {
  folly::BenchmarkSuspender suspender;
  std::unique_ptr<AgentEnsemble> ensemble{};

  AgentEnsembleSwitchConfigFn npuInitialConfig =
      [uplinkSpeed, downlinkSpeed](const AgentEnsemble& ensemble) {
        auto ports = ensemble.masterLogicalPortIds();
        auto numUplinks = getUplinksCount(
            ensemble.getSw()->getPlatformType(), uplinkSpeed, downlinkSpeed);
        // Before m-mpu agent test, use first Asic for initialization.
        auto switchIds = ensemble.getSw()->getHwAsicTable()->getSwitchIDs();
        CHECK_GE(switchIds.size(), 1);
        auto asic =
            ensemble.getSw()->getHwAsicTable()->getHwAsic(*switchIds.cbegin());
        if (!numUplinks) {
          return utility::oneL3IntfNPortConfig(
              ensemble.getSw()->getPlatformMapping(),
              asic,
              ensemble.masterLogicalPortIds(),
              ensemble.getSw()->getPlatformSupportsAddRemovePort(),
              utility::kDefaultLoopbackMap(),
              true /*interfaceHasSubnet*/,
              utility::kBaseVlanId,
              true /*optimizePortProfile*/,
              true /*setInterfaceMac*/,
              ensemble.getSw()->getPlatformType());
        }
        /*
         * Based on the uplink/downlink speed, use the ConfigFactory to create
         * agent config to mimic the production config. For instance, in TH,
         * 100Gx10G as config type will create 100G uplinks and 10G downlinks
         */

        auto config = utility::createUplinkDownlinkConfig(
            ensemble.getSw()->getPlatformMapping(),
            asic,
            ensemble.getSw()->getPlatformType(),
            ensemble.getSw()->getPlatformSupportsAddRemovePort(),
            ensemble.masterLogicalInterfacePortIds(),
            numUplinks.value(),
            uplinkSpeed,
            downlinkSpeed,
            asic->desiredLoopbackModes());
        utility::addProdFeaturesToConfig(config, asic, ensemble.isSai());
        return config;
      };

  AgentEnsembleSwitchConfigFn voqInitialConfig =
      [](const AgentEnsemble& ensemble) {
        FLAGS_hide_fabric_ports = false;
        // Diable dsf subcribe for single-box test
        FLAGS_dsf_subscribe = false;
        auto config = utility::onePortPerInterfaceConfig(
            ensemble.getSw(),
            ensemble.masterLogicalPortIds(),
            true, /*interfaceHasSubnet*/
            true, /*setInterfaceMac*/
            utility::kBaseVlanId,
            true /*enable fabric ports*/);
        utility::populatePortExpectedNeighborsToSelf(
            ensemble.masterLogicalPortIds(), config);
        config.dsfNodes() = *utility::addRemoteIntfNodeCfg(*config.dsfNodes());
        return config;
      };

  AgentEnsembleSwitchConfigFn fabricInitialConfig =
      [](const AgentEnsemble& ensemble) {
        FLAGS_hide_fabric_ports = false;
        auto config = utility::onePortPerInterfaceConfig(
            ensemble.getSw(),
            ensemble.masterLogicalPortIds(),
            false /*interfaceHasSubnet*/,
            false /*setInterfaceMac*/,
            utility::kBaseVlanId,
            true /*enable fabric ports*/);
        utility::populatePortExpectedNeighborsToSelf(
            ensemble.masterLogicalPortIds(), config);
        return config;
      };

  suspender.dismiss();
  {
    /*
     * Measure the hw switch init time
     */
    /*
     * This is to measure the performance when the config is applied during
     * coldboot/warmboot. This measures the time agent took to transition
     * from INITIALIZED to CONFIGURED.
     * We reuse initToConfigBenchmarkHelper for both coldboot init and
     * warmboot setup. Enable benchmarking only for coldboot/warmbot init and
     * disable when setting up for warmboot
     */
    ScopedCallTimer timeIt;
    switch (switchType) {
      case cfg::SwitchType::VOQ: {
        // Program ECMP routes - 8x512 for single stage and 16x2K for dual
        // stage
        auto ecmpWidth = 512;
        auto ecmpGroup = 64;
        if (isDualStage3Q2QMode()) {
          ecmpWidth = 2048;
          ecmpGroup = 16;
        }
        FLAGS_ecmp_resource_percentage = 100;
        FLAGS_ecmp_width = ecmpWidth;

        ensemble = createAgentEnsemble(
            voqInitialConfig, false /*disableLinkStateToggler*/);
        if (ensemble->getSw()->getBootType() == BootType::COLD_BOOT) {
          auto updateDsfStateFn =
              [&ensemble](const std::shared_ptr<SwitchState>& in) {
                std::map<SwitchID, std::shared_ptr<SystemPortMap>>
                    switchId2SystemPorts;
                std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Rifs;
                utility::populateRemoteIntfAndSysPorts(
                    switchId2SystemPorts,
                    switchId2Rifs,
                    ensemble->getSw()->getConfig(),
                    ensemble->getSw()
                        ->getHwAsicTable()
                        ->isFeatureSupportedOnAllAsic(
                            HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE));
                return DsfStateUpdaterUtil::getUpdatedState(
                    in,
                    ensemble->getSw()->getScopeResolver(),
                    ensemble->getSw()->getRib(),
                    switchId2SystemPorts,
                    switchId2Rifs);
              };
          ensemble->getSw()->getRib()->updateStateInRibThread(
              [&ensemble, updateDsfStateFn]() {
                ensemble->getSw()->updateStateWithHwFailureProtection(
                    fmt::format("Update state for node: {}", 0),
                    updateDsfStateFn);
              });

          utility::EcmpSetupTargetedPorts6 ecmpHelper(
              ensemble->getProgrammedState(),
              ensemble->getSw()->needL2EntryForNeighbor());
          auto portDescriptor =
              utility::resolveRemoteNhops(ensemble.get(), ecmpHelper);

          std::vector<RoutePrefixV6> prefixes;
          std::vector<boost::container::flat_set<PortDescriptor>> nhopSets;
          CHECK_GE(portDescriptor.size(), ecmpWidth + ecmpGroup - 1);
          for (int i = 0; i < ecmpGroup; i++) {
            // For default route 0::0, need to use 0 as prefix length
            prefixes.emplace_back(
                folly::IPAddressV6(folly::to<std::string>(i, "::", i)),
                static_cast<uint8_t>(i == 0 ? 0 : 128));
            nhopSets.emplace_back(
                std::make_move_iterator(portDescriptor.begin() + i),
                std::make_move_iterator(
                    portDescriptor.begin() + i + ecmpWidth));
          }
          auto updater = ensemble->getSw()->getRouteUpdater();
          ecmpHelper.programRoutes(&updater, nhopSets, prefixes);
        }
        break;
      }
      case cfg::SwitchType::NPU:
        ensemble = createAgentEnsemble(
            npuInitialConfig, false /*disableLinkStateToggler*/);
        break;
      case cfg::SwitchType::FABRIC:
        ensemble = createAgentEnsemble(
            fabricInitialConfig, false /*disableLinkStateToggler*/);
        break;
      default:
        throw FbossError("Unsupported switch type");
    }
  }
  suspender.rehire();
  // Fabric switch does not support route programming
  // VOQ switch already programs ECMP routes above
  if (ensemble->getBootType() == BootType::COLD_BOOT &&
      switchType == cfg::SwitchType::NPU) {
    auto routeChunks = getRoutes(ensemble.get());
    auto updater = ensemble->getSw()->getRouteUpdater();
    ensemble->programRoutes(RouterID(0), ClientID::BGPD, routeChunks);
  }

  // Block until the stats thread exits. This can take ~0.7s, so pull it
  // outside the stopwatch.
  ensemble->stopStatsThread();
  if (FLAGS_setup_for_warmboot) {
    ScopedCallTimer timeIt;
    // Static such that the object destructor runs as late as possible. In
    // particular in this case, destructor (and thus the duration calculation)
    // will run at the time of program exit when static variable destructors
    // run
    static StopWatch timer("warm_boot_msecs", FLAGS_json);
    ensemble->gracefulExit();
    // Leak HwSwitchEnsemble for warmboot, so that
    // we don't run destructors and unprogram h/w. We are
    // going to exit the process anyways.
    __attribute__((unused)) auto leakedHwEnsemble = ensemble.release();
  }
}
} // namespace facebook::fboss::utility
