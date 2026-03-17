#ifdef FLUXCACHE_ENABLE_RAFT

#include "master/ha/raft_state_manager.h"
#include "in_memory_log_store.hxx"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <unistd.h>

namespace fluxcache {

namespace {

namespace fs = std::filesystem;
constexpr uint32_t kLogMetaVersion = 1;

fs::path ConfigFile(const fs::path& dir) { return dir / "cluster_config.bin"; }
fs::path StateFile(const fs::path& dir) { return dir / "srv_state.bin"; }
fs::path LogMetaFile(const fs::path& dir) { return dir / "log_meta.txt"; }

fs::path TempPathFor(const fs::path& path) {
  return path.string() + ".tmp";
}

fs::path VersionedLogDir(const fs::path& dir, uint64_t generation) {
  return dir / ("logs_v" + std::to_string(generation));
}

bool FsyncFile(const fs::path& path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  bool ok = (fsync(fd) == 0);
  close(fd);
  return ok;
}

bool FsyncDirectory(const fs::path& path) {
  if (path.empty()) return true;
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  bool ok = (fsync(fd) == 0);
  close(fd);
  return ok;
}

bool EnsureDirectories(const fs::path& path) {
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec) return false;
  return FsyncDirectory(path.parent_path());
}

bool WriteBytesToFile(const fs::path& path, const void* data, size_t size) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  if (size > 0) {
    out.write(reinterpret_cast<const char*>(data),
              static_cast<std::streamsize>(size));
  }
  out.flush();
  if (!out.good()) return false;
  out.close();
  return FsyncFile(path);
}

bool WriteBytesAtomically(const fs::path& path, const void* data, size_t size) {
  if (!EnsureDirectories(path.parent_path())) return false;

  const fs::path tmp_path = TempPathFor(path);
  std::error_code ec;
  fs::remove(tmp_path, ec);
  if (!WriteBytesToFile(tmp_path, data, size)) {
    fs::remove(tmp_path, ec);
    return false;
  }
  fs::rename(tmp_path, path, ec);
  if (ec) {
    fs::remove(tmp_path, ec);
    return false;
  }
  return FsyncDirectory(path.parent_path());
}

bool WriteBufferAtomically(const fs::path& path, const nuraft::buffer& buf) {
  return WriteBytesAtomically(path, buf.data_begin(), buf.size());
}

nuraft::ptr<nuraft::buffer> ReadBufferFromFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return nullptr;
  in.seekg(0, std::ios::end);
  std::streamsize size = in.tellg();
  in.seekg(0, std::ios::beg);
  if (size < 0) return nullptr;
  auto buf = nuraft::buffer::alloc(static_cast<size_t>(size));
  if (size > 0) {
    in.read(reinterpret_cast<char*>(buf->data_begin()), size);
    if (!in.good() && !in.eof()) return nullptr;
  }
  buf->pos(0);
  return buf;
}

struct LogMeta {
  uint64_t generation = 0;
  nuraft::ulong start_index = 1;
  nuraft::ulong next_slot = 1;
};

std::optional<LogMeta> ReadLogMeta(const fs::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) return std::nullopt;

  uint32_t version = 0;
  LogMeta meta;
  in >> version >> meta.generation >> meta.start_index >> meta.next_slot;
  if (in.fail() || version != kLogMetaVersion) return std::nullopt;
  if (meta.next_slot < meta.start_index) return std::nullopt;
  return meta;
}

bool WriteLogMetaAtomically(const fs::path& path, const LogMeta& meta) {
  std::ostringstream oss;
  oss << kLogMetaVersion << '\n'
      << meta.generation << '\n'
      << meta.start_index << '\n'
      << meta.next_slot << '\n';
  const std::string payload = oss.str();
  return WriteBytesAtomically(path, payload.data(), payload.size());
}

class PersistentLogStore : public nuraft::log_store {
 public:
  explicit PersistentLogStore(const std::string& storage_dir)
      : storage_dir_(storage_dir),
        backend_(nuraft::cs_new<nuraft::inmem_log_store>()) {
    fs::create_directories(storage_dir_);
    LoadFromDisk();
  }

  nuraft::ulong next_slot() const override { return backend_->next_slot(); }
  nuraft::ulong start_index() const override { return backend_->start_index(); }
  nuraft::ptr<nuraft::log_entry> last_entry() const override {
    return backend_->last_entry();
  }

  nuraft::ulong append(nuraft::ptr<nuraft::log_entry>& entry) override {
    auto idx = backend_->append(entry);
    if (!PersistAll()) healthy_ = false;
    return idx;
  }

  void write_at(nuraft::ulong index,
                nuraft::ptr<nuraft::log_entry>& entry) override {
    backend_->write_at(index, entry);
    if (!PersistAll()) healthy_ = false;
  }

  void end_of_append_batch(nuraft::ulong start, nuraft::ulong cnt) override {
    backend_->end_of_append_batch(start, cnt);
    if (!PersistAll()) healthy_ = false;
  }

  nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries(
      nuraft::ulong start, nuraft::ulong end) override {
    return backend_->log_entries(start, end);
  }

  nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries_ext(
      nuraft::ulong start, nuraft::ulong end,
      int64_t batch_size_hint_in_bytes = 0) override {
    return backend_->log_entries_ext(start, end, batch_size_hint_in_bytes);
  }

  nuraft::ptr<nuraft::log_entry> entry_at(nuraft::ulong index) override {
    return backend_->entry_at(index);
  }

  nuraft::ulong term_at(nuraft::ulong index) override {
    return backend_->term_at(index);
  }

  nuraft::ptr<nuraft::buffer> pack(nuraft::ulong index, int32_t cnt) override {
    return backend_->pack(index, cnt);
  }

  void apply_pack(nuraft::ulong index, nuraft::buffer& pack) override {
    backend_->apply_pack(index, pack);
    if (!PersistAll()) healthy_ = false;
  }

  bool compact(nuraft::ulong last_log_index) override {
    bool ok = backend_->compact(last_log_index);
    if (ok && !PersistAll()) healthy_ = false;
    return ok;
  }

  bool flush() override {
    bool ok = backend_->flush();
    if (ok && !PersistAll()) healthy_ = false;
    return ok;
  }

  nuraft::ulong last_durable_index() override {
    return backend_->last_durable_index();
  }

  bool IsHealthy() const { return healthy_; }
  void FailNextPersistForTest(uint32_t count) { fail_next_persist_count_ = count; }

 private:
  bool PersistAll() {
    if (MaybeFailPersistForTest()) {
      healthy_ = false;
      return false;
    }
    const uint64_t generation = next_generation_++;
    const fs::path final_dir = VersionedLogDir(storage_dir_, generation);
    const fs::path tmp_dir = final_dir.string() + ".tmp";
    std::error_code ec;

    fs::remove_all(tmp_dir, ec);
    if (!EnsureDirectories(tmp_dir)) return false;

    for (nuraft::ulong idx = backend_->start_index();
         idx < backend_->next_slot(); ++idx) {
      auto entry = backend_->entry_at(idx);
      if (!entry) return false;
      auto serialized = entry->serialize();
      if (!serialized) return false;
      const fs::path file =
          tmp_dir / (std::to_string(static_cast<uint64_t>(idx)) + ".bin");
      if (!WriteBufferAtomically(file, *serialized)) {
        fs::remove_all(tmp_dir, ec);
        return false;
      }
    }

    fs::rename(tmp_dir, final_dir, ec);
    if (ec) {
      fs::remove_all(tmp_dir, ec);
      return false;
    }
    if (!FsyncDirectory(final_dir.parent_path())) return false;

    LogMeta meta;
    meta.generation = generation;
    meta.start_index = backend_->start_index();
    meta.next_slot = backend_->next_slot();
    if (!WriteLogMetaAtomically(LogMetaFile(storage_dir_), meta)) return false;

    CleanupOldGenerations(generation);
    active_generation_ = generation;
    return true;
  }

  void LoadFromDisk() {
    uint64_t max_generation = 0;
    if (fs::exists(storage_dir_)) {
      for (const auto& item : fs::directory_iterator(storage_dir_)) {
        if (!item.is_directory()) continue;
        const std::string name = item.path().filename().string();
        if (!name.starts_with("logs_v")) continue;
        std::istringstream iss(name.substr(6));
        uint64_t generation = 0;
        iss >> generation;
        if (!iss.fail()) {
          max_generation = std::max(max_generation, generation);
        }
      }
    }
    next_generation_ = max_generation + 1;

    const auto meta = ReadLogMeta(LogMetaFile(storage_dir_));
    if (!meta) {
      if (fs::exists(LogMetaFile(storage_dir_))) healthy_ = false;
      return;
    }

    active_generation_ = meta->generation;
    const fs::path active_dir = VersionedLogDir(storage_dir_, meta->generation);
    if (!fs::exists(active_dir)) {
      healthy_ = false;
      return;
    }

    if (meta->start_index > 1) {
      backend_->compact(meta->start_index - 1);
    }

    for (nuraft::ulong idx = meta->start_index; idx < meta->next_slot; ++idx) {
      const fs::path path =
          active_dir / (std::to_string(static_cast<uint64_t>(idx)) + ".bin");
      auto buf = ReadBufferFromFile(path);
      if (!buf) {
        healthy_ = false;
        return;
      }
      auto entry = nuraft::log_entry::deserialize(*buf);
      if (!entry) {
        healthy_ = false;
        return;
      }
      if (idx == backend_->next_slot()) {
        backend_->append(entry);
      } else {
        backend_->write_at(idx, entry);
      }
    }
  }

  void CleanupOldGenerations(uint64_t active_generation) {
    std::error_code ec;
    for (const auto& item : fs::directory_iterator(storage_dir_)) {
      if (!item.is_directory()) continue;
      const std::string name = item.path().filename().string();
      if (!name.starts_with("logs_v")) continue;
      std::istringstream iss(name.substr(6));
      uint64_t generation = 0;
      iss >> generation;
      if (!iss.fail() && generation != active_generation) {
        fs::remove_all(item.path(), ec);
      }
    }
  }

  bool MaybeFailPersistForTest() {
    if (fail_next_persist_count_ == 0) return false;
    --fail_next_persist_count_;
    return fail_next_persist_count_ == 0;
  }

  fs::path storage_dir_;
  nuraft::ptr<nuraft::inmem_log_store> backend_;
  uint64_t active_generation_ = 0;
  uint64_t next_generation_ = 1;
  bool healthy_ = true;
  uint32_t fail_next_persist_count_ = 0;
};

}  // namespace

RaftStateManager::RaftStateManager(int srv_id, const std::string& endpoint,
                                   const std::string& storage_dir)
    : my_id_(srv_id),
      my_endpoint_(endpoint),
      storage_dir_(storage_dir),
      log_store_(nuraft::cs_new<PersistentLogStore>(storage_dir)) {
  fs::create_directories(storage_dir_);
  my_srv_config_ = nuraft::cs_new<nuraft::srv_config>(srv_id, endpoint);
  saved_config_ = nuraft::cs_new<nuraft::cluster_config>();
  saved_config_->get_servers().push_back(my_srv_config_);

  if (auto buf = ReadBufferFromFile(ConfigFile(storage_dir_))) {
    if (auto conf = nuraft::cluster_config::deserialize(*buf)) {
      saved_config_ = conf;
      loaded_config_from_disk_ = true;
    } else {
      healthy_ = false;
    }
  } else if (fs::exists(ConfigFile(storage_dir_))) {
    healthy_ = false;
  }

  if (auto buf = ReadBufferFromFile(StateFile(storage_dir_))) {
    saved_state_ = nuraft::srv_state::deserialize(*buf);
    if (!saved_state_) healthy_ = false;
  } else if (fs::exists(StateFile(storage_dir_))) {
    healthy_ = false;
  }

  if (auto* store = dynamic_cast<PersistentLogStore*>(log_store_.get())) {
    healthy_ = healthy_ && store->IsHealthy();
  }
}

nuraft::ptr<nuraft::cluster_config> RaftStateManager::load_config() {
  return saved_config_;
}

void RaftStateManager::save_config(const nuraft::cluster_config& config) {
  nuraft::ptr<nuraft::buffer> buf = config.serialize();
  saved_config_ = nuraft::cluster_config::deserialize(*buf);
  if (!WriteBufferAtomically(ConfigFile(storage_dir_), *buf)) healthy_ = false;
}

void RaftStateManager::save_state(const nuraft::srv_state& state) {
  nuraft::ptr<nuraft::buffer> buf = state.serialize();
  saved_state_ = nuraft::srv_state::deserialize(*buf);
  if (!WriteBufferAtomically(StateFile(storage_dir_), *buf)) healthy_ = false;
}

nuraft::ptr<nuraft::srv_state> RaftStateManager::read_state() {
  return saved_state_;
}

nuraft::ptr<nuraft::log_store> RaftStateManager::load_log_store() {
  return log_store_;
}

void RaftStateManager::ConfigureCluster(const std::vector<RaftPeerConfig>& peers) {
  if (loaded_config_from_disk_) return;

  auto config = nuraft::cs_new<nuraft::cluster_config>();
  config->get_servers().push_back(my_srv_config_);
  for (const auto& peer : peers) {
    if (peer.id == my_id_) continue;
    config->get_servers().push_back(
        nuraft::cs_new<nuraft::srv_config>(peer.id, peer.endpoint));
  }
  save_config(*config);
}

bool RaftStateManager::IsHealthy() const {
  auto* store = dynamic_cast<PersistentLogStore*>(log_store_.get());
  return healthy_ && (!store || store->IsHealthy());
}

void RaftStateManager::FailNextLogWriteForTest(uint32_t count) {
  if (auto* store = dynamic_cast<PersistentLogStore*>(log_store_.get())) {
    store->FailNextPersistForTest(count);
  }
}

int32_t RaftStateManager::server_id() {
  return my_id_;
}

void RaftStateManager::system_exit(const int /*exit_code*/) {}

}  // namespace fluxcache

#endif  // FLUXCACHE_ENABLE_RAFT
