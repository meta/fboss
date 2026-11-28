/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/history/CmdConfigHistory.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <cstdint>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/Table.h"

namespace facebook::fboss {

namespace {

// Format Unix timestamp (seconds) as a human-readable string
std::string formatTime(int64_t timeSec) {
  char buffer[32];
  tm timeinfo{};
  std::time_t time = timeSec;
  localtime_r(&time, &timeinfo);
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return buffer;
}

} // namespace

CmdConfigHistoryTraits::RetType CmdConfigHistory::queryClient(
    const HostInfo& /* hostInfo */) {
  // Read-only: reporting committed history must never stage a session.
  auto& session =
      ConfigSession::getInstance(ConfigSession::SessionInit::ReadOnly);

  // Get the commit history from Git for the CLI config file
  auto commits = session.history(session.getCliConfigPath());

  if (commits.empty()) {
    return "No config revisions found in Git history";
  }

  // Build the table
  utils::Table table;
  table.setHeader({"Commit", "Author", "Commit Time", "Message"});

  for (const auto& commit : commits) {
    table.addRow(
        {Git::shortSha1(commit.sha1),
         commit.authorName,
         formatTime(commit.timestamp),
         commit.subject});
  }

  // Convert table to string
  std::ostringstream oss;
  oss << table;
  return oss.str();
}

void CmdConfigHistory::printOutput(const RetType& tableOutput) {
  std::cout << tableOutput << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigHistory, CmdConfigHistoryTraits>::run();

} // namespace facebook::fboss
