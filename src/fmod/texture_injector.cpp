#include "fh6/fmod/texture_injector.hpp"
#include "fh6/subprocess.hpp"
#include "fh6/net/http_get.hpp"
#include "fh6/log.hpp"
#include <thread>
#include <filesystem>
#include <windows.h>
#include <memory>
#include "fh6/artwork_codec.hpp"

// stb headers
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"


namespace fh6 {

void TextureInjector::update_artwork_url(const std::string& url) {
    std::shared_ptr<worker::WorkerClient> local_worker;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        local_worker = worker_;
        if (has_completed_artwork_ && completed_url_ == url && completed_height_ == target_height_.load()) {
            ++latest_job_id_; // cancel a pending cover for a song skipped past
            is_processing_.store(false);
            return;
        }
    }
    
    is_processing_.store(true);
    
    // grab a unique ticket for this job
    uint64_t my_job_id = ++latest_job_id_;

    std::thread([this, url, my_job_id, local_worker]() {
        // only clear the processing flag if this thread is still the newest job
        struct ProcessingGuard {
            std::atomic<bool>& flag;
            std::atomic<uint64_t>& latest;
            uint64_t mine;
            ~ProcessingGuard() { 
                if (latest.load() == mine) {
                    flag.store(false); 
                }
            }
        } guard{is_processing_, latest_job_id_, my_job_id};

        try {
            // if user already skipped the song, abort immediately
            if (latest_job_id_.load() != my_job_id) return; 

            char dll_path[MAX_PATH];
            GetModuleFileNameA(nullptr, dll_path, MAX_PATH);
            std::filesystem::path game_dir = std::filesystem::path(dll_path).parent_path();
            
            std::filesystem::path temp_dir_path = std::filesystem::temp_directory_path();
            
            std::string raw_path = (temp_dir_path / ("fh6_raw_" + std::to_string(my_job_id))).string(); 
            struct FileCleanupGuard {
                std::string path;
                ~FileCleanupGuard() {
                    std::error_code ec;
                    std::filesystem::remove(path, ec);
                }
            } file_guard{raw_path};

            bool has_valid_source = false;
            bool is_default_artwork = false;

            if (!url.empty()) {
                log::info("[dx12] job {}: delegating artwork download to worker process: {}", my_job_id, url);
                
                if (local_worker) {
                    // IPC call blocks until the worker finishes downloading to raw_path
                    has_valid_source = local_worker->download_file(url, raw_path);
                    
                    if (!has_valid_source) {
                        log::warn("[dx12] job {}: worker failed to download artwork - falling back to default", my_job_id);
                    } else {
                        log::info("[dx12] job {}: worker successfully downloaded artwork", my_job_id);
                    }
                } else {
                    log::error("[dx12] job {}: WorkerClient is not attached to TextureInjector - falling back", my_job_id);
                }
            }

            if (latest_job_id_.load() != my_job_id) return;

            if (!has_valid_source) {
                std::filesystem::path default_art = game_dir / "fh6-radio" / "assets" / "default_artwork.png";
                if (std::filesystem::exists(default_art)) {
                    std::error_code ec;
                    std::filesystem::copy_file(default_art, raw_path, std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        has_valid_source = true;
                        is_default_artwork = true;
                    } else {
                        log::warn("[dx12] job {}: failed to copy default artwork", my_job_id);
                    }
                } else {
                    log::warn("[dx12] job {}: no default artwork found at {} - skipping texture update", my_job_id, default_art.string());
                    return; 
                }
            }

            // don't saturate the CPU with FFmpeg if user skipped
            if (latest_job_id_.load() != my_job_id) return; 

            log::info("[dx12] job {}: resizing artwork natively using stb...", my_job_id);

            // load the raw image
            int width, height, channels;
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> image(
                stbi_load(raw_path.c_str(), &width, &height, &channels, 4), stbi_image_free);
            auto* img_data = image.get();
            if (!img_data) {
                log::warn("[dx12] job {}: failed to load raw image", my_job_id);
                return;
            }

            // calculate aspect-ratio preserving dimensions
            // wait until the DX12 hook discovers the target UI size
            int target_h = 0;
            const auto height_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while ((target_h = target_height_.load()) == 0) {
                if (latest_job_id_.load() != my_job_id) return;
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
            
            // dynamically scale based on what height was discovered
            int target_w = (target_h == 208 || target_h == 392) ? 392 : 196;
            int square_size = (target_w == 392) ? 208 : 104;
            
            // border settings
            int border_thickness = is_default_artwork ? 0 : (square_size / 26);
            
            // calculate scale to fill the 104x104 square
            float scale = std::max((float)square_size / width, (float)square_size / height);
            int new_w = std::max(1, (int)(width * scale));
            int new_h = std::max(1, (int)(height * scale));

            // resize image using STB
            std::vector<unsigned char> resized_data(new_w * new_h * 4);
            stbir_resize_uint8_linear(img_data, width, height, 0, resized_data.data(), new_w, new_h, 0, STBIR_RGBA);
            image.reset();

            // create transparent padded canvas to what DX12 requested
            std::vector<unsigned char> padded_data(target_w * target_h * 4, 0);

            // calculate source offsets to center-crop
            int src_offset_x = (new_w - square_size) / 2;
            int src_offset_y = (new_h - square_size) / 2;

            // calculate the average color of the cropped square
            long long total_r = 0, total_g = 0, total_b = 0;
            for (int y = 0; y < square_size; ++y) {
                for (int x = 0; x < square_size; ++x) {
                    int src_idx = ((y + src_offset_y) * new_w + (x + src_offset_x)) * 4;
                    total_r += resized_data[src_idx];
                    total_g += resized_data[src_idx + 1];
                    total_b += resized_data[src_idx + 2];
                }
            }
            
            int pixel_count = square_size * square_size;
            unsigned char avg_r = (unsigned char)(total_r / pixel_count);
            unsigned char avg_g = (unsigned char)(total_g / pixel_count);
            unsigned char avg_b = (unsigned char)(total_b / pixel_count);

            // border shade
            float brightness_factor = 0.6f; 
            unsigned char b_r = (unsigned char)(avg_r * brightness_factor);
            unsigned char b_g = (unsigned char)(avg_g * brightness_factor);
            unsigned char b_b = (unsigned char)(avg_b * brightness_factor);
            unsigned char b_a = 255;

            // stretches the y-axis if target_h is 196
            for (int y = 0; y < target_h; ++y) {
                for (int x = 0; x < square_size; ++x) {
                    int dst_idx = (y * target_w + x) * 4; 

                    // map the current y back to the 104-pixel source space
                    int orig_y = (y * square_size) / target_h; 

                    if (x < border_thickness || x >= square_size - border_thickness ||
                        orig_y < border_thickness || orig_y >= square_size - border_thickness) {
                        
                        padded_data[dst_idx]   = b_r;
                        padded_data[dst_idx+1] = b_g;
                        padded_data[dst_idx+2] = b_b;
                        padded_data[dst_idx+3] = b_a;
                    } else {
                        int src_idx = ((orig_y + src_offset_y) * new_w + (x + src_offset_x)) * 4;
                        padded_data[dst_idx]   = resized_data[src_idx];
                        padded_data[dst_idx+1] = resized_data[src_idx+1];
                        padded_data[dst_idx+2] = resized_data[src_idx+2];
                        padded_data[dst_idx+3] = resized_data[src_idx+3];
                    }
                }
            }

            // Encode on this background thread: no process fork of the game and
            // no second GPU device competing with the renderer under Proton.
            auto pixels = encode_artwork_bc7(padded_data, target_w, target_h,
                [&] { return latest_job_id_.load() != my_job_id; });
            if (pixels.empty()) return;
            std::lock_guard<std::mutex> lock(mtx_);
            if (latest_job_id_.load() != my_job_id) return;
            width_ = target_w;
            height_ = target_h;
            pending_pixels_ = std::move(pixels);
            has_new_image_ = true;
            has_completed_artwork_ = true;
            completed_url_ = url;
            completed_height_ = target_h;
            log::info("[dx12] job {} complete (CPU BC7, no subprocess)", my_job_id);
        } catch (const std::exception& e) {
            log::warn("[dx12] job {}: artwork pipeline failed: {}", my_job_id, e.what());
        } catch (...) {
            log::warn("[dx12] job {}: artwork pipeline failed with unknown exception", my_job_id);
        }
    }).detach();
}

bool TextureInjector::pop_pending_pixels(std::vector<uint8_t>& out_pixels, int& out_width, int& out_height) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!has_new_image_) return false;

    out_pixels = std::move(pending_pixels_);
    out_width = width_;
    out_height = height_;
    has_new_image_ = false;
    return true;
}

} // namespace fh6