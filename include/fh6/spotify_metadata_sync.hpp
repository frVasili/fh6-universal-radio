#pragma once
#include "fh6/audio_source.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fh6 {
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
