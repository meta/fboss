// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/helpers/ExpectSession.h"

#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <stdexcept>

#include <pty.h>

namespace facebook::fboss::platform {

ExpectSession::ExpectSession(const std::string& command) {
  childPid_ = forkpty(&masterFd_, nullptr, nullptr, nullptr);
  if (childPid_ < 0) {
    throw std::runtime_error("forkpty() failed");
  }
  if (childPid_ == 0) {
    // Child process: exec the command via shell
    execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
    _exit(127);
  }
}

ExpectSession::~ExpectSession() {
  if (masterFd_ >= 0) {
    close(masterFd_);
  }
  // isAlive() may already have reaped the child.
  if (childPid_ > 0) {
    kill(childPid_, SIGTERM);
    int status;
    waitpid(childPid_, &status, 0);
  }
}

bool ExpectSession::expect(
    const std::string& pattern,
    std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    auto pos = buffer_.find(pattern);
    if (pos != std::string::npos) {
      lastOutput_ = buffer_.substr(0, pos + pattern.size());
      buffer_.erase(0, pos + pattern.size());
      return true;
    }

    if (eof_) {
      break;
    }

    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      break;
    }
    readAvailable(remaining);
  }
  return false;
}

void ExpectSession::sendLine(const std::string& cmd) {
  send(cmd);
  send("\n");
}

void ExpectSession::send(const std::string& data) {
  if (masterFd_ < 0) {
    throw std::runtime_error("ExpectSession: not connected");
  }
  size_t written = 0;
  while (written < data.size()) {
    auto n = write(masterFd_, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("ExpectSession: write failed");
    }
    written += n;
  }
}

std::string ExpectSession::getOutput() const {
  return lastOutput_;
}

std::optional<std::string> ExpectSession::readExactly(
    size_t count,
    std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (buffer_.size() < count) {
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (eof_ || remaining.count() <= 0) {
      return std::nullopt;
    }
    readAvailable(remaining);
  }
  std::string data = buffer_.substr(0, count);
  buffer_.erase(0, count);
  return data;
}

bool ExpectSession::isEof() const {
  return eof_;
}

bool ExpectSession::isAlive() {
  if (childPid_ <= 0) {
    return false;
  }
  // kill(pid, 0) succeeds for a zombie, so reap instead. ECHILD means the
  // child is gone too (e.g. reaped elsewhere, or SIGCHLD is ignored).
  int status;
  auto reaped = waitpid(childPid_, &status, WNOHANG);
  if (reaped == childPid_ || (reaped < 0 && errno != EINTR)) {
    childPid_ = -1;
    return false;
  }
  return true;
}

void ExpectSession::readAvailable(std::chrono::milliseconds timeout) {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(masterFd_, &readfds);

  struct timeval tv{};
  tv.tv_sec = timeout.count() / 1000;
  tv.tv_usec = (timeout.count() % 1000) * 1000;

  int ret = select(masterFd_ + 1, &readfds, nullptr, nullptr, &tv);
  if (ret > 0 && FD_ISSET(masterFd_, &readfds)) {
    char buf[4096];
    auto n = read(masterFd_, buf, sizeof(buf));
    if (n > 0) {
      buffer_.append(buf, n);
    } else if (n == 0 || (n < 0 && errno != EINTR)) {
      // Linux reports a closed terminal as EIO rather than end of file.
      eof_ = true;
    }
  }
}

} // namespace facebook::fboss::platform
