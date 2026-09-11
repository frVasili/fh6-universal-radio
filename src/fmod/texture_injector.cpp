#include "fh6/fmod/texture_injector.hpp"

#include "fh6/artwork_codec.hpp"
#include "fh6/log.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace fh6 {
namespace {
constexpr std::size_t kMaxArtworkEntries = 4;
constexpr std::size_t kMaxArtworkBytes   = 8u << 20;
}

TextureInjector::TextureInjector() : worker_thread_([this] { worker_loop(); }) {}

TextureInjector::~TextureInjector() {
    {
        std::lock_guard lock{mtx_};
        stopping_ = true;
    }
    cv_.notify_one();
    if (worker_thread_.joinable()) worker_thread_.join();
}

void TextureInjector::set_target_height(int height) {
    if (height <= 0) return;
    target_height_.store(height, std::memory_order_release);
    target_generation_.fetch_add(1, std::memory_order_acq_rel);
    // Only signal the worker here. The render hook never copies cache data or
    // waits for artwork preparation.
    target_refresh_requested_.store(true, std::memory_order_release);
    cv_.notify_one();
}

void TextureInjector::update_artwork_url(const std::string& url) {
    const auto job_id = latest_job_id_.fetch_add(1, std::memory_order_acq_rel) + 1;
    is_processing_.store(true, std::memory_order_release);
    {
        std::lock_guard lock{mtx_};
        current_url_ = url;
        pending_request_ = ArtworkRequest{url, job_id, worker_};
        target_refresh_requested_.store(false, std::memory_order_release);
    }
    cv_.notify_one();
}

bool TextureInjector::is_current(uint64_t job_id) const noexcept {
    return latest_job_id_.load(std::memory_order_acquire) == job_id;
}

void TextureInjector::cache_insert(std::string url, int height, std::vector<uint8_t> pixels) {
    if (pixels.empty() || pixels.size() > kMaxArtworkBytes) return;
    std::lock_guard lock{mtx_};
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->url == url && it->height == height) {
            cache_bytes_ -= it->pixels.size();
            cache_.erase(it);
            break;
        }
    }
    while ((!cache_.empty() && cache_bytes_ + pixels.size() > kMaxArtworkBytes) ||
           cache_.size() >= kMaxArtworkEntries) {
        cache_bytes_ -= cache_.back().pixels.size();
        cache_.pop_back();
    }
    cache_bytes_ += pixels.size();
    cache_.insert(cache_.begin(), CachedArtwork{std::move(url), height, std::move(pixels)});
}

bool TextureInjector::publish_cached(const ArtworkRequest& request, int target_height,
                                     uint64_t target_generation) {
    std::lock_guard lock{mtx_};
    if (latest_job_id_.load(std::memory_order_acquire) != request.job_id ||
        target_height_.load(std::memory_order_acquire) != target_height ||
        target_generation_.load(std::memory_order_acquire) != target_generation)
        return false;
    auto it = std::find_if(cache_.begin(), cache_.end(), [&](const CachedArtwork& item) {
        return item.url == request.url && item.height == target_height;
    });
    if (it == cache_.end()) return false;

    pending_pixels_ = it->pixels; // keep encoded bytes reusable for recreation
    width_ = target_height == 208 || target_height == 392 ? 392 : 196;
    height_ = target_height;
    has_new_image_.store(true, std::memory_order_release);
    has_completed_artwork_ = true;
    completed_url_ = request.url;
    completed_height_ = target_height;
    if (it != cache_.begin()) {
        CachedArtwork hit = std::move(*it);
        cache_.erase(it);
        cache_.insert(cache_.begin(), std::move(hit));
    }
    return true;
}

void TextureInjector::process_request(const ArtworkRequest& request) {
    struct ProcessingGuard {
        TextureInjector& owner;
        uint64_t job_id;
        ~ProcessingGuard() {
            if (owner.is_current(job_id)) owner.is_processing_.store(false, std::memory_order_release);
        }
    } guard{*this, request.job_id};

    try {
        if (!is_current(request.job_id)) return;

        int target_h = 0;
        const auto height_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((target_h = target_height_.load(std::memory_order_acquire)) == 0) {
            if (!is_current(request.job_id) || std::chrono::steady_clock::now() >= height_deadline)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        const auto target_generation = target_generation_.load(std::memory_order_acquire);
        if (publish_cached(request, target_h, target_generation)) {
            log::info("[dx12] artwork cache hit for {}", request.url.empty() ? "default" : request.url);
            return;
        }

        char dll_path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, dll_path, MAX_PATH);
        const auto game_dir = std::filesystem::path{dll_path}.parent_path();
        const auto raw_path = std::filesystem::temp_directory_path() /
                              ("fh6_raw_" + std::to_string(request.job_id));
        struct FileCleanupGuard {
            std::filesystem::path path;
            ~FileCleanupGuard() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } file_guard{raw_path};

        bool has_valid_source = false;
        bool is_default_artwork = false;
        if (!request.url.empty() && request.worker)
            has_valid_source = request.worker->download_file(request.url, raw_path.string());
        if (!has_valid_source) {
            const auto default_art = game_dir / "fh6-radio" / "assets" / "default_artwork.png";
            std::error_code ec;
            if (!std::filesystem::exists(default_art) ||
                !std::filesystem::copy_file(default_art, raw_path,
                                            std::filesystem::copy_options::overwrite_existing, ec) || ec) {
                log::warn("[dx12] artwork job {}: default artwork unavailable", request.job_id);
                return;
            }
            is_default_artwork = true;
        }
        if (!is_current(request.job_id)) return;

        int width = 0, height = 0, channels = 0;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> image(
            stbi_load(raw_path.string().c_str(), &width, &height, &channels, 4), stbi_image_free);
        if (!image || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
            log::warn("[dx12] artwork job {}: invalid source image", request.job_id);
            return;
        }

        const int target_w    = target_h == 208 || target_h == 392 ? 392 : 196;
        const int square_size = target_w == 392 ? 208 : 104;
        const int border      = is_default_artwork ? 0 : square_size / 26;
        const float scale     = std::max(static_cast<float>(square_size) / width,
                                         static_cast<float>(square_size) / height);
        const int new_w       = std::max(1, static_cast<int>(width * scale));
        const int new_h       = std::max(1, static_cast<int>(height * scale));

        std::vector<unsigned char> resized(static_cast<std::size_t>(new_w) * new_h * 4);
        stbir_resize_uint8_linear(image.get(), width, height, 0, resized.data(), new_w, new_h, 0,
                                  STBIR_RGBA);
        image.reset();

        std::vector<unsigned char> padded(static_cast<std::size_t>(target_w) * target_h * 4, 0);
        const int off_x = (new_w - square_size) / 2;
        const int off_y = (new_h - square_size) / 2;
        long long total_r = 0, total_g = 0, total_b = 0;
        for (int y = 0; y < square_size; ++y) {
            for (int x = 0; x < square_size; ++x) {
                const auto idx = (static_cast<std::size_t>(y + off_y) * new_w + x + off_x) * 4;
                total_r += resized[idx];
                total_g += resized[idx + 1];
                total_b += resized[idx + 2];
            }
        }
        const int count = square_size * square_size;
        const auto br = static_cast<unsigned char>(total_r / count * 0.6f);
        const auto bg = static_cast<unsigned char>(total_g / count * 0.6f);
        const auto bb = static_cast<unsigned char>(total_b / count * 0.6f);
        for (int y = 0; y < target_h; ++y) {
            const int source_y = y * square_size / target_h;
            for (int x = 0; x < square_size; ++x) {
                const auto dst = (static_cast<std::size_t>(y) * target_w + x) * 4;
                if (x < border || x >= square_size - border || source_y < border ||
                    source_y >= square_size - border) {
                    padded[dst] = br;
                    padded[dst + 1] = bg;
                    padded[dst + 2] = bb;
                    padded[dst + 3] = 255;
                } else {
                    const auto src = (static_cast<std::size_t>(source_y + off_y) * new_w + x + off_x) * 4;
                    std::copy_n(resized.data() + src, 4, padded.data() + dst);
                }
            }
        }

        auto pixels = encode_artwork_bc7(padded, target_w, target_h,
                                          [&] { return !is_current(request.job_id); });
        if (pixels.empty() || !is_current(request.job_id)) return;
        if (target_generation_.load(std::memory_order_acquire) != target_generation ||
            target_height_.load(std::memory_order_acquire) != target_h) {
            target_refresh_requested_.store(true, std::memory_order_release);
            return;
        }

        cache_insert(request.url, target_h, pixels);
        {
            std::lock_guard lock{mtx_};
            if (!is_current(request.job_id)) return;
            pending_pixels_ = pixels; // retain encoded bytes for future recreation
            width_ = target_w;
            height_ = target_h;
            has_new_image_.store(true, std::memory_order_release);
            has_completed_artwork_ = true;
            completed_url_ = request.url;
            completed_height_ = target_h;
        }
        log::info("[dx12] artwork job {} complete (cached CPU BC7)", request.job_id);
    } catch (const std::exception& e) {
        log::warn("[dx12] artwork job {} failed: {}", request.job_id, e.what());
    } catch (...) {
        log::warn("[dx12] artwork job {} failed with unknown exception", request.job_id);
    }
}

void TextureInjector::worker_loop() {
    for (;;) {
        ArtworkRequest request;
        bool have_request = false;
        {
            std::unique_lock lock{mtx_};
            cv_.wait(lock, [this] {
                return stopping_ || pending_request_.has_value() ||
                       target_refresh_requested_.load(std::memory_order_acquire);
            });
            if (stopping_) return;
            if (pending_request_) {
                request = std::move(*pending_request_);
                pending_request_.reset();
                have_request = true;
            } else if (target_refresh_requested_.exchange(false, std::memory_order_acq_rel)) {
                request = ArtworkRequest{current_url_, latest_job_id_.load(std::memory_order_acquire),
                                         worker_};
                have_request = true;
            }
        }
        if (have_request) process_request(request);
    }
}

bool TextureInjector::pop_pending_pixels(std::vector<uint8_t>& out_pixels, int& out_width,
                                         int& out_height) {
    std::lock_guard lock{mtx_};
    if (!has_new_image_.load(std::memory_order_acquire)) return false;
    out_pixels = std::move(pending_pixels_);
    out_width = width_;
    out_height = height_;
    has_new_image_.store(false, std::memory_order_release);
    return true;
}

} // namespace fh6
