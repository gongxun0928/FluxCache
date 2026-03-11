#include "worker/worker_service_impl.h"

#include "common/metrics/metrics_registry.h"
#include "common/status.h"
#include "common/types.h"
#include "ufs/ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/page/page_store.h"

#include <chrono>
#include <grpcpp/grpcpp.h>
#include <string>

namespace fluxcache {

namespace {

struct RpcMetricsGuard {
  MetricsRegistry* m;
  std::string method;
  std::chrono::steady_clock::time_point start;
  RpcMetricsGuard(MetricsRegistry* m, const char* method)
      : m(m), method(method), start(std::chrono::steady_clock::now()) {}
  ~RpcMetricsGuard() {
    if (m) {
      auto sec = std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - start)
                     .count();
      m->ObserveLatency("worker", method, sec);
    }
  }
};

bool ParseUfsUri(const std::string& ufs_uri, std::string* scheme,
                  std::string* authority) {
  if (!scheme || !authority) return false;
  size_t pos = ufs_uri.find("://");
  if (pos == std::string::npos) return false;
  *scheme = ufs_uri.substr(0, pos);
  std::string rest = ufs_uri.substr(pos + 3);
  if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/') {
    *authority = rest.substr(1);
  } else {
    *authority = rest;
  }
  return true;
}

}  // namespace

WorkerServiceImpl::WorkerServiceImpl(PageStore* page_store, size_t page_size,
                                     size_t block_size,
                                     MetricsRegistry* metrics,
                                     bool allow_stale_read_on_ufs_timeout)
    : page_store_(page_store),
      page_size_(page_size),
      block_size_(block_size),
      metrics_(metrics),
      allow_stale_read_on_ufs_timeout_(allow_stale_read_on_ufs_timeout) {}

::grpc::Status WorkerServiceImpl::ReadPages(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::ReadPagesRequest* request,
    ::fluxcache::proto::ReadPagesResponse* response) {
  if (metrics_) metrics_->IncCounter("worker", "ReadPages");
  RpcMetricsGuard _guard(metrics_, "ReadPages");
  if (!request || !response) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "null request");
  }

  BlockId block_id = request->block_id();
  int64_t expected_mtime_ms = request->expected_mtime_ms();
  const std::string& ufs_uri = request->ufs_uri();
  const std::string& ufs_path = request->ufs_path();

  if (block_id == kInvalidBlockId || request->page_indices().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "block_id and page_indices required");
  }
  if (ufs_uri.empty() || ufs_path.empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "ufs_uri and ufs_path required");
  }

  std::string scheme, authority;
  if (!ParseUfsUri(ufs_uri, &scheme, &authority)) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "invalid ufs_uri format");
  }

  std::unique_ptr<UFS> ufs;
  Status s = CreateUFS(scheme, authority, &ufs);
  if (!s.ok()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, s.message());
  }
  if (!ufs) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, "CreateUFS returned null");
  }

  uint32_t block_index = GetBlockIndex(block_id);
  uint64_t block_offset = static_cast<uint64_t>(block_index) * block_size_;

  // Single-page fast path: avoid concatenated += page_data copy (zero-copy opt).
  if (request->page_indices().size() == 1) {
    uint32_t pi = request->page_indices(0);
    if (pi > 65535) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                            "page_index exceeds uint16 max");
    }
    uint16_t page_index = static_cast<uint16_t>(pi);
    PageId id{block_id, page_index};
    uint64_t offset = block_offset + static_cast<uint64_t>(page_index) * page_size_;

    std::string page_data;
    s = page_store_->GetPage(id, expected_mtime_ms, &page_data,
                             allow_stale_read_on_ufs_timeout_);
    if (!s.ok()) {
      if (s.code() != StatusCode::kNotFound) {
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, s.message());
      }
      auto ufs_start = std::chrono::steady_clock::now();
      s = ufs->Read(ufs_path, offset, page_size_, &page_data);
      if (metrics_) {
        metrics_->IncCounter("fluxcache_ufs_reads_total");
        if (s.ok()) {
          auto ufs_sec = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - ufs_start)
                             .count();
          metrics_->ObserveHistogram("fluxcache_ufs_read_duration_seconds", ufs_sec);
        }
      }
      if (!s.ok()) {
        if (allow_stale_read_on_ufs_timeout_) {
          s = page_store_->GetPageRelaxed(id, &page_data);
          if (s.ok()) {
            if (metrics_) metrics_->IncDegradationCounter("stale_read_total");
            response->set_data(std::move(page_data));
            response->set_stale(true);
            return ::grpc::Status::OK;
          }
        }
        return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, s.message());
      }
      s = page_store_->PutPage(id, page_data, expected_mtime_ms);
      if (!s.ok()) {
        return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                              s.message());
      }
    }
    response->set_data(std::move(page_data));
    return ::grpc::Status::OK;
  }

  std::string concatenated;
  bool any_stale = false;
  for (uint32_t pi : request->page_indices()) {
    if (pi > 65535) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                            "page_index exceeds uint16 max");
    }
    uint16_t page_index = static_cast<uint16_t>(pi);
    PageId id{block_id, page_index};
    uint64_t offset = block_offset + static_cast<uint64_t>(page_index) * page_size_;

    std::string page_data;
    s = page_store_->GetPage(id, expected_mtime_ms, &page_data,
                             allow_stale_read_on_ufs_timeout_);
    if (!s.ok()) {
      if (s.code() != StatusCode::kNotFound) {
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, s.message());
      }
      auto ufs_start = std::chrono::steady_clock::now();
      s = ufs->Read(ufs_path, offset, page_size_, &page_data);
      if (metrics_) {
        metrics_->IncCounter("fluxcache_ufs_reads_total");
        if (s.ok()) {
          auto ufs_sec = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - ufs_start)
                             .count();
          metrics_->ObserveHistogram("fluxcache_ufs_read_duration_seconds", ufs_sec);
        }
      }
      if (!s.ok()) {
          if (allow_stale_read_on_ufs_timeout_) {
            s = page_store_->GetPageRelaxed(id, &page_data);
            if (s.ok()) {
              any_stale = true;
              if (metrics_) metrics_->IncDegradationCounter("stale_read_total");
            concatenated += page_data;
            continue;
          }
        }
        return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, s.message());
      }
      s = page_store_->PutPage(id, page_data, expected_mtime_ms);
      if (!s.ok()) {
        return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                              s.message());
      }
    }
    concatenated += page_data;
  }

  response->set_data(std::move(concatenated));
  if (any_stale) response->set_stale(true);
  return ::grpc::Status::OK;
}

::grpc::Status WorkerServiceImpl::BatchReadPages(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::BatchReadPagesRequest* request,
    ::fluxcache::proto::BatchReadPagesResponse* response) {
  if (metrics_) metrics_->IncCounter("worker", "BatchReadPages");
  RpcMetricsGuard _guard(metrics_, "BatchReadPages");
  if (!request || !response) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "null request");
  }
  if (request->requests().empty()) {
    return ::grpc::Status::OK;
  }

  for (const auto& req : request->requests()) {
    BlockId block_id = req.block_id();
    int64_t expected_mtime_ms = req.expected_mtime_ms();
    const std::string& ufs_uri = req.ufs_uri();
    const std::string& ufs_path = req.ufs_path();

    if (block_id == kInvalidBlockId || req.page_indices().empty()) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                            "block_id and page_indices required");
    }
    if (ufs_uri.empty() || ufs_path.empty()) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                            "ufs_uri and ufs_path required");
    }

    std::string scheme, authority;
    if (!ParseUfsUri(ufs_uri, &scheme, &authority)) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid ufs_uri format");
    }

    std::unique_ptr<UFS> ufs;
    Status s = CreateUFS(scheme, authority, &ufs);
    if (!s.ok()) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, s.message());
    }
    if (!ufs) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "CreateUFS returned null");
    }

    uint32_t block_index = GetBlockIndex(block_id);
    uint64_t block_offset =
        static_cast<uint64_t>(block_index) * block_size_;

    // Single-page fast path: avoid concatenated += page_data copy (zero-copy opt).
    if (req.page_indices().size() == 1) {
      uint32_t pi = req.page_indices(0);
      if (pi > 65535) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                              "page_index exceeds uint16 max");
      }
      uint16_t page_index = static_cast<uint16_t>(pi);
      PageId id{block_id, page_index};
      uint64_t offset =
          block_offset + static_cast<uint64_t>(page_index) * page_size_;

      std::string page_data;
      s = page_store_->GetPage(id, expected_mtime_ms, &page_data,
                               allow_stale_read_on_ufs_timeout_);
      if (!s.ok()) {
        if (s.code() != StatusCode::kNotFound) {
          return ::grpc::Status(::grpc::StatusCode::INTERNAL, s.message());
        }
        auto ufs_start = std::chrono::steady_clock::now();
        s = ufs->Read(ufs_path, offset, page_size_, &page_data);
        if (metrics_) {
          metrics_->IncCounter("fluxcache_ufs_reads_total");
          if (s.ok()) {
            auto ufs_sec = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - ufs_start)
                               .count();
            metrics_->ObserveHistogram("fluxcache_ufs_read_duration_seconds", ufs_sec);
          }
        }
        if (!s.ok()) {
          if (allow_stale_read_on_ufs_timeout_) {
            s = page_store_->GetPageRelaxed(id, &page_data);
            if (s.ok()) {
              if (metrics_) metrics_->IncDegradationCounter("stale_read_total");
              response->add_block_data(std::move(page_data));
              response->add_stale(true);
              continue;
            }
          }
          return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, s.message());
        }
        s = page_store_->PutPage(id, page_data, expected_mtime_ms);
        if (!s.ok()) {
          return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                                s.message());
        }
      }
      response->add_block_data(std::move(page_data));
      response->add_stale(false);
      continue;
    }

    std::string concatenated;
    bool block_stale = false;
    for (uint32_t pi : req.page_indices()) {
      if (pi > 65535) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                              "page_index exceeds uint16 max");
      }
      uint16_t page_index = static_cast<uint16_t>(pi);
      PageId id{block_id, page_index};
      uint64_t offset =
          block_offset + static_cast<uint64_t>(page_index) * page_size_;

      std::string page_data;
      s = page_store_->GetPage(id, expected_mtime_ms, &page_data,
                               allow_stale_read_on_ufs_timeout_);
      if (!s.ok()) {
        if (s.code() != StatusCode::kNotFound) {
          return ::grpc::Status(::grpc::StatusCode::INTERNAL, s.message());
        }
        auto ufs_start = std::chrono::steady_clock::now();
        s = ufs->Read(ufs_path, offset, page_size_, &page_data);
        if (metrics_) {
          metrics_->IncCounter("fluxcache_ufs_reads_total");
          if (s.ok()) {
            auto ufs_sec = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - ufs_start)
                               .count();
            metrics_->ObserveHistogram("fluxcache_ufs_read_duration_seconds", ufs_sec);
          }
        }
        if (!s.ok()) {
          if (allow_stale_read_on_ufs_timeout_) {
            s = page_store_->GetPageRelaxed(id, &page_data);
            if (s.ok()) {
              block_stale = true;
              if (metrics_) metrics_->IncDegradationCounter("stale_read_total");
              concatenated += page_data;
              continue;
            }
          }
          return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, s.message());
        }
        s = page_store_->PutPage(id, page_data, expected_mtime_ms);
        if (!s.ok()) {
          return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                                s.message());
        }
      }
      concatenated += page_data;
    }

    response->add_block_data(std::move(concatenated));
    response->add_stale(block_stale);
  }
  return ::grpc::Status::OK;
}

::grpc::Status WorkerServiceImpl::WritePages(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::WritePagesRequest* request,
    ::fluxcache::proto::WritePagesResponse* response) {
  if (metrics_) metrics_->IncCounter("worker", "WritePages");
  RpcMetricsGuard _guard(metrics_, "WritePages");
  if (!request) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "null request");
  }

  BlockId block_id = request->block_id();
  const std::string& ufs_uri = request->ufs_uri();
  const std::string& ufs_path = request->ufs_path();
  const std::string& data = request->data();

  if (block_id == kInvalidBlockId || request->page_indices().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "block_id and page_indices required");
  }
  if (ufs_uri.empty() || ufs_path.empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "ufs_uri and ufs_path required");
  }

  size_t num_pages = static_cast<size_t>(request->page_indices().size());
  size_t expected_data_len = num_pages * page_size_;
  if (data.size() != expected_data_len) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "data length must equal page_indices.size() * page_size");
  }

  std::string scheme, authority;
  if (!ParseUfsUri(ufs_uri, &scheme, &authority)) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "invalid ufs_uri format");
  }

  std::unique_ptr<UFS> ufs;
  Status s = CreateUFS(scheme, authority, &ufs);
  if (!s.ok()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, s.message());
  }
  if (!ufs) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, "CreateUFS returned null");
  }

  uint32_t block_index = GetBlockIndex(block_id);
  uint64_t block_offset = static_cast<uint64_t>(block_index) * block_size_;

  // Phase 1: Write all pages to UFS. On any failure, return error without
  // updating PageStore (write-through atomic boundary).
  for (size_t i = 0; i < num_pages; ++i) {
    uint32_t pi = request->page_indices(static_cast<int>(i));
    if (pi > 65535) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                            "page_index exceeds uint16 max");
    }
    uint16_t page_index = static_cast<uint16_t>(pi);
    uint64_t offset = block_offset + static_cast<uint64_t>(page_index) * page_size_;
    std::string_view page_data(data.data() + i * page_size_, page_size_);

    s = ufs->Write(ufs_path, offset, page_data);
    if (!s.ok()) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL, s.message());
    }
  }

  // Phase 2: UFS writes succeeded. Get mtime and update PageStore.
  FileStatus status;
  s = ufs->GetStatus(ufs_path, &status);
  if (!s.ok()) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, s.message());
  }
  int64_t mtime_ms = status.mtime_ms;

  for (size_t i = 0; i < num_pages; ++i) {
    uint32_t pi = request->page_indices(static_cast<int>(i));
    uint16_t page_index = static_cast<uint16_t>(pi);
    PageId id{block_id, page_index};
    std::string_view page_data(data.data() + i * page_size_, page_size_);

    s = page_store_->PutPage(id, page_data, mtime_ms);
    if (!s.ok()) {
      return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED, s.message());
    }
  }

  if (response) {
    response->set_ufs_mtime_ms(mtime_ms);
  }
  return ::grpc::Status::OK;
}

::grpc::Status WorkerServiceImpl::Heartbeat(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::HeartbeatRequest* request,
    ::fluxcache::proto::HeartbeatResponse* response) {
  if (metrics_) metrics_->IncCounter("worker", "Heartbeat");
  RpcMetricsGuard _guard(metrics_, "Heartbeat");
  if (!request || !response) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "null request/response");
  }

  WorkerId my_worker_id = static_cast<WorkerId>(request->worker_id());
  std::vector<uint64_t> orphan_inode_ids(request->orphan_inode_ids().begin(),
                                         request->orphan_inode_ids().end());
  std::vector<uint64_t> misplaced_block_ids(request->misplaced_block_ids().begin(),
                                            request->misplaced_block_ids().end());
  std::vector<WorkerId> ring_worker_ids;
  for (const auto& ep : request->ring_workers()) {
    ring_worker_ids.push_back(static_cast<WorkerId>(ep.worker_id()));
  }

  auto audit = page_store_->ReconcileGc(orphan_inode_ids, misplaced_block_ids,
                                        my_worker_id, ring_worker_ids);

  for (uint64_t id : audit.audit_inode_ids) {
    response->add_audit_inode_ids(id);
  }
  for (uint64_t id : audit.audit_block_ids) {
    response->add_audit_block_ids(id);
  }
  return ::grpc::Status::OK;
}

}  // namespace fluxcache
