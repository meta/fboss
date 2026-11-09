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
#include <nlohmann/json.hpp>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <filesystem>
#include <iostream>
#include "fboss/lib/bsp/bspmapping/Parser.h"

namespace facebook::fboss {
const std::map<std::string, folly::StringPiece> kPlatformNameToCsv = {
    {"ladakh800bcls", kPortMappingLadakh800bclsCsv},
    {"leh800bcls", kPortMappingLeh800bclsCsv},
};

// Helper function to generate PHY CSV filename from regular CSV filename
std::string getPhyCsvFilename(const std::string& regularCsv) {
  std::string phyCsv = regularCsv;
  size_t pos = phyCsv.find("_BspMapping.csv");
  if (pos != std::string::npos) {
    phyCsv.replace(pos, 15, "_PhyBspMapping.csv");
  }
  return phyCsv;
}

int cliMain(int argc, char* argv[]) {
  std::map<std::string, BspPlatformMappingThrift> bspPlatformMap;

  std::string tempDir = std::filesystem::temp_directory_path();
  std::string outputDir = tempDir + "/generated_configs/";
  std::cout << "Output directory: " << outputDir << std::endl;

  std::filesystem::create_directory(outputDir);

  for (const auto& [platformName, csv] : kPlatformNameToCsv) {
    BspPlatformMappingThrift bspPlatformMapping;

    // Dynamically check if PHY CSV exists for this platform
    std::string phyCsvFilename = getPhyCsvFilename(csv.str());
    std::string phyCsvPath = kInputConfigPrefix.toString() + phyCsvFilename;

    if (std::filesystem::exists(phyCsvPath)) {
      std::cout << "Found PHY CSV for " << platformName << ": " << phyCsvPath
                << std::endl;
      bspPlatformMapping = Parser::getBspPlatformMappingFromCsv(
          kInputConfigPrefix.toString() + csv.data(), phyCsvPath);
    } else {
      std::cout << "PHY CSV not found for " << platformName
                << ", using transceiver-only mapping" << std::endl;
      bspPlatformMapping = Parser::getBspPlatformMappingFromCsv(
          kInputConfigPrefix.toString() + csv.data());
    }

    bspPlatformMap[platformName] = bspPlatformMapping;

    auto output = apache::thrift::SimpleJSONSerializer::serialize<std::string>(
        bspPlatformMapping);

    // We do this extra step of re-parsing the json to pretty print it.
    // folly::toPrettyJson(facebook::thrift::to_dynamic(_)) doesn't seem to work
    // due to the nested structs.
    // We also specifically use nlohmann::ordered_json here because folly will
    // sort json keys by alphabetical order, making it difficult to diff with
    // the materialized JSON from configerator.
    auto alt = nlohmann::ordered_json::parse(output);
    std::string prettyOutput = alt.dump(2);

    std::string outputFile = outputDir + platformName + ".json";

    std::cout << "Writing to " << outputFile << std::endl;

    folly::writeFileAtomic(outputFile, prettyOutput);

    std::cout << "Done writing to " << outputFile << std::endl;
  }

  return 0;
}

} // namespace facebook::fboss

int main(int argc, char* argv[]) {
  return facebook::fboss::cliMain(argc, argv);
}
