// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/qsfp_service/test/hw_test/HwTest.h"

#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

#include <folly/gen/Base.h>
#include <folly/portability/Filesystem.h>

namespace {
constexpr int kI2cStressTestIterations = 200;
// Check that the dumped I2c log file size is less than 20MB. This confirms
// that the config for number of I2c log buffer entries is reasonable.
constexpr int kTwentyMB = 20 * 1024 * 1024;
} // namespace

namespace facebook::fboss {

TEST_F(HwTest, i2cStressRead) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::I2C_ACCESS});
  auto transceivers = utility::legacyTransceiverIds(
      utility::getCabledPortTranceivers(getHwQsfpEnsemble()));
  addTestedTransceiverIds(transceivers);
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  std::map<int32_t, TransceiverInfo> transceiversInfo;
  getHwQsfpEnsemble()->getWedgeManager()->getTransceiversInfo(
      transceiversInfo, std::make_unique<std::vector<int32_t>>(transceivers));
  std::map<int32_t, ReadResponse> previousResponse;

  ReadRequest request;
  request.ids() = transceivers;
  TransceiverIOParameters params;
  params.offset() =
      0; // Identifier Byte 0 in all transceivers remains constant across reads
  request.parameter() = params;

  for (auto iteration = 1; iteration <= kI2cStressTestIterations; iteration++) {
    std::unique_ptr<ReadRequest> readRequest =
        std::make_unique<ReadRequest>(request);
    std::map<int32_t, ReadResponse> currentResponse;
    wedgeManager->readTransceiverRegister(
        currentResponse, std::move(readRequest));
    for (auto tcvrId : transceivers) {
      // Assert the following things
      // 1. Response is returned for all the requested transceiver IDs
      // 2. Response is valid
      // 3. Data read should be the same in all iterations
      // 4. Byte 0 is the module identifier and should match the one in
      // transceiversInfo
      EXPECT_TRUE(currentResponse.find(tcvrId) != currentResponse.end());
      auto curr = currentResponse[tcvrId];
      EXPECT_TRUE(*curr.valid())
          << "Invalid response on transceiver " << tcvrId;
      if (!folly::copy(curr.valid().value())) {
        // Don't access data if it's not valid.
        continue;
      }

      TransceiverModuleIdentifier identifier =
          static_cast<TransceiverModuleIdentifier>(*(curr.data()->data()));
      EXPECT_EQ(
          identifier,
          transceiversInfo[tcvrId].tcvrState()->identifier().value_or({}));
      EXPECT_TRUE(identifier != TransceiverModuleIdentifier::UNKNOWN);
      if (iteration != 1) {
        auto prev = previousResponse[tcvrId];
        if (folly::copy(prev.valid().value())) {
          EXPECT_EQ(*(prev.data()->data()), *(curr.data()->data()));
        }
      }
    }
    previousResponse = currentResponse;
  }
  for (auto tcvrId : transceivers) {
    if (wedgeManager->getI2cLogBufferCapacity(tcvrId) == 0) {
      // Logging feature is not supported on this platform / transceiver.
      continue;
    }
    auto entries = wedgeManager->dumpTransceiverI2cLog(tcvrId);
    EXPECT_GT(entries.first, 0);
    EXPECT_GT(entries.second, 0);
  }
}

TEST_F(HwTest, i2cStressWrite) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::I2C_ACCESS});
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  // Only work with optical transceivers. The offset that this test is
  // writing is not writable on copper/flatMem modules

  auto opticalTransceivers = getCabledOpticalAndActiveTransceiverIDs();
  addTestedTransceiverIds(opticalTransceivers);

  EXPECT_TRUE(!opticalTransceivers.empty());

  WriteRequest request;
  request.ids() = opticalTransceivers;
  TransceiverIOParameters params;
  // We can write to byte 123 which is password entry page register in both sff
  // and cmis
  // SFF: Password entry area = 123-126
  // CMIS: Password entry area = 122-125
  params.offset() = 123;
  request.parameter() = params;
  request.data() = 0;

  for (auto iteration = 1; iteration <= kI2cStressTestIterations; iteration++) {
    std::unique_ptr<WriteRequest> writeRequest =
        std::make_unique<WriteRequest>(request);
    std::map<int32_t, WriteResponse> currentResponse;
    wedgeManager->writeTransceiverRegister(
        currentResponse, std::move(writeRequest));
    for (auto tcvrId : opticalTransceivers) {
      // Assert that the write operation was successful
      EXPECT_TRUE(currentResponse.find(tcvrId) != currentResponse.end());
      auto curr = currentResponse[tcvrId];
      EXPECT_TRUE(*curr.success());
    }
  }
  for (auto tcvrId : opticalTransceivers) {
    if (wedgeManager->getI2cLogBufferCapacity(tcvrId) == 0) {
      // Logging feature is not supported on this platform / transceiver.
      continue;
    }
    auto entries = wedgeManager->dumpTransceiverI2cLog(tcvrId);
    EXPECT_GT(entries.first, 0);
    EXPECT_GT(entries.second, 0);
  }
}

TEST_F(HwTest, i2cLogCapacityRead) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::I2C_ACCESS});
  auto transceivers = utility::legacyTransceiverIds(
      utility::getCabledPortTranceivers(getHwQsfpEnsemble()));
  addTestedTransceiverIds(transceivers);
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  std::map<int32_t, TransceiverInfo> transceiversInfo;
  getHwQsfpEnsemble()->getWedgeManager()->getTransceiversInfo(
      transceiversInfo, std::make_unique<std::vector<int32_t>>(transceivers));
  std::map<int32_t, ReadResponse> previousResponse;

  ReadRequest request;
  request.ids() = transceivers;
  TransceiverIOParameters params;
  params.offset() = 0;
  request.parameter() = params;

  if (transceivers.empty()) {
    return;
  }
  auto tcvrId = transceivers[0];
  auto capacity = wedgeManager->getI2cLogBufferCapacity(tcvrId);
  if (capacity == 0) {
    // Logging feature is not supported on this platform / transceiver.
    return;
  }
  for (auto i = 0; i < kI2cStressTestIterations; i++) {
    std::unique_ptr<ReadRequest> readRequest =
        std::make_unique<ReadRequest>(request);
    std::map<int32_t, ReadResponse> currentResponse;
    wedgeManager->readTransceiverRegister(
        currentResponse, std::move(readRequest));
  }
  auto entries = wedgeManager->dumpTransceiverI2cLog(tcvrId);
  EXPECT_GE(entries.second, kI2cStressTestIterations);
  auto logFile = wedgeManager->getI2cLogName(tcvrId);
  auto fileSize = folly::fs::file_size(logFile);
  EXPECT_TRUE(fileSize > 0);
  // The estimated size of full log file is as follows:
  // Full Log = average entry size * capacity
  size_t estimatedFullSize = (fileSize / entries.second) * capacity;
  EXPECT_TRUE(estimatedFullSize < kTwentyMB);
}

TEST_F(HwTest, i2cLogCapacityWrite) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::I2C_ACCESS});
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  auto transceivers = getCabledOpticalAndActiveTransceiverIDs();
  addTestedTransceiverIds(transceivers);
  EXPECT_TRUE(!transceivers.empty());
  WriteRequest request;
  request.ids() = transceivers;
  TransceiverIOParameters params;
  // We can write to byte 123 which is password entry page register in both sff
  // and cmis
  // SFF: Password entry area = 123-126
  // CMIS: Password entry area = 122-125
  params.offset() = 123;
  request.parameter() = params;
  request.data() = 0;

  if (transceivers.empty()) {
    return;
  }
  auto tcvrId = transceivers[0];
  auto capacity = wedgeManager->getI2cLogBufferCapacity(tcvrId);
  if (capacity == 0) {
    // Logging feature is not supported on this platform / transceiver.
    return;
  }
  for (auto i = 0; i < kI2cStressTestIterations; i++) {
    std::unique_ptr<WriteRequest> writeRequest =
        std::make_unique<WriteRequest>(request);
    std::map<int32_t, WriteResponse> currentResponse;
    wedgeManager->writeTransceiverRegister(
        currentResponse, std::move(writeRequest));
  }
  auto entries = wedgeManager->dumpTransceiverI2cLog(tcvrId);
  EXPECT_GE(entries.second, kI2cStressTestIterations);
  auto logFile = wedgeManager->getI2cLogName(tcvrId);
  auto fileSize = folly::fs::file_size(logFile);
  EXPECT_TRUE(fileSize > 0);
  // The estimated size of full log file is as follows:
  // Full Log = average entry size * capacity
  size_t estimatedFullSize = (fileSize / entries.second) * capacity;
  EXPECT_TRUE(estimatedFullSize < kTwentyMB);
}

TEST_F(HwTest, cmisPageChange) {
  addVerifiedProductionFeatures(
      {qsfp_production_features::QsfpProductionFeature::I2C_ACCESS});
  // Switch between page 0x10 and page 0x11 on CMIS modules on all ports and
  // ensure that page 0x10 reads back the same every time
  auto transceivers = utility::legacyTransceiverIds(
      utility::getCabledPortTranceivers(getHwQsfpEnsemble()));
  addTestedTransceiverIds(transceivers);
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  std::map<int32_t, TransceiverInfo> transceiversInfo;
  getHwQsfpEnsemble()->getWedgeManager()->getTransceiversInfo(
      transceiversInfo, std::make_unique<std::vector<int32_t>>(transceivers));

  // Only work with optical CMIS transceivers.
  auto testTransceivers =
      folly::gen::from(transceivers) |
      folly::gen::filter([&transceiversInfo](int32_t tcvrId) {
        const auto& tcvrInfo = transceiversInfo[tcvrId];
        const auto& tcvrState = *tcvrInfo.tcvrState();
        return TransceiverManager::opticalOrActiveCmisCable(tcvrState);
      }) |
      folly::gen::as<std::vector>();

  EXPECT_TRUE(!testTransceivers.empty());

  std::map<int /* page */, std::map<int32_t /* tcvrId */, ReadResponse>>
      previousResponses;
  previousResponses[0x10] = {};

  for (auto iteration = 1; iteration <= kI2cStressTestIterations; iteration++) {
    for (auto page : {0x10, 0x11}) {
      std::unique_ptr<ReadRequest> readRequest =
          std::make_unique<ReadRequest>();
      TransceiverIOParameters params;
      readRequest->ids() = testTransceivers;
      params.offset() = 128; // Start of upper page
      params.page() = page; // Page
      params.length() = 128; // Read Entire page
      readRequest->parameter() = params;

      std::map<int32_t, ReadResponse> currentResponse;
      wedgeManager->readTransceiverRegister(
          currentResponse, std::move(readRequest));

      // Only verify that page 0x10 has not changed. Contents of page 0x11 can
      // change
      if (page != 0x10) {
        continue;
      }
      for (auto tcvrId : testTransceivers) {
        if (previousResponses[page].find(tcvrId) ==
            previousResponses[page].end()) {
          previousResponses[page][tcvrId] = currentResponse[tcvrId];
        }
        for (int i = 0; i < 128; i++) {
          // Expect the previous response and the new response to be the same
          EXPECT_EQ(
              *(previousResponses[page][tcvrId].data()->data() + i),
              *(currentResponse[tcvrId].data()->data() + i))
              << "Transceiver " << tcvrId << " page 0x" << std::hex << page
              << " byte " << std::dec << 128 + i << " changed on iteration "
              << iteration;
        }
      }
    }
  }
}
} // namespace facebook::fboss
