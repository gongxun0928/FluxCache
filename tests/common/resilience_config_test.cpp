#include "common/rpc/resilience_config.h"
#include "common/config/config.h"
#include <gtest/gtest.h>

namespace fluxcache {

TEST(ResilienceConfigTest, DefaultTimeouts) {
  ResilienceConfig cfg;
  EXPECT_EQ(cfg.TimeoutSec(OpType::kMasterRead), 10);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kMasterWrite), 30);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kWorkerRead), 10);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kWorkerWrite), 30);
}

TEST(ResilienceConfigTest, CustomTimeouts) {
  ResilienceConfig cfg;
  cfg.timeout_master_read_sec = 20;
  cfg.timeout_worker_write_sec = 60;
  EXPECT_EQ(cfg.TimeoutSec(OpType::kMasterRead), 20);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kWorkerWrite), 60);
}

TEST(ResilienceConfigTest, FromClientConfig) {
  ClientConfig cc;
  cc.timeout_master_read_sec = 15;
  cc.timeout_master_write_sec = 45;
  cc.timeout_worker_read_sec = 12;
  cc.timeout_worker_write_sec = 40;
  ResilienceConfig cfg = ResilienceConfigFromClientConfig(cc);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kMasterRead), 15);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kMasterWrite), 45);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kWorkerRead), 12);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kWorkerWrite), 40);
}

TEST(ResilienceConfigTest, FromClientConfigUsesDefaultsWhenZero) {
  ClientConfig cc;
  cc.master_host = "127.0.0.1";
  cc.master_port = 9090;
  ResilienceConfig cfg = ResilienceConfigFromClientConfig(cc);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kMasterRead), 10);
  EXPECT_EQ(cfg.TimeoutSec(OpType::kMasterWrite), 30);
}

}  // namespace fluxcache
