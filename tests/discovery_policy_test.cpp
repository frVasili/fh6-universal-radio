#include "fh6/fmod/discovery_policy.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>

int main() {
    using clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    const auto start = clock::time_point{} + 10s;

    assert(!fh6::fmod_bridge::discovery_cache_expired<clock>(start + 29s, start, 30s));
    assert(fh6::fmod_bridge::discovery_cache_expired<clock>(start + 30s, start, 30s));
    assert(fh6::fmod_bridge::heap_scan_allowed<clock>(start, {}, 5s));
    assert(!fh6::fmod_bridge::heap_scan_allowed<clock>(start + 4999ms, start, 5s));
    assert(fh6::fmod_bridge::heap_scan_allowed<clock>(start + 5s, start, 5s));
    std::puts("PASS: discovery cache expiry and heap-scan cooldown are duration based");
}
