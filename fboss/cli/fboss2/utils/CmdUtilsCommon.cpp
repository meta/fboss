/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#ifndef IS_OSS
#include "common/time/TimeUtil.h"
#endif

#include <folly/String.h>
#include <folly/logging/LogConfig.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/xlog.h>
#include <folly/stop_watch.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using folly::ByteRange;
using folly::IPAddress;
using folly::IPAddressV6;

namespace facebook::fboss::utils {

std::pair<std::string, folly::IPAddress> getCanonicalNameAndIPFromHost(
    const std::string& hostname) {
  std::string canonicalHostname = hostname;

  if (IPAddress::validate(hostname)) {
    return std::make_pair(canonicalHostname, IPAddress(hostname));
  }
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_CANONNAME;
  struct addrinfo* info_orig = nullptr;
  auto rv = getaddrinfo(hostname.c_str(), nullptr, &hints, &info_orig);

  SCOPE_EXIT {
    freeaddrinfo(info_orig);
  };
  if (rv < 0) {
    XLOG(ERR) << "Could not get an IP for: " << hostname;
    return std::make_pair(canonicalHostname, IPAddress());
  }

  struct addrinfo* info = info_orig;
  if (info != nullptr) {
    canonicalHostname = info->ai_canonname;
  }

  for (; info != nullptr; info = info->ai_next) {
    if (info->ai_addr->sa_family == AF_INET) { // IPV4
      struct sockaddr_in* sp = (struct sockaddr_in*)info->ai_addr;
      return std::make_pair(
          canonicalHostname, IPAddress::fromLong(sp->sin_addr.s_addr));
    } else if (info->ai_addr->sa_family == AF_INET6) { // IPV6
      struct sockaddr_in6* sp = (struct sockaddr_in6*)info->ai_addr;
      return std::make_pair(
          canonicalHostname,
          IPAddress::fromBinary(ByteRange(
              static_cast<unsigned char*>(&(sp->sin6_addr.s6_addr[0])),
              IPAddressV6::byteCount())));
    }
  }
  XLOG(ERR) << "Could not get an IP for: " << hostname;
  return std::make_pair(canonicalHostname, IPAddress());
}

std::vector<std::string> getHostsFromFile(const std::string& filename) {
  std::vector<std::string> hosts;
  std::ifstream in(filename);
  std::string str;

  while (std::getline(in, str)) {
    hosts.push_back(str);
  }

  return hosts;
}

void setLogLevel(const std::string& logLevelStr) {
  if (logLevelStr == "DBG0") {
    return;
  }

  auto logLevel = folly::stringToLogLevel(logLevelStr);

  auto logConfig = folly::LoggerDB::get().getConfig();
  auto& categoryMap = logConfig.getCategoryConfigs();

  for (auto& p : categoryMap) {
    auto category = folly::LoggerDB::get().getCategory(p.first);
    if (category != nullptr) {
      folly::LoggerDB::get().setLevel(category, logLevel);
    }
  }

  XLOG(DBG1) << "Setting loglevel to " << logLevelStr;
}

// Converts a readable representation of a link-bandwidth value
//
// bandwidthBytesPerSecond: must be positive number in bytes per second
const std::string formatBandwidth(const float bandwidthBytesPerSecond) {
  if (bandwidthBytesPerSecond < 1.0f) {
    return "N/A";
  }
  const std::string suffixes[] = {"", "K", "M"};
  // Represent the bandwidth in bits per second
  // Use long and floor to ensure that we have integer to start with
  long bandwidthBitsPerSecond = std::floor(bandwidthBytesPerSecond) * 8;
  for (const auto& suffix : suffixes) {
    if (bandwidthBitsPerSecond < 1000) {
      return folly::to<std::string>(bandwidthBitsPerSecond) + suffix + "bps";
    }
    // we don't round up
    bandwidthBitsPerSecond /= 1000;
  }
  return folly::to<std::string>(bandwidthBitsPerSecond) + "Gbps";
}

// Get an starting epoch based on a period of time (duration).
// We achieve this by substracting the period of time to the
// current epoch from system_clock::now()
//
// duration: must be a positive value in milliseconds
// return: epoch in seconds
long getEpochFromDuration(const int64_t& duration) {
  std::chrono::milliseconds elapsedMs(duration);
  std::chrono::seconds elapsedSeconds =
      std::chrono::duration_cast<std::chrono::seconds>(elapsedMs);

  // Get the current epoch in seconds.
  const auto currentEpoch = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch());

  return currentEpoch.count() - elapsedSeconds.count();
}

const std::string getDurationStr(folly::stop_watch<>& watch) {
  auto duration = watch.elapsed();
  auto durationInMills =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  float seconds =
      static_cast<float>(durationInMills.count()) / static_cast<float>(1000);
  return fmt::format("{:.3f} sec", seconds);
}

const std::string getPrettyElapsedTime(const int64_t& start_time) {
  /* borrowed from "pretty_timedelta function" in fb_toolkit
     takes an epoch time and returns a pretty string of elapsed time
  */
  auto startTime = std::chrono::seconds(start_time);
  auto time_now = std::chrono::system_clock::now();
  auto elapsed_time = time_now - startTime;

  time_t elapsed_convert = std::chrono::system_clock::to_time_t(elapsed_time);

  int64_t days = 0, hours = 0, minutes = 0;

  days = int64_t(elapsed_convert / 86400);

  int64_t leftover = elapsed_convert % 86400;

  hours = leftover / 3600;
  leftover %= 3600;

  minutes = leftover / 60;
  leftover %= 60;

  std::string pretty_output;
  if (days != 0) {
    pretty_output += std::to_string(days) + "d ";
  }
  pretty_output += std::to_string(hours) + "h ";
  pretty_output += std::to_string(minutes) + "m ";
  pretty_output += std::to_string(leftover) + "s";

  return pretty_output;
}

// Separates fractional seconds from time to parse
// Eg: timer = 6475 | Return: {seconds = 6, fractional_seconds = 475}
timeval splitFractionalSecondsFromTimer(const long& timer) {
  struct timeval tv;
  tv.tv_sec = timer / 1000; // get total amount of seconds
  tv.tv_usec = (timer % 1000); // get fractional milliseconds
  return tv;
}

const std::string parseTimeToTimeStamp(const long& timeToParse) {
  const auto splitTime = utils::splitFractionalSecondsFromTimer(timeToParse);
  std::string formattedTime; // Timestamp storage
#ifndef IS_OSS
  constexpr std::string_view kTimeFormat = "%Y-%m-%d %H:%M:%S %Z";
  stringFormatTimestamp(splitTime.tv_sec, kTimeFormat.data(), &formattedTime);
#else
  // OSS: Use std::put_time instead of Meta-internal stringFormatTimestamp
  std::tm tm_info;
  localtime_r(&splitTime.tv_sec, &tm_info);
  std::ostringstream oss;
  oss << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S %Z");
  formattedTime = oss.str();
#endif

  // stringFormatTimestamp returns a formatted time like the following:
  //      2021-10-27 00:00:06 PDT
  //
  // In order to add the fractional seconds, we find the last space before the
  // timestamp's timezone and concatenate the fractional seconds.
  //      2021-10-27 00:00:06 PDT -> 2021-10-27 00:00:06.475 PDT
  int index = formattedTime.find_last_of(' ');
  return fmt::format(
      "{}.{} {}",
      formattedTime.substr(0, index), // Date and time
      splitTime.tv_usec, // fractional seconds
      formattedTime.substr(index + 1)); // timezone
}

std::string getUserInfo() {
  std::vector<std::string> userInfo;
  auto fbidStr = getenv("FB_UID");
  if (fbidStr && strcmp(fbidStr, "") != 0) {
    userInfo.emplace_back(fbidStr);
  }
  auto envuser = getenv("USER");
  if (envuser && strcmp(envuser, "") != 0) {
    userInfo.emplace_back(envuser);
  }
  return folly::join<std::string, std::vector<std::string>>(" | ", userInfo);
}

std::string getUnixname() {
  auto envuser = getenv("USER");
  if (strcmp(envuser, "root") != 0 && strcmp(envuser, "netops") != 0) {
    return envuser;
  }
  // Last resort get unixname from user input
  std::string user_from_input;
  std::cout << "Please enter your unixname: ";
  std::cin >> user_from_input;
  return user_from_input;
}

} // namespace facebook::fboss::utils
