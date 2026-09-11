#pragma once

#include <chrono>

namespace fh6::fmod_bridge {

template <class Clock>
constexpr bool discovery_cache_expired(
    typename Clock::time_point now,
    typename Clock::time_point empty_since,
    typename Clock::duration delay) noexcept {
    return empty_since != typename Clock::time_point{} && now - empty_since >= delay;
}

template <class Clock>
constexpr bool heap_scan_allowed(
    typename Clock::time_point now,
    typename Clock::time_point last_scan,
    typename Clock::duration cooldown) noexcept {
    return last_scan == typename Clock::time_point{} || now - last_scan >= cooldown;
}

} // namespace fh6::fmod_bridge
