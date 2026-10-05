#include "fh6/audio_source_manager.hpp"
#include "fh6/log.hpp"

#include <utility>

namespace fh6 {

AudioSourceManager::AudioSourceManager(std::size_t ring_bytes)
    : ring_{ring_bytes}, transport_thread_{[this](std::stop_token token) {
          transport_loop(token);
      }} {}

void AudioSourceManager::register_source(std::unique_ptr<IAudioSource> src) {
    if (!src) return;
    std::lock_guard operation_lock{source_operation_mutex_};
    std::scoped_lock lk{swap_mutex_};
    auto key = std::string{src->name()};
    log::info("[mgr] registered source '{}' ({})", key, src->display_name());
    sources_.insert_or_assign(std::move(key), std::move(src));
}

std::unique_ptr<IAudioSource> AudioSourceManager::unregister_source(std::string_view name) {
    std::lock_guard operation_lock{source_operation_mutex_};
    std::scoped_lock lk{swap_mutex_};
    auto it = sources_.find(std::string{name});
    if (it == sources_.end()) return nullptr;

    auto removed = std::move(it->second);
    sources_.erase(it);

    if (active_.load(std::memory_order_acquire) == removed.get()) {
        removed->pause();
        ring_.drain();
        active_.store(nullptr, std::memory_order_release);
        ++active_generation_;
    }
    log::info("[mgr] unregistered source '{}'", removed->name());
    return removed;
}

bool AudioSourceManager::switch_to(std::string_view name) {
    std::lock_guard operation_lock{source_operation_mutex_};
    std::scoped_lock lk{swap_mutex_};
    auto it = sources_.find(std::string{name});
    if (it == sources_.end()) {
        log::warn("[mgr] switch_to('{}'): unknown source", name);
        return false;
    }
    auto* next = it->second.get();
    auto* prev = active_.load(std::memory_order_acquire);
    if (prev == next) return true;

    if (prev) {
        prev->stop();
    }
    ring_.drain();
    next->play();
    active_.store(next, std::memory_order_release);
    ++active_generation_;
    log::info("[mgr] active source = '{}'", next->name());
    return true;
}

bool AudioSourceManager::enqueue_active_transport(TransportCommand command,
                                                  TransportCompletion completion, int restart_seconds) {
    return enqueue_transport(nullptr, command, std::move(completion), restart_seconds);
}

bool AudioSourceManager::enqueue_transport(IAudioSource* expected, TransportCommand command,
                                           TransportCompletion completion, int restart_seconds) {
    std::lock_guard swap_lock{swap_mutex_};
    auto* current = active_.load(std::memory_order_acquire);
    if (!current || (expected && current != expected)) return false;

    TransportRequest request{std::string{current->name()}, active_generation_, command,
                             std::move(completion), restart_seconds, std::nullopt};
    if (command == TransportCommand::smart_skip)
        request.event_position_ms = current->current_track().position_ms;
    {
        std::lock_guard queue_lock{transport_mutex_};
        constexpr std::size_t kMaxQueuedTransport = 8;
        if (transport_queue_.size() >= kMaxQueuedTransport) return false;
        transport_queue_.push_back(std::move(request));
    }
    transport_cv_.notify_one();
    return true;
}

void AudioSourceManager::transport_loop(std::stop_token token) {
    for (;;) {
        TransportRequest request;
        {
            std::unique_lock lock{transport_mutex_};
            transport_cv_.wait(lock, token, [this] { return !transport_queue_.empty(); });
            if (token.stop_requested()) return;
            request = std::move(transport_queue_.front());
            transport_queue_.pop_front();
        }

        bool succeeded = false;
        {
            std::lock_guard operation_lock{source_operation_mutex_};
            IAudioSource* source = nullptr;
            const bool changes_audio = request.command == TransportCommand::next ||
                request.command == TransportCommand::previous ||
                request.command == TransportCommand::restart ||
                request.command == TransportCommand::smart_skip ||
                request.command == TransportCommand::stop;
            {
                std::lock_guard swap_lock{swap_mutex_};
                auto it = sources_.find(request.source_name);
                if (it != sources_.end() && active_.load(std::memory_order_acquire) == it->second.get() &&
                    request.generation == active_generation_) {
                    source = it->second.get();
                    transport_changing_audio_ = changes_audio;
                }
            }
            // Source replacement waits on operation_lock; pumping can continue
            // during external transport I/O without waiting on the registry lock.
            if (source) {
                switch (request.command) {
                    case TransportCommand::play: source->play(); succeeded = true; break;
                    case TransportCommand::pause: source->pause(); succeeded = true; break;
                    case TransportCommand::stop: source->stop(); succeeded = true; break;
                    case TransportCommand::next: succeeded = source->skip_next(); break;
                    case TransportCommand::previous: source->previous(); succeeded = true; break;
                    case TransportCommand::restart: succeeded = source->restart_current(); break;
                    case TransportCommand::smart_skip:
                        succeeded = source->smart_skip(request.restart_seconds, request.event_position_ms);
                        log::info("[mgr] smart skip source={} action={} event_position_ms={} threshold_ms={} succeeded={}",
                                  request.source_name,
                                  request.source_name == "spotify" ? "provider-position-check" :
                                      (restart_recent_track(request.event_position_ms.value_or(0), request.restart_seconds) ? "restart" : "next"),
                                  request.event_position_ms.value_or(0),
                                  song_restart_seconds(request.restart_seconds) * 1000, succeeded);
                        break;
                }
                // No producer can refill the ring until the completed action's
                // old PCM is discarded. Never discard audio on failed restart.
                std::lock_guard swap_lock{swap_mutex_};
                if (changes_audio && succeeded) ring_.drain();
                transport_changing_audio_ = false;
            }
        }
        if (request.completion) request.completion(succeeded);
    }
}

IAudioSource* AudioSourceManager::find(std::string_view name) const {
    std::scoped_lock lk{swap_mutex_};
    auto it = sources_.find(std::string{name});
    return it == sources_.end() ? nullptr : it->second.get();
}

std::vector<IAudioSource*> AudioSourceManager::sources_snapshot() const {
    std::scoped_lock lk{swap_mutex_};
    std::vector<IAudioSource*> out;
    out.reserve(sources_.size());
    for (const auto& [_, s] : sources_) out.push_back(s.get());
    return out;
}

void AudioSourceManager::pump_once() {
    // Lock so an unregister can't free the source mid-pump.
    std::scoped_lock lk{swap_mutex_};
    auto* a = active_.load(std::memory_order_acquire);
    if (a && !transport_changing_audio_) a->pump(ring_);
}

void AudioSourceManager::shutdown() noexcept {
    transport_thread_.request_stop();
    transport_cv_.notify_one();
    if (transport_thread_.joinable()) transport_thread_.join();

    std::scoped_lock lk{swap_mutex_};
    active_.store(nullptr, std::memory_order_release);
    for (auto& [_, s] : sources_) {
        try {
            s->shutdown();
        } catch (...) {}
    }
    sources_.clear();
}

} // namespace fh6
