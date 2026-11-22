// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/qos/buffer_pool/CmdConfigQosBufferPool.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

class CmdConfigQosBufferPoolTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigQosBufferPoolTestFixture()
      : CmdConfigTestBase(
            "fboss_bp_test_%%%%-%%%%-%%%%-%%%%", // unique_path
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      }
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config qos buffer-pool";
};

// Test BufferPoolConfig argument validation
TEST_F(CmdConfigQosBufferPoolTestFixture, bufferPoolConfigValidation) {
  // Valid config with one attribute
  EXPECT_NO_THROW(BufferPoolConfig({"ingress_pool", "shared-bytes", "1000"}));
  EXPECT_NO_THROW(
      BufferPoolConfig({"egress-lossy-pool", "headroom-bytes", "2000"}));
  EXPECT_NO_THROW(BufferPoolConfig({"Pool1", "reserved-bytes", "3000"}));

  // Valid config with multiple attributes
  EXPECT_NO_THROW(BufferPoolConfig(
      {"test_pool", "shared-bytes", "1000", "headroom-bytes", "2000"}));
  EXPECT_NO_THROW(BufferPoolConfig(
      {"test_pool",
       "shared-bytes",
       "1000",
       "headroom-bytes",
       "2000",
       "reserved-bytes",
       "3000"}));

  // Empty should throw
  EXPECT_THROW(BufferPoolConfig({}), std::invalid_argument);

  // Name only (no attributes) should throw
  EXPECT_THROW(BufferPoolConfig({"pool1"}), std::invalid_argument);

  // Name with only one arg (odd number after name) should throw
  EXPECT_THROW(
      BufferPoolConfig({"pool1", "shared-bytes"}), std::invalid_argument);

  // Invalid pool names - must start with letter
  EXPECT_THROW(
      BufferPoolConfig({"123pool", "shared-bytes", "1000"}),
      std::invalid_argument);
  EXPECT_THROW(
      BufferPoolConfig({"_pool", "shared-bytes", "1000"}),
      std::invalid_argument);

  // Invalid attribute name
  EXPECT_THROW(
      BufferPoolConfig({"pool1", "invalid-attr", "1000"}),
      std::invalid_argument);

  // Note: Invalid bytes values (negative, non-numeric) are validated in
  // queryClient, not in the constructor. This allows the command to be
  // constructed and provide better error messages at execution time.
}

// Test BufferPoolConfig getters
TEST_F(CmdConfigQosBufferPoolTestFixture, bufferPoolConfigGetters) {
  BufferPoolConfig config(
      {"test_pool", "shared-bytes", "1000", "headroom-bytes", "2000"});

  EXPECT_EQ(config.getName(), "test_pool");

  const auto& attrs = config.getAttributes();
  ASSERT_EQ(attrs.size(), 2);
  EXPECT_EQ(attrs[0].first, "shared-bytes");
  EXPECT_EQ(attrs[0].second, "1000");
  EXPECT_EQ(attrs[1].first, "headroom-bytes");
  EXPECT_EQ(attrs[1].second, "2000");
}

// Test shared-bytes command creates buffer pool config
TEST_F(CmdConfigQosBufferPoolTestFixture, sharedBytesCreatesBufferPool) {
  setupTestableConfigSession(cmdPrefix_, "test_pool shared-bytes 50000");

  auto cmd = CmdConfigQosBufferPool();
  BufferPoolConfig config(getCmdArgsList());

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully configured"));
  EXPECT_THAT(result, ::testing::HasSubstr("test_pool"));

  // Verify the config was actually modified
  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  ASSERT_TRUE(switchConfig.bufferPoolConfigs().has_value());

  auto& bufferPoolConfigs = *switchConfig.bufferPoolConfigs();
  auto it = bufferPoolConfigs.find("test_pool");
  ASSERT_NE(it, bufferPoolConfigs.end());
  EXPECT_EQ(*it->second.sharedBytes(), 50000);
}

// Test headroom-bytes command creates buffer pool config
TEST_F(CmdConfigQosBufferPoolTestFixture, headroomBytesCreatesBufferPool) {
  setupTestableConfigSession(cmdPrefix_, "headroom_pool headroom-bytes 10000");

  auto cmd = CmdConfigQosBufferPool();
  BufferPoolConfig config(getCmdArgsList());

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully configured"));
  EXPECT_THAT(result, ::testing::HasSubstr("headroom_pool"));

  // Verify the config was actually modified
  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  ASSERT_TRUE(switchConfig.bufferPoolConfigs().has_value());

  auto& bufferPoolConfigs = *switchConfig.bufferPoolConfigs();
  auto it = bufferPoolConfigs.find("headroom_pool");
  ASSERT_NE(it, bufferPoolConfigs.end());
  EXPECT_EQ(*it->second.sharedBytes(), 0); // Default value
  ASSERT_TRUE(it->second.headroomBytes().has_value());
  EXPECT_EQ(*it->second.headroomBytes(), 10000);
}

// Test reserved-bytes command creates buffer pool config
TEST_F(CmdConfigQosBufferPoolTestFixture, reservedBytesCreatesBufferPool) {
  setupTestableConfigSession(cmdPrefix_, "reserved_pool reserved-bytes 20000");

  auto cmd = CmdConfigQosBufferPool();
  BufferPoolConfig config(getCmdArgsList());

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully configured"));
  EXPECT_THAT(result, ::testing::HasSubstr("reserved_pool"));

  // Verify the config was actually modified
  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  ASSERT_TRUE(switchConfig.bufferPoolConfigs().has_value());

  auto& bufferPoolConfigs = *switchConfig.bufferPoolConfigs();
  auto it = bufferPoolConfigs.find("reserved_pool");
  ASSERT_NE(it, bufferPoolConfigs.end());
  EXPECT_EQ(*it->second.sharedBytes(), 0); // Default value
  ASSERT_TRUE(it->second.reservedBytes().has_value());
  EXPECT_EQ(*it->second.reservedBytes(), 20000);
}

// Test updating an existing buffer pool with multiple attributes
TEST_F(CmdConfigQosBufferPoolTestFixture, updateExistingBufferPool) {
  setupTestableConfigSession(
      cmdPrefix_,
      "existing_pool shared-bytes 30000 headroom-bytes 5000 reserved-bytes 2000");

  auto cmd = CmdConfigQosBufferPool();
  // Set all attributes in one command
  BufferPoolConfig config(getCmdArgsList());
  cmd.queryClient(localhost(), config);

  // Verify all values are set correctly
  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  ASSERT_TRUE(switchConfig.bufferPoolConfigs().has_value());

  auto& bufferPoolConfigs = *switchConfig.bufferPoolConfigs();
  auto it = bufferPoolConfigs.find("existing_pool");
  ASSERT_NE(it, bufferPoolConfigs.end());
  EXPECT_EQ(*it->second.sharedBytes(), 30000);
  ASSERT_TRUE(it->second.headroomBytes().has_value());
  EXPECT_EQ(*it->second.headroomBytes(), 5000);
  ASSERT_TRUE(it->second.reservedBytes().has_value());
  EXPECT_EQ(*it->second.reservedBytes(), 2000);
}

// Test printOutput for buffer-pool command
TEST_F(CmdConfigQosBufferPoolTestFixture, printOutputBufferPool) {
  auto cmd = CmdConfigQosBufferPool();
  std::string successMessage = "Successfully configured buffer-pool 'my_pool'";

  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
  cmd.printOutput(successMessage);
  std::cout.rdbuf(old);

  EXPECT_EQ(buffer.str(), successMessage + "\n");
}

} // namespace facebook::fboss
