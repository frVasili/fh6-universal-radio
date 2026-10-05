#pragma once
#include "fh6/playback_policy.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace fh6::sources {
// Keep the owner and cache tag together: album tags belong to the album,
// not to the audio item. Missing/null metadata is normal in Jellyfin.
inline std::string jellyfin_artwork_url(const nlohmann::json& item, const std::string& server) {
    auto str = [](const nlohmann::json& obj, const char* key) -> std::string {
        auto it = obj.find(key);
        return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    std::string id, tag;
    if (auto images = item.find("ImageTags"); images != item.end() && images->is_object()) {
        tag = str(*images, "Primary");
        if (!tag.empty()) id = jellyfin_item_id(str(item, "Id"));
    }
    if (id.empty()) {
        id = jellyfin_item_id(str(item, "AlbumId"));
        tag = str(item, "AlbumPrimaryImageTag");
        if (tag.empty()) id.clear();
    }
    if (id.empty()) {
        id = jellyfin_item_id(str(item, "ParentPrimaryImageItemId"));
        tag = str(item, "ParentPrimaryImageTag");
        if (tag.empty()) id.clear();
    }
    const auto base = jellyfin_base_url(server);
    if (id.empty() || base.empty()) return {};
    std::string encoded;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : tag) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            encoded += static_cast<char>(c);
        else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
    }
    return base + "/Items/" + id + "/Images/Primary?tag=" + encoded + "&fillWidth=480&quality=90";
}
} // namespace fh6::sources
