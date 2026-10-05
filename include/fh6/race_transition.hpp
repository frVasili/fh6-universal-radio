#pragma once
#include <cstdint>
#include <optional>

namespace fh6 {
enum class RaceTransition { none, started, ended };

// Debounce state changes, not whole races. A fixed 45-second action cooldown
// discarded legitimate quick starts before smart skip could choose restart.
// Online races can leave both activity flags set through the results screen.
// Phase -1 means finished: it rearms the next start, NEVER starts a race.
class RaceTransitionTracker {
public:
    RaceTransition observe(bool valid, bool active, std::uint64_t now_ms,
                           std::optional<std::int32_t> phase = std::nullopt) noexcept {
        if (phase && *phase == -1) active = false;
        if (!valid) {
            pending_ = false;
            return RaceTransition::none; // retain the last known activity
        }
        if (!initialized_) {
            initialized_ = true;
            active_ = active;
            return RaceTransition::none;
        }
        if (active == active_) {
            pending_ = false;
            return RaceTransition::none;
        }
        if (!pending_) {
            pending_ = true;
            since_ms_ = now_ms;
            return RaceTransition::none;
        }
        if (now_ms - since_ms_ < 250) return RaceTransition::none;
        active_ = active;
        pending_ = false;
        return active ? RaceTransition::started : RaceTransition::ended;
    }
    void reset() noexcept { initialized_ = pending_ = false; }
private:
    bool initialized_ = false;
    bool active_ = false;
    bool pending_ = false;
    std::uint64_t since_ms_ = 0;
};
} // namespace fh6
