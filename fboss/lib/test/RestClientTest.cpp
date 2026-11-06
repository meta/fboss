/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/RestClient.h"

#include <folly/IPAddressV6.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
namespace {
constexpr int kPort = 8080;
} // namespace

TEST(RestClientTest, SourceAddressRejectsNonLinkLocal) {
  RestClient client(folly::IPAddress("::1"), kPort);
  EXPECT_THROW(
      client.setSourceAddress(folly::IPAddressV6("2401:db00::1")), FbossError);
}

TEST(RestClientTest, SourceAddressRejectsMissingZone) {
  RestClient client(folly::IPAddress("::1"), kPort);
  // Without a %zone getaddrinfo leaves sin6_scope_id at 0, which cannot
  // identify a link, and bind() would fail EINVAL at request time.
  EXPECT_THROW(
      client.setSourceAddress(folly::IPAddressV6("fe80::2")), FbossError);
}

TEST(RestClientTest, SourceAddressRejectsV4Destination) {
  RestClient client(folly::IPAddress("127.0.0.1"), kPort);
  try {
    client.setSourceAddress(folly::IPAddressV6("fe80::2%lo"));
    FAIL() << "expected FbossError for an IPv4 destination";
  } catch (const FbossError& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("IPv6 destination"));
  }
}

TEST(RestClientTest, SourceAddressAcceptsZonedLinkLocal) {
  RestClient client(folly::IPAddress("::1"), kPort);
  // lo is the only interface guaranteed to exist in a sandboxed test.
  EXPECT_NO_THROW(client.setSourceAddress(folly::IPAddressV6("fe80::2%lo")));
}

TEST(RestClientTest, ReportsNoResponseCodeBeforeAnyRequest) {
  RestClient client(folly::IPAddress("::1"), kPort);
  // nullopt is the "never got a response" signal callers branch on, so it
  // has to be the starting state rather than an arbitrary status.
  EXPECT_EQ(client.lastResponseCode(), std::nullopt);
}

} // namespace facebook::fboss
