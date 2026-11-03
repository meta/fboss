/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *  fboss_pai_diag_shell_client — interactive REPL client for qsfp_service's
 *  PAI diag shell on Broadcom Agera3 retimer platforms (e.g. Ladakh800bcls).
 *
 *  USAGE
 *      fboss_pai_diag_shell_client [--host=<ip-or-host>] [--port=5910]
 *
 *  WHAT IT DOES
 *      Connects to qsfp_service over thrift, opens a streaming PAI shell
 *      session via startPaiDiagShell(), forwards every keystroke from stdin
 *      to the server via producePaiDiagShellInput(), and prints the
 *      streamed shell output to stdout. The end result is a bcmsh-style
 *      interactive prompt for the Broadcom PAI library's diag shell.
 *
 *  DESIGN
 *      Mirrors fboss/agent/hw/sai/diag/DiagShellClient.cpp — the
 *      production-vetted pattern for the BCM NPU diag shell. Two threads:
 *        - "stream" thread: subscribes to the server-stream of shell output
 *          and writes each chunk to stdout.
 *        - "stdin"  thread: reads from the user's terminal and forwards to
 *          the server.
 *      A SIGINT/SIGTERM handler cleanly tears both threads down.
 *
 *      Two getPaiDiagStreamingClient() implementations live in oss/ and
 *      facebook/ — the OSS variant uses raw AsyncSocket; the FB-internal
 *      variant uses ServiceRouter with cert-aware fallback. Selection is
 *      via BUCK / cmake source list, no preprocessor switching.
 *
 *  EXIT
 *      The user can exit by:
 *        - typing 'quit' or 'exit' on a line by itself (intercepted client-
 *          side BEFORE forwarding to the server, since the PAI shell's
 *          own 'q'/'exit' would terminate the SaiRepl thread server-side)
 *        - sending EOF (Ctrl+D)
 *        - sending SIGINT (Ctrl+C) or SIGTERM
 */
#include "fboss/qsfp_service/diag/PaiDiagShellClient.h"

#include "fboss/agent/if/gen-cpp2/common_types.h"

#include <folly/FileUtil.h>
#include <folly/IPAddress.h>
#include <folly/Memory.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/init/Init.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/EventBase.h>

#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <thread>

DEFINE_string(
    host,
    "::1",
    "qsfp_service host (IPv4/IPv6 literal or hostname). Defaults to ::1 "
    "(localhost) for on-switch use.");
DEFINE_int32(port, 5910, "qsfp_service thrift port (default 5910).");
DEFINE_string(
    reason,
    "not_provided",
    "Reason for opening the diag shell. For prod environments, please "
    "provide a Task or SEV ID — this is logged server-side for audit.");
DEFINE_int32(
    stdin_poll_interval_sec,
    1,
    "How often the stdin thread checks for shutdown (in seconds). Lower "
    "values terminate faster on Ctrl+C; higher values use less CPU.");

namespace {
using ::facebook::fboss::ClientInformation;
using ::facebook::fboss::QsfpService;
using folly::AsyncSignalHandler;
using folly::ByteRange;
using folly::IPAddress;
using folly::IPAddressV6;

ClientInformation getClientInformation() {
  ClientInformation ci;
  if (const char* user = std::getenv("USER")) {
    ci.username() = user;
  }
  std::array<char, 256> hostname{};
  if (::gethostname(hostname.data(), hostname.size()) == 0) {
    ci.hostname() = hostname.data();
  }
  ci.reason() = FLAGS_reason;
  return ci;
}

// Resolve a hostname (or IP literal) to a folly::IPAddress.
IPAddress getIPFromHost(const std::string& hostname) {
  if (IPAddress::validate(hostname)) {
    return IPAddress(hostname);
  }
  struct addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  // NOLINTNEXTLINE(cpp-dns-deps): user supplied --host=<hostname>, we MUST
  // resolve it via DNS — there's no SMC/SeRF lookup that maps user-typed
  // hostnames to IPs. Same pattern as agent's DiagShellClient::getIPFromHost.
  int rv = ::getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
  SCOPE_EXIT {
    if (result) {
      ::freeaddrinfo(result);
    }
  };
  if (rv != 0) {
    LOG(ERROR) << "Could not resolve host '" << hostname
               << "': " << gai_strerror(rv);
    return IPAddress();
  }
  for (auto* res = result; res != nullptr; res = res->ai_next) {
    if (res->ai_addr->sa_family == AF_INET) {
      auto* sp = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
      return IPAddress::fromLong(sp->sin_addr.s_addr);
    }
    if (res->ai_addr->sa_family == AF_INET6) {
      auto* sp = reinterpret_cast<struct sockaddr_in6*>(res->ai_addr);
      return IPAddress::fromBinary(ByteRange(
          static_cast<unsigned char*>(&(sp->sin6_addr.s6_addr[0])),
          IPAddressV6::byteCount()));
    }
  }
  LOG(ERROR) << "No usable address for host '" << hostname << "'";
  return IPAddress();
}

// Stream thread: opens the PAI shell on the server, prints the initial
// prompt, then subscribes to the server-stream of shell output and writes
// each chunk to stdout.
void subscribeToPaiShell(folly::EventBase* evb, const IPAddress& ip) {
  std::unique_ptr<apache::thrift::Client<QsfpService>> client;
  try {
    client = facebook::fboss::getPaiDiagStreamingClient(evb, ip, FLAGS_port);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to construct streaming client: " << e.what();
    evb->terminateLoopSoon();
    return;
  }

  // sync_startPaiDiagShell() returns an internal thrift type
  // (ResponseAndServerStream) that's not stably namespaced. Use auto here
  // — same approach as agent's DiagShellClient.cpp.
  // We call the RPC in its own try/catch so we can graceful-return on
  // failure (instead of throwing out of the std::thread lambda, which would
  // call std::terminate()).
  decltype(client->sync_startPaiDiagShell()) responseAndStream;
  try {
    responseAndStream = client->sync_startPaiDiagShell();
  } catch (const std::exception& e) {
    LOG(ERROR) << "startPaiDiagShell RPC failed: " << e.what();
    evb->terminateLoopSoon();
    return;
  }

  // Print the initial prompt (e.g., "BRCM:10> ") so the user sees something
  // immediately on connect.
  folly::writeFull(
      STDOUT_FILENO,
      responseAndStream.response.c_str(),
      responseAndStream.response.size());

  std::move(responseAndStream.stream)
      .subscribeExTry(
          evb,
          [evb](auto&& t) {
            if (t.hasValue()) {
              const auto& shellOut = t.value();
              folly::writeFull(
                  STDOUT_FILENO, shellOut.c_str(), shellOut.size());
              // Note: do NOT use folly logging here — it touches terminal
              // mode and breaks backspace/Ctrl+D for the user.
            } else if (t.hasException()) {
              auto msg = folly::exceptionStr(std::move(t.exception()));
              LOG(ERROR) << "PAI shell stream error: " << msg;
              evb->terminateLoopSoon();
            } else {
              LOG(INFO) << "PAI shell stream completed.";
              evb->terminateLoopSoon();
            }
          })
      .detach();

  evb->loop();
}

// Stdin thread: reads keystrokes from the user's terminal and forwards
// each chunk to the server via producePaiDiagShellInput. Polls every
// stdin_poll_interval_sec so SIGINT/SIGTERM can wake us out of select().
void handleStdin(
    folly::EventBase* evb,
    const IPAddress& ip,
    const std::atomic<bool>* shouldStop) {
  std::unique_ptr<apache::thrift::Client<QsfpService>> client;
  try {
    client = facebook::fboss::getPaiDiagStreamingClient(evb, ip, FLAGS_port);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to construct stdin client: " << e.what();
    evb->terminateLoopSoon();
    return;
  }

  constexpr ssize_t kBufSize = 512;
  std::array<char, kBufSize> buf{};
  auto ci = getClientInformation();

  while (!shouldStop->load()) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(STDIN_FILENO, &readSet);
    struct timeval timeout{FLAGS_stdin_poll_interval_sec, 0};
    int sel = ::select(STDIN_FILENO + 1, &readSet, nullptr, nullptr, &timeout);
    if (sel == 0) {
      continue; // poll timeout — re-check shouldStop
    }
    if (sel < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG(ERROR) << "select(stdin) failed: " << folly::errnoStr(errno);
      break;
    }

    ssize_t nread = ::read(STDIN_FILENO, buf.data(), kBufSize);
    if (nread < 0) {
      LOG(ERROR) << "Failed to read from stdin: " << folly::errnoStr(errno);
      break;
    }
    if (nread == 0) {
      // EOF on stdin (Ctrl+D or pipe closed) — exit cleanly.
      break;
    }
    std::string input(buf.data(), nread);

    // Client-side convenience: typing "quit" or "exit" cleanly exits the
    // CLI. Intercept BEFORE forwarding — the PAI shell's own 'q'/'exit'
    // command would terminate the SaiRepl thread server-side, which we
    // don't want. EOF / Ctrl+C are also valid ways to exit.
    if (input.find("quit") == 0 || input.find("exit") == 0) {
      break;
    }

    try {
      client->sync_producePaiDiagShellInput(input, ci);
    } catch (const std::exception& e) {
      LOG(ERROR) << "producePaiDiagShellInput RPC failed: " << e.what();
      break;
    }
  }
  evb->terminateLoopSoon();
}

// Async-signal-safe handler that flips shouldStop and breaks the stream
// EventBase loop so both threads can join cleanly.
class SignalHandler : public AsyncSignalHandler {
 public:
  SignalHandler(folly::EventBase* eb, std::atomic<bool>* shouldStop)
      : AsyncSignalHandler(eb), shouldStop_(shouldStop) {
    registerSignalHandler(SIGINT);
    registerSignalHandler(SIGTERM);
  }
  void signalReceived(int /*signum*/) noexcept override {
    getEventBase()->terminateLoopSoon();
    shouldStop_->store(true);
  }

 private:
  std::atomic<bool>* shouldStop_;
};

} // namespace

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage(
      "fboss_pai_diag_shell_client — interactive PAI diag shell client.\n"
      "\n"
      "Connects to a qsfp_service running on a Broadcom Agera3 retimer\n"
      "platform (e.g. Ladakh800bcls) and gives you an interactive REPL\n"
      "into the PAI library's diag shell. Same UX as bcmsh for the BCM\n"
      "NPU; same commands as `anacapa_xphy_cli` and the on-chip PAI shell.\n"
      "\n"
      "Useful commands once connected:\n"
      "    ls                         List all retimer chip switch IDs\n"
      "    s <switch_id>              Hop chip context\n"
      "    l                          List ports on current chip\n"
      "    pd <port_id> <flags>       Per-lane diagnostic dump\n"
      "    polarity <map> <side>      Print polarity\n"
      "    polarity <map> <side> <tx> <rx>  Set polarity\n"
      "    fw                         Print firmware version\n"
      "    h | help                   Print full command menu\n"
      "    quit | exit                Exit this client (Ctrl+C also works)\n"
      "\n"
      "See `fboss_pai_diag_shell_client --help` for available flags.");

  gflags::ParseCommandLineNonHelpFlags(&argc, &argv, false);
  if (FLAGS_help) {
    gflags::ShowUsageWithFlagsRestrict(
        gflags::ProgramInvocationShortName(), "PaiDiagShellClient");
    return 0;
  }
  const folly::Init init(&argc, &argv);

  folly::EventBase streamEvb;
  folly::EventBase stdinEvb;
  std::atomic<bool> stopThread{false};

  IPAddress hostIp = getIPFromHost(FLAGS_host);
  if (hostIp.empty()) {
    LOG(ERROR) << "No usable host given to connect.";
    return 1;
  }

  LOG(INFO) << "Connecting to qsfp_service at " << hostIp << ":" << FLAGS_port;

  std::thread streamThread(
      [&streamEvb, &hostIp]() { subscribeToPaiShell(&streamEvb, hostIp); });

  std::thread stdinThread([&stdinEvb, &hostIp, &stopThread]() {
    handleStdin(&stdinEvb, hostIp, &stopThread);
  });

  // The constructor side-effect (registerSignalHandler for SIGINT/SIGTERM)
  // is what's actually wanted here — Infer's DEAD_STORE warning is a false
  // positive. Same pattern as agent's DiagShellClient::main.
  // NOLINTNEXTLINE(facebook-hte-LocalUncheckedArrayBounds)
  [[maybe_unused]] SignalHandler signals(&streamEvb, &stopThread);

  // Wait for stdin thread to finish (user typed quit / hit Ctrl+D / signal).
  stdinThread.join();
  // Then stop the stream loop and wait for it to drain.
  streamEvb.terminateLoopSoon();
  streamThread.join();

  // Use exit(0) instead of return 0 — same trick as agent's diag_shell_client.
  // The thrift client/connection destructors can hang waiting for the loop;
  // exit() short-circuits that so the user doesn't need multiple Ctrl+Cs.
  std::exit(0);
}
