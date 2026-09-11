#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <d3d12.h>
#include "fh6/worker/worker_client.hpp"
#include "fh6/config_store.hpp"
#include "fh6/deps.hpp"

namespace fh6 {
class TextureInjector {
public:
    static TextureInjector& instance() {
        static TextureInjector inst;
        return inst;
    }

    void update_artwork_url(const std::string& url);
    ~TextureInjector();
    
    // the DX12 hook will call this to see if there's a new image ready to be converted
    bool has_pending_pixels() const noexcept { return has_new_image_.load(std::memory_order_acquire); }
    bool pop_pending_pixels(std::vector<uint8_t>& out_pixels, int& out_width, int& out_height);

    // lets the control loop know if currently building a new texture
    bool is_processing() const { return is_processing_.load(); }

    void set_target_height(int height);

    void set_worker_client(std::shared_ptr<worker::WorkerClient> w) { 
        std::lock_guard<std::mutex> lock(mtx_);
        worker_ = std::move(w); 
    }

    void set_config_store(ConfigStore* store) {
        std::lock_guard<std::mutex> lock(mtx_);
        config_store_ = store;
    }

    void set_deps(DependencyManager* deps) {
        std::lock_guard<std::mutex> lock(mtx_);
        deps_ = deps;
    }

private:
    struct ArtworkRequest {
        std::string url;
        std::uint64_t job_id = 0;
        std::shared_ptr<worker::WorkerClient> worker;
    };

    struct CachedArtwork {
        std::string url;
        int height = 0;
        std::vector<std::uint8_t> pixels;
        std::uint64_t use_tick = 0;
    };

    TextureInjector();
    void worker_loop(std::stop_token stop);
    void process_request(const ArtworkRequest& request);
    bool try_publish_cached_locked(const std::string& url, int height);
    void remember_cached_locked(std::string url, int height, std::vector<std::uint8_t> pixels);

    std::mutex mtx_;
    std::vector<uint8_t> pending_pixels_;
    int width_ = 0;
    int height_ = 0;
    std::atomic<bool> has_new_image_{false};
    std::string current_url_;
    bool has_current_request_ = false;

    std::mutex request_mtx_;
    std::condition_variable request_cv_;
    std::optional<ArtworkRequest> pending_request_;
    std::jthread worker_thread_;
    std::deque<CachedArtwork> cache_;
    std::size_t cached_bytes_ = 0;
    std::uint64_t cache_tick_ = 0;

    std::atomic<bool> is_processing_{false}; 
    std::atomic<uint64_t> latest_job_id_{0};
    std::atomic<int> target_height_{0};

    std::shared_ptr<worker::WorkerClient> worker_;
    ConfigStore* config_store_ = nullptr;
    DependencyManager* deps_ = nullptr;
};
} // namespace fh6
