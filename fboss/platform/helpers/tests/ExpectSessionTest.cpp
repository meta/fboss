// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/helpers/ExpectSession.h"

#include <csignal>

#include <gtest/gtest.h>

using namespace facebook::fboss::platform;

namespace {
constexpr std::chrono::seconds kTimeout{10};
} // namespace

TEST(ExpectSessionTest, ExpectAndSend) {
  ExpectSession session("read line; echo got:$line");

  session.sendLine("hello");

  EXPECT_TRUE(session.expect("got:hello", kTimeout));
}

TEST(ExpectSessionTest, ReadExactlyReturnsRequestedBytes) {
  ExpectSession session("printf 'header:abcdef'; sleep 5");
  ASSERT_TRUE(session.expect("header:", kTimeout));

  EXPECT_EQ(session.readExactly(4, kTimeout), "abcd");
  EXPECT_EQ(session.readExactly(2, kTimeout), "ef");
}

TEST(ExpectSessionTest, ReadExactlyTimesOut) {
  ExpectSession session("printf 'ab'; sleep 5");

  EXPECT_EQ(
      session.readExactly(3, std::chrono::milliseconds(500)), std::nullopt);
}

TEST(ExpectSessionTest, NoticesWhenChildExits) {
  ExpectSession session("printf done");
  ASSERT_TRUE(session.expect("done", kTimeout));

  EXPECT_FALSE(session.expect("more", kTimeout));
  EXPECT_TRUE(session.isEof());
  EXPECT_FALSE(session.isAlive());
}

TEST(ExpectSessionTest, ChildReapedElsewhereIsNotAlive) {
  // With SIGCHLD ignored, the kernel reaps children itself, so waitpid() in
  // isAlive() fails with ECHILD.
  auto previous = std::signal(SIGCHLD, SIG_IGN);
  {
    ExpectSession session("true");
    ASSERT_FALSE(session.expect("never printed", kTimeout));

    EXPECT_FALSE(session.isAlive());
  }
  std::signal(SIGCHLD, previous);
}
