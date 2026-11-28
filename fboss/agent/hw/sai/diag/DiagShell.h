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

#include <mutex>
#include <string>
#include <thread>

#include <folly/File.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp2/async/ServerStream.h>
#include "fboss/agent/hw/sai/diag/Repl.h"
#include "fboss/agent/hw/sai/switch/gen-cpp2/SaiCtrl.h"

namespace facebook::fboss {

class SaiSwitch;

namespace detail {

// Own the master side of a created PTY
struct PtyMaster {
  // Could throw std::system_error if pty calls fail
  PtyMaster();
  folly::File file;
  std::string name;
};

// Own the slave side of a created PTY
struct PtySlave {
  // Could throw std::system_error if open() fails
  explicit PtySlave(const PtyMaster& ptyMaster);
  folly::File file;
};

// Manage the lifetime of settings for running a shell session
struct TerminalSession {
  TerminalSession(
      const PtySlave& ptySlave,
      const std::vector<folly::File>& streams);
  ~TerminalSession() noexcept;
  TerminalSession(const TerminalSession&) = delete;
  TerminalSession& operator=(const TerminalSession&) = delete;
  TerminalSession(TerminalSession&&) = delete;
  TerminalSession& operator=(TerminalSession&&) = delete;

  // Track the streams we change for this terminal session
  std::map<int, folly::File> fd2oldStreams_;
};

// Separate sessions for each connection
class DiagShellClientState {
 public:
  DiagShellClientState(
      const std::string& clientAddrAndPort,
      apache::thrift::ServerStreamPublisher<std::string>&& publisher);

  void publishOutput(const std::string& output);
  void completeStream();

 private:
  const std::string clientAddrAndPort_;
  apache::thrift::ServerStreamPublisher<std::string> publisher_;
};

} // namespace detail

class DiagShell {
 public:
  explicit DiagShell(const SaiSwitch* hw);
  virtual ~DiagShell() noexcept = default;
  DiagShell(const DiagShell&) = delete;
  DiagShell& operator=(const DiagShell&) = delete;
  DiagShell(DiagShell&&) = delete;
  DiagShell& operator=(DiagShell&&) = delete;

  void consumeInput(
      std::unique_ptr<std::string> input,
      std::unique_ptr<ClientInformation> client);

  void logCommandToReplayer(const std::string& command);

  std::string readOutput(int timeoutMs);
  bool tryConnect();
  void disconnect();
  std::string getPrompt() const;

 protected:
  void initTerminal();

  std::mutex diagShellMutex_;
  std::unique_lock<std::mutex> diagShellLock_;

  // Buffer to read into from pty master side
  std::array<char, 5120> producerBuffer_;

 private:
  std::unique_ptr<Repl> makeRepl() const;
  void syncBcmShell();
  std::string getClientInformationStr(
      std::unique_ptr<ClientInformation> clientInfo) const;
  void logToScuba(
      std::unique_ptr<ClientInformation> client,
      const std::string& input,
      const std::optional<std::string>& result = std::nullopt) const;
  int getPtymFd() const;
  const SaiSwitch* hw_;
  std::unique_ptr<detail::PtyMaster> ptym_;
  std::unique_ptr<detail::PtySlave> ptys_;
  std::unique_ptr<Repl> repl_;
  std::unique_ptr<detail::TerminalSession> ts_;
};

class StreamingDiagShellServer : public DiagShell {
 public:
  explicit StreamingDiagShellServer(const SaiSwitch* hw);

  std::string start(
      apache::thrift::ServerStreamPublisher<std::string>&& publisher);
  void markResetPublisher();
  void consumeInput(
      std::unique_ptr<std::string> input,
      std::unique_ptr<ClientInformation> client);

 private:
  void setPublisher(
      apache::thrift::ServerStreamPublisher<std::string>&& publisher);
  void resetPublisher();
  void streamOutput();

  std::unique_ptr<std::thread> producerThread_;
  folly::Synchronized<
      std::unique_ptr<apache::thrift::ServerStreamPublisher<std::string>>,
      std::mutex>
      publisher_;
  std::atomic<bool> shouldResetPublisher_ = false;
};

class DiagCmdServer {
 public:
  explicit DiagCmdServer(const SaiSwitch* hw, DiagShell* diagShell);
  ~DiagCmdServer() noexcept;
  DiagCmdServer(const DiagCmdServer&) = delete;
  DiagCmdServer& operator=(const DiagCmdServer&) = delete;
  DiagCmdServer(DiagCmdServer&&) = delete;
  DiagCmdServer& operator=(DiagCmdServer&&) = delete;

  // Processing programmatic inputs
  std::string diagCmd(
      std::unique_ptr<fbstring> input,
      std::unique_ptr<ClientInformation> client);

 private:
  std::string produceOutput(int timeoutMs = 0);
  std::string getDelimiterDiagCmd(const std::string& UUID) const;
  std::string& cleanUpOutput(std::string& output, const std::string& input);
  const SaiSwitch* hw_;
  DiagShell* diagShell_;
  const std::string uuid_; // UUID used for I/O
};

} // namespace facebook::fboss
