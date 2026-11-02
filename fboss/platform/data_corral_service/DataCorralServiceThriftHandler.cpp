/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/data_corral_service/DataCorralServiceThriftHandler.h"

#include <folly/logging/xlog.h>

#include "fboss/lib/LogThriftCall.h"
#include "fboss/platform/weutil/Weutil.h"

namespace facebook::fboss::platform::data_corral_service {

void DataCorralServiceThriftHandler::getFruid(
    DataCorralFruidReadResponse& response,
    bool uncached) {
  auto log = LOG_THRIFT_CALL(DBG1);

  if (uncached || fruid_.rlock()->empty()) {
    auto fruid =
        createWeUtilIntf("chassis" /*eepromName*/, eepromPath_, 0 /*offset*/)
            ->getContents();
    fruid_.withWLock([&](auto& cached) { cached = std::move(fruid); });
  }

  auto vData = fruid_.withRLock([](const auto& fruid) {
    std::vector<FruIdData> data;
    data.reserve(fruid.size());
    for (const auto& [name, value] : fruid) {
      FruIdData fruIdData;
      fruIdData.name() = name;
      fruIdData.value() = value;
      data.emplace_back(std::move(fruIdData));
    }
    return data;
  });

  response.fruidData() = std::move(vData);
}
} // namespace facebook::fboss::platform::data_corral_service
