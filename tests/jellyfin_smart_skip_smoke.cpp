// Read-only server/audio smoke test. PCM stays in memory; no speaker output.
#include "fh6/config.hpp"
#include "fh6/sources/jellyfin_source.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    auto cfg = fh6::load_config(argv[1]);
    cfg.jellyfin.enabled = true;
    fh6::sources::JellyfinSource source(cfg.jellyfin, argv[2], nullptr);
    if (!source.initialize()) return 3;
    source.play();
    fh6::RingBuffer ring(1024 * 1024);
    auto fill = [&] {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (ring.readable() < 192000 * 3 && std::chrono::steady_clock::now() < end) {
            source.pump(ring);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return ring.readable() >= 192000 * 3;
    };
    if (!fill()) return 4;
    auto initial = source.current_track();
    if (initial.position_ms != 0) return 5;
    std::vector<unsigned char> pcm(192000 / 2);
    if (ring.read(pcm.data(), pcm.size()) != pcm.size()) return 6;
    source.pump(ring);
    auto event = source.current_track();
    std::printf("decoded >3s ahead; consumed 0.5s; event timestamp=%llu ms\n",
                (unsigned long long)event.position_ms);
    if (event.position_ms != 500) return 7;
    if (!source.smart_skip(59, event.position_ms)) return 8;
    ring.drain(); // manager ordering: action, flush, then pump
    auto restarted = source.current_track();
    if (restarted.title != initial.title || restarted.position_ms != 0) return 9;
    if (!fill()) return 10;
    if (source.current_track().position_ms != 0) return 11;
    source.shutdown();
    std::puts("PASS: Jellyfin uses consumed PCM timestamp and smart skip restarts the same song at zero");
}
