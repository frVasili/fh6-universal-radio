#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
namespace fh6 {
// Empty result means canceled or invalid dimensions/input. RGBA, BC7 mode 6.
std::vector<std::uint8_t> encode_artwork_bc7(std::span<const std::uint8_t> rgba,
    int width, int height, const std::function<bool()>& canceled = {});
}
