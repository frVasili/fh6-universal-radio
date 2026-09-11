#include "fh6/artwork_codec.hpp"
#include "bc7enc.h"
#include <algorithm>
#include <cstring>
#include <mutex>

namespace fh6 {
std::vector<std::uint8_t> encode_artwork_bc7(std::span<const std::uint8_t> rgba,
    int width, int height, const std::function<bool()>& canceled) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4) return {};
    static std::once_flag encoder_init;
    std::call_once(encoder_init, bc7enc_compress_block_init);
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    params.m_mode_mask = 1u << 6;
    bc7enc_compress_block_params_init_linear_weights(&params);
    const int blocks_x = (width + 3) / 4;
    const int blocks_y = (height + 3) / 4;
    std::vector<std::uint8_t> pixels(blocks_x * blocks_y * 16);
    for (int by = 0; by < blocks_y; ++by) {
        if (canceled && canceled()) return {};
        for (int bx = 0; bx < blocks_x; ++bx) {
            std::uint8_t block[64];
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(bx * 4 + x, width - 1);
                    const int sy = std::min(by * 4 + y, height - 1);
                    std::memcpy(block + (y * 4 + x) * 4,
                                rgba.data() + (sy * width + sx) * 4, 4);
                }
            }
            bc7enc_compress_block(pixels.data() + (by * blocks_x + bx) * 16, block, &params);
        }
    }
    return pixels;
}
}
