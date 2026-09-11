#include "fh6/spotify_metadata_sync.hpp"
#include <cassert>
#include <cstdio>
fh6::TrackInfo track(const char* title) {
    fh6::TrackInfo t;
    t.title = title;
    t.artwork_url = std::string("https://example.invalid/") + title;
    t.duration_ms = 100000;
    return t;
}
int main() {
    using namespace fh6;
    assert(spotify_unescape_debug(R"(A \u{4e2d}\u{56fd})") == "A 中国");
    assert(spotify_unescape_debug(R"(foo \"bar\"\n)") == "foo \"bar\"\n");
    const auto bounded = spotify_display_text(
        "0123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789");
    assert(bounded.size() == 128 && bounded.ends_with("..."));
    SpotifyMetadataSync sync;
    assert(spotify_log_track_id("command=Load(SpotifyUri(\"spotify:track:A\"), true, 0)") == "A");
    assert(spotify_log_track_id("command=Preload(SpotifyUri(\"spotify:track:B\"))") == "B");
    assert(spotify_log_track_id("command=Seek(15000)").empty());
    assert(!sync.load("A"));
    sync.metadata_started();
    auto first = sync.decoded(track("current"));
    assert(first && first->title == "current");
    // Replay the observed Preload -> metadata/loaded -> 29 second wait -> Load.
    sync.preload("B");
    sync.metadata_started();
    assert(!sync.decoded(track("next")));
    assert(sync.active_id == "A");
    auto next = sync.load("B");
    assert(next && next->title == "next" && next->artwork_url.ends_with("next"));
    // An abandoned preload must not replace a manually selected song.
    sync.preload("C");
    sync.metadata_started();
    assert(!sync.load("D"));
    assert(!sync.decoded(track("abandoned")));
    sync.metadata_started();
    auto selected = sync.decoded(track("selected"));
    assert(selected && selected->title == "selected");
    // A repeat/restart reuses the active track; no decoder-loaded event required.
    auto restart = sync.load("D");
    assert(restart && restart->title == "selected");
    std::puts("PASS: preload does not change display; Load promotes cached title/art; stale preload ignored; restart keeps track");
}
