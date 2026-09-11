#include "fh6/single_flight_worker.hpp"
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <cstdio>

int main() {
    fh6::SingleFlightWorker worker;
    assert(!worker.busy());
    std::promise<void> gpu_done;
    auto fence = gpu_done.get_future().share();
    auto resource = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = resource;
    assert(worker.try_reserve());
    const auto start = std::chrono::steady_clock::now();
    worker.dispatch([resource, fence] { fence.wait(); });
    resource.reset();
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
    // Deliberately leave the GPU unfinished; submission must already be back.
    assert(worker.busy());
    assert(!worker.try_reserve());
    assert(!lifetime.expired());
    gpu_done.set_value();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (worker.busy() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    assert(!worker.busy());
    assert(lifetime.expired());
    assert(worker.try_reserve());
    worker.release(); // failed resource preparation frees the reservation
    assert(worker.try_reserve());
    worker.release();
    std::puts("PASS: dispatch returns before GPU completes; resources stay alive; busy slot refuses new work; completion frees resources and slot");
}
