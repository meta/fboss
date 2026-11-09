/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/diag/DiagShell.h"
#include "fboss/agent/hw/sai/diag/PythonRepl.h"
#include "fboss/agent/hw/sai/diag/SaiRepl.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/tracer/SaiTracer.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <thread>

// The python_repl library is not built against Leaba SDKs >= 24.8.3001, whose
// Python bindings still require Python 3.10. Keep every PythonRepl call site
// out of those builds so they do not link against the missing library.
#if !defined(TAJO_SDK) || defined(TAJO_SDK_VERSION_1_42_8)
#define FBOSS_HAS_PYTHON_REPL
#endif

DEFINE_bool(
    sai_log_to_scribe,
    true,
    "Log SAI shell commands and output to scribe");

DEFINE_int32(
    diag_shell_read_timeout_ms,
    2000,
    "Timeout for reading output of diag shell");

namespace facebook::fboss {

namespace detail {

// Helper functions for creating a PTY
folly::File posixOpenpt() {
  int fd = ::posix_openpt(O_RDWR);
  folly::checkUnixError(fd, "Failed to open a new PTY");
  return folly::File(fd, true /* ownsFd */);
}

void grantpt(const folly::File& file) {
  int ret = ::grantpt(file.fd());
  folly::checkUnixError(ret, "Failed to grant access to PTY slave");
}

void unlockpt(const folly::File& file) {
  int ret = ::unlockpt(file.fd());
  folly::checkUnixError(ret, "Failed to unlock PTY slave");
}

std::string ptsnameR(const folly::File& file) {
  std::string name;
  std::array<char, 50> buf;
  int ret = ::ptsname_r(file.fd(), buf.data(), 50);
  folly::checkUnixError(ret, "Failed to get PTY name");
  name = buf.data();
  return name;
}

folly::File dup2(const folly::File& oldFile, const folly::File& newFile) {
  int fd = ::dup2(oldFile.fd(), newFile.fd());
  folly::checkUnixError(
      fd, "Failed to dup2(", oldFile.fd(), ", ", newFile.fd(), ")");
  return folly::File(fd, false /* ownsFd */);
}

PtyMaster::PtyMaster() {
  file = posixOpenpt();
  grantpt(file);
  unlockpt(file);
  name = ptsnameR(file);
}

PtySlave::PtySlave(const PtyMaster& ptyMaster)
    : file(ptyMaster.name.c_str(), O_RDWR) {}

TerminalSession::TerminalSession(
    const PtySlave& ptySlave,
    const std::vector<folly::File>& streams) {
  for (const auto& stream : streams) {
    XLOG(DBG2) << "Redirect stream to PTY slave: " << stream.fd();
    // Save old stream (OWNING File objects!)
    fd2oldStreams_.insert({stream.fd(), stream.dup()});
    // Set the pty slave as the stream
    dup2(ptySlave.file, stream);
  }
}

TerminalSession::~TerminalSession() noexcept {
  try {
    // wait 100ms for additional diag output, so as to avoid
    // potential offending write() during dup2() that might
    // cause pty fd got stuck
    /* sleep override */
    usleep(100 * 1000);
    for (auto& fd2stream : fd2oldStreams_) {
      // Restore the old streams
      dup2(fd2stream.second, folly::File(fd2stream.first));
      XLOG(DBG2) << "Restore stream to standard std fd " << fd2stream.first;
    }
  } catch (const std::system_error& e) {
    XLOG(ERR) << "Failed to reset terminal parameters: " << e.what();
  }
}

DiagShellClientState::DiagShellClientState(
    const std::string& clientAddrAndPort,
    apache::thrift::ServerStreamPublisher<std::string>&& publisher)
    : clientAddrAndPort_(clientAddrAndPort), publisher_(std::move(publisher)) {}

void DiagShellClientState::publishOutput(const std::string& output) {
  publisher_.next(output);
}

void DiagShellClientState::completeStream() {
  std::move(publisher_).complete();
  XLOG(DBG2) << "Completed Stream on " << clientAddrAndPort_;
}
} // namespace detail

DiagShell::DiagShell(const SaiSwitch* hw) : hw_(hw) {
  // Set up PTY
  ptym_ = std::make_unique<detail::PtyMaster>();
  ptys_ = std::make_unique<detail::PtySlave>(*ptym_);
  diagShellLock_ = std::unique_lock(diagShellMutex_, std::defer_lock);
}

std::unique_ptr<Repl> DiagShell::makeRepl() const {
  switch (hw_->getPlatform()->getAsic()->getAsicVendor()) {
    case HwAsic::AsicVendor::ASIC_VENDOR_BCM:
      return std::make_unique<SaiRepl>(hw_->getSaiSwitchId());
    case HwAsic::AsicVendor::ASIC_VENDOR_TAJO:
#if defined(TAJO_SDK_VERSION_1_42_8)
      return std::make_unique<PythonRepl>(ptys_->file.fd());
#else
      throw FbossError(
          "The Python REPL shell is no longer available through diag_shell "
          "for this SDK version. Please use the fboss_leaba_shell tool instead.");
#endif
    case HwAsic::AsicVendor::ASIC_VENDOR_CHENAB:
#if defined(FBOSS_HAS_PYTHON_REPL)
      return std::make_unique<PythonRepl>(ptys_->file.fd());
#else
      throw FbossError("The Python REPL shell is not built for this SDK");
#endif
    default:
      throw FbossError("Shell not supported for fake platforms");
  }
  CHECK(0) << " Should never get here";
  return nullptr;
}

void DiagShell::initTerminal() {
  if (!repl_) {
    // Set up REPL on connect if needed
    repl_ = makeRepl();

    // Set up terminal to raw mode
    // For python repl, py_main will override the setting so not actually needed
    struct termios oldSettings{};
    int ret = ::tcgetattr(ptys_->file.fd(), &oldSettings);
    folly::checkUnixError(ret, "Failed to get current terminal settings");
    auto newSettings = oldSettings;
    ::cfmakeraw(&newSettings);
    ret = ::tcsetattr(ptys_->file.fd(), TCSANOW, &newSettings);
    folly::checkUnixError(
        ret, "Failed to set new terminal settings on PTY slave");

    ts_.reset(new detail::TerminalSession(*ptys_, repl_->getStreams()));
    repl_->run();
  } else if (!ts_) {
    ts_.reset(new detail::TerminalSession(*ptys_, repl_->getStreams()));
  }
}

int DiagShell::getPtymFd() const {
  /* Helper function to return the file descriptor for pty master */
  return ptym_->file.fd();
}

bool DiagShell::tryConnect() {
  /* Checks if there are no other clients connected.
   * Once this function runs, it will obtain ownership of a lock
   * and block other clients from connecting
   */
  try {
    if (diagShellLock_.try_lock()) {
      try {
        initTerminal();
      } catch (...) {
        diagShellLock_.unlock();
        throw;
      }
      return true;
    }
  } catch (const std::system_error&) {
    LOG(WARNING) << "Another diag shell client already connected";
  }
  throw FbossError(
      "Unable to acquire diag shell lock, client already connected");
}

void DiagShell::disconnect() {
  try {
    // Reset terminal session to restore stdin/stdout
    ts_.reset();
    diagShellLock_.unlock();
  } catch (const std::system_error&) {
    XLOG(WARNING) << "Trying to disconnect when it was never connected";
  }
}

// TODO: Customize
std::string DiagShell::getPrompt() const {
  return repl_->getPrompt();
}

void DiagShell::consumeInput(
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> client) {
  if (FLAGS_sai_log_to_scribe) {
    logToScuba(std::move(client), *input);
  }
  if (!repl_) {
    return;
  }
  std::size_t ret =
      folly::writeFull(getPtymFd(), input->c_str(), input->size() + 1);
  folly::checkUnixError(ret, "Failed to write diag shell input to PTY master");
  if (*input == "quit") {
    // TODO: block until repl loop is completed
  }
}

void DiagShell::logCommandToReplayer(const std::string& command) {
  // Skip logging shell initialization command "\r\n"
  if (command == "\r\n") {
    return;
  }

  // Log the shell command as a comment in the SAI replayer log
  if (auto tracer = SaiTracer::getInstance()) {
    tracer->logShellCommand(command);
  } else {
    XLOG(ERR) << "DiagShell: SaiTracer instance not found!";
  }
}

std::string DiagShell::readOutput(int timeoutMs) {
  auto fd = getPtymFd();
  std::string output;
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(fd, &readSet);
  /* Set the timeout for each read.
   * This is to check that a client has disconnected in the meantime.
   * If the client is still connected, will continue to wait.
   * If the client has disconnected, will clean up the client states.
   */
  struct timeval timeout = {0, timeoutMs * 1000};
  // If timeout is < 0, have no timeout and block indefinitely
  timeval* timeoutPtr = (timeoutMs >= 0) ? &timeout : nullptr;
  if (select(fd + 1, &readSet, nullptr, nullptr, timeoutPtr) > 0) {
    if (!FD_ISSET(fd, &readSet)) {
      std::string msg = "Invalid file descriptor for the PTY";
      XLOG(WARNING) << msg;
      throw FbossError(msg);
    }
    int nread = ::read(fd, producerBuffer_.data(), producerBuffer_.size());
    folly::checkUnixError(nread, "Failed to read from the pty master");
    if (nread == 0) {
      std::string msg = "read 0 bytes from PTY slave;";
      XLOG(WARNING) << msg;
      throw FbossError(msg);
    }
    output += std::string(producerBuffer_.data(), nread);
  }
  return output;
}

StreamingDiagShellServer::StreamingDiagShellServer(const SaiSwitch* hw)
    : DiagShell(hw) {}

void StreamingDiagShellServer::setPublisher(
    apache::thrift::ServerStreamPublisher<std::string>&& publisher) {
  publisher_ =
      std::make_unique<apache::thrift::ServerStreamPublisher<std::string>>(
          std::move(publisher));
}

std::string StreamingDiagShellServer::start(
    apache::thrift::ServerStreamPublisher<std::string>&& publisher) {
  assert(diagShellLock_.owns_lock());
  setPublisher(std::move(publisher));
  // Create a stream producer thread
  producerThread_.reset(new std::thread([this]() { streamOutput(); }));
  return getPrompt();
}

void StreamingDiagShellServer::markResetPublisher() {
  XLOG(DBG2) << "Marked to reset diag shell client states";
  shouldResetPublisher_ = true;
}

void StreamingDiagShellServer::consumeInput(
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> client) {
  auto locked = publisher_.lock();
  if (!*locked) {
    std::string msg = "No diag shell connected!";
    XLOG(WARNING) << msg;
    throw FbossError(msg);
  }
  DiagShell::consumeInput(std::move(input), std::move(client));
}

void StreamingDiagShellServer::resetPublisher() {
  XLOG(DBG2) << "Resetting diag shell client state";
  auto locked = publisher_.lock();
  if (!shouldResetPublisher_) {
    return;
  }
  if (*locked) {
    std::move(**locked).complete();
  }
  // Detaching the producer thread, since it should already be completed
  if (producerThread_) {
    producerThread_->detach();
  }
  shouldResetPublisher_ = false;
  locked->reset();
  disconnect();
  XLOG(DBG2) << "Ready to accept new clients";
}

// TODO: Log command output to Scuba
void StreamingDiagShellServer::streamOutput() {
  while (!shouldResetPublisher_) {
    /* Set the timeout for each read.
     * This is to check that a client has disconnected in the meantime.
     * If the client is still connected, will continue to wait.
     * If the client has disconnected, will clean up the client states.
     */
    std::string toPublish = readOutput(FLAGS_diag_shell_read_timeout_ms);
    if (toPublish.length() > 0) {
      // publish string on stream
      auto locked = publisher_.lock();
      if (*locked) {
        (*locked)->next(toPublish);
      }
    }
  }
  resetPublisher();
}

DiagCmdServer::DiagCmdServer(const SaiSwitch* hw, DiagShell* diagShell)
    : hw_(hw),
      diagShell_(diagShell),
      uuid_(boost::uuids::to_string(boost::uuids::random_generator()())) {}

DiagCmdServer::~DiagCmdServer() noexcept {}

std::string DiagCmdServer::getDelimiterDiagCmd(const std::string& UUID) const {
  /* Returns the command used for separating each diagCmd,
   * which varies between platform.
   */
  switch (hw_->getPlatform()->getAsic()->getAsicVendor()) {
    case HwAsic::AsicVendor::ASIC_VENDOR_BCM:
      return UUID + "\n";
    case HwAsic::AsicVendor::ASIC_VENDOR_TAJO:
    case HwAsic::AsicVendor::ASIC_VENDOR_CHENAB:
      return folly::to<std::string>("print('", UUID, "')\n");
    default:
      throw FbossError("Shell not supported for fake platforms");
  }
  CHECK(0) << " Should never get here";
  return "";
}

std::string& DiagCmdServer::cleanUpOutput(
    std::string& output,
    const std::string& input) {
  switch (hw_->getPlatform()->getAsic()->getAsicVendor()) {
    case HwAsic::AsicVendor::ASIC_VENDOR_BCM:
      // Clean up the back of the string
      if (!output.empty() && !input.empty()) {
        std::string shell = "drivshell>";
        int pos = output.rfind(shell + uuid_);
        if (pos != std::string::npos) {
          // Remove uuid if the output is for an actual command.
          output = output.erase(pos);
        }
        // Cleaning shell inputs
        std::string inputStr = input.substr(0, input.find('\n')) + "\x0d\n";
        pos = output.find(input.substr(0, input.find('\n')) + "\x0d\n");
        if (pos != std::string::npos) {
          output = output.erase(0, pos + inputStr.length());
        }
      }
      return output;
    case HwAsic::AsicVendor::ASIC_VENDOR_TAJO:
    case HwAsic::AsicVendor::ASIC_VENDOR_CHENAB:
      return output;
    default:
      throw FbossError("Shell not supported for fake platforms");
  }
  CHECK(0) << " Should never get here";
  return output;
}

std::string DiagCmdServer::diagCmd(
    std::unique_ptr<fbstring> input,
    std::unique_ptr<ClientInformation> client) {
  // TODO: Look into adding timeout for try_connect
  // TODO: Look into adding context on which client is connected
  if (!diagShell_->tryConnect()) {
    // TODO: Look into how we can add timeout in future diffs
    throw FbossError("Another client connected");
  }
  std::string inputStr = input.get()->c_str();
  XLOG(DBG1) << "Connected and running diagCmd: " << inputStr;
  // Flush out old output
  // These should be made in different consumeInput calls,
  // or the input gets mixed up.
  diagShell_->consumeInput(
      std::make_unique<std::string>("\n"), std::move(client));
  produceOutput();
  diagShell_->consumeInput(
      std::make_unique<std::string>(inputStr), std::move(client));
  if (FLAGS_enable_replayer) {
    diagShell_->logCommandToReplayer(inputStr);
  }
  diagShell_->consumeInput(
      std::make_unique<std::string>(getDelimiterDiagCmd(uuid_)),
      std::move(client));
  // TODO: Look into requesting results that take a long time
  std::string output = produceOutput(FLAGS_diag_shell_read_timeout_ms);
  cleanUpOutput(output, inputStr);
  diagShell_->disconnect();
  return output;
}

std::string DiagCmdServer::produceOutput(int timeoutMs) {
  std::ostringstream os;
  int currTimeout = timeoutMs;
  while (true) {
    std::string tmpOutput = diagShell_->readOutput(currTimeout);
    if (tmpOutput.length() == 0) {
      break;
    }
    os << tmpOutput;
    // TODO: Check platform specific termination criteria
    if (tmpOutput.rfind("Unknown command: " + uuid_) != std::string::npos) {
      break;
    }
  }
  return os.str();
}

} // namespace facebook::fboss
