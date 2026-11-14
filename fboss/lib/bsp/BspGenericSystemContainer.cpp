// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspGenericSystemContainer.h"
#include <folly/FileUtil.h>
#include <folly/Singleton.h>
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

DEFINE_string(
    bsp_platform_mapping_override_path,
    "",
    "The path to the BSP Platform Mapping JSON file");

namespace facebook {
namespace fboss {

template <typename T>
std::unique_ptr<BspPlatformMapping>
BspGenericSystemContainer<T>::initBspPlatformMapping() {
  std::string platformMappingStr;
  bool mappingOverride = !FLAGS_bsp_platform_mapping_override_path.empty();
  if (mappingOverride) {
    if (!folly::readFile(
            FLAGS_bsp_platform_mapping_override_path.data(),
            platformMappingStr)) {
      throw FbossError(
          "unable to read ", FLAGS_bsp_platform_mapping_override_path);
    }
    XLOG(INFO) << "Overriding BSP platform mapping from "
               << FLAGS_bsp_platform_mapping_override_path;
  }
  if (mappingOverride) {
    return std::make_unique<T>(platformMappingStr);
  }

  return std::make_unique<T>();
}

using Meru800biaSystemContainer =
    BspGenericSystemContainer<Meru800biaBspPlatformMapping>;
folly::Singleton<Meru800biaSystemContainer> _meru800biaSystemContainer;
template <>
std::shared_ptr<Meru800biaSystemContainer>
Meru800biaSystemContainer::getInstance() {
  return _meru800biaSystemContainer.try_get();
}

using Meru800bfaSystemContainer =
    BspGenericSystemContainer<Meru800bfaBspPlatformMapping>;
folly::Singleton<Meru800bfaSystemContainer> _meru800bfaSystemContainer;
template <>
std::shared_ptr<Meru800bfaSystemContainer>
Meru800bfaSystemContainer::getInstance() {
  return _meru800bfaSystemContainer.try_get();
}

using MontblancSystemContainer =
    BspGenericSystemContainer<MontblancBspPlatformMapping>;
folly::Singleton<MontblancSystemContainer> _montblancSystemContainer;
template <>
std::shared_ptr<MontblancSystemContainer>
MontblancSystemContainer::getInstance() {
  return _montblancSystemContainer.try_get();
}

using Minipack3BTASystemContainer =
    BspGenericSystemContainer<Minipack3BTABspPlatformMapping>;
folly::Singleton<Minipack3BTASystemContainer> _minipack3btaSystemContainer;
template <>
std::shared_ptr<Minipack3BTASystemContainer>
Minipack3BTASystemContainer::getInstance() {
  return _minipack3btaSystemContainer.try_get();
}

using Minipack3NSystemContainer =
    BspGenericSystemContainer<Minipack3NBspPlatformMapping>;
folly::Singleton<Minipack3NSystemContainer> _minipack3NSystemContainer;
template <>
std::shared_ptr<Minipack3NSystemContainer>
Minipack3NSystemContainer::getInstance() {
  return _minipack3NSystemContainer.try_get();
}

using Morgan800ccSystemContainer =
    BspGenericSystemContainer<Morgan800ccBspPlatformMapping>;
folly::Singleton<Morgan800ccSystemContainer> _morgan800ccSystemContainer;
template <>
std::shared_ptr<Morgan800ccSystemContainer>
Morgan800ccSystemContainer::getInstance() {
  return _morgan800ccSystemContainer.try_get();
}

using Janga800bicSystemContainer =
    BspGenericSystemContainer<Janga800bicBspPlatformMapping>;
folly::Singleton<Janga800bicSystemContainer> _janga800bicSystemContainer;
template <>
std::shared_ptr<Janga800bicSystemContainer>
Janga800bicSystemContainer::getInstance() {
  return _janga800bicSystemContainer.try_get();
}

using Tahan800bcSystemContainer =
    BspGenericSystemContainer<Tahan800bcBspPlatformMapping>;
folly::Singleton<Tahan800bcSystemContainer> _tahan800bcSystemContainer;
template <>
std::shared_ptr<Tahan800bcSystemContainer>
Tahan800bcSystemContainer::getInstance() {
  return _tahan800bcSystemContainer.try_get();
}

using Icecube800bcSystemContainer =
    BspGenericSystemContainer<Icecube800bcBspPlatformMapping>;
folly::Singleton<Icecube800bcSystemContainer> _icecube800bcSystemContainer;
template <>
std::shared_ptr<Icecube800bcSystemContainer>
Icecube800bcSystemContainer::getInstance() {
  return _icecube800bcSystemContainer.try_get();
}

using Icetea800bcSystemContainer =
    BspGenericSystemContainer<Icetea800bcBspPlatformMapping>;
folly::Singleton<Icetea800bcSystemContainer> _icetea800bcSystemContainer;
template <>
std::shared_ptr<Icetea800bcSystemContainer>
Icetea800bcSystemContainer::getInstance() {
  return _icetea800bcSystemContainer.try_get();
}

using Tahansb800bcSystemContainer =
    BspGenericSystemContainer<Tahansb800bcBspPlatformMapping>;
folly::Singleton<Tahansb800bcSystemContainer> _tahansb800bcSystemContainer;
template <>
std::shared_ptr<Tahansb800bcSystemContainer>
Tahansb800bcSystemContainer::getInstance() {
  return _tahansb800bcSystemContainer.try_get();
}

using Wedge800BACTSystemContainer =
    BspGenericSystemContainer<Wedge800BACTBspPlatformMapping>;
folly::Singleton<Wedge800BACTSystemContainer> _wedge800bactSystemContainer;
template <>
std::shared_ptr<Wedge800BACTSystemContainer>
Wedge800BACTSystemContainer::getInstance() {
  return _wedge800bactSystemContainer.try_get();
}

using Wedge800CACTSystemContainer =
    BspGenericSystemContainer<Wedge800CACTBspPlatformMapping>;
folly::Singleton<Wedge800CACTSystemContainer> _wedge800cactSystemContainer;
template <>
std::shared_ptr<Wedge800CACTSystemContainer>
Wedge800CACTSystemContainer::getInstance() {
  return _wedge800cactSystemContainer.try_get();
}

using Ladakh800bclsSystemContainer =
    BspGenericSystemContainer<Ladakh800bclsBspPlatformMapping>;
folly::Singleton<Ladakh800bclsSystemContainer> _Ladakh800bclsSystemContainer;
template <>
std::shared_ptr<Ladakh800bclsSystemContainer>
Ladakh800bclsSystemContainer::getInstance() {
  return _Ladakh800bclsSystemContainer.try_get();
}

using Leh800bclsSystemContainer =
    BspGenericSystemContainer<Leh800bclsBspPlatformMapping>;
folly::Singleton<Leh800bclsSystemContainer> _Leh800bclsSystemContainer;
template <>
std::shared_ptr<Leh800bclsSystemContainer>
Leh800bclsSystemContainer::getInstance() {
  return _Leh800bclsSystemContainer.try_get();
}

using M4061clscSystemContainer =
    BspGenericSystemContainer<M4061clscBspPlatformMapping>;
folly::Singleton<M4061clscSystemContainer> _m4061clscSystemContainer;
template <>
std::shared_ptr<M4061clscSystemContainer>
M4061clscSystemContainer::getInstance() {
  return _m4061clscSystemContainer.try_get();
}

using M4052ACTMSystemContainer =
    BspGenericSystemContainer<M4052ACTMBspPlatformMapping>;
folly::Singleton<M4052ACTMSystemContainer> _m4052actmSystemContainer;
template <>
std::shared_ptr<M4052ACTMSystemContainer>
M4052ACTMSystemContainer::getInstance() {
  return _m4052actmSystemContainer.try_get();
}

using M4061nhpSystemContainer =
    BspGenericSystemContainer<M4061nhpBspPlatformMapping>;
folly::Singleton<M4061nhpSystemContainer> _m4061nhpSystemContainer;
template <>
std::shared_ptr<M4061nhpSystemContainer>
M4061nhpSystemContainer::getInstance() {
  return _m4061nhpSystemContainer.try_get();
}

using M4062nhpSystemContainer =
    BspGenericSystemContainer<M4062nhpBspPlatformMapping>;
folly::Singleton<M4062nhpSystemContainer> _m4062nhpSystemContainer;
template <>
std::shared_ptr<M4062nhpSystemContainer>
M4062nhpSystemContainer::getInstance() {
  return _m4062nhpSystemContainer.try_get();
}
using M5120cscSystemContainer =
    BspGenericSystemContainer<M5120cscBspPlatformMapping>;
folly::Singleton<M5120cscSystemContainer> _m5120cscSystemContainer;
template <>
std::shared_ptr<M5120cscSystemContainer>
M5120cscSystemContainer::getInstance() {
  return _m5120cscSystemContainer.try_get();
}

using M4340nhpSystemContainer =
    BspGenericSystemContainer<M4340nhpBspPlatformMapping>;
folly::Singleton<M4340nhpSystemContainer> _m4340nhpSystemContainer;
template <>
std::shared_ptr<M4340nhpSystemContainer>
M4340nhpSystemContainer::getInstance() {
  // Instance-form try_get() keeps the singleton referenced so the linker
  // cannot drop its registration with the SingletonVault.
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  return _m4340nhpSystemContainer.try_get();
}

} // namespace fboss
} // namespace facebook
