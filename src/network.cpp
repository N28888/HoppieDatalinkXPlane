#include "hoppie/network.hpp"

#include <utility>

namespace hoppie {

NetworkWorker::NetworkWorker() : thread_(&NetworkWorker::run, this) {}

NetworkWorker::~NetworkWorker() { stop(); }

bool NetworkWorker::submit(NetworkTask task) {
    NetworkResult rejected;
    if (task.url.rfind("https://", 0) != 0) {
        rejected.id = task.id;
        rejected.purpose = task.purpose;
        rejected.errorCategory = "https_required";
        std::lock_guard<std::mutex> lock(mutex_);
        results_.push_back(std::move(rejected));
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        tasks_.push_back(std::move(task));
    }
    wake_.notify_one();
    return true;
}

bool NetworkWorker::tryPop(NetworkResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty()) return false;
    result = std::move(results_.front());
    results_.pop_front();
    return true;
}

void NetworkWorker::discardPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
}

void NetworkWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        tasks_.clear();
    }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void NetworkWorker::run() {
    for (;;) {
        NetworkTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
            if (stopping_) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        auto result = performPlatformHttps(task);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_) results_.push_back(std::move(result));
        }
    }
}

}  // namespace hoppie
