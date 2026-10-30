/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/platforms/wedge/WedgeManagerInit.h"

#include "fboss/agent/platforms/common/PlatformMappingUtils.h"
#include "fboss/lib/bsp/BspGenericSystemContainer.h"
#include "fboss/lib/bsp/icecube800bc/Icecube800bcBspPlatformMapping.h"
#include "fboss/lib/bsp/icetea800bc/Icetea800bcBspPlatformMapping.h"
#include "fboss/lib/bsp/janga800bic/Janga800bicBspPlatformMapping.h"
#include "fboss/lib/bsp/ladakh800bcls/Ladakh800bclsBspPlatformMapping.h"
#include "fboss/lib/bsp/leh800bcls/Leh800bclsBspPlatformMapping.h"
#include "fboss/lib/bsp/m4052actm/M4052ACTMBspPlatformMapping.h"
#include "fboss/lib/bsp/m4061clsc/M4061clscBspPlatformMapping.h"
#include "fboss/lib/bsp/m4061nhp/M4061nhpBspPlatformMapping.h"
#include "fboss/lib/bsp/m4062nhp/M4062nhpBspPlatformMapping.h"
#include "fboss/lib/bsp/m4340nhp/M4340nhpBspPlatformMapping.h"
#include "fboss/lib/bsp/m5120csc/M5120cscBspPlatformMapping.h"
#include "fboss/lib/bsp/meru800bfa/Meru800bfaBspPlatformMapping.h"
#include "fboss/lib/bsp/meru800bia/Meru800biaBspPlatformMapping.h"
#include "fboss/lib/bsp/minipack3bta/Minipack3BTABspPlatformMapping.h"
#include "fboss/lib/bsp/minipack3n/Minipack3NBspPlatformMapping.h"
#include "fboss/lib/bsp/montblanc/MontblancBspPlatformMapping.h"
#include "fboss/lib/bsp/morgan800cc/Morgan800ccBspPlatformMapping.h"
#include "fboss/lib/bsp/tahan800bc/Tahan800bcBspPlatformMapping.h"
#include "fboss/lib/bsp/tahansb800bc/Tahansb800bcBspPlatformMapping.h"
#include "fboss/lib/bsp/wedge800bact/Wedge800BACTBspPlatformMapping.h"
#include "fboss/lib/bsp/wedge800cact/Wedge800CACTBspPlatformMapping.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"
#include "fboss/qsfp_service/PortManager.h"
#include "fboss/qsfp_service/QsfpServiceThreads.h"
#include "fboss/qsfp_service/platforms/wedge/BspWedgeManager.h"
#include "fboss/qsfp_service/platforms/wedge/GalaxyManager.h"
#include "fboss/qsfp_service/platforms/wedge/Wedge100Manager.h"
#include "fboss/qsfp_service/platforms/wedge/Wedge400CManager.h"
#include "fboss/qsfp_service/platforms/wedge/Wedge400Manager.h"

#include "fboss/lib/CommonFileUtils.h"

namespace facebook::fboss {
namespace {

struct ManagerInitComponents {
  std::unique_ptr<PlatformProductInfo> productInfo;
  std::shared_ptr<const PlatformMapping> platformMapping;
  std::shared_ptr<QsfpServiceThreads> qsfpServiceThreads;
};

ManagerInitComponents initializeManagerComponents() {
  auto productInfo =
      std::make_unique<PlatformProductInfo>(FLAGS_fruid_filepath);
  productInfo->initialize();
  auto mode = productInfo->getType();

  // Only used for platform mapping overrides.
  std::string platformMappingStr;
  if (!FLAGS_platform_mapping_override_path.empty()) {
    if (!folly::readFile(
            FLAGS_platform_mapping_override_path.data(), platformMappingStr)) {
      throw FbossError("unable to read ", FLAGS_platform_mapping_override_path);
    }
    XLOG(INFO) << "Overriding platform mapping from "
               << FLAGS_platform_mapping_override_path;
  }

  std::shared_ptr<const PlatformMapping> platformMapping =
      utility::initPlatformMapping(mode);

  auto qsfpServiceThreads = createQsfpServiceThreads(platformMapping);

  return {std::move(productInfo), platformMapping, qsfpServiceThreads};
}

} // namespace

std::unique_ptr<WedgeManager> createWedgeManager(
    std::unique_ptr<PlatformProductInfo> productInfo,
    const std::shared_ptr<const PlatformMapping> platformMapping,
    std::shared_ptr<QsfpServiceThreads> qsfpServiceThreads) {
  auto mode = productInfo->getType();

  createDir(FLAGS_qsfp_service_volatile_dir);

  switch (mode) {
    case PlatformType::PLATFORM_WEDGE100:
      return std::make_unique<Wedge100Manager>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_GALAXY_LC:
    case PlatformType::PLATFORM_GALAXY_FC:
      return std::make_unique<GalaxyManager>(
          mode, platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_YAMP:
      return createYampWedgeManager(platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_DARWIN:
    case PlatformType::PLATFORM_DARWIN48V:
      return createDarwinWedgeManager(platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_ELBERT:
      return createElbertWedgeManager(platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_MERU800BIA:
    case PlatformType::PLATFORM_MERU800BIAB:
    case PlatformType::PLATFORM_MERU800BIAC:
      return createBspWedgeManager<
          Meru800biaBspPlatformMapping,
          PlatformType::PLATFORM_MERU800BIA>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_MERU800BFA:
    case PlatformType::PLATFORM_MERU800BFA_P1:
      return createBspWedgeManager<
          Meru800bfaBspPlatformMapping,
          PlatformType::PLATFORM_MERU800BFA>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_MONTBLANC:
      return createBspWedgeManager<
          MontblancBspPlatformMapping,
          PlatformType::PLATFORM_MONTBLANC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_ICECUBE800BC:
    case PlatformType::PLATFORM_ICECUBE800BANW:
      return createBspWedgeManager<
          Icecube800bcBspPlatformMapping,
          PlatformType::PLATFORM_ICECUBE800BC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_M4052ACTM:
      return createBspWedgeManager<
          M4052ACTMBspPlatformMapping,
          PlatformType::PLATFORM_M4052ACTM>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_M4061NHP:
      return createBspWedgeManager<
          M4061nhpBspPlatformMapping,
          PlatformType::PLATFORM_M4061NHP>(platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_M4062NHP:
      return createBspWedgeManager<
          M4062nhpBspPlatformMapping,
          PlatformType::PLATFORM_M4062NHP>(platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_M4340NHP:
      return createBspWedgeManager<
          M4340nhpBspPlatformMapping,
          PlatformType::PLATFORM_M4340NHP>(platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_M5120CSC:
      return createBspWedgeManager<
          M5120cscBspPlatformMapping,
          PlatformType::PLATFORM_M5120CSC>(platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_M4061CLSC:
      return createBspWedgeManager<
          M4061clscBspPlatformMapping,
          PlatformType::PLATFORM_M4061CLSC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_ICETEA800BC:
      return createBspWedgeManager<
          Icetea800bcBspPlatformMapping,
          PlatformType::PLATFORM_ICETEA800BC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_MINIPACK3BTA:
      return createBspWedgeManager<
          Minipack3BTABspPlatformMapping,
          PlatformType::PLATFORM_MINIPACK3BTA>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_MINIPACK3N:
      return createBspWedgeManager<
          Minipack3NBspPlatformMapping,
          PlatformType::PLATFORM_MINIPACK3N>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_MORGAN800CC:
      return createBspWedgeManager<
          Morgan800ccBspPlatformMapping,
          PlatformType::PLATFORM_MORGAN800CC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_WEDGE400C:
      return std::make_unique<Wedge400CManager>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_JANGA800BIC:
      return createBspWedgeManager<
          Janga800bicBspPlatformMapping,
          PlatformType::PLATFORM_JANGA800BIC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_TAHAN800BC:
      return createBspWedgeManager<
          Tahan800bcBspPlatformMapping,
          PlatformType::PLATFORM_TAHAN800BC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_FUJI:
    case PlatformType::PLATFORM_MINIPACK:
      return createFBWedgeManager(
          std::move(productInfo), platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_WEDGE400:
      return std::make_unique<Wedge400Manager>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_TAHANSB800BC:
      return createBspWedgeManager<
          Tahansb800bcBspPlatformMapping,
          PlatformType::PLATFORM_TAHANSB800BC>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_WEDGE800BACT:
    case PlatformType::PLATFORM_WEDGE800BNHP:
      return createBspWedgeManager<
          Wedge800BACTBspPlatformMapping,
          PlatformType::PLATFORM_WEDGE800BACT>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_WEDGE800CACT:
    case PlatformType::PLATFORM_WEDGE800CNHP:
      return createBspWedgeManager<
          Wedge800CACTBspPlatformMapping,
          PlatformType::PLATFORM_WEDGE800CACT>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_LADAKH800BCLS:
      return createBspWedgeManager<
          Ladakh800bclsBspPlatformMapping,
          PlatformType::PLATFORM_LADAKH800BCLS>(
          platformMapping, qsfpServiceThreads);
    case PlatformType::PLATFORM_LEH800BCLS:
      return createBspWedgeManager<
          Leh800bclsBspPlatformMapping,
          PlatformType::PLATFORM_LEH800BCLS>(
          platformMapping, qsfpServiceThreads);
    default:
      throw FbossError("Unsupported platform type");
  }
}

template <typename BspPlatformMapping, PlatformType platformType>
std::unique_ptr<WedgeManager> createBspWedgeManager(
    const std::shared_ptr<const PlatformMapping> platformMapping,
    std::shared_ptr<QsfpServiceThreads> qsfpServiceThreads) {
  auto systemContainer =
      BspGenericSystemContainer<BspPlatformMapping>::getInstance().get();
  return std::make_unique<BspWedgeManager>(
      systemContainer,
      std::make_unique<BspTransceiverApi>(systemContainer),
      platformMapping,
      platformType,
      qsfpServiceThreads);
}

std::pair<std::unique_ptr<WedgeManager>, std::unique_ptr<PortManager>>
createQsfpManagers() {
  auto [productInfo, platformMapping, qsfpServiceThreads] =
      initializeManagerComponents();

  const auto platformType = productInfo->getType();
  auto wedgeManager = createWedgeManager(
      std::move(productInfo), platformMapping, qsfpServiceThreads);
  auto phyManager =
      createPhyManager(platformType, platformMapping.get(), wedgeManager.get());

  std::unique_ptr<PortManager> portManager{nullptr};
  if (FLAGS_port_manager_mode) {
    portManager = createPortManager(
        platformType,
        wedgeManager.get(),
        std::move(phyManager),
        platformMapping,
        qsfpServiceThreads);
  } else {
    if (phyManager) {
      wedgeManager->setPhyManager(std::move(phyManager));
    }
  }

  return std::
      make_pair<std::unique_ptr<WedgeManager>, std::unique_ptr<PortManager>>(
          std::move(wedgeManager), std::move(portManager));
}

} // namespace facebook::fboss
