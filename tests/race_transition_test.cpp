#include "fh6/race_transition.hpp"
#include <cassert>
#include <cstdio>
int main() {
    using enum fh6::RaceTransition;
    fh6::RaceTransitionTracker t;
    assert(t.observe(true, false, 0) == none);
    assert(t.observe(true, true, 1000) == none);
    assert(t.observe(true, true, 1249) == none);
    assert(t.observe(true, true, 1250) == started);
    // Finish capture: activity stayed true while unrelated +0x80 became -1.
    assert(t.observe(true, true, 3000) == none);
    // Brief scene-change glitches do not rearm another start.
    assert(t.observe(true, false, 4000) == none);
    assert(t.observe(true, true, 4100) == none);
    assert(t.observe(true, true, 4500) == none);
    assert(t.observe(true, false, 5000) == none);
    assert(t.observe(true, false, 5250) == ended);
    // New race within 45s still fires: no whole-race cooldown.
    assert(t.observe(true, true, 6000) == none);
    assert(t.observe(true, true, 6250) == started);
    // Failed memory reads do not fabricate end/start or swallow the next
    // genuine transition from a known inactive baseline.
    assert(t.observe(false, false, 7000) == none);
    assert(t.observe(true, true, 7300) == none);
    assert(t.observe(true, false, 8000) == none);
    assert(t.observe(true, false, 8250) == ended);
    assert(t.observe(false, false, 8300) == none);
    assert(t.observe(true, true, 8400) == none);
    assert(t.observe(true, true, 8650) == started);
    t.reset();
    assert(t.observe(true, true, 9000) == none);

    // Captured online-race sequence: both activity flags stay at 1 through
    // finish/results and the next race. Previously only the first start fired.
    t.reset();
    assert(t.observe(true, true, 0, -1) == none);
    assert(t.observe(true, true, 1000, 3) == none);
    assert(t.observe(true, true, 1250, 3) == started);
    assert(t.observe(true, true, 180000, -1) == none);
    assert(t.observe(true, true, 180250, -1) == ended);
    assert(t.observe(true, true, 200000, -1) == none);
    assert(t.observe(false, false, 200500, -1) == none);
    assert(t.observe(true, true, 210000, 3) == none);
    assert(t.observe(true, true, 210250, 3) == started);
    assert(t.observe(true, true, 210500, 3) == none);
    // A single noisy phase sample or unreadable memory cannot double-skip.
    assert(t.observe(true, true, 211000, -1) == none);
    assert(t.observe(true, true, 211100, 3) == none);
    assert(t.observe(true, true, 211500, 3) == none);
    assert(t.observe(false, false, 212000, -1) == none);
    assert(t.observe(true, true, 212500, 3) == none);

    // Replay the live Sep 22 capture at its actual timing. The old detector
    // (no phase input) misses the second online race; the corrected one fires.
    fh6::RaceTransitionTracker old, fixed;
    int old_starts = 0, starts = 0, ends = 0;
    for (std::uint64_t ms = 69853324; ms <= 69967500; ms += 20) {
        const int phase = ms < 69869070 || ms >= 69967147 ? 3 : -1;
        old_starts += old.observe(true, true, ms) == started;
        const auto event = fixed.observe(true, true, ms, phase);
        starts += event == started;
        ends += event == ended;
    }
    assert(old_starts == 0 && starts == 1 && ends == 1);
    std::puts("PASS: consecutive online races, finish without skip, actual race trace replay, debounce, unreadable samples");
}
