#pragma once
#include <cstdint>
#include <intrin.h>
#include <windows.h>

namespace OBW {

// Collect entropy from hardware timing sources.
// __rdtsc()             — CPU cycle counter, sub-nanosecond resolution
// QueryPerformanceCounter — additional timing dimension, different clock domain
// splitmix64 finalizer  — avalanche effect, ensures good bit distribution
//
// Not cryptographically secure, but more than sufficient for RNG seeding in games.
// No kernel entropy pool, no blocking, no CryptGenRandom overhead.

namespace detail {

inline std::uint64_t splitmix64(std::uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

}  // namespace detail

inline std::uint32_t CollectEntropy() noexcept {
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    const std::uint64_t tsc = __rdtsc();

    // XOR two independent clock sources, then avalanche
    const std::uint64_t mixed = detail::splitmix64(
        tsc ^ (static_cast<std::uint64_t>(qpc.QuadPart) * 0x9e3779b97f4a7c15ULL));

    // Fold 64→32 bits
    return static_cast<std::uint32_t>(mixed ^ (mixed >> 32));
}

}  // namespace OBW
