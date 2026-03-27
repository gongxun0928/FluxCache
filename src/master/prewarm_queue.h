#pragma once

#include "master/path_resolver.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace fluxcache {

/// PrewarmQueue accepts logical paths and processes them asynchronously
/// via PrewarmRecursive. Thread-safe.
class PrewarmQueue {
 public:
  explicit PrewarmQueue(PathResolver* resolver);
  ~PrewarmQueue();

  PrewarmQueue(const PrewarmQueue&) = delete;
  PrewarmQueue& operator=(const PrewarmQueue&) = delete;

  /// Submit a path for prewarming. Returns immediately.
  void Submit(const std::string& path);

  /// Returns the number of pending tasks.
  size_t PendingCount() const;

 private:
  void WorkerLoop();

  PathResolver* resolver_;
  std::queue<std::string> queue_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

}  // namespace fluxcache
