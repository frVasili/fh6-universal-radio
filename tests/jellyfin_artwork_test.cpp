#include "fh6/sources/jellyfin_artwork.hpp"
#include <cassert>
int main() {
    using nlohmann::json;
    using fh6::sources::jellyfin_artwork_url;
    const std::string track(32, 'a'), album(32, 'b'), parent(32, 'c');
    json item = {{"Id", track}, {"AlbumId", album}, {"AlbumPrimaryImageTag", "album-tag"}};
    auto url = jellyfin_artwork_url(item, " https://server/jellyfin/ ");
    assert(url == "https://server/jellyfin/Items/" + album + "/Images/Primary?tag=album-tag&fillWidth=480&quality=90");
    item["ImageTags"] = {{"Primary", "track-tag"}};
    assert(jellyfin_artwork_url(item, "http://server").find("/Items/" + track + "/") != std::string::npos);
    item["ImageTags"] = nullptr;
    assert(jellyfin_artwork_url(item, "http://server").find("/Items/" + album + "/") != std::string::npos);
    item["AlbumPrimaryImageTag"] = nullptr;
    assert(jellyfin_artwork_url(item, "http://server").empty());
    item["ParentPrimaryImageItemId"] = parent;
    item["ParentPrimaryImageTag"] = "a&b";
    assert(jellyfin_artwork_url(item, "http://server").find("tag=a%26b") != std::string::npos);
    item["ParentPrimaryImageItemId"] = "../invalid";
    assert(jellyfin_artwork_url(item, "http://server").empty());
    assert(jellyfin_artwork_url(json::object(), "http://server").empty());
    assert(jellyfin_artwork_url(item, "").empty());
}
