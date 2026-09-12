#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <cctype>

namespace fh6 {
constexpr int song_restart_seconds(int seconds) noexcept {
    return seconds < 1 ? 1 : seconds > 59 ? 59 : seconds;
}
constexpr bool restart_recent_track(std::uint64_t position_ms, int seconds = 30) noexcept {
    return position_ms <= static_cast<std::uint64_t>(song_restart_seconds(seconds)) * 1000;
}

// Accept raw UUIDs and Jellyfin web links/fragments. Never send query strings
// or arbitrary paths as IDs; in particular serverId is not the playlist ID.
inline std::string jellyfin_item_id(std::string_view input) {
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) input.remove_suffix(1);
    for (auto marker : {std::string_view{"?id="}, std::string_view{"&id="}}) {
        auto pos = input.find(marker);
        if (pos != std::string_view::npos) {
            input.remove_prefix(pos + marker.size());
            input = input.substr(0, input.find_first_of("&#"));
            break;
        }
    }
    std::string id;
    for (char c : input) {
        if (c == '-') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        id.push_back(c);
    }
    return id.size() == 32 ? id : std::string{};
}
inline std::string jellyfin_base_url(std::string url) {
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) url.erase(0, 1);
    while (!url.empty() && (url.back() == '/' || std::isspace(static_cast<unsigned char>(url.back())))) url.pop_back();
    return url;
}
}
