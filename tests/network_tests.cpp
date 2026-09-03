#include "hoppie/network.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace hoppie {

NetworkResult performPlatformHttps(const NetworkTask& task) {
    return {task.id, task.purpose, true, 200, "ok", {}};
}

}  // namespace hoppie

namespace {

int failures = 0;
#define CHECK(expression)                                                                  \
    do {                                                                                   \
        if (!(expression)) {                                                               \
            std::cerr << "line " << __LINE__ << ": check failed: " #expression "\n";     \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

bool waitResult(hoppie::NetworkWorker& worker, hoppie::NetworkResult& result) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.tryPop(result)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

int main() {
    hoppie::NetworkTask defaults;
    CHECK(defaults.timeoutSeconds == 15);

    hoppie::NetworkWorker worker;
    hoppie::NetworkTask insecure{1, hoppie::NetworkPurpose::Send, hoppie::HttpMethod::Post,
                                 "http://example.invalid", {}, {}, 15};
    CHECK(!worker.submit(insecure));
    hoppie::NetworkResult result;
    CHECK(waitResult(worker, result));
    CHECK(!result.transportOk && result.errorCategory == "https_required");

    hoppie::NetworkTask secure{2, hoppie::NetworkPurpose::Poll, hoppie::HttpMethod::Post,
                               "https://example.invalid", {}, {}, 15};
    CHECK(worker.submit(secure));
    CHECK(waitResult(worker, result));
    CHECK(result.transportOk && result.id == 2 && result.httpStatus == 200);
    worker.stop();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All HoppieDatalinkXP network policy tests passed\n";
    return EXIT_SUCCESS;
}
