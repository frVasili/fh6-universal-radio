#include "fh6/audio_source_manager.hpp"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

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
        return true;
    }
    fh6::TrackInfo current_track() const override { return {}; }
    fh6::PlaybackState playback_state() const noexcept override {
        return fh6::PlaybackState::playing;
    }
    fh6::AuthState auth_state() const noexcept override { return fh6::AuthState::none_required; }
    fh6::SourceCapabilities capabilities() const noexcept override { return {}; }

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
    while (first_ptr->restarts() == 0 && std::chrono::steady_clock::now() < restart_deadline)
        std::this_thread::yield();
    assert(first_ptr->restarts() == 1 && restart_completed.load());

    assert(manager.switch_to("second"));
    assert(!manager.enqueue_transport(first_ptr, fh6::AudioSourceManager::TransportCommand::next));
    assert(manager.enqueue_transport(second_ptr, fh6::AudioSourceManager::TransportCommand::next));
    assert(wait_for_next(*second_ptr));
    manager.shutdown();
    std::puts("PASS: transport enqueue is asynchronous and rejects stale source generations");
}
