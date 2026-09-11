#include "fh6/fmod/texture_injector.hpp"
#include "fh6/subprocess.hpp"
#include "fh6/log.hpp"
#include "fh6/artwork_codec.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <thread>
#include <windows.h>

// stb headers
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace fh6 {

namespace {
constexpr std::size_t kMaxCachedArtworkEntries = 4;
constexpr std::size_t kMaxCachedArtworkBytes   = 8u * 1024u * 1024u;
}

TextureInjector::TextureInjector()
    : worker_thread_([this](std::stop_token stop) { worker_loop(stop); }) {}

TextureInjector::~TextureInjector() {
    worker_thread_.request_stop();
    request_cv_.notify_one();
}

void TextureInjector::set_target_height(int height) {
    const int previous = target_height_.exchange(height, std::memory_order_acq_rel);
    if (previous == height) return;

    std::string url;
    bool has_request = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        url = current_url_;
        has_request = has_current_request_;
    }
    // A recreated target needs the current cover again even when its URL did
    // not change. update_artwork_url() can satisfy this from the encoded cache.
    if (has_request) update_artwork_url(url);
}

bool TextureInjector::try_publish_cached_locked(const std::string& url, int height) {
    if (height == 0) return false;
    for (auto& entry : cache_) {
        if (entry.height != height || entry.url != url) continue;
        entry.use_tick = ++cache_tick_;
        width_ = height == 208 || height == 392 ? 392 : 196;
        height_ = height;
        pending_pixels_ = entry.pixels;
        has_new_image_.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

void TextureInjector::remember_cached_locked(std::string url, int height,
                                             std::vector<std::uint8_t> pixels) {
    if (pixels.empty()) return;

    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->height == height && it->url == url) {
            cached_bytes_ -= it->pixels.size();
            cache_.erase(it);
            break;
        }
    }
    cached_bytes_ += pixels.size();
    cache_.push_back({std::move(url), height, std::move(pixels), ++cache_tick_});

    while (cache_.size() > kMaxCachedArtworkEntries || cached_bytes_ > kMaxCachedArtworkBytes) {
        auto oldest = std::min_element(cache_.begin(), cache_.end(),
                                       [](const auto& a, const auto& b) {
                                           return a.use_tick < b.use_tick;
                                       });
        if (oldest == cache_.end()) break;
        cached_bytes_ -= oldest->pixels.size();
        cache_.erase(oldest);
    }
}

void TextureInjector::update_artwork_url(const std::string& url) {
    const std::uint64_t my_job_id = latest_job_id_.fetch_add(1, std::memory_order_acq_rel) + 1;
    std::shared_ptr<worker::WorkerClient> local_worker;
    const int target_h = target_height_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        current_url_ = url;
        has_current_request_ = true;
        local_worker = worker_;
        if (try_publish_cached_locked(url, target_h)) {
            is_processing_.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> request_lock(request_mtx_);
            pending_request_.reset();
            return;
        }
    }

    is_processing_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(request_mtx_);
        pending_request_ = ArtworkRequest{url, my_job_id, std::move(local_worker)};
    }
    request_cv_.notify_one();
}

void TextureInjector::worker_loop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        ArtworkRequest request;
        {
            std::unique_lock<std::mutex> lock(request_mtx_);
            request_cv_.wait(lock, [&] { return stop.stop_requested() || pending_request_.has_value(); });
            if (stop.stop_requested()) return;
            request = std::move(*pending_request_);
            pending_request_.reset();
        }
        process_request(request);
    }
}

void TextureInjector::process_request(const ArtworkRequest& request) {
    const auto& url = request.url;
    const auto my_job_id = request.job_id;
    const auto& local_worker = request.worker;

    struct ProcessingGuard {
        TextureInjector& owner;
        std::uint64_t job_id;
        ~ProcessingGuard() {
            if (owner.latest_job_id_.load(std::memory_order_acquire) == job_id)
                owner.is_processing_.store(false, std::memory_order_release);
        }
    } guard{*this, my_job_id};

    try {
        auto canceled = [&] {
            return latest_job_id_.load(std::memory_order_acquire) != my_job_id;
        };
        if (canceled()) return;

        char dll_path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, dll_path, MAX_PATH);
        const std::filesystem::path game_dir = std::filesystem::path(dll_path).parent_path();
        const auto temp_dir_path = std::filesystem::temp_directory_path();
        const std::string raw_path = (temp_dir_path / ("fh6_raw_" + std::to_string(my_job_id))).string();
        struct FileCleanupGuard {
            std::string path;
            ~FileCleanupGuard() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } file_guard{raw_path};

        bool has_valid_source = false;
        bool is_default_artwork = false;
        if (!url.empty() && local_worker) {
            log::info("[dx12] job {}: delegating artwork download to worker process: {}", my_job_id, url);
            has_valid_source = local_worker->download_file(url, raw_path);
            if (!has_valid_source)
                log::warn("[dx12] job {}: worker failed to download artwork - falling back to default", my_job_id);
        }
        if (canceled()) return;

        if (!has_valid_source) {
            const auto default_art = game_dir / "fh6-radio" / "assets" / "default_artwork.png";
            if (!std::filesystem::exists(default_art)) {
                log::warn("[dx12] job {}: no default artwork found at {} - skipping texture update",
                          my_job_id, default_art.string());
                return;
            }
            std::error_code ec;
            std::filesystem::copy_file(default_art, raw_path,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                log::warn("[dx12] job {}: failed to copy default artwork", my_job_id);
                return;
            }
            is_default_artwork = true;
        }

        int width = 0, height = 0, channels = 0;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> image(
            stbi_load(raw_path.c_str(), &width, &height, &channels, 4), stbi_image_free);
        if (!image || canceled()) return;

        int target_h = 0;
        const auto height_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((target_h = target_height_.load(std::memory_order_acquire)) == 0) {
            if (canceled()) return;
            if (std::chrono::steady_clock::now() >= height_deadline) {
                log::warn("[dx12] job {}: timed out waiting for target texture height", my_job_id);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (target_h != 104 && target_h != 196 && target_h != 208 && target_h != 392) {
            log::warn("[dx12] job {}: unsupported target texture height {}", my_job_id, target_h);
            return;
        }

        const int target_w = (target_h == 208 || target_h == 392) ? 392 : 196;
        const int square_size = target_w == 392 ? 208 : 104;
        const int border_thickness = is_default_artwork ? 0 : square_size / 26;
        const float scale = std::max(static_cast<float>(square_size) / width,
                                     static_cast<float>(square_size) / height);
        const int new_w = std::max(1, static_cast<int>(width * scale));
        const int new_h = std::max(1, static_cast<int>(height * scale));

        std::vector<unsigned char> resized_data(static_cast<std::size_t>(new_w) * new_h * 4);
        stbir_resize_uint8_linear(image.get(), width, height, 0, resized_data.data(),
                                  new_w, new_h, 0, STBIR_RGBA);
        image.reset();
        std::vector<unsigned char> padded_data(static_cast<std::size_t>(target_w) * target_h * 4, 0);
        const int src_offset_x = (new_w - square_size) / 2;
        const int src_offset_y = (new_h - square_size) / 2;

        long long total_r = 0, total_g = 0, total_b = 0;
        for (int y = 0; y < square_size; ++y) {
            for (int x = 0; x < square_size; ++x) {
                const int src_idx = ((y + src_offset_y) * new_w + (x + src_offset_x)) * 4;
                total_r += resized_data[src_idx];
                total_g += resized_data[src_idx + 1];
                total_b += resized_data[src_idx + 2];
            }
        }
        const int pixel_count = square_size * square_size;
        const auto b_r = static_cast<unsigned char>(total_r / pixel_count * 0.6f);
        const auto b_g = static_cast<unsigned char>(total_g / pixel_count * 0.6f);
        const auto b_b = static_cast<unsigned char>(total_b / pixel_count * 0.6f);

        for (int y = 0; y < target_h; ++y) {
            for (int x = 0; x < square_size; ++x) {
                const int dst_idx = (y * target_w + x) * 4;
                const int orig_y = (y * square_size) / target_h;
                const bool border = x < border_thickness || x >= square_size - border_thickness ||
                                    orig_y < border_thickness || orig_y >= square_size - border_thickness;
                if (border) {
                    padded_data[dst_idx] = b_r;
                    padded_data[dst_idx + 1] = b_g;
                    padded_data[dst_idx + 2] = b_b;
                    padded_data[dst_idx + 3] = 255;
                } else {
                    const int src_idx = ((orig_y + src_offset_y) * new_w + (x + src_offset_x)) * 4;
                    std::memcpy(padded_data.data() + dst_idx, resized_data.data() + src_idx, 4);
                }
            }
        }

        auto pixels = encode_artwork_bc7(padded_data, target_w, target_h, canceled);
        if (pixels.empty() || canceled()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        if (canceled()) return;
        remember_cached_locked(url, target_h, std::move(pixels));
        try_publish_cached_locked(url, target_h);
        log::info("[dx12] job {} complete (CPU BC7, cached encoded payload)", my_job_id);
    } catch (const std::exception& e) {
        log::warn("[dx12] job {}: artwork pipeline failed: {}", my_job_id, e.what());
    } catch (...) {
        log::warn("[dx12] job {}: artwork pipeline failed with unknown exception", my_job_id);
    }
}

bool TextureInjector::pop_pending_pixels(std::vector<uint8_t>& out_pixels, int& out_width,
                                         int& out_height) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!has_new_image_.load(std::memory_order_acquire)) return false;

    out_pixels = std::move(pending_pixels_);
    out_width = width_;
    out_height = height_;
    has_new_image_.store(false, std::memory_order_release);
    return true;
}

} // namespace fh6
