// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/lib/fpga/MinipackSystemContainer.h"

#include "fboss/agent/FbossError.h"
#include "fboss/lib/fpga/MinipackPimContainer.h"

#include <folly/Singleton.h>
#include <pciaccess.h>

namespace facebook::fboss {

folly::Singleton<MinipackSystemContainer> _minipackSystemContainer;
std::shared_ptr<MinipackSystemContainer>
MinipackSystemContainer::getInstance() {
  return _minipackSystemContainer.try_get();
}

MinipackSystemContainer::MinipackSystemContainer()
    : MinipackSystemContainer(
          std::make_unique<FpgaDevice>(
              PciVendorId(kFacebookFpgaVendorID),
              PciDeviceId(PCI_MATCH_ANY))) {}

MinipackSystemContainer::MinipackSystemContainer(
    std::unique_ptr<FpgaDevice> fpgaDevice)
    : MinipackBaseSystemContainer(std::move(fpgaDevice)) {
  // create all PIM FPGA controllers
  for (auto pim = kPimStartNum; pim < kPimStartNum + kNumberPim; pim++) {
    setPimContainer(
        pim,
        std::make_unique<MinipackPimContainer>(
            pim,
            fmt::format("pim{:d}", pim),
            getFpgaDevice(),
            getPimOffset(pim),
            kFacebookFpgaPimSize));
    ;
  }
}

MultiPimPlatformPimContainer::PimType MinipackSystemContainer::getPimType(
    int pim) const {
  static constexpr auto kMinipack16QPimVal = 0xA3000000;
  static constexpr auto kMinipack16OPimVal = 0xA5000000;
  // a PIM that isn't answering reads 0, which no PIM type uses
  static constexpr uint32_t kDeadPimVal = 0x0;

  static constexpr uint32_t kFacebookFpgaPimTypeBase = 0xFF000000;
  static constexpr uint32_t kFacebookFpgaPimTypeReg = 0x0;

  auto pimOffset = getPimOffset(pim);
  uint32_t curPimTypeReg =
      getFpgaDevice()->read(pimOffset + kFacebookFpgaPimTypeReg) &
      kFacebookFpgaPimTypeBase;

  switch (curPimTypeReg) {
    case kMinipack16QPimVal:
      return MultiPimPlatformPimContainer::PimType::MINIPACK_16Q;
    case kMinipack16OPimVal:
      return MultiPimPlatformPimContainer::PimType::MINIPACK_16O;
    case kDeadPimVal:
    case kFacebookFpgaPimTypeBase:
      throw PimTypeError(
          pim,
          curPimTypeReg,
          PimTypeError::Reason::UNREADABLE,
          "Error in reading PIM Type from DOM FPGA, register value: ",
          curPimTypeReg,
          " for pim:",
          pim);
    default:
      throw PimTypeError(
          pim,
          curPimTypeReg,
          PimTypeError::Reason::UNRECOGNIZED,
          "Unrecognized pim type with register value:",
          curPimTypeReg,
          " for pim:",
          pim);
  }
}
} // namespace facebook::fboss
