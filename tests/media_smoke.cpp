// Read-only integration test: real worker, source code, HTTP and audio decode.
// All output is drained in memory; nothing is played through the speakers.
#include "fh6/config.hpp"
#include "fh6/log.hpp"
#include "fh6/ring_buffer.hpp"
#include "fh6/sources/jellyfin_source.hpp"
#include "fh6/sources/youtube_music_source.hpp"
#include "fh6/worker/worker_client.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <algorithm>

bool receive(fh6::IAudioSource& source, fh6::RingBuffer& ring) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    std::size_t bytes = 0;
    bool nonzero = false;
    while (std::chrono::steady_clock::now() < deadline && bytes < 192000 * 3) {
        source.pump(ring);
        unsigned char data[16384];
        auto count = ring.read(data, sizeof(data));
        nonzero |= std::any_of(data, data + count, [](auto v) { return v != 0; });
        bytes += count;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("decoded %zu PCM bytes, nonzero=%d\n", bytes, nonzero);
    return bytes >= 192000 * 3 && nonzero;
}
int main(int argc, char** argv) {
    if (argc != 8) return 2; // config worker yt-dlp quickjs ffmpeg URL log
    fh6::log::init(argv[7]);
    auto cfg = fh6::load_config(argv[1]);
    fh6::worker::WorkerClient worker;
    if (!worker.start(argv[2])) return 3;
    bool ok = true;
    {
        cfg.youtube_music.enabled = true;
        cfg.youtube_music.yt_dlp_path = argv[3];
        cfg.youtube_music.js_runtime_path = argv[4];
        cfg.youtube_music.stations.clear();
        fh6::sources::YouTubeMusicSource source(cfg.youtube_music, argv[5], &worker);
        source.initialize();
        source.set_target(argv[6]);
        source.play();
        fh6::RingBuffer ring(65536);
        bool decoded = receive(source, ring);
        const bool restarted = source.restart_current();
        ring.drain();
        bool decoded_again = receive(source, ring);
        ok &= decoded && restarted && decoded_again;
        std::printf("YouTube playback + restart: %s\n", ok ? "PASS" : "FAIL");
    }
    {
        cfg.jellyfin.enabled = true;
        fh6::sources::JellyfinSource source(cfg.jellyfin, argv[5], &worker);
        source.initialize();
        source.play();
        fh6::RingBuffer ring(65536);
        bool decoded = receive(source, ring);
        const auto title = source.current_track().title;
        const bool restarted = source.restart_current();
        ring.drain();
        bool decoded_again = receive(source, ring);
        const bool passed = decoded && restarted && decoded_again && source.current_track().title == title;
        ok &= passed;
        std::printf("Jellyfin saved-link playback + same-track restart: %s\n", passed ? "PASS" : "FAIL");
    }
    worker.stop();
    return ok ? 0 : 1;
}
