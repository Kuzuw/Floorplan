#include "Sequencer/Pattern.h"

#include <algorithm>

namespace fp
{

namespace
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed != 0 ? seed : 0xA5A5A5A5u) {}
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
};

// Musical hit likelihood per 16th position for each voice.
constexpr float kWeights[kNumVoices][16] = {
    { 1.0f, 0.02f, 0.05f, 0.12f, 1.0f, 0.02f, 0.05f, 0.10f, 1.0f, 0.02f, 0.15f, 0.08f, 1.0f, 0.04f, 0.25f, 0.10f }, // kick
    { 0.0f, 0.02f, 0.02f, 0.05f, 0.9f, 0.02f, 0.05f, 0.20f, 0.0f, 0.20f, 0.02f, 0.05f, 0.9f, 0.05f, 0.10f, 0.20f }, // snare
    { 0.8f, 0.45f, 0.8f,  0.45f, 0.8f, 0.45f, 0.8f,  0.45f, 0.8f, 0.45f, 0.8f,  0.45f, 0.8f, 0.45f, 0.8f,  0.45f }, // closed hat
    { 0.02f, 0.02f, 0.95f, 0.02f, 0.02f, 0.02f, 0.95f, 0.02f, 0.02f, 0.02f, 0.95f, 0.02f, 0.02f, 0.02f, 0.95f, 0.05f }, // open hat
    { 0.0f, 0.02f, 0.02f, 0.15f, 0.95f, 0.02f, 0.02f, 0.05f, 0.0f, 0.02f, 0.02f, 0.15f, 0.95f, 0.02f, 0.05f, 0.15f }, // clap
    { 0.15f, 0.35f, 0.2f, 0.35f, 0.15f, 0.35f, 0.2f, 0.35f, 0.15f, 0.35f, 0.2f, 0.35f, 0.15f, 0.35f, 0.2f, 0.35f }, // perc
    { 0.05f, 0.05f, 0.08f, 0.1f, 0.05f, 0.05f, 0.08f, 0.1f, 0.05f, 0.05f, 0.08f, 0.1f, 0.05f, 0.3f, 0.3f, 0.3f },    // tom
    { 0.6f, 0.0f, 0.02f, 0.0f, 0.02f, 0.0f, 0.02f, 0.0f, 0.05f, 0.0f, 0.02f, 0.0f, 0.02f, 0.0f, 0.02f, 0.0f },       // cymbal
};
} // namespace

bool Step::isDefaultExceptOn() const noexcept
{
    return velocity == 100 && probability == 100 && micro == 0 && flags == 0 && ratchet == 1 && tune == 0 && decay == 0;
}

void Step::sanitise() noexcept
{
    on = on ? 1 : 0;
    velocity = (uint8_t) std::clamp ((int) velocity, 1, 127);
    probability = (uint8_t) std::clamp ((int) probability, 0, 100);
    micro = (int8_t) std::clamp ((int) micro, -50, 50);
    flags &= (Accent | Flam);
    ratchet = (uint8_t) std::clamp ((int) ratchet, 1, 4);
    tune = (int8_t) std::clamp ((int) tune, -24, 24);
    decay = (int8_t) std::clamp ((int) decay, -100, 100);
}

void Pattern::clear() noexcept
{
    for (auto& t : tracks)
        t = Track {};
}

bool Pattern::isEmpty() const noexcept
{
    for (const auto& t : tracks)
        for (const auto& s : t.steps)
            if (s.on)
                return false;
    return true;
}

void Pattern::clearTrack (int voice) noexcept
{
    tracks[(size_t) voice] = Track {};
}

void Pattern::rotateTrack (int voice, int amount) noexcept
{
    const int len = std::max (1, (int) length);
    auto& steps = tracks[(size_t) voice].steps;
    amount = ((amount % len) + len) % len;
    std::rotate (steps.begin(), steps.begin() + (len - amount), steps.begin() + len);
}

bool euclideanHit (int index, int steps, int pulses, int rotation) noexcept
{
    if (steps <= 0 || pulses <= 0)
        return false;
    pulses = std::min (pulses, steps);
    const int i = (((index - rotation) % steps) + steps) % steps;
    return ((i * pulses) % steps) < pulses;
}

void Pattern::euclidean (int voice, int pulses, int rotation) noexcept
{
    const int len = std::max (1, (int) length);
    auto& steps = tracks[(size_t) voice].steps;
    for (int i = 0; i < len; ++i)
    {
        const bool hit = euclideanHit (i, len, pulses, rotation);
        if (hit && ! steps[(size_t) i].on)
            steps[(size_t) i] = Step {};
        steps[(size_t) i].on = hit ? 1 : 0;
    }
}

void Pattern::doubleLength() noexcept
{
    const int len = (int) length;
    if (len * 2 > kMaxSteps)
        return;
    for (auto& t : tracks)
        std::copy (t.steps.begin(), t.steps.begin() + len, t.steps.begin() + len);
    length = (uint8_t) (len * 2);
}

void Pattern::randomiseTrack (int voice, float intensity, uint32_t seed) noexcept
{
    intensity = std::clamp (intensity, 0.0f, 1.0f);
    Rng rng (seed * 2654435761u + (uint32_t) voice * 40503u + 1u);
    auto& steps = tracks[(size_t) voice].steps;
    const int len = std::max (1, (int) length);
    const bool hats = voice == ClosedHat || voice == OpenHat || voice == Perc;

    for (int i = 0; i < len; ++i)
    {
        const float w = kWeights[voice][i % 16];
        // Strong positions survive at low intensity; weak ones appear as it rises.
        const float p = w >= 0.9f ? 1.0f : std::min (1.0f, w * intensity * 2.0f);
        Step s;
        s.on = rng.uniform() < p ? 1 : 0;
        if (s.on)
        {
            const float base = w >= 0.9f ? 112.0f : 88.0f;
            s.velocity = (uint8_t) std::clamp ((int) (base + (rng.uniform() - 0.5f) * 60.0f * intensity), 30, 127);
            if (w < 0.9f && intensity > 0.5f && rng.uniform() < intensity - 0.5f)
                s.probability = (uint8_t) (50 + rng.next() % 41);
            if (hats && intensity > 0.6f && rng.uniform() < (intensity - 0.6f))
                s.ratchet = 2;
            if (hats)
                s.micro = (int8_t) ((rng.uniform() - 0.5f) * 20.0f * intensity);
        }
        steps[(size_t) i] = s;
    }
}

void Pattern::sanitise() noexcept
{
    length = (uint8_t) std::clamp ((int) length, 1, kMaxSteps);
    for (auto& t : tracks)
        for (auto& s : t.steps)
            s.sanitise();
}

Pattern makeDefaultHousePattern() noexcept
{
    Pattern p;
    p.length = 16;
    auto set = [&p] (int voice, int step, int vel, int prob = 100)
    {
        auto& s = p.tracks[(size_t) voice].steps[(size_t) step];
        s.on = 1;
        s.velocity = (uint8_t) vel;
        s.probability = (uint8_t) prob;
    };

    for (int i = 0; i < 16; i += 4) set (Kick, i, 118);
    set (Clap, 4, 108);
    set (Clap, 12, 108);
    for (int i = 2; i < 16; i += 4) set (OpenHat, i, 96);
    for (int i = 1; i < 16; i += 2) set (ClosedHat, i, (i % 4 == 1) ? 74 : 88);
    set (Perc, 3, 62);
    set (Perc, 11, 76);
    set (Perc, 14, 58, 50);
    set (Snare, 15, 44, 40);
    return p;
}

} // namespace fp
