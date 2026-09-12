#include "fh6/audio_source_manager.hpp"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <future>

namespace {
class MockSource final : public fh6::IAudioSource {
public:
    explicit MockSource(const char* id) : id_{id} {}

    std::string_view name() const noexcept override { return id_; }
    std::string_view display_name() const noexcept override { return id_; }
    bool initialize() override { return true; }
    void shutdown() noexcept override {}
    void play() override { ++plays_; }
    void pause() override { ++pauses_; }
    void stop() override { ++stops_; }
    void next() override { ++nexts_; }
    bool restart_current() override {
        ++restarts_;
        while (block_restart.load()) std::this_thread::yield();
        return true;
    }
    fh6::TrackInfo current_track() const override { return {}; }
    fh6::PlaybackState playback_state() const noexcept override {
        return fh6::PlaybackState::playing;
    }
    fh6::AuthState auth_state() const noexcept override { return fh6::AuthState::none_required; }
    fh6::SourceCapabilities capabilities() const noexcept override { return {}; }

    std::atomic<bool> block_restart{false};
    int nexts() const { return nexts_; }
    int restarts() const { return restarts_; }

private:
    std::string id_;
    std::atomic<int> plays_{0};
    std::atomic<int> pauses_{0};
    std::atomic<int> stops_{0};
    std::atomic<int> nexts_{0};
    std::atomic<int> restarts_{0};
};

bool wait_for_next(const MockSource& source) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (source.nexts() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return source.nexts() == 1;
}
} // namespace

int main() {
    fh6::AudioSourceManager manager{4096};
    auto first = std::make_unique<MockSource>("first");
    auto* first_ptr = first.get();
    auto second = std::make_unique<MockSource>("second");
    auto* second_ptr = second.get();
    manager.register_source(std::move(first));
    manager.register_source(std::move(second));
    assert(manager.switch_to("first"));

    const auto start = std::chrono::steady_clock::now();
    assert(manager.enqueue_transport(first_ptr, fh6::AudioSourceManager::TransportCommand::next));
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
    assert(wait_for_next(*first_ptr));

    std::atomic<bool> restart_completed{false};
    assert(manager.enqueue_transport(first_ptr,
        fh6::AudioSourceManager::TransportCommand::restart,
        [&restart_completed](bool succeeded) { restart_completed.store(succeeded); }));
    const auto restart_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!restart_completed.load() && std::chrono::steady_clock::now() < restart_deadline)
        std::this_thread::yield();
    assert(first_ptr->restarts() == 1 && restart_completed.load());

    assert(manager.switch_to("second"));
    assert(!manager.enqueue_transport(first_ptr, fh6::AudioSourceManager::TransportCommand::next));
    assert(manager.enqueue_transport(second_ptr, fh6::AudioSourceManager::TransportCommand::next));
    assert(wait_for_next(*second_ptr));
    // A slow seek must not prevent PCM pumping or another enqueue.
    second_ptr->block_restart.store(true);
    assert(manager.enqueue_active_transport(fh6::AudioSourceManager::TransportCommand::restart));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!second_ptr->restarts() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(second_ptr->restarts() == 1);
    auto pump = std::async(std::launch::async, [&] { manager.pump_once(); });
    const bool pump_ready = pump.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
    auto enqueue = std::async(std::launch::async, [&] {
        return manager.enqueue_active_transport(fh6::AudioSourceManager::TransportCommand::next);
    });
    const bool enqueue_ready = enqueue.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
    // Replacement must wait for the operation that still owns the source.
    auto removal = std::async(std::launch::async, [&] { return manager.unregister_source("second"); });
    const bool removal_waits = removal.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
    second_ptr->block_restart.store(false);
    pump.get();
    enqueue.get();
    auto removed = removal.get();
    assert(pump_ready && enqueue_ready && removal_waits && removed.get() == second_ptr);
    manager.shutdown();
    std::puts("PASS: transport enqueue is asynchronous and rejects stale source generations");
}
