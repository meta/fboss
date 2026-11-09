/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/CheckRunner.h"

#include <stdexcept>

#include <gtest/gtest.h>

using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_checks;

namespace {

class FakeCheck : public PlatformCheck {
 public:
  explicit FakeCheck(
      std::string name,
      std::set<std::string> platforms = {},
      bool throws = false)
      : name_(std::move(name)),
        platforms_(std::move(platforms)),
        throws_(throws) {}

  CheckResult run() override {
    if (throws_) {
      throw std::runtime_error("boom");
    }
    return makeOK();
  }
  CheckType getType() const override {
    return CheckType::PCI_DEVICE_CHECK;
  }
  std::string getDescription() const override {
    return name_;
  }
  std::string getName() const override {
    return name_;
  }
  std::set<std::string> getSupportedPlatforms() const override {
    return platforms_;
  }

 private:
  std::string name_;
  std::set<std::string> platforms_;
  bool throws_;
};

class DisruptiveCheck : public FakeCheck {
 public:
  explicit DisruptiveCheck(std::string name) : FakeCheck(std::move(name)) {}
  bool isDisruptive() const override {
    return true;
  }
};

std::vector<std::string> names(const std::vector<CheckResult>& results) {
  std::vector<std::string> out;
  out.reserve(results.size());
  for (const auto& result : results) {
    out.push_back(*result.checkName());
  }
  return out;
}

} // namespace

TEST(CheckRunnerTest, RunsOnlyChecksForPlatform) {
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<FakeCheck>("any"));
  checks.push_back(
      std::make_unique<FakeCheck>("other", std::set<std::string>{"OTHER"}));
  checks.push_back(
      std::make_unique<FakeCheck>("mine", std::set<std::string>{"MONTBLANC"}));

  auto results = fixmyfboss::CheckRunner("MONTBLANC").run(checks);

  EXPECT_EQ(names(results), (std::vector<std::string>{"any", "mine"}));
}

TEST(CheckRunnerTest, ExceptionBecomesError) {
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(
      std::make_unique<FakeCheck>(
          "throws", std::set<std::string>{}, /*throws=*/true));

  auto results = fixmyfboss::CheckRunner("MONTBLANC").run(checks);

  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(*results[0].status(), CheckStatus::ERROR);
  EXPECT_TRUE(results[0].durationMs().has_value());
  EXPECT_EQ(
      *results[0].errorMessage(), "Exception during check execution: boom");
}

TEST(CheckRunnerTest, ExceptionFromSkipReasonBecomesError) {
  class SkipReasonThrows : public FakeCheck {
   public:
    SkipReasonThrows() : FakeCheck("skip throws") {}
    [[noreturn]] std::optional<std::string> getSkipReason() const override {
      throw std::runtime_error("forkpty failed");
    }
  };
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<SkipReasonThrows>());
  checks.push_back(std::make_unique<FakeCheck>("after"));

  auto results = fixmyfboss::CheckRunner("MONTBLANC").run(checks);

  ASSERT_EQ(results.size(), 2);
  EXPECT_EQ(*results[0].status(), CheckStatus::ERROR);
  EXPECT_EQ(
      *results[0].errorMessage(),
      "Exception during check execution: forkpty failed");
  EXPECT_EQ(*results[1].status(), CheckStatus::OK);
}

TEST(CheckRunnerTest, UnavailableHostIsSkippedWithReason) {
  class NeedsHostCheck : public FakeCheck {
   public:
    NeedsHostCheck() : FakeCheck("needs host", {}, /*throws=*/true) {}
    std::optional<std::string> getSkipReason() const override {
      return "no BMC";
    }
  };
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<NeedsHostCheck>());

  auto results = fixmyfboss::CheckRunner("MONTBLANC").run(checks);

  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(*results[0].status(), CheckStatus::SKIPPED);
  EXPECT_EQ(*results[0].errorMessage(), "no BMC");
  EXPECT_FALSE(results[0].durationMs().has_value());
}

TEST(CheckRunnerTest, DisruptiveChecksAreOptInAndRunLast) {
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<DisruptiveCheck>("reboot"));
  checks.push_back(std::make_unique<FakeCheck>("read"));

  auto skipped = fixmyfboss::CheckRunner("MONTBLANC").run(checks);
  auto included =
      fixmyfboss::CheckRunner(
          "MONTBLANC", fixmyfboss::RunOptions{.includeDisruptive = true})
          .run(checks);

  EXPECT_EQ(names(skipped), (std::vector<std::string>{"read", "reboot"}));
  EXPECT_EQ(*skipped[1].status(), CheckStatus::SKIPPED);
  EXPECT_EQ(*included[1].status(), CheckStatus::OK);
}

TEST(CheckRunnerTest, OptInIsCheckedBeforeTheChecksOwnSkipReason) {
  class QueriesHostToSkip : public DisruptiveCheck {
   public:
    QueriesHostToSkip() : DisruptiveCheck("reboot") {}
    std::optional<std::string> getSkipReason() const override {
      ADD_FAILURE() << "queried without --include-disruptive";
      return "cannot confirm";
    }
  };
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<QueriesHostToSkip>());

  auto results = fixmyfboss::CheckRunner("MONTBLANC").run(checks);

  EXPECT_EQ(
      *results[0].errorMessage(),
      "Disruptive; pass --include-disruptive to run");
}

TEST(CheckRunnerTest, NameFilterSelectsChecks) {
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<FakeCheck>("BMC MAC Address"));
  checks.push_back(std::make_unique<FakeCheck>("PCI Devices Exist"));

  auto results = fixmyfboss::CheckRunner(
                     "MONTBLANC", fixmyfboss::RunOptions{.nameFilter = "bmc"})
                     .run(checks);

  EXPECT_EQ(names(results), (std::vector<std::string>{"BMC MAC Address"}));
}

TEST(CheckRunnerTest, PlanExplainsWhatWillHappen) {
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<DisruptiveCheck>("reboot"));
  checks.push_back(
      std::make_unique<FakeCheck>("other", std::set<std::string>{"OTHER"}));
  checks.push_back(std::make_unique<FakeCheck>("read"));

  auto plan = fixmyfboss::CheckRunner("MONTBLANC").plan(checks);

  ASSERT_EQ(plan.size(), 3);
  EXPECT_EQ(plan[0].check.get().getName(), "other");
  EXPECT_FALSE(plan[0].appliesToPlatform);
  EXPECT_EQ(plan[0].skipReason, "Not supported on MONTBLANC");
  EXPECT_EQ(plan[1].check.get().getName(), "read");
  EXPECT_EQ(plan[1].skipReason, std::nullopt);
  EXPECT_EQ(plan[2].check.get().getName(), "reboot");
  EXPECT_TRUE(plan[2].skipReason.has_value());
}

TEST(CheckRunnerTest, PlanKeepsTheSkipReasonError) {
  class SkipReasonThrows : public FakeCheck {
   public:
    SkipReasonThrows() : FakeCheck("skip throws") {}
    [[noreturn]] std::optional<std::string> getSkipReason() const override {
      throw std::runtime_error("forkpty failed");
    }
  };
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<SkipReasonThrows>());

  auto plan = fixmyfboss::CheckRunner("MONTBLANC").plan(checks);

  ASSERT_EQ(plan.size(), 1);
  EXPECT_EQ(plan[0].skipReason, std::nullopt);
  EXPECT_EQ(plan[0].skipReasonError, "forkpty failed");
}

TEST(CheckRunnerTest, ProgressSkipsChecksWhoseSkipReasonThrew) {
  class SkipReasonThrows : public FakeCheck {
   public:
    SkipReasonThrows() : FakeCheck("skip throws") {}
    [[noreturn]] std::optional<std::string> getSkipReason() const override {
      throw std::runtime_error("forkpty failed");
    }
  };
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<SkipReasonThrows>());
  checks.push_back(std::make_unique<FakeCheck>("runs"));
  const fixmyfboss::CheckRunner runner("MONTBLANC");
  std::vector<std::string> progress;

  auto results = runner.run(
      runner.plan(checks), [&](int index, int total, const auto& check) {
        progress.push_back(
            std::to_string(index) + "/" + std::to_string(total) + " " +
            check.getName());
      });

  EXPECT_EQ(progress, (std::vector<std::string>{"1/1 runs"}));
  ASSERT_EQ(results.size(), 2);
  EXPECT_EQ(*results[0].status(), CheckStatus::ERROR);
}

TEST(CheckRunnerTest, ProgressCountsOnlyChecksThatRun) {
  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(std::make_unique<FakeCheck>("first"));
  checks.push_back(std::make_unique<DisruptiveCheck>("skipped"));
  checks.push_back(std::make_unique<FakeCheck>("second"));
  const fixmyfboss::CheckRunner runner("MONTBLANC");
  std::vector<std::string> progress;

  runner.run(runner.plan(checks), [&](int index, int total, const auto& check) {
    progress.push_back(
        std::to_string(index) + "/" + std::to_string(total) + " " +
        check.getName());
  });

  EXPECT_EQ(progress, (std::vector<std::string>{"1/2 first", "2/2 second"}));
}
