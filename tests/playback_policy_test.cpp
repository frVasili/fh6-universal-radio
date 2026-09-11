#include "fh6/playback_policy.hpp"
#include <cassert>
int main() {
    using namespace fh6;
    static_assert(restart_recent_track(0));
    static_assert(restart_recent_track(29999));
    static_assert(restart_recent_track(30000));
    static_assert(!restart_recent_track(30001));
    const std::string id = "0123456789abcdef0123456789abcdef";
    assert(jellyfin_item_id(id) == id);
    assert(jellyfin_item_id("  " + id + "\n") == id);
    assert(jellyfin_item_id("01234567-89ab-cdef-0123-456789abcdef") == id);
    assert(jellyfin_item_id("details?id=" + id + "&serverId=deadbeef") == id);
    assert(jellyfin_item_id("https://host/web/#/details?serverId=other&id=" + id) == id);
    assert(jellyfin_item_id("details?serverId=" + id).empty());
    assert(jellyfin_item_id("../" + id).empty());
    assert(jellyfin_item_id("bad").empty());
    assert(jellyfin_item_id("").empty());
    assert(jellyfin_base_url(" https://host/jellyfin/\n") == "https://host/jellyfin");
}
