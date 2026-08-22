#pragma once

#include <cstdint>

namespace gb {

// Deterministic xorshift32. Deliberately hand-rolled rather than <random>: the
// standard distributions are not specified to produce identical sequences across
// implementations, and a seed has to mean the same thing forever. "Seed 4172
// sounded great" must still be true after a compiler or platform change.
class Rng
{
public:
    explicit Rng (uint32_t seed = 1u) { reseed (seed); }

    void reseed (uint32_t seed) { state = seed ? seed : 0x9E3779B9u; }

    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    int below (int n)
    {
        return n <= 0 ? 0 : static_cast<int> (next() % static_cast<uint32_t> (n));
    }

    int range (int lo, int hi)
    {
        return hi <= lo ? lo : lo + below (hi - lo + 1);
    }

    double unit()
    {
        return static_cast<double> (next() >> 8) / 16777216.0;
    }

    // Symmetric jitter in [-amount, +amount].
    double bipolar (double amount)
    {
        return (unit() * 2.0 - 1.0) * amount;
    }

    bool chance (double p) { return unit() < p; }

private:
    uint32_t state = 0x9E3779B9u;
};

// Derives a stable child seed, so re-rolling one section cannot disturb another.
inline uint32_t deriveSeed (uint32_t base, uint32_t salt)
{
    uint32_t h = base ^ (salt * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h ? h : 0x9E3779B9u;
}

} // namespace gb
