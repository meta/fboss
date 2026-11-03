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

#include <folly/IPAddress.h>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace facebook::fboss {
class RestClient {
 public:
  RestClient(std::string hostname, int port);
  RestClient(folly::IPAddress ipAddress, int port);
  RestClient(folly::IPAddress ipAddress, int port, std::string interface);
  virtual ~RestClient() = default;
  /*
   * Calls the particular Rest api
   */
  bool request(std::string path);
  virtual std::string requestWithOutput(
      std::string path,
      std::string postData = "");
  void setTimeout(std::chrono::milliseconds timeout);

  void setClientCertAndKey(std::string_view cert, std::string_view key);
  void setVerifyHostname(bool verify);
  /*
   * Pin the local socket to this source address, which must be link-local
   * and carry its zone, as in IPAddressV6("fe80::2%eth0.4088"). Needed when
   * the peer authorizes by source address and the interface holds more than
   * one link-local, since the kernel's pick is then arbitrary. Requires an
   * IPv6 destination. Throws FbossError otherwise.
   */
  void setSourceAddress(folly::IPAddressV6 source);

  /*
   * HTTP status of the most recent request. std::nullopt when it produced no
   * response, such as a refused connection or a timeout. Both of those throw,
   * so read this from the catch block.
   */
  std::optional<long> lastResponseCode() const {
    return lastResponseCode_;
  }

 private:
  // Forbidden copy contructor and assignment operator
  RestClient(RestClient const&) = delete;
  RestClient& operator=(RestClient const&) = delete;

  static size_t writer(
      char* buffer,
      size_t size,
      size_t entries,
      std::stringbuf* writer_buffer);
  void createEndpoint();
  std::string hostname_;
  folly::IPAddress ipAddress_;
  std::string interface_;
  int port_;
  std::chrono::milliseconds timeout_{1000};
  std::string endpoint_;
  std::string cert_, key_;
  bool verifyHostname_ = true;
  std::optional<folly::IPAddressV6> sourceAddress_;
  std::optional<long> lastResponseCode_;
};

} // namespace facebook::fboss
