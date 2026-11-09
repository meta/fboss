// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"

#include <gtest/gtest.h>

#include <folly/Subprocess.h>
#include <folly/system/Shell.h>

#include "fboss/lib/CommonUtils.h"

using namespace ::testing;
using namespace facebook::fboss;
using folly::literals::shell_literals::operator""_shellify;

DEFINE_string(oob_asset, "", "name/IP of the oob");
DEFINE_string(oob_flash_device_name, "", "name of the device to flash");
DEFINE_string(openbmc_password, "", "password to access the oob as root");

class AgentEnsembleOpenBmcUpgradeTest : public AgentEnsembleLinkTest {
 private:
  void rebootOob(int waitAfterRebootSeconds = 300) const {
    XLOG(DBG2) << "Rebooting oob....";
    folly::Subprocess p(
        "sshpass -p {} ssh root@{} reboot"_shellify(
            FLAGS_openbmc_password, FLAGS_oob_asset),
        folly::Subprocess::Options().pipeStdout());
    auto [stdOut, stdErr] = p.communicate();
    int exitStatus = p.wait().exitStatus();
    if (exitStatus != 0) {
      XLOG(ERR) << "Result str = " << stdOut;
      XLOG(ERR) << "Err str = " << stdErr;
      if (stdErr.find("closed by remote host") == std::string::npos) {
        throw FbossError("Reboot command failed : ", stdErr);
      }
    }
    /* sleep override */
    sleep(waitAfterRebootSeconds);
  }

  void waitForSshAccessToOob() const {
    WITH_RETRIES({
      folly::Subprocess p(
          "sshpass -p {} ssh root@{} ls"_shellify(
              FLAGS_openbmc_password, FLAGS_oob_asset),
          folly::Subprocess::Options().pipeStdout());
      auto [stdOut, stdErr] = p.communicate();
      int exitStatus = p.wait().exitStatus();
      EXPECT_EVENTUALLY_EQ(exitStatus, 0)
          << "Result str = " << stdOut << "\nErr str = " << stdErr;
    });
  }

  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    return {
        link_test_production_features::LinkTestProductionFeature::L1_LINK_TEST,
        link_test_production_features::LinkTestProductionFeature::BMC_CLASSIC};
  }

 protected:
  void openBmcSanityCheck() const {
    XLOG(DBG2) << "Checking ssh access to oob";
    waitForSshAccessToOob();
    folly::Subprocess p(
        "ping6 -c 5 {}"_shellify(FLAGS_oob_asset),
        folly::Subprocess::Options().pipeStdout());
    auto [stdOut, stdErr] = p.communicate();
    int exitStatus = p.wait().exitStatus();
    if (exitStatus != 0) {
      XLOG(ERR) << "Result str = " << stdOut;
      XLOG(ERR) << "Err str = " << stdErr;
      throw FbossError("OpenBMC Sanity check failed : ", stdErr);
    }
    XLOG(DBG2) << "OpenBMC sanity check passed";
  }

  void upgradeOpenBmc() const {
    folly::Subprocess p(
        "sshpass -p {} ssh root@{} /run/scripts/run_flashy.sh --device {}"_shellify(
            FLAGS_openbmc_password,
            FLAGS_oob_asset,
            FLAGS_oob_flash_device_name),
        folly::Subprocess::Options().pipeStdout());
    auto [stdOut, stdErr] = p.communicate();
    int exitStatus = p.wait().exitStatus();
    if (exitStatus != 0) {
      XLOG(ERR) << "Result str = " << stdOut;
      XLOG(ERR) << "Err str = " << stdErr;
      throw FbossError("OpenBMC upgrade failed : ", stdErr);
    }
    // Reboot OOB
    rebootOob();
    XLOG(DBG2) << "OpenBMC upgrade successful! Starting OpenBMC sanity check";
    openBmcSanityCheck();

    XLOG(DBG2) << "OpenBMC version after upgrade : " << openBmcVersion();
  }

  std::string openBmcVersion() const {
    folly::Subprocess p(
        "sshpass -p {} ssh root@{} head -n 1 /etc/issue"_shellify(
            FLAGS_openbmc_password, FLAGS_oob_asset),
        folly::Subprocess::Options().pipeStdout());
    auto [stdOut, stdErr] = p.communicate();
    int exitStatus = p.wait().exitStatus();
    if (exitStatus != 0) {
      XLOG(ERR) << "Result str = " << stdOut;
      XLOG(ERR) << "Err str = " << stdErr;
      throw FbossError("Reading OpenBMC version failed : ", stdErr);
    }
    return stdOut;
  }
};

TEST_F(AgentEnsembleOpenBmcUpgradeTest, openBmcHitlessUpgrade) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::BMC_UPGRADE});
  addTestedPorts(getCabledPorts());
  // Do an initial sanity check on the OpenBmc
  openBmcSanityCheck();
  XLOG(DBG2) << "OpenBMC version before upgrade : " << openBmcVersion();

  // Start traffic
  createL3DataplaneFlood();
  // Assert traffic is clean before upgrading OpenBMC
  assertNoInDiscards();

  // Upgrade OpenBMC
  upgradeOpenBmc();

  // Assert no traffic loss and no ecmp shrink. If ports flap
  // these conditions will not be true
  assertNoInDiscards();
  auto ecmpSizeInSw = getSingleVlanOrRoutedCabledPorts().size();
  auto client = getAgentEnsemble()->getHwAgentTestClient(SwitchID(0));
  facebook::fboss::utility::CIDRNetwork cidr;
  cidr.IPAddress() = "::";
  cidr.mask() = 0;
  EXPECT_EQ(client->sync_getHwEcmpSize(cidr, 0, ecmpSizeInSw), ecmpSizeInSw);
}
