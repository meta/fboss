/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/ResultPrinter.h"

#include <functional>
#include <sstream>

namespace facebook::fboss::platform::fixmyfboss {

void ResultPrinter::printProgress(const std::string& message) {
  out_ << ANSI_CLEAR_LINE << message << ANSI_CARRIAGE_RETURN << std::flush;
}

void ResultPrinter::clearLine() {
  out_ << ANSI_CLEAR_LINE << ANSI_CARRIAGE_RETURN << std::flush;
}

void ResultPrinter::printSummary(
    const std::vector<platform_checks::CheckResult>& results) {
  out_ << "Ran " << results.size() << " checks\n";
  out_ << "-----\n";
  out_ << "Result summary:\n";
  printStatusGroup(
      results, platform_checks::CheckStatus::OK, "Passed", COLOR_GREEN);
  printStatusGroup(
      results, platform_checks::CheckStatus::PROBLEM, "Problems", COLOR_RED);
  printStatusGroup(
      results, platform_checks::CheckStatus::ERROR, "Errors", COLOR_ORANGE);
  printStatusGroup(
      results, platform_checks::CheckStatus::SKIPPED, "Skipped", COLOR_GRAY);
  out_ << "-----\n";
}

void ResultPrinter::printStatusGroup(
    const std::vector<platform_checks::CheckResult>& results,
    platform_checks::CheckStatus status,
    const std::string& label,
    const char* color) {
  std::vector<std::reference_wrapper<const platform_checks::CheckResult>> group;
  for (const auto& result : results) {
    if (*result.status() == status) {
      group.emplace_back(result);
    }
  }
  if (group.empty()) {
    return;
  }
  out_ << indent(colorize(
              label + ": " + std::to_string(group.size()), color, true))
       << "\n";
  for (const auto& resultRef : group) {
    const auto& result = resultRef.get();
    std::string line = result.checkName().value_or("Unknown");
    if (status == platform_checks::CheckStatus::SKIPPED &&
        result.errorMessage()) {
      line += " (" + *result.errorMessage() + ")";
    }
    out_ << indent(line, 2) << "\n";
  }
}

void ResultPrinter::printDetails(
    const std::vector<platform_checks::CheckResult>& results) {
  for (const auto& result : results) {
    // Skip reasons are already part of the summary.
    if (*result.status() == platform_checks::CheckStatus::OK ||
        *result.status() == platform_checks::CheckStatus::SKIPPED) {
      continue;
    }

    const std::string name = result.checkName().value_or("Unknown");
    std::string statusBadge = colorizeBackground(
        "[  " + getStatusName(*result.status()) + "  ]",
        getStatusBackgroundColor(*result.status()).c_str());

    out_ << colorize(statusBadge, "", true) << " " << name << "\n";

    if (result.errorMessage()) {
      out_ << indent(*result.errorMessage()) << "\n";
    }

    if (result.remediation() &&
        *result.remediation() != platform_checks::RemediationType::NONE) {
      if (result.remediationMessage()) {
        out_ << indent("Remediation: " + *result.remediationMessage()) << "\n";
      }
    }

    if (showDetails_ && result.durationMs()) {
      out_ << indent("Took " + std::to_string(*result.durationMs()) + " ms")
           << "\n";
    }

    if (showDetails_ && result.details()) {
      out_ << indent("Details:") << "\n"
           << indent(*result.details(), 2) << "\n";
    }
  }
}

std::string
ResultPrinter::colorize(const std::string& text, const char* color, bool bold) {
  if (!color_) {
    return text;
  }
  std::string result;
  if (bold) {
    result += STYLE_BOLD;
  }
  result += color;
  result += text;
  result += COLOR_RESET;
  return result;
}

std::string ResultPrinter::colorizeBackground(
    const std::string& text,
    const char* bgColor) {
  if (!color_) {
    return text;
  }
  return std::string(bgColor) + STYLE_BOLD + text + COLOR_RESET;
}

std::string ResultPrinter::indent(const std::string& text, int level) {
  std::string indentation(size_t(level) * 2, ' ');
  std::istringstream iss(text);
  std::ostringstream oss;
  std::string line;
  bool first = true;

  while (std::getline(iss, line)) {
    if (!first) {
      oss << "\n";
    }
    oss << indentation << line;
    first = false;
  }

  return oss.str();
}

std::string ResultPrinter::getStatusBackgroundColor(
    platform_checks::CheckStatus status) {
  switch (status) {
    case platform_checks::CheckStatus::OK:
      return BG_GREEN;
    case platform_checks::CheckStatus::PROBLEM:
      return BG_RED;
    case platform_checks::CheckStatus::ERROR:
      return BG_ORANGE;
    case platform_checks::CheckStatus::SKIPPED:
      return BG_GRAY;
  }
  return "";
}

std::string ResultPrinter::getStatusName(platform_checks::CheckStatus status) {
  switch (status) {
    case platform_checks::CheckStatus::OK:
      return "OK";
    case platform_checks::CheckStatus::PROBLEM:
      return "PROBLEM";
    case platform_checks::CheckStatus::ERROR:
      return "ERROR";
    case platform_checks::CheckStatus::SKIPPED:
      return "SKIPPED";
  }
  return "UNKNOWN";
}

} // namespace facebook::fboss::platform::fixmyfboss
