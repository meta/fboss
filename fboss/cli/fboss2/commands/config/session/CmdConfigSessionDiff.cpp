/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/session/CmdConfigSessionDiff.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/Subprocess.h>
#include <unistd.h>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace facebook::fboss {

namespace {

using ConfigDomain = ConfigSession::ConfigDomain;

// Read a file, returning empty content (not an error) when it doesn't exist.
std::string readFileOrEmpty(const std::string& path) {
  std::string content;
  folly::readFile(path.c_str(), content);
  return content;
}

// Get config content from a revision specifier for a specific domain file.
// "current" reads the live system file. A path absent at the given revision
// (e.g. a commit predating BGP config) is treated as empty content.
// validationPath is a file present in every commit, used to distinguish a
// genuinely invalid revision from a domain simply absent there.
std::pair<std::string, std::string> getRevisionContent(
    const std::string& revision,
    const ConfigDomain& domain,
    const std::string& validationPath,
    ConfigSession& session,
    Git& git) {
  if (revision == "current") {
    return {
        readFileOrEmpty(session.getCurrentConfigPath(domain.service)),
        "current live config"};
  }
  std::string resolvedSha = git.resolveRef(revision);
  // Verify the revision is real before treating a missing domain path as empty.
  // A genuinely invalid revision throws here and propagates; only a path absent
  // at an otherwise-valid revision is treated as empty.
  git.fileAtRevision(resolvedSha, validationPath);
  std::string content;
  try {
    content = git.fileAtRevision(resolvedSha, domain.gitRelPath);
  } catch (const std::exception&) {
    content = "";
  }
  return {content, Git::shortSha1(revision)};
}

// Helper function to execute diff on two strings and return the result
std::string executeDiff(
    const std::string& content1,
    const std::string& content2,
    const std::string& label1,
    const std::string& label2) {
  try {
    // Write content to temporary files for diff
    std::string tmpFile1 = "/tmp/fboss2_diff_1_XXXXXX";
    std::string tmpFile2 = "/tmp/fboss2_diff_2_XXXXXX";

    int fd1 = mkstemp(tmpFile1.data());
    int fd2 = mkstemp(tmpFile2.data());

    if (fd1 < 0 || fd2 < 0) {
      throw std::runtime_error("Failed to create temporary files for diff");
    }

    // Write content and close files
    folly::writeFull(fd1, content1.data(), content1.size());
    folly::writeFull(fd2, content2.data(), content2.size());
    close(fd1);
    close(fd2);

    // Run diff
    folly::Subprocess proc(
        std::vector<std::string>{
            "/usr/bin/diff",
            "-u",
            "--label",
            label1,
            "--label",
            label2,
            tmpFile1,
            tmpFile2},
        folly::Subprocess::Options().pipeStdout().pipeStderr());

    auto result = proc.communicate();
    int returnCode = proc.wait().exitStatus();

    // Clean up temp files
    unlink(tmpFile1.c_str());
    unlink(tmpFile2.c_str());

    // diff returns 0 if files are identical, 1 if different, 2 on error
    if (returnCode == 0) {
      return "No differences between " + label1 + " and " + label2 + ".";
    } else if (returnCode == 1) {
      // Files differ - return the diff output
      return result.first;
    } else {
      // Error occurred
      throw std::runtime_error("diff command failed: " + result.second);
    }
  } catch (const std::exception& ex) {
    throw std::runtime_error(
        "Failed to execute diff command: " + std::string(ex.what()));
  }
}

// Append a (optionally headered) diff section to the combined output.
void appendSection(
    std::string& out,
    const std::string& name,
    const std::string& body,
    bool withHeader) {
  if (withHeader) {
    if (!out.empty()) {
      out += "\n";
    }
    out += fmt::format("===== {} config =====\n", name);
  }
  out += body;
  if (!body.empty() && body.back() != '\n') {
    out += "\n";
  }
}

} // namespace

CmdConfigSessionDiffTraits::RetType CmdConfigSessionDiff::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::RevisionList& revisions) {
  // Diffing must never stage a session.
  auto& session =
      ConfigSession::getInstance(ConfigSession::SessionInit::ReadOnly);
  auto& git = session.getGit();
  auto domains = session.configDomains();

  // Metadata is present even before either service establishes its baseline.
  const std::string validationPath = "cli/cli_metadata.json";

  // Modes 1 and 2 both diff each staged domain's session file against some
  // "base" (current live config for mode 1; a revision for mode 2). The only
  // difference is how the base content+label is obtained, so share the loop.
  auto diffStagedDomains =
      [&](const std::function<std::pair<std::string, std::string>(
              const ConfigDomain&)>& getBase) {
        // Read each domain's staged content once via the shared primitive
        // (nullopt == not staged), so we neither re-stat nor re-read files.
        std::vector<std::pair<ConfigDomain, std::string>> staged;
        for (const auto& d : domains) {
          if (auto content = session.readStagedContent(d)) {
            staged.emplace_back(d, std::move(*content));
          }
        }
        std::string out;
        for (const auto& [d, sessionContent] : staged) {
          auto [baseContent, baseLabel] = getBase(d);
          appendSection(
              out,
              d.name,
              executeDiff(
                  baseContent, sessionContent, baseLabel, "session config"),
              staged.size() > 1);
        }
        return out;
      };

  // Mode 1: No arguments - diff each staged domain's session vs current live.
  if (revisions.empty()) {
    if (!session.hasActiveSession()) {
      return "No config session exists. Make a config change first.";
    }
    return diffStagedDomains([&](const ConfigDomain& d) {
      return std::make_pair(
          readFileOrEmpty(session.getCurrentConfigPath(d.service)),
          std::string("current live config"));
    });
  }

  // Mode 2: One argument - diff each staged domain's session vs a revision.
  if (revisions.size() == 1) {
    if (!session.hasActiveSession()) {
      return "No config session exists. Make a config change first.";
    }
    return diffStagedDomains([&](const ConfigDomain& d) {
      return getRevisionContent(revisions[0], d, validationPath, session, git);
    });
  }

  // Mode 3: Two arguments - diff between two revisions for each domain that
  // has content at either revision.
  if (revisions.size() == 2) {
    // Pre-compute each domain's rendered diff section so headers are only added
    // when more than one domain is shown.
    std::vector<std::pair<std::string, std::string>> sections; // {name, body}
    for (const auto& d : domains) {
      auto [c1, l1] =
          getRevisionContent(revisions[0], d, validationPath, session, git);
      auto [c2, l2] =
          getRevisionContent(revisions[1], d, validationPath, session, git);
      if (c1.empty() && c2.empty()) {
        continue; // domain absent at both revisions
      }
      sections.emplace_back(d.name, executeDiff(c1, c2, l1, l2));
    }
    if (sections.empty()) {
      return "No config found at the given revisions.";
    }
    std::string out;
    const bool multi = sections.size() > 1;
    for (const auto& [name, body] : sections) {
      appendSection(out, name, body, multi);
    }
    return out;
  }

  // More than 2 arguments is an error
  throw std::invalid_argument(
      "Too many arguments. Expected 0, 1, or 2 revision specifiers.");
}

void CmdConfigSessionDiff::printOutput(const RetType& diffOutput) {
  std::cout << diffOutput;
  if (!diffOutput.empty() && diffOutput.back() != '\n') {
    std::cout << std::endl;
  }
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigSessionDiff, CmdConfigSessionDiffTraits>::run();

} // namespace facebook::fboss
