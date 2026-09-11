#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <d3d12.h>
#include <optional>
#include <thread>
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
    
    // the DX12 hook will call this to see if there's a new image ready to be converted
    bool has_pending_pixels() const noexcept { return has_new_image_.load(std::memory_order_acquire); }
    bool pop_pending_pixels(std::vector<uint8_t>& out_pixels, int& out_width, int& out_height);

    // lets the control loop know if currently building a new texture
    bool is_processing() const { return is_processing_.load(); }

    // A new target can have the same dimensions as the old one. Still bump
    // the generation so the current cover is republished after recreation.
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
        uint64_t job_id = 0;
        std::shared_ptr<worker::WorkerClient> worker;
    };

    struct CachedArtwork {
        std::string url;
        int height = 0;
        std::vector<uint8_t> pixels;
    };

    TextureInjector();
    ~TextureInjector();
    TextureInjector(const TextureInjector&) = delete;
    TextureInjector& operator=(const TextureInjector&) = delete;

    void worker_loop();
    void process_request(const ArtworkRequest& request);
    bool publish_cached(const ArtworkRequest& request, int target_height,
                       uint64_t target_generation);
    bool is_current(uint64_t job_id) const noexcept;
    void cache_insert(std::string url, int height, std::vector<uint8_t> pixels);

    std::mutex mtx_;
    std::condition_variable cv_;
    bool stopping_ = false;
    std::optional<ArtworkRequest> pending_request_;
    std::atomic<bool> target_refresh_requested_{false};
    std::string current_url_;
    std::vector<CachedArtwork> cache_;
    std::size_t cache_bytes_ = 0;
    std::vector<uint8_t> pending_pixels_;
    int width_ = 0;
    int height_ = 0;
    std::atomic<bool> has_new_image_{false};
    bool has_completed_artwork_ = false;
    std::string completed_url_;
    int completed_height_ = 0;

    std::atomic<bool> is_processing_{false}; 
    std::atomic<uint64_t> latest_job_id_{0};
    std::atomic<int> target_height_{0};
    std::atomic<uint64_t> target_generation_{0};

    std::shared_ptr<worker::WorkerClient> worker_;
    ConfigStore* config_store_ = nullptr;
    DependencyManager* deps_ = nullptr;
    // Keep this last: its constructor starts worker_loop(), so every field it
    // can inspect must already have completed construction.
    std::thread worker_thread_;
};
} // namespace fh6
