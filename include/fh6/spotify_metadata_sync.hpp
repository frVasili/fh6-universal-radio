#pragma once
#include "fh6/audio_source.hpp"
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fh6 {
inline void spotify_append_utf8(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

inline int spotify_hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode the Rust Debug representation used by librespot's metadata trace.
// In particular, Rust emits non-ASCII characters as \u{...}, not as four-digit
// C/C++ escapes.
inline std::string spotify_unescape_debug(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '\\' || i + 1 >= input.size()) {
            out.push_back(input[i]);
            continue;
        }
        const char escaped = input[++i];
        switch (escaped) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '0': out.push_back('\0'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case '\'': out.push_back('\''); break;
            case 'u': {
                std::uint32_t cp = 0;
                std::size_t digits = 0;
                std::size_t end = i + 1;
                if (end < input.size() && input[end] == '{') {
                    ++end;
                    while (end < input.size() && input[end] != '}' && digits < 6) {
                        const int digit = spotify_hex_digit(input[end]);
                        if (digit < 0) break;
                        cp = (cp << 4) | static_cast<std::uint32_t>(digit);
                        ++digits;
                        ++end;
                    }
                    if (digits > 0 && end < input.size() && input[end] == '}' &&
                        cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        spotify_append_utf8(out, cp);
                        i = end;
                        break;
                    }
                } else if (end + 4 <= input.size()) {
                    for (std::size_t j = 0; j < 4; ++j) {
                        const int digit = spotify_hex_digit(input[end + j]);
                        if (digit < 0) {
                            digits = 0;
                            break;
                        }
                        cp = (cp << 4) | static_cast<std::uint32_t>(digit);
                        ++digits;
                    }
                    if (digits == 4 && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        spotify_append_utf8(out, cp);
                        i = end + 3;
                        break;
                    }
                }
                // Keep malformed escapes visible instead of silently dropping text.
                out.push_back('u');
                break;
            }
            default: out.push_back(escaped); break;
        }
    }
    return out;
}

// Game-owned metadata strings must stay bounded. Copy complete UTF-8 code
// points and reserve room for an ellipsis so one bad trace cannot expand the HUD.
inline std::string spotify_display_text(std::string_view input, std::size_t max_bytes = 128) {
    if (input.size() <= max_bytes) return std::string{input};
    if (max_bytes <= 3) return std::string{input.substr(0, max_bytes)};

    const std::size_t limit = max_bytes - 3;
    std::size_t pos = 0;
    while (pos < input.size() && pos < limit) {
        const auto c = static_cast<unsigned char>(input[pos]);
        std::size_t width = 1;
        if (c >= 0xC2 && c <= 0xDF) width = 2;
        else if (c >= 0xE0 && c <= 0xEF) width = 3;
        else if (c >= 0xF0 && c <= 0xF4) width = 4;
        if (pos + width > limit || pos + width > input.size()) break;
        pos += width;
    }
    return std::string{input.substr(0, pos)} + "...";
}

// Decoder completion is not playback: preloads finish ~30 seconds early.
// Only command=Load authorizes a change in the audible track identity.
class SpotifyMetadataSync {
public:
    std::string requested_id;
    std::string decoding_id;
    std::string active_id;
    void preload(std::string id) { requested_id = std::move(id); }
    void metadata_started() { decoding_id = requested_id; }
    std::optional<TrackInfo> load(std::string id) {
        active_id = std::move(id);
        requested_id = active_id;
        if (auto it = cache_.find(active_id); it != cache_.end()) return it->second;
        return std::nullopt;
    }
    std::optional<TrackInfo> decoded(TrackInfo info) {
        const std::string id = decoding_id.empty() ? requested_id : decoding_id;
        decoding_id.clear();
        if (id.empty()) return std::nullopt;
        if (cache_.size() >= 8 && !cache_.contains(id)) cache_.clear();
        cache_[id] = info;
        return id == active_id ? std::optional<TrackInfo>{std::move(info)} : std::nullopt;
    }
private:
    std::unordered_map<std::string, TrackInfo> cache_;
};
inline std::string spotify_log_track_id(std::string_view line) {
    auto begin = line.find("spotify:track:");
    if (begin == std::string_view::npos) return {};
    begin += 14;
    auto end = line.find_first_of("\"') ,}", begin);
    return std::string(line.substr(begin, end - begin));
}
}
