#include "client/fluxcache_client.h"
#include "common/types.h"
#include "master.pb.h"
#include "worker.pb.h"
#include <algorithm>
#include <string>
#include <unordered_map>

namespace fluxcache {

FluxCacheClient::FluxCacheClient(const ClientConfig& config)
    : master_address_(config.master_host + ":" +
                      std::to_string(config.master_port)),
      page_size_(config.page_size > 0 ? config.page_size : 1024 * 1024),
      retry_policy_(RetryPolicyFromConfig(config)),
      pool_(ChannelPoolOptions{config.channel_pool_size > 0
                                  ? config.channel_pool_size
                                  : 4}),
      ring_fetched_(false) {
  master_client_ = std::make_unique<MasterClient>(
      &pool_, master_address_, 10, retry_policy_);
  if (config.local_cache_enabled && config.local_cache_size_bytes > 0) {
    cache_ = std::make_unique<ClientPageCache>(config.local_cache_size_bytes);
  }
}

Status FluxCacheClient::RefreshRing() {
  auto result = master_client_->GetHashRing();
  if (!result.ok()) {
    return result.status();
  }
  cached_ring_.Update(result.value());
  ring_fetched_ = true;
  return Status::OK();
}

StatusOr<WorkerId> FluxCacheClient::GetWorkerForBlock(BlockId block_id) {
  if (!ring_fetched_) {
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
  if (!ring_fetched_) {
    auto s = RefreshRing();
    if (!s.ok()) {
      return s;
    }
  }

  std::string addr = cached_ring_.GetWorkerAddress(worker_id);
  if (addr.empty()) {
    return Status::NotFound("worker not in ring");
  }

  return std::make_unique<WorkerClient>(&pool_, addr, 10, retry_policy_);
}

void FluxCacheClient::SetRingForTest(const proto::GetHashRingResponse& resp) {
  cached_ring_.Update(resp);
  ring_fetched_ = true;
}

StatusOr<std::string> FluxCacheClient::Read(const std::string& path,
                                            uint64_t offset, uint64_t size) {
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
  ring_fetched_ = true;

  uint32_t first_block_idx =
      static_cast<uint32_t>(offset / block_size);
  uint32_t last_block_idx =
      static_cast<uint32_t>((end_offset - 1) / block_size);

  std::string result;
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

    std::unordered_map<uint32_t, std::string> page_data;
    std::vector<uint32_t> missing_pages;

    for (uint32_t pi = first_page; pi <= last_page; ++pi) {
      PageId page_id = MakePageId(block_id, static_cast<uint16_t>(pi));
      if (cache_) {
        auto cached = cache_->Get(page_id, expected_mtime_ms);
        if (cached) {
          page_data[pi] =
              std::string(cached->begin(), cached->end());
          continue;
        }
      }
      missing_pages.push_back(pi);
    }

    if (!missing_pages.empty()) {
      proto::ReadPagesRequest req;
      req.set_block_id(block_id);
      req.set_expected_mtime_ms(expected_mtime_ms);
      req.set_ufs_uri(ufs_uri);
      req.set_ufs_path(ufs_path);
      for (uint32_t pi : missing_pages) {
        req.add_page_indices(pi);
      }

      auto worker_result = GetWorkerForBlock(block_id);
      if (!worker_result.ok()) {
        return worker_result.status();
      }
      auto client_result = GetWorkerClient(worker_result.value());
      if (!client_result.ok()) {
        return client_result.status();
      }

      proto::ReadPagesResponse read_resp;
      Status s = client_result.value()->ReadPages(req, &read_resp);
      if (!s.ok()) {
        Status refresh_s = RefreshRing();
        if (refresh_s.ok()) {
          worker_result = GetWorkerForBlock(block_id);
          if (worker_result.ok()) {
            client_result = GetWorkerClient(worker_result.value());
            if (client_result.ok()) {
              s = client_result.value()->ReadPages(req, &read_resp);
            }
          }
        }
        if (!s.ok()) {
          return s;
        }
      }

      const std::string& data = read_resp.data();
      size_t pos = 0;
      for (uint32_t pi : missing_pages) {
        if (pos >= data.size()) break;
        size_t chunk_len = std::min(page_size_, data.size() - pos);
        std::string chunk = data.substr(pos, chunk_len);
        page_data[pi] = chunk;
        if (cache_) {
          std::vector<uint8_t> vec(chunk.begin(), chunk.end());
          cache_->Put(MakePageId(block_id, static_cast<uint16_t>(pi)),
                     std::move(vec), expected_mtime_ms);
        }
        pos += chunk_len;
      }
    }

    std::string assembled;
    for (uint32_t pi = first_page; pi <= last_page; ++pi) {
      auto it = page_data.find(pi);
      if (it != page_data.end()) {
        assembled += it->second;
      }
    }
    size_t skip = block_local_start % page_size_;
    size_t take = static_cast<size_t>(overlap_end - overlap_start);
    if (skip < assembled.size() && take > 0) {
      size_t available = assembled.size() - skip;
      result += assembled.substr(skip, std::min(take, available));
    }
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
    ring_fetched_ = true;
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
        ring_fetched_ = true;
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
