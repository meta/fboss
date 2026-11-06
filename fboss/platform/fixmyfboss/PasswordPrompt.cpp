/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/PasswordPrompt.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <csignal>

#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>

namespace facebook::fboss::platform::fixmyfboss {

std::optional<std::string> readPasswordFromTerminal(const std::string& prompt) {
  int fd = ::open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    return std::nullopt;
  }
  SCOPE_EXIT {
    ::close(fd);
  };
  termios original{};
  if (::tcgetattr(fd, &original) != 0) {
    return std::nullopt;
  }
  termios hidden = original;
  // ECHONL still echoes the final newline. Without ISIG, Ctrl-C arrives as a
  // character, so the terminal is restored before interrupting.
  hidden.c_lflag &= ~(ECHO | ISIG);
  hidden.c_lflag |= ECHONL;
  if (::tcsetattr(fd, TCSAFLUSH, &hidden) != 0) {
    return std::nullopt;
  }
  bool interrupted = false;
  std::optional<std::string> line;
  {
    SCOPE_EXIT {
      ::tcsetattr(fd, TCSAFLUSH, &original);
    };
    folly::writeFull(fd, prompt.data(), prompt.size());
    std::string input;
    char c = 0;
    while (folly::readNoInt(fd, &c, 1) == 1) {
      if (c == '\n') {
        line = std::move(input);
        break;
      }
      // A disabled interrupt character is _POSIX_VDISABLE, which is NUL.
      const cc_t intr = original.c_cc[VINTR];
      if (intr != _POSIX_VDISABLE && c == static_cast<char>(intr)) {
        interrupted = true;
        break;
      }
      input.push_back(c);
    }
  }
  if (interrupted) {
    folly::writeFull(fd, "\n", 1);
    std::raise(SIGINT);
  }
  return line;
}

} // namespace facebook::fboss::platform::fixmyfboss
