/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/hw/sai/switch/gen-cpp2/SaiCtrlAsyncClient.h"

#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/init/Init.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/EventBase.h>
#include "fboss/agent/hw/sai/diag/DiagShellClient.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <csignal>
#include <thread>

DEFINE_string(host, "::1", "The host to connect to");
DEFINE_int32(port, 5931, "The port to connect to");
DEFINE_string(
    reason,
    "not_provided",
    "Reason to run the diag shell client. For Prod environment, please provide Task or SEV ID.");

namespace {
using folly::AsyncSignalHandler;
using folly::ByteRange;
using folly::IPAddress;
using folly::IPAddressV6;

facebook::fboss::ClientInformation getClientInformation() {
  facebook::fboss::ClientInformation ci;
  const char* username = std::getenv("USER");
  if (username) {
    ci.username() = username;
  }
  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) == 0) {
    ci.hostname() = hostname;
  }
  ci.reason() = FLAGS_reason;
  return ci;
}

const IPAddress getIPFromHost(const std::string& hostname) {
  if (IPAddress::validate(hostname)) {
    return IPAddress(hostname);
  }
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  auto rv = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
  SCOPE_EXIT {
    LOG(INFO) << "Finished getting IP for " << hostname;
    freeaddrinfo(result);
  };
  if (rv < 0) {
    LOG(ERROR) << "Could not get an IP for " << hostname;
    return IPAddress();
  }
  struct addrinfo* res;
  for (res = result; res != nullptr; res = res->ai_next) {
    if (res->ai_addr->sa_family == AF_INET) { // IPV4
      struct sockaddr_in* sp = (struct sockaddr_in*)res->ai_addr;
      return IPAddress::fromLong(sp->sin_addr.s_addr);
    } else if (res->ai_addr->sa_family == AF_INET6) { // IPV6
      struct sockaddr_in6* sp = (struct sockaddr_in6*)res->ai_addr;
      return IPAddress::fromBinary(ByteRange(
          static_cast<unsigned char*>(&(sp->sin6_addr.s6_addr[0])),
          IPAddressV6::byteCount()));
    }
  }
  LOG(ERROR) << "Could not get an IP for " << hostname;
  return IPAddress();
}

void subscribeToDiagShell(
    folly::EventBase* evb,
    const IPAddress& ip,
    std::atomic<bool>* shouldStop,
    std::atomic<bool>* streamLost) {
  auto client = utility::getStreamingClient(evb, ip, FLAGS_port);
  auto responseAndStream = client->sync_startDiagShell();
  folly::writeFull(
      STDOUT_FILENO,
      responseAndStream.response.c_str(),
      responseAndStream.response.size());
  std::move(responseAndStream.stream)
      .subscribeExTry(
          evb,
          [evb, shouldStop, streamLost](auto&& t) {
            if (t.hasValue()) {
              const auto& shellOut = t.value();
              folly::writeFull(
                  STDOUT_FILENO, shellOut.c_str(), shellOut.size());
              return;
            }
            if (shouldStop->load()) {
              // User quit / Ctrl-C: the stream ending is expected.
              evb->terminateLoopSoon();
              return;
            }
            // Server closed the stream (agent restart/shutdown, idle
            // timeout): stop the stdin loop so the user gets their shell
            // back instead of a REPL that errors on every keystroke.
            // N.B., can't use folly logging, because it messes with the
            // terminal mode such that backspace, ctrl+D, etc.. don't seem
            // to work anymore.
            if (t.hasException()) {
              LOG(ERROR) << "diag shell session ended: "
                         << folly::exceptionStr(std::move(t.exception()));
            } else {
              LOG(INFO) << "diag shell session ended: stream completed";
            }
            *streamLost = true;
            *shouldStop = true;
            evb->terminateLoopSoon();
          })
      .detach();
  evb->loop();
}

void handleStdin(
    folly::EventBase* evb,
    const IPAddress& ip,
    std::atomic<bool>* shouldStop) {
  auto client = utility::getStreamingClient(evb, ip, FLAGS_port);
  ssize_t nread = 0;
  constexpr ssize_t bufSize = 512;
  std::array<char, bufSize> buf;
  auto ci = getClientInformation();
  while (true) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(STDIN_FILENO, &readSet);
    // Checks every second if we should stop the process.
    struct timeval timeout = {1, 0};
    if (select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &timeout) > 0) {
      if ((nread = ::read(STDIN_FILENO, buf.data(), bufSize)) < 0) {
        folly::throwSystemError("failed to read from stdin");
      } else if (nread == 0 || *shouldStop) {
        *shouldStop = true; // stdin EOF (Ctrl-D)
        break;
      } else {
        std::string input(buf.data(), nread);
        try {
          client->sync_produceDiagShellInput(input, ci);
        } catch (const std::exception& e) {
          LOG(ERROR) << "cli caught server exception " << e.what();
        }
        // When user enters quit, finish the stdin thread
        if (input.find("quit") == 0 || *shouldStop) {
          *shouldStop = true;
          break;
        }
      }
    }
    if (*shouldStop) {
      break;
    }
  }
  evb->terminateLoopSoon();
}

class SignalHandler : public AsyncSignalHandler {
 public:
  SignalHandler(folly::EventBase* eventBase, std::atomic<bool>* shouldStop)
      : AsyncSignalHandler(eventBase), shouldStop_(shouldStop) {
    registerSignalHandler(SIGINT);
    registerSignalHandler(SIGTERM);
  }
  void signalReceived(int /*signum*/) noexcept override {
    getEventBase()->terminateLoopSoon();
    *shouldStop_ = true;
  }

 private:
  std::atomic<bool>* shouldStop_;
};

} // namespace

/*
 * Connect to the FBOSS agent SAI streaming thrift service,
 *
 * start a diag shell session to get a thrift stream; handle new stream data by
 * printing it.
 *
 * start a thread reading standard input and sending it to the FBOSS agent
 */

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage("Diagnostic Shell Client for FBOSS agent");
  gflags::ParseCommandLineNonHelpFlags(&argc, &argv, false);
  if (FLAGS_help) {
    const char* shortName = gflags::ProgramInvocationShortName();
    gflags::ShowUsageWithFlagsRestrict(shortName, "DiagShellClient");
    return 0;
  }
  const folly::Init init(&argc, &argv);

  facebook::fboss::FbossEventBase streamEvb{"DiagShellClientStreamEventBase"};
  facebook::fboss::FbossEventBase stdinEvb{"DiagShellClientStdinEventBase"};

  std::atomic<bool> stopThread{false};
  // Set when the server ended the session (vs. user quit / Ctrl-C).
  std::atomic<bool> streamLost{false};
  // Converts the host to IP address if a hostname is given
  IPAddress hostIP = getIPFromHost(FLAGS_host);
  // No host given
  if (hostIP.empty()) {
    LOG(ERROR) << "No host given to connect";
    exit(1);
  }

  LOG(INFO) << "Connecting to: " << hostIP;
  std::thread streamT([&streamEvb, &hostIP, &stopThread, &streamLost]() {
    subscribeToDiagShell(&streamEvb, hostIP, &stopThread, &streamLost);
  });

  std::thread readStdinT([&stdinEvb, &hostIP, &stopThread]() {
    handleStdin(&stdinEvb, hostIP, &stopThread);
  });

  SignalHandler signalHandler(&streamEvb, &stopThread);

  readStdinT.join();
  streamEvb.terminateLoopSoon();
  streamT.join();
  // call exit() otherwise EventBase destructor will be hanging due to active
  // client/connection. Without this, we need multiple Ctrl+C to terminate
  exit(streamLost ? 1 : 0);
}
