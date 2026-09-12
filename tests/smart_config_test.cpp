#include "fh6/config.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path path = argv[1];
    for (int seconds : {1, 17, 59}) {
        fh6::Config config;
        config.playback.song_restart_seconds = seconds;
        config.playback.race_start_playback = "smart";
        fh6::save_config(path, config);
        auto saved = fh6::load_config(path);
        assert(saved.playback.song_restart_seconds == seconds);
        assert(saved.playback.race_start_playback == "smart");
    }
    { std::ofstream f(path); f << "[playback]\nsong_restart_seconds = 99\n"; }
    assert(fh6::load_config(path).playback.song_restart_seconds == 59);
    { std::ofstream f(path); f << "[playback]\nrace_start_playback = \"ignore\"\n"; }
    assert(fh6::load_config(path).playback.song_restart_seconds == 30);
    std::filesystem::remove(path);
}
