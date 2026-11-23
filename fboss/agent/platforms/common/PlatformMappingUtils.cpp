/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/platforms/common/PlatformMappingUtils.h"
#include "fboss/agent/platforms/common/blackwolf800banw/Blackwolf800banwPlatformMapping.h"
#include "fboss/agent/platforms/common/darwin/DarwinPlatformMapping.h"
#include "fboss/agent/platforms/common/elbert/ElbertPlatformMapping.h"
#include "fboss/agent/platforms/common/fake_test/FakeTestPlatformMapping.h"
#include "fboss/agent/platforms/common/fuji/FujiPlatformMapping.h"
#include "fboss/agent/platforms/common/galaxy/GalaxyFCPlatformMapping.h"
#include "fboss/agent/platforms/common/galaxy/GalaxyLCPlatformMapping.h"
#include "fboss/agent/platforms/common/icecube800bc/Icecube800bcPlatformMapping.h"
#include "fboss/agent/platforms/common/icetea800bc/Icetea800bcPlatformMapping.h"
#include "fboss/agent/platforms/common/janga800bic/Janga800bicPlatformMapping.h"
#include "fboss/agent/platforms/common/ladakh800bcls/Ladakh800bclsPlatformMapping.h"
#include "fboss/agent/platforms/common/leh800bcls/Leh800bclsPlatformMapping.h"
#include "fboss/agent/platforms/common/meru800bfa/Meru800bfaP1PlatformMapping.h"
#include "fboss/agent/platforms/common/meru800bfa/Meru800bfaPlatformMapping.h"
#include "fboss/agent/platforms/common/meru800bia/Meru800biaPlatformMapping.h"
#include "fboss/agent/platforms/common/minipack/MinipackPlatformMapping.h"
#include "fboss/agent/platforms/common/minipack3bta/Minipack3BTAPlatformMapping.h"
#include "fboss/agent/platforms/common/minipack3n/Minipack3NPlatformMapping.h"
#include "fboss/agent/platforms/common/montblanc/MontblancPlatformMapping.h"
#include "fboss/agent/platforms/common/morgan800cc/Morgan800ccPlatformMapping.h"
#include "fboss/agent/platforms/common/tahan800bc/Tahan800bcPlatformMapping.h"
#include "fboss/agent/platforms/common/tahansb800bc/Tahansb800bcPlatformMapping.h"
#include "fboss/agent/platforms/common/wedge100/Wedge100PlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400/Wedge400GrandTetonPlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400/Wedge400PlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400/Wedge400PlatformUtil.h"
#include "fboss/agent/platforms/common/wedge400c/Wedge400CGrandTetonPlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400c/Wedge400CPlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400c/Wedge400CPlatformUtil.h"
#include "fboss/agent/platforms/common/yamp/YampPlatformMapping.h"
#include "fboss/agent/platforms/common/yangra/YangraPlatformMapping.h"
#include "fboss/agent/platforms/common/yangra2/Yangra2PlatformMapping.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"

namespace {
std::vector<int> getFakeSaiControllingPortIDs() {
  std::vector<int> controllingPorts;
  for (int i = 0; i < 128; i += 4) {
    controllingPorts.push_back(i);
  }
  return controllingPorts;
}
} // namespace

namespace facebook::fboss::utility {

std::unique_ptr<PlatformMapping> initPlatformMapping(
    PlatformType type,
    const cfg::PlatformConfig& platformConfig) {
  if (!FLAGS_use_raw_platform_mapping) {
    return initPlatformMapping(type);
  }
  return std::make_unique<PlatformMapping>(
      PlatformDescriptorRegistry::get().loadPlatformMappingFromRaw(
          type, platformConfig));
}

std::unique_ptr<PlatformMapping> initPlatformMapping(PlatformType type) {
  std::string platformMappingStr;
  if (!FLAGS_platform_mapping_override_path.empty()) {
    if (!folly::readFile(
            FLAGS_platform_mapping_override_path.data(), platformMappingStr)) {
      throw FbossError("unable to read ", FLAGS_platform_mapping_override_path);
    }
    XLOG(INFO) << "Overriding platform mapping from "
               << FLAGS_platform_mapping_override_path;
  }
  if (platformMappingStr.empty() &&
      !FLAGS_platform_descriptor_config_path.empty()) {
    auto descriptorPlatformMapping =
        PlatformDescriptorRegistry::get().loadPlatformMapping(type);
    if (descriptorPlatformMapping.has_value()) {
      XLOG(INFO) << "Loading platform mapping from platform descriptor config "
                 << FLAGS_platform_descriptor_config_path;
      return std::make_unique<PlatformMapping>(*descriptorPlatformMapping);
    }
  }
  switch (type) {
    case PlatformType::PLATFORM_WEDGE100:
      return platformMappingStr.empty()
          ? std::make_unique<Wedge100PlatformMapping>()
          : std::make_unique<Wedge100PlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_GALAXY_LC:
      return std::make_unique<GalaxyLCPlatformMapping>(
          GalaxyLCPlatformMapping::getLinecardName());
    case PlatformType::PLATFORM_GALAXY_FC:
      return std::make_unique<GalaxyFCPlatformMapping>(
          GalaxyFCPlatformMapping::getFabriccardName());
    case PlatformType::PLATFORM_MINIPACK:
      return std::make_unique<MinipackPlatformMapping>(
          ExternalPhyVersion::MILN5_2, platformMappingStr);
    case PlatformType::PLATFORM_YAMP:
      return std::make_unique<YampPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_FUJI:
      return std::make_unique<FujiPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_ELBERT:
      return std::make_unique<ElbertPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_WEDGE400:
    case PlatformType::PLATFORM_WEDGE400_GRANDTETON:
      if (type == PlatformType::PLATFORM_WEDGE400_GRANDTETON ||
          utility::isWedge400PlatformRackTypeInference()) {
        return platformMappingStr.empty()
            ? std::make_unique<Wedge400GrandTetonPlatformMapping>()
            : std::make_unique<Wedge400GrandTetonPlatformMapping>(
                  platformMappingStr);
      } else {
        return platformMappingStr.empty()
            ? std::make_unique<Wedge400PlatformMapping>()
            : std::make_unique<Wedge400PlatformMapping>(platformMappingStr);
      }
    case PlatformType::PLATFORM_WEDGE400C:
    case PlatformType::PLATFORM_WEDGE400C_GRANDTETON:
      if (type == PlatformType::PLATFORM_WEDGE400C_GRANDTETON ||
          utility::isWedge400CPlatformRackTypeInference()) {
        return platformMappingStr.empty()
            ? std::make_unique<Wedge400CGrandTetonPlatformMapping>()
            : std::make_unique<Wedge400CGrandTetonPlatformMapping>(
                  platformMappingStr);
      } else {
        return platformMappingStr.empty()
            ? std::make_unique<Wedge400CPlatformMapping>()
            : std::make_unique<Wedge400CPlatformMapping>(platformMappingStr);
      }
    case PlatformType::PLATFORM_DARWIN:
    case PlatformType::PLATFORM_DARWIN48V:
      return platformMappingStr.empty()
          ? std::make_unique<DarwinPlatformMapping>()
          : std::make_unique<DarwinPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_MONTBLANC:
      return platformMappingStr.empty()
          ? std::make_unique<MontblancPlatformMapping>()
          : std::make_unique<MontblancPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_JANGA800BIC:
      return platformMappingStr.empty()
          ? std::make_unique<Janga800bicPlatformMapping>()
          : std::make_unique<Janga800bicPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_TAHAN800BC:
      return platformMappingStr.empty()
          ? std::make_unique<Tahan800bcPlatformMapping>()
          : std::make_unique<Tahan800bcPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_FAKE_WEDGE: {
      if (!platformMappingStr.empty()) {
        return std::make_unique<PlatformMapping>(platformMappingStr);
      }
      return std::make_unique<FakeTestPlatformMapping>(
          getFakeSaiControllingPortIDs());
    }
    case PlatformType::PLATFORM_WEDGE400C_SIM:
      return platformMappingStr.empty()
          ? std::make_unique<Wedge400CPlatformMapping>()
          : std::make_unique<Wedge400CPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_MERU800BIA:
    case PlatformType::PLATFORM_MERU800BIAB:
    case PlatformType::PLATFORM_MERU800BIAC:
      return platformMappingStr.empty()
          ? std::make_unique<Meru800biaPlatformMapping>()
          : std::make_unique<Meru800biaPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_MERU800BFA:
      return platformMappingStr.empty()
          ? std::make_unique<Meru800bfaPlatformMapping>()
          : std::make_unique<Meru800bfaPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_MERU800BFA_P1:
      return platformMappingStr.empty()
          ? std::make_unique<Meru800bfaP1PlatformMapping>()
          : std::make_unique<Meru800bfaP1PlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_MORGAN800CC:
      return platformMappingStr.empty()
          ? std::make_unique<Morgan800ccPlatformMapping>()
          : std::make_unique<Morgan800ccPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_YANGRA:
      return platformMappingStr.empty()
          ? std::make_unique<YangraPlatformMapping>()
          : std::make_unique<YangraPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_MINIPACK3BTA:
      return platformMappingStr.empty()
          ? std::make_unique<Minipack3BTAPlatformMapping>()
          : std::make_unique<Minipack3BTAPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_MINIPACK3N:
      return platformMappingStr.empty()
          ? std::make_unique<Minipack3NPlatformMapping>()
          : std::make_unique<Minipack3NPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_ICECUBE800BC:
      return platformMappingStr.empty()
          ? std::make_unique<Icecube800bcPlatformMapping>()
          : std::make_unique<Icecube800bcPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_ICETEA800BC:
      return platformMappingStr.empty()
          ? std::make_unique<Icetea800bcPlatformMapping>()
          : std::make_unique<Icetea800bcPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_TAHANSB800BC:
      return platformMappingStr.empty()
          ? std::make_unique<Tahansb800bcPlatformMapping>()
          : std::make_unique<Tahansb800bcPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_LADAKH800BCLS:
      return platformMappingStr.empty()
          ? std::make_unique<Ladakh800bclsPlatformMapping>()
          : std::make_unique<Ladakh800bclsPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_LEH800BCLS:
      return platformMappingStr.empty()
          ? std::make_unique<Leh800bclsPlatformMapping>()
          : std::make_unique<Leh800bclsPlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_BLACKWOLF800BANW:
      return platformMappingStr.empty()
          ? std::make_unique<Blackwolf800banwPlatformMapping>()
          : std::make_unique<Blackwolf800banwPlatformMapping>(
                platformMappingStr);
    case PlatformType::PLATFORM_YANGRA2:
      return platformMappingStr.empty()
          ? std::make_unique<Yangra2PlatformMapping>()
          : std::make_unique<Yangra2PlatformMapping>(platformMappingStr);
    case PlatformType::PLATFORM_FAKE_SAI: {
      std::vector<int> controllingPorts = getFakeSaiControllingPortIDs();
      return std::make_unique<FakeTestPlatformMapping>(controllingPorts);
    }
    case PlatformType::PLATFORM_LASSEN_DEPRECATED:
    case PlatformType::PLATFORM_CLOUDRIPPER_DEPRECATED:
    case PlatformType::PLATFORM_CLOUDRIPPER_FABRIC_DEPRECATED:
    case PlatformType::PLATFORM_CLOUDRIPPER_VOQ_DEPRECATED:
    case PlatformType::PLATFORM_WEDGE:
    case PlatformType::PLATFORM_FAKE_WEDGE40:
    case PlatformType::PLATFORM_WEDGE400C_FABRIC:
    case PlatformType::PLATFORM_WEDGE400C_VOQ:
    case PlatformType::PLATFORM_SANDIA:
    case PlatformType::PLATFORM_MERU400BIU_DEPRECATED:
    case PlatformType::PLATFORM_MERU400BFU_DEPRECATED:
    case PlatformType::PLATFORM_MERU400BIA_DEPRECATED:
    case PlatformType::PLATFORM_UNKNOWN:
      throw FbossError("Unsupported platform type");
    default:
      if (!platformMappingStr.empty()) {
        return std::make_unique<PlatformMapping>(platformMappingStr);
      }
      throw FbossError(
          "all newer platforms than wedge800 no longer ship a compiled-in "
          "platform mapping. The mapping must be provided externally via "
          "--platform_descriptor_config_path or "
          "--platform_mapping_override_path.");
  }
}
} // namespace facebook::fboss::utility
