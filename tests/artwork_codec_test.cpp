#include "fh6/artwork_codec.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
int main(int argc, char** argv) {
    using fh6::encode_artwork_bc7;
    assert(encode_artwork_bc7({}, 0, 0).empty());
    assert(encode_artwork_bc7({}, 392, 392).empty());
    for (int height : {104, 196, 208, 392, 7}) {
        const int width = height == 7 ? 5 : height >= 208 ? 392 : 196;
        std::vector<std::uint8_t> rgba(width * height * 4);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            auto i = (y * width + x) * 4;
            // Solid red cover and transparent padding, both block-aligned.
            rgba[i] = x < width / 2 ? 255 : 0;
            rgba[i + 3] = x < width / 2 ? 255 : 0;
        }
        assert(encode_artwork_bc7(rgba, width, height, [] { return true; }).empty());
        auto begin = std::chrono::steady_clock::now();
        auto encoded = encode_artwork_bc7(rgba, width, height);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        assert(encoded.size() == static_cast<std::size_t>((width + 3) / 4 * ((height + 3) / 4) * 16));
        // BC7 mode 6 has six zero prefix bits, then a one.
        for (std::size_t i = 0; i < encoded.size(); i += 16) assert((encoded[i] & 127) == 64);
        std::printf("%dx%d: %.2f ms, %zu bytes\n", width, height, ms, encoded.size());
        if (height == 392 && argc == 2) {
            std::ofstream out(argv[1], std::ios::binary);
            out.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }
    }
}
