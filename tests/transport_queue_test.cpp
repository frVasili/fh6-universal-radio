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
        return restart_ok.load();
    }
    void pump(fh6::RingBuffer& ring) override {
        const unsigned char sample[4] = {1, 2, 3, 4};
        ring.write(sample, sizeof(sample));
    }
    fh6::TrackInfo current_track() const override {
        fh6::TrackInfo info;
        info.position_ms = position_ms.load();
        return info;
    }
    fh6::PlaybackState playback_state() const noexcept override {
        return fh6::PlaybackState::playing;
    }
    fh6::AuthState auth_state() const noexcept override { return fh6::AuthState::none_required; }
    fh6::SourceCapabilities capabilities() const noexcept override { return {}; }

    std::atomic<bool> block_restart{false};
    std::atomic<bool> restart_ok{true};
    std::atomic<uint64_t> position_ms{0};
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
    // A race event below the threshold must restart even when execution is
    // delayed until the decoder/player position has crossed that threshold.
    fh6::AudioSourceManager smart{4096};
    auto mock = std::make_unique<MockSource>("jellyfin-test");
    auto* src = mock.get();
    smart.register_source(std::move(mock));
    assert(smart.switch_to("jellyfin-test"));
    smart.pump_once();
    assert(smart.ring().readable() == 4);
    src->block_restart = true;
    assert(smart.enqueue_active_transport(fh6::AudioSourceManager::TransportCommand::restart));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!src->restarts() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    assert(src->restarts() == 1);
    smart.pump_once();
    assert(smart.ring().readable() == 4); // no old-song refill during restart
    src->position_ms = 58000;
    std::promise<bool> restarted;
    assert(smart.enqueue_active_transport(fh6::AudioSourceManager::TransportCommand::smart_skip,
        [&](bool ok) { restarted.set_value(ok); }, 59));
    src->position_ms = 80000; // queue delay/buffer advancement must not change decision
    src->block_restart = false;
    auto result = restarted.get_future();
    assert(result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(result.get());
    assert(src->restarts() == 2 && src->nexts() == 0);
    assert(smart.ring().readable() == 0); // stale PCM removed after restart
    smart.pump_once();
    assert(smart.ring().readable() == 4); // pumping resumes normally

    src->position_ms = 59001;
    std::promise<bool> skipped;
    assert(smart.enqueue_active_transport(fh6::AudioSourceManager::TransportCommand::smart_skip,
        [&](bool ok) { skipped.set_value(ok); }, 59));
    result = skipped.get_future();
    assert(result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(result.get() && src->nexts() == 1 && src->restarts() == 2);
    assert(smart.ring().readable() == 0);

    // At the threshold inclusive, restart; unsupported restart leaves the
    // current song and queued PCM alone rather than silently skipping.
    src->position_ms = 59000;
    src->restart_ok = false;
    smart.pump_once();
    std::promise<bool> failed;
    assert(smart.enqueue_active_transport(fh6::AudioSourceManager::TransportCommand::smart_skip,
        [&](bool ok) { failed.set_value(ok); }, 59));
    result = failed.get_future();
    assert(result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(!result.get() && src->restarts() == 3 && src->nexts() == 1);
    assert(smart.ring().readable() == 4);
    smart.shutdown();
    std::puts("PASS: transport queue, event-time threshold, restart/next, and PCM cleanup");
}
