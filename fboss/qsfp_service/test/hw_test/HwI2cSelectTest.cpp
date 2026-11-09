// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/qsfp_service/test/hw_test/HwTest.h"

#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

/*
 * This test reads the serial numbers of all connected transceivers and verify
 * that they are unique. If they are not, the test will fail. Note an exception
 * is connected DACs/AECs because the transceivers from both ends will have the
 * same serial number.
 */
TEST_F(HwTest, i2cUniqueSerialNumbers) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::I2C_ACCESS});
  // Get the IDs of all connected transceivers
  auto ensemble = getHwQsfpEnsemble();
  auto qsfpServiceHandler = ensemble->getQsfpServiceHandler();
  WedgeManager* wedgeManager = ensemble->getWedgeManager();
  auto transceivers = utility::legacyTransceiverIds(
      utility::getCabledPortTranceivers(ensemble));
  addTestedTransceiverIds(transceivers);

  // Get information of all connected transceivers
  std::map<int32_t, TransceiverInfo> transceiverInfo;
  wedgeManager->getTransceiversInfo(
      transceiverInfo, std::make_unique<std::vector<int32_t>>(transceivers));

  // Build a dictionary of transceiver ID, its ethernet name (eg eth1/1/1), and
  // the ethernet name of the connected transceiver from the other end
  std::unordered_map<int32_t, std::pair<std::string, std::string>> cabledNames;

  for (auto& cabledPairs : utility::getCabledPairsWithTransceivers(ensemble)) {
    auto aPortID = qsfpServiceHandler->getPortIdByPortName(cabledPairs.first);
    auto zPortID = qsfpServiceHandler->getPortIdByPortName(cabledPairs.second);
    CHECK(aPortID.has_value());
    CHECK(zPortID.has_value());
    auto aTcvrID = wedgeManager->getTransceiverID(*aPortID);
    auto zTcvrID = wedgeManager->getTransceiverID(*zPortID);
    CHECK(aTcvrID.has_value());
    CHECK(zTcvrID.has_value());
    cabledNames[*aTcvrID] = {cabledPairs.first, cabledPairs.second};
    cabledNames[*zTcvrID] = {cabledPairs.second, cabledPairs.first};
    XLOG(INFO) << fmt::format(
        "Transceiver {:d} has neighbor {:d}",
        static_cast<int>(*aTcvrID),
        static_cast<int>(*zTcvrID));
  }

  // Get the serial numbers of all connected transceivers and insert them into
  // a dictionary if they are unique
  std::unordered_map<std::string, int32_t> snIds;

  for (auto tcvrId : transceivers) {
    auto vendor = transceiverInfo[tcvrId].tcvrState()->vendor();
    CHECK(vendor);
    auto vendorInfo = *vendor;
    auto sn = *(vendorInfo.serialNumber());
    XLOG(INFO) << fmt::format(
        "Transceiver {:d} has serial number: {:s}", tcvrId, sn);

    if (snIds.find(sn) == snIds.end()) {
      snIds[sn] = tcvrId;
    } else {
      // Found duplicated serial numbers, module must be a single cable
      // assembly (DAC/AEC/AOC) connected back to itself on the other end
      auto neighborId = snIds[sn];
      CHECK(cabledNames.find(tcvrId) != cabledNames.end());
      CHECK(cabledNames.find(neighborId) != cabledNames.end());
      EXPECT_EQ(cabledNames[tcvrId].first, cabledNames[neighborId].second);
      EXPECT_EQ(cabledNames[tcvrId].second, cabledNames[neighborId].first);

      for (auto id : {tcvrId, neighborId}) {
        const auto& tcvrState = *transceiverInfo[id].tcvrState();
        auto transmitterTech = *(tcvrState.cable()->transmitterTech());
        EXPECT_TRUE(
            transmitterTech == TransmitterTechnology::COPPER ||
            utility::HwTransceiverUtils::isAoc(tcvrState))
            << "Transceiver " << id
            << " shares a serial number but is neither copper nor AOC";
      }
    }
  }
}
} // namespace facebook::fboss
