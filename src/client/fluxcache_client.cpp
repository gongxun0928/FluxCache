#include "client/fluxcache_client.h"
#include "common/metrics/http_metrics_server.h"
#include "common/metrics/metrics_registry.h"
#include "common/metrics/slow_request_tracker.h"
#include "common/types.h"
#include "master.pb.h"
#include "worker.pb.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

namespace fluxcache {

FluxCacheClient::FluxCacheClient(const ClientConfig& config)
    : master_address_(config.master_host + ":" +
                      std::to_string(config.master_port)),
      page_size_(config.page_size > 0 ? config.page_size : 1024 * 1024),
      prefetch_blocks_(config.prefetch_blocks),
      batch_read_max_blocks_(config.batch_read_max_blocks > 0
                                 ? config.batch_read_max_blocks
                                 : 8),
      retry_policy_(RetryPolicyFromConfig(config)),
      resilience_config_(ResilienceConfigFromClientConfig(config)),
      pool_(ChannelPoolOptions{config.channel_pool_size > 0
                                  ? config.channel_pool_size
                                  : 4}),
      ring_fetched_(false) {
  MetricsRegistry* metrics_ptr = nullptr;
  SlowRequestTracker* slow_tracker_ptr = nullptr;
  if (config.metrics_port > 0) {
    metrics_registry_ = std::make_unique<MetricsRegistry>();
    slow_request_tracker_ = std::make_unique<SlowRequestTracker>(1.0);
    metrics_ptr = metrics_registry_.get();
    slow_tracker_ptr = slow_request_tracker_.get();
    metrics_registry_->RegisterPrometheusExporter([this]() {
      return slow_request_tracker_->ExportPrometheusFragment();
    });
    http_metrics_server_ = std::make_unique<HttpMetricsServer>(
        config.metrics_port, metrics_registry_.get());
    http_metrics_server_->RegisterDebugEndpoint(
        "/debug/slow-requests",
        [this]() { return slow_request_tracker_->FormatDebug(); });
    http_metrics_server_->Start();
  }
  CircuitBreaker* master_cb = nullptr;
  if (config.circuit_breaker_enabled) {
    CircuitBreaker::Options opts;
    opts.failure_threshold = config.circuit_breaker_failure_threshold > 0
                                 ? config.circuit_breaker_failure_threshold
                                 : 5;
    opts.open_duration_ms = config.circuit_breaker_open_duration_ms > 0
                                ? config.circuit_breaker_open_duration_ms
                                : 30000;
    opts.half_open_probes = config.circuit_breaker_half_open_probes > 0
                                ? config.circuit_breaker_half_open_probes
                                : 1;
    opts.metrics = metrics_ptr;
    master_circuit_breaker_ = std::make_unique<CircuitBreaker>(opts);
    master_cb = master_circuit_breaker_.get();
  }
  master_client_ = std::make_unique<MasterClient>(
      &pool_, master_address_, resilience_config_, retry_policy_, master_cb,
      metrics_ptr);
  if (config.local_cache_enabled && config.local_cache_size_bytes > 0) {
    cache_ = std::make_unique<ClientPageCache>(config.local_cache_size_bytes);
  }
}

FluxCacheClient::~FluxCacheClient() {
  std::lock_guard<std::mutex> lock(prefetch_tasks_mutex_);
  for (auto& task : prefetch_tasks_) {
    try {
      task.get();
    } catch (...) {
      // Prefetch is best-effort; never throw from destructor.
    }
  }
  prefetch_tasks_.clear();
}
void FluxCacheClient::ReapCompletedPrefetchTasksLocked() {
  auto done = prefetch_tasks_.begin();
  while (done != prefetch_tasks_.end()) {
    if (done->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      done->get();
      done = prefetch_tasks_.erase(done);
      continue;
    }
    ++done;
  }
}

Status FluxCacheClient::RefreshRing() {
  auto result = master_client_->GetHashRing();
  if (!result.ok()) {
    return result.status();
  }
  cached_ring_.Update(result.value());
  ring_fetched_.store(true, std::memory_order_release);
  return Status::OK();
}

StatusOr<WorkerId> FluxCacheClient::GetWorkerForBlock(BlockId block_id) {
  if (!ring_fetched_.load(std::memory_order_acquire)) {
    auto s = RefreshRing();
    if (!s.ok()) {
      return s;
    }
  }

  WorkerId wid = cached_ring_.GetWorker(block_id);
  if (wid == 0) {
    return Status::NotFound("no worker in ring");
  }
  return wid;
}

StatusOr<std::unique_ptr<WorkerClient>> FluxCacheClient::GetWorkerClient(
    WorkerId worker_id) {
  if (!ring_fetched_.load(std::memory_order_acquire)) {
    auto s = RefreshRing();
    if (!s.ok()) {
      return s;
    }
  }

  std::string addr = cached_ring_.GetWorkerAddress(worker_id);
  if (addr.empty()) {
    return Status::NotFound("worker not in ring");
  }

  CircuitBreaker* worker_cb = GetOrCreateWorkerCircuitBreaker(addr);
  MetricsRegistry* metrics_ptr = metrics_registry_ ? metrics_registry_.get()
                                                    : nullptr;
  SlowRequestTracker* slow_tracker_ptr =
      slow_request_tracker_ ? slow_request_tracker_.get() : nullptr;
  return std::make_unique<WorkerClient>(&pool_, addr, resilience_config_,
                                        retry_policy_, worker_cb, metrics_ptr,
                                        slow_tracker_ptr);
}

CircuitBreaker* FluxCacheClient::GetOrCreateWorkerCircuitBreaker(
    const std::string& address) {
  std::lock_guard<std::mutex> lock(worker_cbs_mutex_);
  auto it = worker_circuit_breakers_.find(address);
  if (it != worker_circuit_breakers_.end()) {
    return it->second.get();
  }
  CircuitBreaker::Options opts;
  opts.failure_threshold = 5;
  opts.open_duration_ms = 30000;
  opts.half_open_probes = 1;
  opts.metrics = metrics_registry_ ? metrics_registry_.get() : nullptr;
  auto cb = std::make_unique<CircuitBreaker>(opts);
  CircuitBreaker* ptr = cb.get();
  worker_circuit_breakers_[address] = std::move(cb);
  return ptr;
}

void FluxCacheClient::SetRingForTest(const proto::GetHashRingResponse& resp) {
  cached_ring_.Update(resp);
  ring_fetched_.store(true, std::memory_order_release);
}

StatusOr<std::string> FluxCacheClient::Read(const std::string& path,
                                            uint64_t offset, uint64_t size,
                                            bool* stale_out) {
  return ReadInternal(path, offset, size, stale_out, true);
}

StatusOr<std::string> FluxCacheClient::ReadInternal(const std::string& path,
                                                    uint64_t offset,
                                                    uint64_t size,
                                                    bool* stale_out,
                                                    bool allow_prefetch) {
  auto fi_result = master_client_->GetFileInfo(path);
  if (!fi_result.ok()) {
    return fi_result.status();
  }
  const proto::GetFileInfoResponse& fi_resp = fi_result.value();
  const proto::FileInfo& fi = fi_resp.file_info();

  if (fi.is_directory()) {
    return Status::InvalidArgument("cannot read directory");
  }
  uint64_t file_size = fi.size();
  if (offset >= file_size) {
    return std::string();
  }
  uint64_t end_offset = offset + size;
  if (end_offset > file_size) {
    size = file_size - offset;
    end_offset = file_size;
  }

  size_t block_size = fi.block_size();
  if (block_size == 0) {
    return Status::InvalidArgument("invalid block_size from Master");
  }
  InodeId inode_id = fi.inode_id();
  int64_t expected_mtime_ms = fi.ufs_mtime_ms();
  const std::string& ufs_uri = fi_resp.ufs_uri();
  const std::string& ufs_path = fi_resp.ufs_path();

  if (ufs_uri.empty() || ufs_path.empty()) {
    return Status::InvalidArgument("GetFileInfo missing ufs_uri/ufs_path");
  }

  proto::GetHashRingResponse ring_resp;
  ring_resp.set_ring_version(fi_resp.ring_version());
  for (const auto& w : fi_resp.workers()) {
    *ring_resp.add_workers() = w;
  }
  cached_ring_.Update(ring_resp);
  ring_fetched_.store(true, std::memory_order_release);

  uint32_t first_block_idx =
      static_cast<uint32_t>(offset / block_size);
  uint32_t last_block_idx =
      static_cast<uint32_t>((end_offset - 1) / block_size);

  // Per-block state: block_idx, block_id, block_start, overlap, page range,
  // page_data, missing_pages.
  struct BlockReadState {
    uint32_t bi;
    BlockId block_id;
    uint64_t block_start;
    uint64_t overlap_start;
    uint64_t overlap_end;
    uint32_t first_page;
    uint32_t last_page;
    std::unordered_map<uint32_t, std::string> page_data;
    std::vector<uint32_t> missing_pages;
  };
  std::vector<BlockReadState> blocks;
  blocks.reserve(last_block_idx - first_block_idx + 1);
  bool any_stale = false;

  for (uint32_t bi = first_block_idx; bi <= last_block_idx; ++bi) {
    BlockId block_id = MakeBlockId(inode_id, bi);
    uint64_t block_start = static_cast<uint64_t>(bi) * block_size;
    size_t block_len =
        GetBlockLength(file_size, bi, static_cast<size_t>(block_size));
    uint64_t block_end = block_start + block_len;

    uint64_t overlap_start = std::max(offset, block_start);
    uint64_t overlap_end = std::min(end_offset, block_end);
    if (overlap_start >= overlap_end) continue;

    uint64_t block_local_start = overlap_start - block_start;
    uint64_t block_local_end = overlap_end - block_start;

    uint32_t first_page = static_cast<uint32_t>(block_local_start / page_size_);
    uint32_t last_page =
        static_cast<uint32_t>((block_local_end - 1) / page_size_);

    BlockReadState state{bi, block_id, block_start, overlap_start, overlap_end,
                        first_page, last_page, {}, {}};
    for (uint32_t pi = first_page; pi <= last_page; ++pi) {
      PageId page_id = MakePageId(block_id, static_cast<uint16_t>(pi));
      if (cache_) {
        auto cached = cache_->Get(page_id, expected_mtime_ms);
        if (cached) {
          state.page_data[pi] = std::string(cached->begin(), cached->end());
          continue;
        }
      }
      state.missing_pages.push_back(pi);
    }
    blocks.push_back(std::move(state));
  }

  // Group consecutive blocks by worker, then batch or single RPC.
  WorkerId prev_worker = 0;
  std::vector<BlockReadState*> batch;
  auto flush_batch = [&](WorkerId wid) -> Status {
    if (batch.empty()) return Status::OK();
    auto client_result = GetWorkerClient(wid);
    if (!client_result.ok()) return client_result.status();
    auto* wc = client_result.value().get();

    if (batch.size() == 1) {
      auto* st = batch[0];
      proto::ReadPagesRequest req;
      req.set_block_id(st->block_id);
      req.set_expected_mtime_ms(expected_mtime_ms);
      req.set_ufs_uri(ufs_uri);
      req.set_ufs_path(ufs_path);
      for (uint32_t pi : st->missing_pages) req.add_page_indices(pi);

      proto::ReadPagesResponse read_resp;
      Status s = wc->ReadPages(req, &read_resp);
      if (!s.ok()) {
        Status refresh_s = RefreshRing();
        if (refresh_s.ok()) {
          auto wr = GetWorkerForBlock(st->block_id);
          if (wr.ok()) {
            auto cr = GetWorkerClient(wr.value());
            if (cr.ok()) s = cr.value()->ReadPages(req, &read_resp);
          }
        }
        if (!s.ok()) return s;
      }
      if (read_resp.stale()) any_stale = true;
      const std::string& data = read_resp.data();
      size_t pos = 0;
      for (uint32_t pi : st->missing_pages) {
        if (pos >= data.size()) break;
        size_t chunk_len = std::min(page_size_, data.size() - pos);
        std::string chunk = data.substr(pos, chunk_len);
        st->page_data[pi] = chunk;
        if (cache_) {
          std::vector<uint8_t> vec(chunk.begin(), chunk.end());
          cache_->Put(MakePageId(st->block_id, static_cast<uint16_t>(pi)),
                     std::move(vec), expected_mtime_ms);
        }
        pos += chunk_len;
      }
    } else {
      proto::BatchReadPagesRequest batch_req;
      for (auto* st : batch) {
        auto* r = batch_req.add_requests();
        r->set_block_id(st->block_id);
        r->set_expected_mtime_ms(expected_mtime_ms);
        r->set_ufs_uri(ufs_uri);
        r->set_ufs_path(ufs_path);
        for (uint32_t pi : st->missing_pages) r->add_page_indices(pi);
      }
      proto::BatchReadPagesResponse batch_resp;
      Status s = wc->BatchReadPages(batch_req, &batch_resp);
      if (!s.ok()) {
        Status refresh_s = RefreshRing();
        if (refresh_s.ok()) {
          auto wr = GetWorkerForBlock(batch[0]->block_id);
          if (wr.ok()) {
            auto cr = GetWorkerClient(wr.value());
            if (cr.ok()) s = cr.value()->BatchReadPages(batch_req, &batch_resp);
          }
        }
        if (!s.ok()) return s;
      }
      for (int i = 0; i < batch_resp.stale_size(); ++i) {
        if (batch_resp.stale(i)) any_stale = true;
      }
      for (size_t i = 0; i < batch.size(); ++i) {
        auto* st = batch[i];
        const std::string& data =
            i < static_cast<size_t>(batch_resp.block_data_size())
                ? batch_resp.block_data(static_cast<int>(i))
                : "";
        size_t pos = 0;
        for (uint32_t pi : st->missing_pages) {
          if (pos >= data.size()) break;
          size_t chunk_len = std::min(page_size_, data.size() - pos);
          std::string chunk = data.substr(pos, chunk_len);
          st->page_data[pi] = chunk;
          if (cache_) {
            std::vector<uint8_t> vec(chunk.begin(), chunk.end());
            cache_->Put(MakePageId(st->block_id, static_cast<uint16_t>(pi)),
                       std::move(vec), expected_mtime_ms);
          }
          pos += chunk_len;
        }
      }
    }
    batch.clear();
    return Status::OK();
  };

  for (auto& st : blocks) {
    auto worker_result = GetWorkerForBlock(st.block_id);
    if (!worker_result.ok()) return worker_result.status();
    WorkerId wid = worker_result.value();

    if (!st.missing_pages.empty()) {
      if (wid != prev_worker && prev_worker != 0) {
        Status s = flush_batch(prev_worker);
        if (!s.ok()) return s;
      }
      prev_worker = wid;
      batch.push_back(&st);
      if (batch.size() >= batch_read_max_blocks_) {
        Status s = flush_batch(wid);
        if (!s.ok()) return s;
        prev_worker = 0;
      }
    }
  }
  if (prev_worker != 0) {
    Status s = flush_batch(prev_worker);
    if (!s.ok()) return s;
  }

  if (stale_out) *stale_out = any_stale;
  std::string result;
  for (const auto& st : blocks) {
    std::string assembled;
    for (uint32_t pi = st.first_page; pi <= st.last_page; ++pi) {
      auto it = st.page_data.find(pi);
      if (it != st.page_data.end()) assembled += it->second;
    }
    size_t skip = (st.overlap_start - st.block_start) % page_size_;
    size_t take = static_cast<size_t>(st.overlap_end - st.overlap_start);
    if (skip < assembled.size() && take > 0) {
      size_t available = assembled.size() - skip;
      result += assembled.substr(skip, std::min(take, available));
    }
  }

  // Prefetch next blocks when sequential read.
  if (allow_prefetch && prefetch_blocks_ > 0 && cache_ &&
      last_block_idx + 1 < GetBlockCount(file_size, block_size)) {
    uint32_t prefetch_start = last_block_idx + 1;
    uint32_t prefetch_end = std::min(
        prefetch_start + static_cast<uint32_t>(prefetch_blocks_),
        GetBlockCount(file_size, block_size));
    std::string path_copy = path;
    std::lock_guard<std::mutex> lock(prefetch_tasks_mutex_);
    ReapCompletedPrefetchTasksLocked();
    prefetch_tasks_.emplace_back(
        std::async(std::launch::async, [this, path_copy, prefetch_start,
                                        prefetch_end, block_size, file_size]() {
          uint64_t off = static_cast<uint64_t>(prefetch_start) * block_size;
          uint64_t len = static_cast<uint64_t>(prefetch_end - prefetch_start) *
                         block_size;
          if (off + len > file_size) len = file_size - off;
          (void)ReadInternal(path_copy, off, len, nullptr, false);
        }));
  }

  return result;
}

Status FluxCacheClient::Write(const std::string& path, uint64_t offset,
                             std::string_view data) {
  InodeId inode_id = 0;
  uint64_t file_size = 0;
  size_t block_size = 0;
  std::string ufs_uri, ufs_path;

  auto fi_result = master_client_->GetFileInfo(path);
  if (fi_result.ok()) {
    const proto::GetFileInfoResponse& fi_resp = fi_result.value();
    const proto::FileInfo& fi = fi_resp.file_info();
    if (fi.is_directory()) {
      return Status::InvalidArgument("cannot write directory");
    }
    inode_id = fi.inode_id();
    file_size = fi.size();
    block_size = fi.block_size();
    ufs_uri = fi_resp.ufs_uri();
    ufs_path = fi_resp.ufs_path();

    proto::GetHashRingResponse ring_resp;
    ring_resp.set_ring_version(fi_resp.ring_version());
    for (const auto& w : fi_resp.workers()) {
      *ring_resp.add_workers() = w;
    }
    cached_ring_.Update(ring_resp);
    ring_fetched_.store(true, std::memory_order_release);
  } else if (fi_result.status().code() == StatusCode::kNotFound) {
    auto create_result = master_client_->CreateFile(path);
    if (!create_result.ok()) {
      if (create_result.status().code() == StatusCode::kAlreadyExists) {
        fi_result = master_client_->GetFileInfo(path);
        if (!fi_result.ok()) return fi_result.status();
        const proto::GetFileInfoResponse& fi_resp = fi_result.value();
        const proto::FileInfo& fi = fi_resp.file_info();
        inode_id = fi.inode_id();
        file_size = fi.size();
        block_size = fi.block_size();
        ufs_uri = fi_resp.ufs_uri();
        ufs_path = fi_resp.ufs_path();
        proto::GetHashRingResponse ring_resp;
        ring_resp.set_ring_version(fi_resp.ring_version());
        for (const auto& w : fi_resp.workers()) {
          *ring_resp.add_workers() = w;
        }
        cached_ring_.Update(ring_resp);
        ring_fetched_.store(true, std::memory_order_release);
      } else {
        return create_result.status();
      }
    } else {
      const proto::CreateFileResponse& cr = create_result.value();
      inode_id = cr.file_info().inode_id();
      file_size = cr.file_info().size();
      block_size = cr.file_info().block_size();
      ufs_uri = cr.ufs_uri();
      ufs_path = cr.ufs_path();
      Status s = RefreshRing();
      if (!s.ok()) return s;
    }
  } else {
    return fi_result.status();
  }

  if (block_size == 0) {
    return Status::InvalidArgument("invalid block_size from Master");
  }
  if (ufs_uri.empty() || ufs_path.empty()) {
    return Status::InvalidArgument("missing ufs_uri/ufs_path");
  }

  uint64_t new_size = (offset + data.size() > file_size)
                          ? (offset + data.size())
                          : file_size;

  if (data.empty()) {
    if (new_size != file_size) {
      return master_client_->CompleteFile(inode_id, new_size, std::nullopt);
    }
    return Status::OK();
  }

  uint32_t first_block_idx =
      static_cast<uint32_t>(offset / block_size);
  uint32_t last_block_idx =
      static_cast<uint32_t>((offset + data.size() - 1) / block_size);

  int64_t last_mtime_ms = 0;

  for (uint32_t bi = first_block_idx; bi <= last_block_idx; ++bi) {
    BlockId block_id = MakeBlockId(inode_id, bi);
    uint64_t block_start = static_cast<uint64_t>(bi) * block_size;
    uint64_t block_end = block_start + block_size;
    uint64_t end_offset = offset + data.size();

    uint64_t overlap_start = std::max(offset, block_start);
    uint64_t overlap_end = std::min(end_offset, block_end);
    if (overlap_start >= overlap_end) continue;

    uint64_t block_local_start = overlap_start - block_start;
    uint64_t block_local_end = overlap_end - block_start;

    uint32_t first_page =
        static_cast<uint32_t>(block_local_start / page_size_);
    uint32_t last_page =
        static_cast<uint32_t>((block_local_end - 1) / page_size_);

    std::string page_data;
    for (uint32_t pi = first_page; pi <= last_page; ++pi) {
      uint64_t page_start_file =
          block_start + static_cast<uint64_t>(pi) * page_size_;
      uint64_t page_end_file = page_start_file + page_size_;
      uint64_t page_overlap_start = std::max(overlap_start, page_start_file);
      uint64_t page_overlap_end = std::min(overlap_end, page_end_file);

      size_t pad_before = page_overlap_start - page_start_file;
      size_t copy_len = page_overlap_end - page_overlap_start;
      size_t data_offset_in_buf = page_overlap_start - offset;

      std::string full_page(page_size_, '\0');
      if (copy_len > 0 && data_offset_in_buf < data.size()) {
        size_t actual_copy = std::min(copy_len, data.size() - data_offset_in_buf);
        data.copy(full_page.data() + pad_before, actual_copy, data_offset_in_buf);
      }
      page_data += full_page;
    }

    proto::WritePagesRequest req;
    req.set_block_id(block_id);
    req.set_ufs_uri(ufs_uri);
    req.set_ufs_path(ufs_path);
    for (uint32_t pi = first_page; pi <= last_page; ++pi) {
      req.add_page_indices(pi);
    }
    req.set_data(std::move(page_data));

    auto worker_result = GetWorkerForBlock(block_id);
    if (!worker_result.ok()) {
      return worker_result.status();
    }
    auto client_result = GetWorkerClient(worker_result.value());
    if (!client_result.ok()) {
      return client_result.status();
    }

    proto::WritePagesResponse write_resp;
    Status s = client_result.value()->WritePages(req, &write_resp);
    if (!s.ok()) {
      return s;
    }
    if (write_resp.has_ufs_mtime_ms()) {
      last_mtime_ms = write_resp.ufs_mtime_ms();
    }
  }

  return master_client_->CompleteFile(
      inode_id, new_size,
      last_mtime_ms != 0 ? std::optional<int64_t>(last_mtime_ms) : std::nullopt);
}

Status FluxCacheClient::Delete(const std::string& path) {
  auto fi_result = master_client_->GetFileInfo(path);
  InodeId inode_id = 0;
  if (fi_result.ok()) {
    inode_id = fi_result.value().file_info().inode_id();
  }
  Status s = master_client_->DeleteFile(path);
  if (s.ok() && cache_ && inode_id != 0) {
    cache_->InvalidateFile(inode_id);
  }
  return s;
}

}  // namespace fluxcache
