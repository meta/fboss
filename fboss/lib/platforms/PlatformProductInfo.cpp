/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/platforms/PlatformProductInfo.h"
#include "fboss/agent/FbossError.h"
#include "fboss/lib/platforms/PlatformDescriptor.h"

#include <cstdint>

#include <boost/algorithm/string.hpp>
#include <folly/FileUtil.h>
#include <folly/MacAddress.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <folly/testing/TestUtil.h>

#include "fboss/lib/platforms/FruIdFields.h"

DEFINE_string(
    mode,
    "",
    "The mode the FBOSS controller is running as, wedge, lc, or fc");
DEFINE_string(
    fruid_filepath,
    "/var/facebook/fboss/fruid.json",
    "File for storing the fruid data");

namespace facebook::fboss {

using folly::dynamic;
using folly::MacAddress;
using folly::parseJson;
using folly::StringPiece;

PlatformProductInfo::PlatformProductInfo(StringPiece path) : path_(path) {}

void PlatformProductInfo::initialize() {
  try {
    std::string data;
    folly::readFile(path_.str().c_str(), data);
    parse(data);
  } catch (const std::exception& err) {
    XLOG(ERR) << "Failed initializing ProductInfo from " << path_
              << ", fall back to use fbwhoami: " << err.what();
    // if fruid info fails fall back to fbwhoami
    initFromFbWhoAmI();
  }
  initMode();
}

std::string PlatformProductInfo::getFabricLocation() {
  return *productInfo_.fabricLocation();
}

std::string PlatformProductInfo::getOem() const {
  return *productInfo_.oem();
}

std::string PlatformProductInfo::getProductName() {
  return *productInfo_.product();
}

int PlatformProductInfo::getProductVersion() const {
  return *productInfo_.productVersion();
}

int PlatformProductInfo::getProductionState() const {
  return *productInfo_.productionState();
}

void PlatformProductInfo::initMode() {
  if (!FLAGS_platform_descriptor_config_path.empty()) {
    auto descriptorPlatformType =
        PlatformDescriptorRegistry::get().findPlatformType(
            getProductName(), FLAGS_mode);
    if (descriptorPlatformType.has_value()) {
      type_ = *descriptorPlatformType;
      return;
    }
    XLOG(WARNING) << "No platform descriptor file under "
                  << FLAGS_platform_descriptor_config_path
                  << " can match product '" << getProductName()
                  << "' and mode '" << FLAGS_mode
                  << "'; falling back to legacy platform detection";
  }
  if (FLAGS_mode.empty()) {
    auto modelName = getProductName();
    if (modelName.find("MINIPACK2") == 0) {
      type_ = PlatformType::PLATFORM_FUJI;
    } else if (
        modelName.find("Wedge100") == 0 || modelName.find("WEDGE100") == 0) {
      // Wedge100 comes from fruid.json, WEDGE100 comes from fbwhoami
      type_ = PlatformType::PLATFORM_WEDGE100;
    } else if (
        modelName.find("Wedge400c") == 0 || modelName.find("WEDGE400C") == 0) {
      type_ = PlatformType::PLATFORM_WEDGE400C;
    } else if (
        modelName.find("Wedge400") == 0 || modelName.find("WEDGE400") == 0) {
      type_ = PlatformType::PLATFORM_WEDGE400;
    } else if (modelName.find("DARWIN48V") == 0) {
      type_ = PlatformType::PLATFORM_DARWIN48V;
    } else if (
        modelName.find("Darwin") == 0 || modelName.find("DARWIN") == 0 ||
        modelName.find("DCS-7060") == 0 || modelName.find("Rackhawk") == 0) {
      type_ = PlatformType::PLATFORM_DARWIN;
    } else if (modelName == "Wedge" || modelName == "WEDGE") {
      type_ = PlatformType::PLATFORM_WEDGE;
    } else if (modelName.find("SCM-LC") == 0 || modelName.find("LC") == 0) {
      // TODO remove LC once fruid.json is fixed on Galaxy Linecards
      type_ = PlatformType::PLATFORM_GALAXY_LC;
    } else if (
        modelName.find("SCM-FC") == 0 || modelName.find("SCM-FAB") == 0 ||
        modelName.find("FAB") == 0) {
      // TODO remove FAB once fruid.json is fixed on Galaxy fabric cards
      type_ = PlatformType::PLATFORM_GALAXY_FC;
    } else if (
        modelName.find("Minipack3N") == 0 ||
        modelName.find("MINIPACK3N") == 0) {
      type_ = PlatformType::PLATFORM_MINIPACK3N;
    } else if (
        modelName.find("Minipack3BTA") == 0 ||
        modelName.find("MINIPACK3BTA") == 0) {
      type_ = PlatformType::PLATFORM_MINIPACK3BTA;
    } else if (
        modelName.find("Montblanc") == 0 || modelName.find("MONTBLANC") == 0 ||
        modelName.find("Montblancm") == 0 ||
        modelName.find("MONTBLANCM") == 0 ||
        modelName.find("MINIPACK3_CHASSIS_BUNDLE") == 0 ||
        modelName.find("MINIPACK3") == 0 ||
        modelName.find("MINIPACK3-48V-ORV3") == 0) {
      type_ = PlatformType::PLATFORM_MONTBLANC;
    } else if (
        modelName.find("MINIPACK") == 0 || modelName.find("MINIPHOTON") == 0) {
      type_ = PlatformType::PLATFORM_MINIPACK;
    } else if (modelName.find("DCS-7368") == 0 || modelName.find("YAMP") == 0) {
      type_ = PlatformType::PLATFORM_YAMP;
    } else if (
        modelName.find("DCS-7388") == 0 || modelName.find("ELBERT") == 0) {
      type_ = PlatformType::PLATFORM_ELBERT;
    } else if (modelName.find("fake_wedge40") == 0) {
      type_ = PlatformType::PLATFORM_FAKE_WEDGE40;
    } else if (modelName.find("fake_wedge") == 0) {
      type_ = PlatformType::PLATFORM_FAKE_WEDGE;
    } else if (
        modelName.find("Yangra2") == 0 || modelName.find("yangra2") == 0 ||
        modelName.find("YANGRA2") == 0) {
      type_ = PlatformType::PLATFORM_YANGRA2;
    } else if (
        modelName.find("Yangra") == 0 || modelName.find("yangra") == 0 ||
        modelName.find("YANGRA") == 0) {
      type_ = PlatformType::PLATFORM_YANGRA;
    } else if (
        modelName.find("Meru800bia") == 0 ||
        modelName.find("MERU800BIA") == 0 ||
        modelName.find("ASY-92458-101") == 0 ||
        modelName.find("ASY-92493-104") == 0 ||
        modelName.find("ASY-92458-104") == 0 ||
        modelName.find("DCS-DL-7700R4C-38PE-AC-F") == 0 ||
        modelName.find("DCS-DL-7700R4C-38PE-DC-F") == 0 ||
        modelName.find("DCS-DL-7700R4C-38PE-B-F") == 0 ||
        modelName.find("DCS-7700R4C-38PE-DC-F") == 0 ||
        modelName.find("DCS-DES-VIPER-ORV3") == 0 ||
        modelName.find("DCS-DES-VIPER-AC") == 0 ||
        modelName.find("DCS-DES-VIPER-ORV3N") == 0 ||
        modelName.find("DCS-7700R4C-38PE-DC-F-RV3") == 0 ||
        modelName.find("DCS-7700R4C-38PE-DC-F") == 0 ||
        modelName.find("DCS-7700R4C-38PE-F") == 0) {
      type_ = PlatformType::PLATFORM_MERU800BIA;
    } else if (
        modelName.find("Meru800biab") == 0 ||
        modelName.find("MERU800BIAB") == 0 ||
        modelName.find("ASY-92459-109") == 0 ||
        modelName.find("ASY-08999-09") == 0 ||
        modelName.find("DCS-DL-7700R4C-38PE-B-AC-F") == 0 ||
        modelName.find("DCS-DL-7700R4C-38PE-B-DC-F") == 0) {
      type_ = PlatformType::PLATFORM_MERU800BIAB;
    } else if (
        modelName.find("Meru800biac") == 0 ||
        modelName.find("MERU800BIAC") == 0 ||
        modelName.find("EFT-DL-7700R4C-38PE-C-F") == 0 ||
        modelName.find("FB-DL-7700R4C-38PE-C-F") == 0) {
      type_ = PlatformType::PLATFORM_MERU800BIAC;
    } else if (
        modelName.find("Meru800bfa") == 0 ||
        modelName.find("MERU800BFA") == 0 ||
        modelName.find("ASY-57651-102") == 0 ||
        modelName.find("DCS-DS-7720R4-128PE-AC-F") == 0 ||
        modelName.find("DCS-DEC-WHISTLER-AC") == 0 ||
        modelName.find("DCS-DEC-WHISTLER-DC") == 0 ||
        modelName.find("DCS-7720R4-128PE") == 0) {
      type_ = PlatformType::PLATFORM_MERU800BFA;
    } else if (
        modelName.find("MORGAN800CC") == 0 ||
        modelName.find("8501-SYS-MT") == 0 ||
        modelName.find("8501-SYS-MT-PVT") == 0 ||
        modelName.find("8501-SYS-MT-DC") == 0) {
      type_ = PlatformType::PLATFORM_MORGAN800CC;
    } else if (modelName.find("YANGRA2") == 0) {
      type_ = PlatformType::PLATFORM_YANGRA2;
    } else if (modelName.find("YANGRA") == 0) {
      type_ = PlatformType::PLATFORM_YANGRA;
    } else if (modelName.find("FAKE_SAI") == 0) {
      type_ = PlatformType::PLATFORM_FAKE_SAI;
    } else if (
        modelName.find("JANGA800BIC") == 0 || modelName.find("JANGA") == 0) {
      type_ = PlatformType::PLATFORM_JANGA800BIC;
    } else if (
        modelName.find("TAHANSB800BC") == 0 ||
        modelName.find("TAHANSB800BCM") == 0 ||
        modelName.find("TAHAN_SB") == 0) {
      type_ = PlatformType::PLATFORM_TAHANSB800BC;
    } else if (
        modelName.find("TAHAN") == 0 || modelName.find("TAHAN800BC") == 0 ||
        modelName.find("R4063-F9001-01") == 0) {
      type_ = PlatformType::PLATFORM_TAHAN800BC;
    } else if (
        modelName.find("Icecube800banw") == 0 ||
        modelName.find("ICECUBE800BANW") == 0 ||
        modelName.find("ASY-92647-101") == 0 ||
        modelName.find("EFT-7060XE7-64PRS-RV3-L") == 0) {
      type_ = PlatformType::PLATFORM_ICECUBE800BANW;
    } else if (
        modelName.find("ICECUBE") == 0 || modelName.find("ICECUBEM") == 0) {
      type_ = PlatformType::PLATFORM_ICECUBE800BC;
    } else if (modelName.find("ICETEA") == 0) {
      type_ = PlatformType::PLATFORM_ICETEA800BC;
    } else if (
        modelName.find("LADAKH800BCLS") == 0 ||
        modelName.find("LADAKH800BCLSM") == 0) {
      type_ = PlatformType::PLATFORM_LADAKH800BCLS;
    } else if (modelName.find("LEH800BCLS") == 0) {
      type_ = PlatformType::PLATFORM_LEH800BCLS;
    } else if (
        modelName.find("Blackwolf800banw") == 0 ||
        modelName.find("BLACKWOLF800BANW") == 0) {
      type_ = PlatformType::PLATFORM_BLACKWOLF800BANW;
    } else {
      throw FbossError("invalid model name " + modelName);
    }
  } else {
    if (FLAGS_mode == "wedge") {
      type_ = PlatformType::PLATFORM_WEDGE;
    } else if (FLAGS_mode == "wedge100") {
      type_ = PlatformType::PLATFORM_WEDGE100;
    } else if (FLAGS_mode == "galaxy_lc") {
      type_ = PlatformType::PLATFORM_GALAXY_LC;
    } else if (FLAGS_mode == "galaxy_fc") {
      type_ = PlatformType::PLATFORM_GALAXY_FC;
    } else if (FLAGS_mode == "minipack") {
      type_ = PlatformType::PLATFORM_MINIPACK;
    } else if (FLAGS_mode == "yamp") {
      type_ = PlatformType::PLATFORM_YAMP;
    } else if (FLAGS_mode == "fake_wedge40") {
      type_ = PlatformType::PLATFORM_FAKE_WEDGE40;
    } else if (FLAGS_mode == "wedge400") {
      type_ = PlatformType::PLATFORM_WEDGE400;
    } else if (FLAGS_mode == "wedge400_grandteton") {
      type_ = PlatformType::PLATFORM_WEDGE400_GRANDTETON;
    } else if (FLAGS_mode == "fuji") {
      type_ = PlatformType::PLATFORM_FUJI;
    } else if (FLAGS_mode == "elbert") {
      type_ = PlatformType::PLATFORM_ELBERT;
    } else if (FLAGS_mode == "darwin") {
      type_ = PlatformType::PLATFORM_DARWIN;
    } else if (FLAGS_mode == "meru800bia") {
      type_ = PlatformType::PLATFORM_MERU800BIA;
    } else if (FLAGS_mode == "meru800biab") {
      type_ = PlatformType::PLATFORM_MERU800BIAB;
    } else if (FLAGS_mode == "meru800biac") {
      type_ = PlatformType::PLATFORM_MERU800BIAC;
    } else if (FLAGS_mode == "meru800bfa") {
      type_ = PlatformType::PLATFORM_MERU800BFA;
    } else if (FLAGS_mode == "meru800bfa_p1") {
      type_ = PlatformType::PLATFORM_MERU800BFA_P1;
    } else if (FLAGS_mode == "wedge400c") {
      type_ = PlatformType::PLATFORM_WEDGE400C;
    } else if (FLAGS_mode == "wedge400c_voq") {
      type_ = PlatformType::PLATFORM_WEDGE400C_VOQ;
    } else if (FLAGS_mode == "wedge400c_fabric") {
      type_ = PlatformType::PLATFORM_WEDGE400C_FABRIC;
    } else if (
        FLAGS_mode == "montblanc" || FLAGS_mode == "montblancm" ||
        FLAGS_mode == "minipack3ba" || FLAGS_mode == "minipack3bam") {
      type_ = PlatformType::PLATFORM_MONTBLANC;
    } else if (FLAGS_mode == "icecube800bc" || FLAGS_mode == "icecube800bcm") {
      type_ = PlatformType::PLATFORM_ICECUBE800BC;
    } else if (FLAGS_mode == "icetea800bc") {
      type_ = PlatformType::PLATFORM_ICETEA800BC;
    } else if (FLAGS_mode == "fake_sai") {
      type_ = PlatformType::PLATFORM_FAKE_SAI;
    } else if (FLAGS_mode == "janga800bic") {
      type_ = PlatformType::PLATFORM_JANGA800BIC;
    } else if (FLAGS_mode == "tahan800bc") {
      type_ = PlatformType::PLATFORM_TAHAN800BC;
    } else if (FLAGS_mode == "morgan800cc") {
      type_ = PlatformType::PLATFORM_MORGAN800CC;
    } else if (FLAGS_mode == "yangra") {
      type_ = PlatformType::PLATFORM_YANGRA;
    } else if (FLAGS_mode == "minipack3bta") {
      type_ = PlatformType::PLATFORM_MINIPACK3BTA;
    } else if (FLAGS_mode == "minipack3n") {
      type_ = PlatformType::PLATFORM_MINIPACK3N;
    } else if (FLAGS_mode == "tahansb800bc" || FLAGS_mode == "tahansb800bcm") {
      type_ = PlatformType::PLATFORM_TAHANSB800BC;
    } else if (
        FLAGS_mode == "ladakh800bcls" || FLAGS_mode == "ladakh800bclsm") {
      type_ = PlatformType::PLATFORM_LADAKH800BCLS;
    } else if (FLAGS_mode == "leh800bcls") {
      type_ = PlatformType::PLATFORM_LEH800BCLS;
    } else if (FLAGS_mode == "icecube800banw") {
      type_ = PlatformType::PLATFORM_ICECUBE800BANW;
    } else if (FLAGS_mode == "blackwolf800banw") {
      type_ = PlatformType::PLATFORM_BLACKWOLF800BANW;
    } else if (FLAGS_mode == "yangra2") {
      type_ = PlatformType::PLATFORM_YANGRA2;
    } else {
      throw std::runtime_error("invalid mode " + FLAGS_mode);
    }
  }
}

std::string PlatformProductInfo::getField(
    const folly::dynamic& info,
    const std::vector<std::string>& keys) {
  for (const auto& key : keys) {
    if (info.count(key)) {
      try {
        return folly::to<std::string>(info[key].asString());
      } catch (const std::exception& err) {
        throw FbossError(
            "Failed to parse string field '",
            key,
            "' from ",
            path_,
            ": ",
            err.what());
      }
    }
  }
  // If field does not exist in fruid.json, return empty string
  return "";
}

int16_t PlatformProductInfo::getInt16Field(
    const folly::dynamic& info,
    StringPiece key) {
  try {
    return folly::to<int16_t>(info[key].asInt());
  } catch (const std::exception& err) {
    throw FbossError(
        "Failed to parse integer field '",
        key,
        "' from ",
        path_,
        ": ",
        err.what());
  }
}

void PlatformProductInfo::parse(std::string data) {
  dynamic info;
  try {
    // Fruid data can be in two formats.
    // old format (deprecated). If used, it is probably in old BMC Classic
    // hardware
    // {
    //   "Information" : fruid json
    // }
    // new format (BMC Lite Devices)
    // {
    //  Fruid json
    // }
    info = data.find(kInfo) != std::string::npos ? parseJson(data).at(kInfo)
                                                 : parseJson(data);
  } catch (const std::exception& err) {
    XLOG(ERR) << err.what();
    throw;
  }

  productInfo_.product() = getField(info, {kProdName});
  // Product Name is a required field, throw if it is not present in fruid.json.
  // It will be populated from fbwhoami if missing in fruid.json.
  if (productInfo_.product()->empty()) {
    throw FbossError("Product Name not found in fruid.json");
  }
  productInfo_.oem() = getField(info, {kSysMfg});
  productInfo_.serial() = getField(info, {kSerialNum});
  productInfo_.mfgDate() = getField(info, {kSysMfgDate});
  productInfo_.systemPartNumber() = getField(info, {kSysAmbPartNum});
  productInfo_.assembledAt() = getField(info, {kAmbAt});
  productInfo_.pcbManufacturer() = getField(info, {kPcbMfg});
  productInfo_.partNumber() = getField(info, {kProdPartNum});
  // FB only - we apply custom logic to construct unique SN for
  // cases where we create multiple assets for a single physical
  // card in chassis.
  setFBSerial();

  // Optional field in Information
  if (info.count(kVersion)) {
    productInfo_.version() = getInt16Field(info, kVersion);
  }
  if (info.count(kSubVersion)) {
    productInfo_.subVersion() = getInt16Field(info, kSubVersion);
  }
  // EEPROM V5 and V6 use different names for this field. A malformed value is
  // deliberately non-fatal: most initialize() callers, including
  // SaiPlatformInit, do not catch, and no consumer requires this field.
  for (auto key : {kProductionState, kProductionStateV6}) {
    if (info.count(key)) {
      try {
        productInfo_.productionState() = getInt16Field(info, key);
      } catch (const std::exception& ex) {
        XLOG(WARNING) << "Ignoring unparseable '" << key << "': " << ex.what();
      }
      break;
    }
  }
  if (info.count(kProdVersion)) {
    productInfo_.productVersion() = getInt16Field(info, kProdVersion);
  }

  // There are different keys for these values in BMC
  // and BMC-Lite platforms.
  productInfo_.fbPcbaPartNumber() =
      getField(info, {kFbPcbaPartNum, kFbPcbaPartNumBmcLite});
  productInfo_.fbPcbPartNumber() =
      getField(info, {kFbPcbPartNum, kFbPcbPartNumBmcLite});
  productInfo_.odmPcbaPartNumber() =
      getField(info, {kOdmPcbaPartNum, kOdmPcbaPartNumBmcLite});
  productInfo_.odmPcbaSerial() =
      getField(info, {kOdmPcbaSerialNum, kOdmPcbaSerialNumBmcLite});
  productInfo_.fabricLocation() =
      getField(info, {kFabricLocation, kFabricLocationBmcLite});
  productInfo_.bmcMac() = getField(info, {kLocalMac, kLocalMacBmcLite});
  productInfo_.mgmtMac() = getField(info, {kExtMacBase, kExtMacBaseBmcLite});
  uint64_t macBase = 0;
  try {
    macBase = MacAddress(productInfo_.mgmtMac().value()).u64HBO() + 1;
  } catch (const std::exception& err) {
    throw FbossError(
        "Failed to parse MAC field '",
        kExtMacBase,
        "'/'",
        kExtMacBaseBmcLite,
        "' (value: '",
        productInfo_.mgmtMac().value(),
        "') from ",
        path_,
        ": ",
        err.what());
  }
  productInfo_.macRangeStart() = MacAddress::fromHBO(macBase).toString();
  if (info.count(kExtMacSize)) {
    productInfo_.macRangeSize() =
        folly::to<int16_t>(getInt16Field(info, kExtMacSize) - 1);
  } else if (info.count(kExtMacSizeBmcLite)) {
    productInfo_.macRangeSize() =
        folly::to<int16_t>(getInt16Field(info, kExtMacSizeBmcLite) - 1);
  }

  // Product Asset Tag is not present in BMC-Lite platforms.
  if (info.count(kProdAssetTag)) {
    productInfo_.assetTag() = getField(info, {kProdAssetTag});
  }
  XLOG(INFO) << "Success parsing product info fields";
}

std::unique_ptr<PlatformProductInfo> fakeProductInfo() {
  folly::test::TemporaryDirectory tmpDir;
  auto fruidFilename = tmpDir.path().string() + "fruid.json";
  folly::writeFile(getFakeFruIdJson(), fruidFilename.c_str());
  auto productInfo =
      std::make_unique<facebook::fboss::PlatformProductInfo>(fruidFilename);
  productInfo->initialize();
  return productInfo;
}
} // namespace facebook::fboss
