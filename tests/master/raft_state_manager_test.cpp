#include "master/ha/raft_state_manager.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace fluxcache {

namespace {

std::string TempDir(const char* prefix) {
  std::filesystem::path tmp = std::filesystem::temp_directory_path();
  tmp /= prefix;
  std::string s = tmp.string();
  std::vector<char> buf(s.begin(), s.end());
  buf.push_back('\0');
  char* result = mkdtemp(buf.data());
  if (!result) return "";
  return std::string(result);
}

std::string BufferToString(const nuraft::buffer& buf) {
  return std::string(reinterpret_cast<const char*>(buf.data_begin()), buf.size());
}

struct LogMeta {
  uint32_t version = 0;
  uint64_t generation = 0;
  uint64_t start_index = 0;
  uint64_t next_slot = 0;
};

LogMeta ReadLogMeta(const std::string& state_dir) {
  std::ifstream in(std::filesystem::path(state_dir) / "log_meta.txt");
  LogMeta meta;
  in >> meta.version >> meta.generation >> meta.start_index >> meta.next_slot;
  return meta;
}

}  // namespace

TEST(RaftStateManagerTest, PersistsStateConfigAndLogsAcrossRestart) {
  std::string state_dir = TempDir("fluxcache_raft_mgr_state_XXXXXX");
  ASSERT_FALSE(state_dir.empty());

  {
    RaftStateManager mgr(1, "localhost:21001", state_dir);

    auto cfg = nuraft::cs_new<nuraft::cluster_config>();
    cfg->get_servers().push_back(nuraft::cs_new<nuraft::srv_config>(
        1, "localhost:21001"));
    cfg->get_servers().push_back(nuraft::cs_new<nuraft::srv_config>(
        2, "localhost:21002"));
    mgr.save_config(*cfg);

    nuraft::srv_state state;
    state.set_term(7);
    state.set_voted_for(2);
    mgr.save_state(state);

    auto log_store = mgr.load_log_store();
    auto payload = nuraft::buffer::alloc(3);
    std::memcpy(payload->data_begin(), "abc", 3);
    payload->pos(0);
    auto entry = nuraft::cs_new<nuraft::log_entry>(7, payload);
    EXPECT_EQ(log_store->append(entry), 1u);
    EXPECT_TRUE(log_store->flush());
  }

  {
    RaftStateManager mgr(1, "localhost:21001", state_dir);

    auto state = mgr.read_state();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->get_term(), 7u);
    EXPECT_EQ(state->get_voted_for(), 2);

    auto cfg = mgr.load_config();
    ASSERT_NE(cfg, nullptr);
    ASSERT_EQ(cfg->get_servers().size(), 2u);
    auto server2 = cfg->get_server(2);
    ASSERT_NE(server2, nullptr);
    EXPECT_EQ(server2->get_endpoint(), "localhost:21002");

    auto log_store = mgr.load_log_store();
    EXPECT_EQ(log_store->start_index(), 1u);
    EXPECT_EQ(log_store->next_slot(), 2u);

    auto entry = log_store->entry_at(1);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->get_term(), 7u);
    EXPECT_EQ(BufferToString(entry->get_buf()), "abc");
  }

  std::error_code ec;
  std::filesystem::remove_all(state_dir, ec);
}

TEST(RaftStateManagerTest, IgnoresUncommittedTempArtifactsDuringRecovery) {
  std::string state_dir = TempDir("fluxcache_raft_mgr_tmp_XXXXXX");
  ASSERT_FALSE(state_dir.empty());

  {
    RaftStateManager mgr(1, "localhost:22001", state_dir);
    auto cfg = nuraft::cs_new<nuraft::cluster_config>();
    cfg->get_servers().push_back(
        nuraft::cs_new<nuraft::srv_config>(1, "localhost:22001"));
    mgr.save_config(*cfg);

    nuraft::srv_state state;
    state.set_term(3);
    mgr.save_state(state);

    auto payload = nuraft::buffer::alloc(4);
    std::memcpy(payload->data_begin(), "good", 4);
    payload->pos(0);
    auto entry = nuraft::cs_new<nuraft::log_entry>(3, payload);
    EXPECT_EQ(mgr.load_log_store()->append(entry), 1u);
    EXPECT_TRUE(mgr.load_log_store()->flush());
  }

  const auto meta = ReadLogMeta(state_dir);
  const auto active_dir = std::filesystem::path(state_dir) /
                          ("logs_v" + std::to_string(meta.generation));
  const auto stray_tmp_dir = std::filesystem::path(state_dir) / "logs_v999.tmp";
  std::filesystem::create_directories(stray_tmp_dir);
  {
    std::ofstream out(stray_tmp_dir / "1.bin", std::ios::binary | std::ios::trunc);
    out << "bad";
  }
  {
    std::ofstream out(std::filesystem::path(state_dir) / "cluster_config.bin.tmp",
                      std::ios::binary | std::ios::trunc);
    out << "corrupt";
  }

  RaftStateManager mgr(1, "localhost:22001", state_dir);
  ASSERT_TRUE(mgr.IsHealthy());
  auto entry = mgr.load_log_store()->entry_at(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(BufferToString(entry->get_buf()), "good");
  EXPECT_TRUE(std::filesystem::exists(active_dir));

  std::error_code ec;
  std::filesystem::remove_all(state_dir, ec);
}

TEST(RaftStateManagerTest, MarksMissingActiveLogAsUnhealthy) {
  std::string state_dir = TempDir("fluxcache_raft_mgr_badlog_XXXXXX");
  ASSERT_FALSE(state_dir.empty());

  {
    RaftStateManager mgr(1, "localhost:23001", state_dir);
    auto payload = nuraft::buffer::alloc(3);
    std::memcpy(payload->data_begin(), "abc", 3);
    payload->pos(0);
    auto entry = nuraft::cs_new<nuraft::log_entry>(5, payload);
    EXPECT_EQ(mgr.load_log_store()->append(entry), 1u);
    EXPECT_TRUE(mgr.load_log_store()->flush());
  }

  const auto meta = ReadLogMeta(state_dir);
  const auto active_log = std::filesystem::path(state_dir) /
                          ("logs_v" + std::to_string(meta.generation)) / "1.bin";
  std::filesystem::remove(active_log);

  RaftStateManager mgr(1, "localhost:23001", state_dir);
  EXPECT_FALSE(mgr.IsHealthy());

  std::error_code ec;
  std::filesystem::remove_all(state_dir, ec);
}

}  // namespace fluxcache
