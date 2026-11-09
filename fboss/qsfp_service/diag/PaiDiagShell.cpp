/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/diag/PaiDiagShell.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/diag/SaiRepl.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/lib/phy/SaiPhyManager.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fmt/format.h>
#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>

#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <thread>

namespace {
// Escape special chars (\n, \r, \t, non-printable) for readable logging.
std::string escapeForLog(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (unsigned char c : s) {
    if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c >= 0x20 && c < 0x7f) {
      out += static_cast<char>(c);
    } else {
      out += fmt::format("\\x{:02x}", c);
    }
  }
  return out;
}

// Truncate long strings for log output so we don't flood the log.
std::string truncForLog(const std::string& s, std::size_t max = 200) {
  if (s.size() <= max) {
    return escapeForLog(s);
  }
  return escapeForLog(s.substr(0, max)) +
      fmt::format("...[+{} bytes]", s.size() - max);
}
} // namespace

DEFINE_int32(
    pai_diag_shell_read_timeout_ms,
    2000,
    "Timeout for reading output of the PAI diag shell");

namespace facebook::fboss {

namespace pai_diag_detail {

// Helper functions for creating a PTY
namespace {
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
  std::array<char, 50> buf{};
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
} // namespace

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
    fd2oldStreams_.insert({stream.fd(), stream.dup()});
    dup2(ptySlave.file, stream);
  }
}

TerminalSession::~TerminalSession() noexcept {
  try {
    /* sleep override */
    usleep(100 * 1000);
    for (auto& fd2stream : fd2oldStreams_) {
      dup2(fd2stream.second, folly::File(fd2stream.first));
      XLOG(DBG2) << "Restore stream to standard std fd " << fd2stream.first;
    }
  } catch (const std::system_error& e) {
    XLOG(ERR) << "Failed to reset terminal parameters: " << e.what();
  }
}

} // namespace pai_diag_detail

PaiDiagShell::PaiDiagShell(uint64_t switchId) : switchId_(switchId) {
  // Set up PTY
  ptym_ = std::make_unique<pai_diag_detail::PtyMaster>();
  ptys_ = std::make_unique<pai_diag_detail::PtySlave>(*ptym_);
  paiDiagShellLock_ = std::unique_lock(paiDiagShellMutex_, std::defer_lock);
}

void PaiDiagShell::initTerminal() {
  XLOG(DBG1) << "PaiDiagShell::initTerminal: switchId=0x" << std::hex
             << switchId_ << std::dec
             << " repl_=" << (repl_ ? "exists" : "null")
             << " ptym_fd=" << (ptym_ ? ptym_->file.fd() : -1)
             << " ptys_fd=" << (ptys_ ? ptys_->file.fd() : -1);
  if (!repl_) {
    // Set up REPL on connect if needed.
    // For Agera3 (Broadcom PHY), the SAI attribute SwitchShellEnable triggers
    // the PAI-internal diag shell which does fgets(stdin); SaiRepl spawns a
    // dedicated thread for that.
    XLOG(DBG1) << "PaiDiagShell::initTerminal: creating SaiRepl for switchId=0x"
               << std::hex << switchId_ << std::dec;
    repl_ = std::make_unique<SaiRepl>(SwitchSaiId{switchId_});

    // Set up terminal to raw mode
    struct termios oldSettings{};
    int ret = ::tcgetattr(ptys_->file.fd(), &oldSettings);
    folly::checkUnixError(ret, "Failed to get current terminal settings");
    auto newSettings = oldSettings;
    ::cfmakeraw(&newSettings);
    ret = ::tcsetattr(ptys_->file.fd(), TCSANOW, &newSettings);
    folly::checkUnixError(
        ret, "Failed to set new terminal settings on PTY slave");

    auto streams = repl_->getStreams();
    XLOG(DBG1) << "PaiDiagShell::initTerminal: SaiRepl getStreams() returned "
               << streams.size() << " streams";
    for (const auto& s : streams) {
      XLOG(DBG1) << "  stream fd=" << s.fd();
    }
    ts_.reset(new pai_diag_detail::TerminalSession(*ptys_, streams));
    // CRITICAL: After dup2, libc's stdout/stderr FILE* still has whatever
    // buffering mode was set when they were first used. In a daemon context,
    // stdout was initially attached to a pipe (journalctl) so libc set
    // FULLY-BUFFERED mode. The shell's printf output then sits in libc's
    // buffer indefinitely (\n doesn't trigger flush in fully-buffered mode).
    // Force unbuffered so every printf the shell does immediately reaches
    // the PTY master (where we read it from).
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    XLOG(DBG1) << "PaiDiagShell::initTerminal: forced stdout/stderr to "
               << "unbuffered (_IONBF) so shell printf reaches the PTY";
    XLOG(DBG1) << "PaiDiagShell::initTerminal: calling repl_->run() — "
               << "this spawns the SAI shell thread (setAttribute "
               << "SwitchShellEnable=true)";
    repl_->run();
    XLOG(DBG1) << "PaiDiagShell::initTerminal: repl_->run() returned";
  } else if (!ts_) {
    ts_.reset(
        new pai_diag_detail::TerminalSession(*ptys_, repl_->getStreams()));
  }
}

int PaiDiagShell::getPtymFd() const {
  return ptym_->file.fd();
}

bool PaiDiagShell::tryConnect() {
  try {
    if (paiDiagShellLock_.try_lock()) {
      try {
        initTerminal();
      } catch (...) {
        paiDiagShellLock_.unlock();
        throw;
      }
      return true;
    }
  } catch (const std::system_error&) {
    LOG(WARNING) << "Another PAI diag shell client already connected";
  }
  throw FbossError(
      "Unable to acquire PAI diag shell lock, client already connected");
}

void PaiDiagShell::disconnect() {
  try {
    ts_.reset();
    paiDiagShellLock_.unlock();
  } catch (const std::system_error&) {
    XLOG(WARNING) << "Trying to disconnect when it was never connected";
  }
}

std::string PaiDiagShell::getPrompt() const {
  return repl_->getPrompt();
}

void PaiDiagShell::consumeInput(
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> /* client */) {
  if (!repl_) {
    XLOG(DBG1) << "PaiDiagShell::consumeInput: SKIPPED (repl_ is null)";
    return;
  }
  // Write only the actual string bytes — NOT input.size() + 1. The +1 was
  // including the trailing C-string NUL terminator, which the PAI shell
  // sees as a stray byte after each newline. Subsequent fgets() reads
  // would start with a NUL → strcmp / strtok stops at NUL → shell sees
  // an empty command → prints a bare prompt with no menu output.
  XLOG(DBG1) << "PaiDiagShell::consumeInput: writing " << input->size()
             << " bytes to PTY master fd=" << getPtymFd() << " content=\""
             << truncForLog(*input) << "\"";
  std::size_t ret =
      folly::writeFull(getPtymFd(), input->c_str(), input->size());
  folly::checkUnixError(
      ret, "Failed to write PAI diag shell input to PTY master");
  XLOG(DBG1) << "PaiDiagShell::consumeInput: wrote " << ret << " bytes";
}

std::string PaiDiagShell::readOutput(int timeoutMs) {
  auto fd = getPtymFd();
  std::string output;
  // Use poll() instead of select() — qsfp_service can have very high fd
  // numbers (we've seen PTY master fd ~2190) and select()'s fd_set is
  // limited to FD_SETSIZE (1024). Calling FD_SET / FD_ISSET on an fd
  // outside that range is undefined behavior and silently produces
  // garbage results. poll() has no such limit.
  struct pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  int pollRet = poll(&pfd, 1, timeoutMs);
  XLOG(DBG1) << "PaiDiagShell::readOutput: fd=" << fd
             << " timeoutMs=" << timeoutMs << " poll=" << pollRet
             << " errno=" << (pollRet < 0 ? errno : 0) << " revents=0x"
             << std::hex << pfd.revents << std::dec;
  if (pollRet > 0) {
    if (!(pfd.revents & POLLIN)) {
      std::string msg = fmt::format(
          "PaiDiagShell::readOutput: poll returned {} but POLLIN not set "
          "(revents=0x{:x}, fd={})",
          pollRet,
          pfd.revents,
          fd);
      XLOG(WARNING) << msg;
      throw FbossError(msg);
    }
    auto nread = ::read(fd, producerBuffer_.data(), producerBuffer_.size());
    folly::checkUnixError(nread, "Failed to read from the pty master");
    if (nread == 0) {
      std::string msg = "read 0 bytes from PTY slave;";
      XLOG(WARNING) << msg;
      throw FbossError(msg);
    }
    output += std::string(producerBuffer_.data(), nread);
    XLOG(DBG1) << "PaiDiagShell::readOutput: read " << nread
               << " bytes from PTY master fd=" << fd << " content=\""
               << truncForLog(output) << "\"";
  } else {
    XLOG(DBG1) << "PaiDiagShell::readOutput: poll returned "
               << (pollRet == 0 ? "TIMEOUT" : "ERROR")
               << " (no data available within " << timeoutMs << "ms)";
  }
  return output;
}

StreamingPaiDiagShell::StreamingPaiDiagShell(uint64_t switchId)
    : PaiDiagShell(switchId) {}

void StreamingPaiDiagShell::setPublisher(
    apache::thrift::ServerStreamPublisher<std::string>&& publisher) {
  publisher_ =
      std::make_unique<apache::thrift::ServerStreamPublisher<std::string>>(
          std::move(publisher));
}

std::string StreamingPaiDiagShell::start(
    apache::thrift::ServerStreamPublisher<std::string>&& publisher) {
  assert(paiDiagShellLock_.owns_lock());
  setPublisher(std::move(publisher));
  producerThread_.reset(new std::thread([this]() { streamOutput(); }));
  return getPrompt();
}

void StreamingPaiDiagShell::markResetPublisher() {
  XLOG(DBG2) << "Marked to reset PAI diag shell client states";
  shouldResetPublisher_ = true;
}

void StreamingPaiDiagShell::consumeInput(
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> client) {
  auto locked = publisher_.lock();
  if (!*locked) {
    std::string msg = "No PAI diag shell connected!";
    XLOG(WARNING) << msg;
    throw FbossError(msg);
  }
  PaiDiagShell::consumeInput(std::move(input), std::move(client));
}

void StreamingPaiDiagShell::resetPublisher() {
  XLOG(DBG2) << "Resetting PAI diag shell client state";
  auto locked = publisher_.lock();
  if (!shouldResetPublisher_) {
    return;
  }
  if (*locked) {
    std::move(**locked).complete();
  }
  if (producerThread_) {
    // NOLINTNEXTLINE(facebook-hte-BadCall-detach): mirrors agent's
    // StreamingDiagShellServer::resetPublisher pattern. The producer
    // thread is blocked in readOutput()/poll() on the PTY master and
    // we cannot safely join() it from here without risking deadlock.
    producerThread_->detach();
  }
  shouldResetPublisher_ = false;
  locked->reset();
  disconnect();
  XLOG(DBG2) << "Ready to accept new PAI diag shell clients";
}

void StreamingPaiDiagShell::streamOutput() {
  while (!shouldResetPublisher_) {
    std::string toPublish = readOutput(FLAGS_pai_diag_shell_read_timeout_ms);
    if (toPublish.length() > 0) {
      auto locked = publisher_.lock();
      if (*locked) {
        (*locked)->next(toPublish);
      }
    }
  }
  resetPublisher();
}

PaiDiagCmdServer::PaiDiagCmdServer(PaiDiagShell* paiDiagShell)
    : paiDiagShell_(paiDiagShell),
      uuid_(boost::uuids::to_string(boost::uuids::random_generator()())) {}

PaiDiagCmdServer::~PaiDiagCmdServer() noexcept {}

std::string PaiDiagCmdServer::getDelimiterDiagCmd(
    const std::string& UUID) const {
  // For Broadcom PAI shell, sending a UUID as input causes the shell to
  // echo "Unknown command: <UUID>" which we use to detect command boundaries.
  return UUID + "\n";
}

std::string& PaiDiagCmdServer::cleanUpOutput(
    std::string& output,
    const std::string& input) {
  if (output.empty() || input.empty()) {
    return output;
  }
  // Same pattern as agent's DiagCmdServer::cleanUpOutput, adapted for PAI
  // shell whose prompt has a variable chip-id suffix (BRCM:<hex>> rather
  // than bcmsh's fixed "drivshell>").
  //
  // After our UUID delimiter command, the PAI shell emits roughly:
  //
  //   <real command output>
  //   BRCM:<hex>> \t<UUID> ??
  //   List of available commands\n
  //   ... full help menu ...
  //   BRCM:<hex>>
  //
  // We want to strip everything from the BRCM prompt that immediately
  // precedes the UUID echo onward (so the leftover help-menu noise
  // disappears).
  std::size_t uuidPos = output.find(uuid_);
  if (uuidPos != std::string::npos) {
    // Walk backward from uuidPos to find the nearest "BRCM:" prompt that
    // sits just before the UUID echo. Erase from that prompt to end.
    std::size_t promptPos = output.rfind("BRCM:", uuidPos);
    if (promptPos != std::string::npos) {
      output = output.erase(promptPos);
    } else {
      // No BRCM prompt found between start and UUID — just chop from UUID.
      output = output.erase(uuidPos);
    }
  }
  // Strip the echoed input from the front (PAI shell echoes it with \r\n
  // when running in raw PTY).
  std::string firstLine = input.substr(0, input.find('\n')) + "\x0d\n";
  std::size_t inputPos = output.find(firstLine);
  if (inputPos != std::string::npos) {
    output = output.erase(0, inputPos + firstLine.length());
  }
  // Strip any leading BRCM:<hex>> prompt + the trailing space (this is the
  // prompt that printed BEFORE our command was processed; PAI shell prints
  // a fresh prompt every loop iteration).
  while (!output.empty() && output.compare(0, 5, "BRCM:") == 0) {
    std::size_t promptEnd = output.find("> ");
    if (promptEnd == std::string::npos) {
      break;
    }
    output = output.erase(0, promptEnd + 2); // drop "BRCM:<hex>> "
  }
  return output;
}

std::string PaiDiagCmdServer::paiDiagCmd(
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> client) {
  if (!paiDiagShell_->tryConnect()) {
    throw FbossError("Another PAI diag shell client connected");
  }
  // Regenerate the UUID delimiter for THIS call so any stale UUID echo
  // left in the PTY buffer from a previous call (e.g. from PHASE 4
  // hitting timeout before fully draining the help menu PAI prints for
  // unknown commands) cannot match this call's terminator detection or
  // confuse cleanUpOutput.
  uuid_ = boost::uuids::to_string(boost::uuids::random_generator()());
  // Wrap the body in try/catch so any SDK failure cleanly disconnects and
  // returns an error to the thrift caller instead of crashing qsfp_service.
  try {
    std::string inputStr = input.get()->c_str();
    // Ensure the user's command ends with a newline so the shell's fgets()
    // returns it as a single command, BEFORE we send the UUID delimiter.
    // Without this, "ls" + "<uuid>\n" would be read as the single command
    // "ls<uuid>" and our terminator-detection pattern "Unknown command: <uuid>"
    // would never match, causing the read loop to time out with empty output.
    if (inputStr.empty() || inputStr.back() != '\n') {
      inputStr += '\n';
    }
    XLOG(DBG1) << "Connected and running paiDiagCmd: \""
               << escapeForLog(inputStr) << "\" uuid=" << uuid_;
    XLOG(DBG1) << "  PHASE 1/4: send initial \"\\n\" to flush prompt";
    paiDiagShell_->consumeInput(
        std::make_unique<std::string>("\n"), std::move(client));
    XLOG(DBG1) << "  PHASE 2/4: drain initial output (longer timeout to "
               << "absorb any leftover from previous call's PHASE 4 timeout)";
    // Drain anything left in the PTY from prior calls. Use a small per-read
    // timeout (200ms) so the shell has time to flush whatever it was still
    // printing when our previous call returned. If two reads in a row come
    // back empty, we're confident the PTY is drained.
    std::string drained;
    int drainEmptyStreak = 0;
    while (drainEmptyStreak < 2) {
      std::string chunk = paiDiagShell_->readOutput(200);
      if (chunk.empty()) {
        drainEmptyStreak++;
      } else {
        drained += chunk;
        drainEmptyStreak = 0;
      }
    }
    XLOG(DBG1) << "  PHASE 2/4 drained " << drained.size()
               << " bytes content=\"" << truncForLog(drained) << "\"";
    XLOG(DBG1) << "  PHASE 3/4: send user command \"" << escapeForLog(inputStr)
               << "\" + UUID delimiter \"" << uuid_ << "\\n\"";
    paiDiagShell_->consumeInput(
        std::make_unique<std::string>(inputStr), nullptr);
    paiDiagShell_->consumeInput(
        std::make_unique<std::string>(getDelimiterDiagCmd(uuid_)), nullptr);
    XLOG(DBG1) << "  PHASE 4/4: read output until UUID terminator or "
               << FLAGS_pai_diag_shell_read_timeout_ms << "ms timeout";
    std::string output = produceOutput(FLAGS_pai_diag_shell_read_timeout_ms);
    XLOG(DBG1) << "  PHASE 4/4 done: output.size()=" << output.size()
               << " content=\"" << truncForLog(output, 500) << "\"";
    cleanUpOutput(output, inputStr);
    XLOG(DBG1) << "  After cleanUpOutput: output.size()=" << output.size()
               << " content=\"" << truncForLog(output, 500) << "\"";
    paiDiagShell_->disconnect();
    return output;
  } catch (const std::exception& e) {
    XLOG(ERR) << "paiDiagCmd failed: " << e.what();
    paiDiagShell_->disconnect();
    throw FbossError("paiDiagCmd failed: ", e.what());
  }
}

std::string PaiDiagCmdServer::produceOutput(int timeoutMs) {
  std::ostringstream os;
  int iter = 0;
  while (true) {
    iter++;
    std::string tmpOutput = paiDiagShell_->readOutput(timeoutMs);
    XLOG(DBG1) << "PaiDiagCmdServer::produceOutput: iter=" << iter
               << " timeoutMs=" << timeoutMs << " readOutput returned "
               << tmpOutput.size() << " bytes";
    if (tmpOutput.length() == 0) {
      XLOG(DBG1) << "PaiDiagCmdServer::produceOutput: empty read, breaking "
                 << "loop after " << iter << " iter(s), total accumulated "
                 << os.str().size() << " bytes";
      break;
    }
    os << tmpOutput;
    // PAI shell's "unknown command" handler prints "\t<cmd> ??\n" — NOT
    // bcmsh's "Unknown command: <cmd>" format. So our terminator pattern
    // must match PAI's actual format. Without this, the read loop never
    // breaks early and we always wait for the full timeout, allowing
    // bytes to leak into subsequent calls.
    // Use "\?\?" to escape the question marks — adjacent ?? is a C trigraph
    // sequence that triggers -Wtrigraphs warnings even though it's harmless
    // in modern C++. The escaped version compiles to the same byte string.
    std::string terminator = "\t" + uuid_ + " \?\?";
    if (os.str().rfind(terminator) != std::string::npos) {
      XLOG(DBG1) << "PaiDiagCmdServer::produceOutput: hit PAI terminator "
                 << "(\\t<UUID> \?\?), breaking after " << iter << " iter(s), "
                 << "total " << os.str().size() << " bytes";
      break;
    }
  }
  return os.str();
}

uint64_t getFirstSaiSwitchIdForPaiDiagShell(PhyManager* phyMgr) {
  if (!phyMgr) {
    throw FbossError("PaiDiagShell: PhyManager is null");
  }
  auto* saiPhyMgr = dynamic_cast<SaiPhyManager*>(phyMgr);
  if (!saiPhyMgr) {
    throw FbossError(
        "PaiDiagShell: PhyManager is not a SaiPhyManager — "
        "no SAI switch available for retimer diag shell");
  }
  auto xphyPorts = saiPhyMgr->getXphyPorts();
  if (xphyPorts.empty()) {
    throw FbossError("PaiDiagShell: no XPHY ports available");
  }
  // Pick any port — its SaiSwitch gives us a switch_id we can attach the
  // PAI shell to. Inside the shell, the user can hop chip context with `s`.
  auto* saiSwitch = saiPhyMgr->getSaiSwitch(xphyPorts.front());
  if (!saiSwitch) {
    throw FbossError(
        "PaiDiagShell: getSaiSwitch returned null for port ",
        static_cast<int>(xphyPorts.front()));
  }
  return static_cast<uint64_t>(saiSwitch->getSaiSwitchId());
}

} // namespace facebook::fboss
