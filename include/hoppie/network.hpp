#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace hoppie {

enum class NetworkPurpose { Ping, Send, Poll, Vatsim, SimBrief };
enum class HttpMethod { Get, Post };

struct NetworkTask {
    std::uint64_t id{};
    NetworkPurpose purpose{NetworkPurpose::Send};
    HttpMethod method{HttpMethod::Post};
    std::string url;
    std::string body;
    std::string contentType{"application/x-www-form-urlencoded"};
    unsigned timeoutSeconds{15};
};

struct NetworkResult {
    std::uint64_t id{};
    NetworkPurpose purpose{NetworkPurpose::Send};
    bool transportOk{false};
    int httpStatus{};
    std::string body;
    std::string errorCategory;
};

class NetworkWorker {
public:
    NetworkWorker();
    ~NetworkWorker();
    NetworkWorker(const NetworkWorker&) = delete;
    NetworkWorker& operator=(const NetworkWorker&) = delete;

    bool submit(NetworkTask task);
    bool tryPop(NetworkResult& result);
    void discardPending();
    void stop();

private:
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<NetworkTask> tasks_;
    std::deque<NetworkResult> results_;
    bool stopping_{false};
    std::thread thread_;
};

NetworkResult performPlatformHttps(const NetworkTask& task);

}  // namespace hoppie
