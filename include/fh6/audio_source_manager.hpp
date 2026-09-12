#pragma once

#include "fh6/audio_source.hpp"
#include "fh6/ring_buffer.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <stop_token>
#include <unordered_map>
#include <vector>
#include <thread>

namespace fh6 {

// Owns every registered source plus the shared ring buffer. The DSP read
// callback consumes from ring(); pump_once() is the producer side.
class AudioSourceManager {
public:
    enum class TransportCommand { play, pause, stop, next, previous, restart, smart_skip };

    explicit AudioSourceManager(std::size_t ring_bytes);

    void register_source(std::unique_ptr<IAudioSource> src);

    // Pulls a source out. If it was active, ring is drained + active cleared.
    std::unique_ptr<IAudioSource> unregister_source(std::string_view name);

    // Hot-swap: stop old (reaping its subprocess tree), drain ring, start new.
    // False if name unknown.
    bool switch_to(std::string_view name);

    // Queue a command for the current source. The source is looked up again
    // by name and generation on the worker, so no asynchronous job owns a
    // potentially dangling IAudioSource pointer.
    using TransportCompletion = std::function<void(bool)>;
    bool enqueue_active_transport(TransportCommand command, TransportCompletion completion = {}, int restart_seconds = 30);
    bool enqueue_transport(IAudioSource* expected, TransportCommand command,
                           TransportCompletion completion = {}, int restart_seconds = 30);

    IAudioSource* active() const noexcept { return active_.load(std::memory_order_acquire); }
    RingBuffer& ring() noexcept { return ring_; }

    IAudioSource* find(std::string_view name) const;
    std::vector<IAudioSource*> sources_snapshot() const;
    void pump_once();

    void shutdown() noexcept;

private:
    struct TransportRequest {
        std::string source_name;
        std::uint64_t generation = 0;
        TransportCommand command = TransportCommand::next;
        TransportCompletion completion;
        int restart_seconds = 30;
    };

    void transport_loop(std::stop_token token);

    // Serializes transport with source replacement; PCM pumping never takes it.
    std::mutex source_operation_mutex_;
    mutable std::mutex swap_mutex_;
    RingBuffer ring_;
    std::unordered_map<std::string, std::unique_ptr<IAudioSource>> sources_;
    std::atomic<IAudioSource*> active_{nullptr};
    std::uint64_t active_generation_ = 0;

    std::mutex transport_mutex_;
    std::condition_variable_any transport_cv_;
    std::deque<TransportRequest> transport_queue_;
    std::jthread transport_thread_;
};

} // namespace fh6
