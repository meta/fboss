// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/led_service/LedManagerInit.h"
#include "fboss/led_service/DarwinLedManager.h"
#include "fboss/led_service/ElbertLedManager.h"
#include "fboss/led_service/FujiLedManager.h"
#include "fboss/led_service/Icecube800banwLedManager.h"
#include "fboss/led_service/Icecube800bcLedManager.h"
#include "fboss/led_service/Icetea800bcLedManager.h"
#include "fboss/led_service/Janga800bicLedManager.h"
#include "fboss/led_service/Ladakh800bclsLedManager.h"
#include "fboss/led_service/Leh800bclsLedManager.h"
#include "fboss/led_service/M4052ACTMLedManager.h"
#include "fboss/led_service/M4061clscLedManager.h"
#include "fboss/led_service/M4061nhpLedManager.h"
#include "fboss/led_service/M4062nhpLedManager.h"
#include "fboss/led_service/M4340nhpLedManager.h"
#include "fboss/led_service/Meru800bfaLedManager.h"
#include "fboss/led_service/Meru800biaLedManager.h"
#include "fboss/led_service/Minipack3BTALedManager.h"
#include "fboss/led_service/Minipack3NLedManager.h"
#include "fboss/led_service/MinipackLedManager.h"
#include "fboss/led_service/MontblancLedManager.h"
#include "fboss/led_service/Morgan800ccLedManager.h"
#include "fboss/led_service/Tahan800bcLedManager.h"
#include "fboss/led_service/Tahansb800bcLedManager.h"
#include "fboss/led_service/Wedge400LedManager.h"
#include "fboss/led_service/Wedge800BACTLedManager.h"
#include "fboss/led_service/Wedge800CACTLedManager.h"
#include "fboss/led_service/YampLedManager.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"

namespace facebook::fboss {

/*
 * createLedManager
 *
 * createLedManager will create the LED manager class for the given platform
 */
std::unique_ptr<LedManager> createLedManager() {
  auto productInfo =
      std::make_unique<PlatformProductInfo>(FLAGS_fruid_filepath);
  productInfo->initialize();
  auto mode = productInfo->getType();

  if (mode == PlatformType::PLATFORM_MONTBLANC) {
    return std::make_unique<MontblancLedManager>();
  } else if (mode == PlatformType::PLATFORM_FUJI) {
    return std::make_unique<FujiLedManager>();
  } else if (mode == PlatformType::PLATFORM_MINIPACK) {
    return std::make_unique<MinipackLedManager>();
  } else if (mode == PlatformType::PLATFORM_YAMP) {
    return std::make_unique<YampLedManager>();
  } else if (mode == PlatformType::PLATFORM_ELBERT) {
    return std::make_unique<ElbertLedManager>();
  } else if (
      mode == PlatformType::PLATFORM_DARWIN ||
      mode == PlatformType::PLATFORM_DARWIN48V) {
    return std::make_unique<DarwinLedManager>();
  } else if (mode == PlatformType::PLATFORM_WEDGE400) {
    return std::make_unique<Wedge400LedManager>();
  } else if (
      mode == PlatformType::PLATFORM_MERU800BFA ||
      mode == PlatformType::PLATFORM_MERU800BFA_P1) {
    return std::make_unique<Meru800bfaLedManager>();
  } else if (
      mode == PlatformType::PLATFORM_MERU800BIA ||
      mode == PlatformType::PLATFORM_MERU800BIAB ||
      mode == PlatformType::PLATFORM_MERU800BIAC) {
    return std::make_unique<Meru800biaLedManager>();
  } else if (mode == PlatformType::PLATFORM_MORGAN800CC) {
    return std::make_unique<Morgan800ccLedManager>();
  } else if (mode == PlatformType::PLATFORM_MINIPACK3BTA) {
    return std::make_unique<Minipack3BTALedManager>();
  } else if (mode == PlatformType::PLATFORM_MINIPACK3N) {
    return std::make_unique<Minipack3NLedManager>();
  } else if (mode == PlatformType::PLATFORM_JANGA800BIC) {
    return std::make_unique<Janga800bicLedManager>();
  } else if (mode == PlatformType::PLATFORM_TAHAN800BC) {
    return std::make_unique<Tahan800bcLedManager>();
  } else if (mode == PlatformType::PLATFORM_ICECUBE800BANW) {
    return std::make_unique<Icecube800banwLedManager>();
  } else if (mode == PlatformType::PLATFORM_ICECUBE800BC) {
    return std::make_unique<Icecube800bcLedManager>();
  } else if (mode == PlatformType::PLATFORM_M4052ACTM) {
    return std::make_unique<M4052ACTMLedManager>();
  } else if (mode == PlatformType::PLATFORM_M4061NHP) {
    return std::make_unique<M4061nhpLedManager>();
  } else if (mode == PlatformType::PLATFORM_M4062NHP) {
    return std::make_unique<M4062nhpLedManager>();
  } else if (mode == PlatformType::PLATFORM_M4340NHP) {
    return std::make_unique<M4340nhpLedManager>();
  } else if (mode == PlatformType::PLATFORM_M4061CLSC) {
    return std::make_unique<M4061clscLedManager>();
  } else if (mode == PlatformType::PLATFORM_ICETEA800BC) {
    return std::make_unique<Icetea800bcLedManager>();
  } else if (mode == PlatformType::PLATFORM_TAHANSB800BC) {
    return std::make_unique<Tahansb800bcLedManager>();
  } else if (
      mode == PlatformType::PLATFORM_WEDGE800BACT ||
      mode == PlatformType::PLATFORM_WEDGE800BNHP) {
    return std::make_unique<Wedge800BACTLedManager>();
  } else if (
      mode == PlatformType::PLATFORM_WEDGE800CACT ||
      mode == PlatformType::PLATFORM_WEDGE800CNHP) {
    return std::make_unique<Wedge800CACTLedManager>();
  } else if (mode == PlatformType::PLATFORM_LADAKH800BCLS) {
    return std::make_unique<Ladakh800bclsLedManager>();
  } else if (mode == PlatformType::PLATFORM_LEH800BCLS) {
    return std::make_unique<Leh800bclsLedManager>();
  }
  return nullptr;
}

} // namespace facebook::fboss
