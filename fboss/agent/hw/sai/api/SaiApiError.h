/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Conv.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/api/LoggingUtil.h"

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class SaiApiError : public FbossError {
 public:
  template <typename... Args>
  SaiApiError(sai_status_t status, sai_api_t apiType, Args&&... args)
      : FbossError(
            "[",
            saiApiTypeToString(apiType),
            "] ",
            std::forward<Args>(args)...,
            ": ",
            saiStatusToString(status)),
        status_(status),
        apiType_(apiType) {}
  sai_status_t getSaiStatus() const {
    return status_;
  }
  sai_api_t getSaiApiType() const {
    return apiType_;
  }
  ~SaiApiError() noexcept override {}

 private:
  sai_status_t status_;
  sai_api_t apiType_;
};

template <typename... Args>
void _saiDoLog(
    bool isFatal,
    sai_status_t status,
    sai_api_t apiType,
    Args&&... args) {
  if (status != SAI_STATUS_SUCCESS) {
    auto msg = folly::to<std::string>(
        "[",
        saiApiTypeToString(apiType),
        "] ",
        folly::to<std::string>(std::forward<Args>(args)...),
        ": ",
        saiStatusToString(status));
    if (isFatal) {
      XLOG(FATAL) << msg;
    } else {
      XLOG(ERR) << msg;
    }
  }
}

template <typename... Args>
void _saiDoLogEveryMs(
    int ms,
    sai_status_t status,
    sai_api_t apiType,
    Args&&... args) {
  if (status != SAI_STATUS_SUCCESS) {
    auto msg = folly::to<std::string>(
        "[",
        saiApiTypeToString(apiType),
        "] ",
        folly::to<std::string>(std::forward<Args>(args)...),
        ": ",
        saiStatusToString(status));
    XLOG_EVERY_MS(ERR, ms) << msg;
  }
}

template <typename... Args>
void saiLogError(sai_status_t status, sai_api_t apiType, Args&&... args) {
  _saiDoLog(false, status, apiType, std::forward<Args>(args)...);
}

template <typename... Args>
void saiLogErrorEveryMs(
    int ms,
    sai_status_t status,
    sai_api_t apiType,
    Args&&... args) {
  _saiDoLogEveryMs(ms, status, apiType, std::forward<Args>(args)...);
}

template <typename... Args>
void saiLogFatal(sai_status_t status, sai_api_t apiType, Args&&... args) {
  _saiDoLog(true, status, apiType, std::forward<Args>(args)...);
}

template <typename... Args>
void saiApiCheckError(sai_status_t status, sai_api_t apiType, Args&&... args) {
  if (status != SAI_STATUS_SUCCESS) {
    saiLogError(status, apiType, (args)...);
    throw SaiApiError(status, apiType, std::forward<Args>(args)...);
  }
}

template <typename... Args>
void saiCheckError(sai_status_t status, Args&&... args) {
  saiApiCheckError(status, SAI_API_UNSPECIFIED, std::forward<Args>(args)...);
}

} // namespace facebook::fboss
