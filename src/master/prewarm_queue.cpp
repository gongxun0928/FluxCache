#include "master/prewarm_queue.h"

namespace fluxcache {

PrewarmQueue::PrewarmQueue(PathResolver* resolver) : resolver_(resolver) {
  worker_ = std::thread(&PrewarmQueue::WorkerLoop, this);
}

PrewarmQueue::~PrewarmQueue() {
  stop_.store(true);
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void PrewarmQueue::Submit(const std::string& path) {
  if (path.empty()) return;
  std::lock_guard<std::mutex> lock(mu_);
  queue_.push(path);
  cv_.notify_one();
}

size_t PrewarmQueue::PendingCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return queue_.size();
}

void PrewarmQueue::WorkerLoop() {
  while (!stop_.load()) {
    std::string path;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] {
        return stop_.load() || !queue_.empty();
      });
      if (stop_.load() && queue_.empty()) break;
      if (queue_.empty()) continue;
      path = std::move(queue_.front());
      queue_.pop();
    }
    if (resolver_) {
      (void)resolver_->PrewarmRecursive(path);
    }
  }
}

}  // namespace fluxcache
