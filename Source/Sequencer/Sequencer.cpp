#include "Sequencer/Sequencer.h"

#include <algorithm>
#include <cmath>

namespace fp
{

namespace
{
constexpr double kBrokenGroove[16] = { 0.0, 0.10, -0.05, 0.15, 0.0, 0.05, -0.08, 0.12, 0.0, 0.10, -0.05, 0.18, 0.0, 0.02, -0.10, 0.10 };

inline uint64_t mix64 (uint64_t x) noexcept
{
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

inline int64_t floorDiv (int64_t a, int64_t b) noexcept
{
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

inline int patternLength (const PatternBank& bank, int p) noexcept
{
    return std::clamp ((int) bank.patterns[(size_t) p].length, 1, kMaxSteps);
}

int chainEntry (const SequencerSettings& s, int index) noexcept
{
    int n = 0;
    for (auto e : s.chain)
        if (e >= 0 && e < kNumPatterns && n++ == index)
            return e;
    return std::clamp (s.pattern, 0, kNumPatterns - 1);
}
} // namespace

void Sequencer::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    running = false;
    displayStep = -1;
}

double Sequencer::grooveOffset (int64_t k, const SequencerSettings& s) noexcept
{
    const bool odd = (k & 1) != 0;
    const int pos = (int) (((k % 16) + 16) % 16);
    double off = 0.0;

    if (odd && s.resolution <= 2) // swing only applies to straight resolutions
        off += 2.0 * std::clamp ((double) s.swing, 50.0, 75.0) / 100.0 - 1.0;

    switch (s.groove)
    {
        case 1: off += odd ? 0.33 : 0.0; break;                                   // Shuffle
        case 2: off += odd ? 0.08 : ((pos == 4 || pos == 12) ? 0.04 : 0.0); break; // Laid back
        case 3: off -= odd ? 0.06 : ((pos == 4 || pos == 12) ? 0.03 : 0.0); break; // Push
        case 4: off += ((double) (mix64 ((uint64_t) k * 0x9E3779B97F4A7C15ull ^ s.seed) & 1023) / 1023.0 - 0.5) * 0.1; break;
        case 5: off += kBrokenGroove[pos]; break;
        default: break;
    }

    return off;
}

bool Sequencer::probabilityPasses (int64_t k, int voice, int probability, uint32_t seed) noexcept
{
    if (probability >= 100) return true;
    if (probability <= 0) return false;
    const uint64_t h = mix64 ((uint64_t) k * 0x9E3779B97F4A7C15ull ^ ((uint64_t) (voice + 1) << 48) ^ (uint64_t) seed * 0xD6E8FEB86659FD93ull);
    return (int) (h % 100) < probability;
}

int Sequencer::chainCount (const SequencerSettings& s) const noexcept
{
    if (! s.chainEnabled)
        return 0;
    int n = 0;
    for (auto e : s.chain)
        if (e >= 0 && e < kNumPatterns)
            ++n;
    return n;
}

int Sequencer::nextPattern (const SequencerSettings& s) noexcept
{
    const int n = chainCount (s);
    if (n > 0)
    {
        chainPos = (chainPos + 1) % n;
        return chainEntry (s, chainPos);
    }
    return std::clamp (s.pattern, 0, kNumPatterns - 1);
}

void Sequencer::resync (int64_t absStep, const PatternBank& bank, const SequencerSettings& s) noexcept
{
    const int n = chainCount (s);
    if (n > 0)
    {
        const int len = patternLength (bank, chainEntry (s, 0));
        const int64_t cycle = floorDiv (absStep, len);
        chainPos = (int) (((cycle % n) + n) % n);
        curPattern = chainEntry (s, chainPos);
        cycleStart = cycle * len;
    }
    else
    {
        curPattern = std::clamp (s.pattern, 0, kNumPatterns - 1);
        const int len = patternLength (bank, curPattern);
        cycleStart = floorDiv (absStep, len) * len;
    }

    prevPattern = curPattern;
    prevCycleStart = cycleStart - patternLength (bank, curPattern);
}

Sequencer::Location Sequencer::locate (int64_t k, const PatternBank& bank, const SequencerSettings& s) noexcept
{
    if (k - cycleStart > 4 * kMaxSteps)
        resync (k, bank, s);

    while (k >= cycleStart + patternLength (bank, curPattern))
    {
        prevPattern = curPattern;
        prevCycleStart = cycleStart;
        cycleStart += patternLength (bank, curPattern);
        curPattern = nextPattern (s);
    }

    if (k >= cycleStart)
        return { curPattern, (int) (k - cycleStart), true };

    if (k >= prevCycleStart)
    {
        const int local = (int) (k - prevCycleStart);
        if (local < patternLength (bank, prevPattern))
            return { prevPattern, local, true };
    }

    return { 0, 0, false };
}

int Sequencer::process (const PatternBank& bank, const TransportState& transport,
                        const SequencerSettings& s, int numSamples,
                        TriggerEvent* out, int maxEvents) noexcept
{
    if (! transport.playing)
    {
        running = false;
        displayStep = -1;
        return 0;
    }

    if (numSamples <= 0)
        return 0;

    const int res = std::clamp (s.resolution, 0, (int) kResolutionPpq.size() - 1);
    const double stepLen = kResolutionPpq[(size_t) res];
    const double bpm = std::clamp (transport.bpm, 20.0, 999.0);
    const double ppqPerSample = bpm / (60.0 * sampleRate);
    const double hostStart = transport.ppq;

    // Events are placed on the nearest sample, so the window for this block is
    // offset by half a sample: [-0.5, n - 0.5) samples around the block start.
    const double halfSample = 0.5 * ppqPerSample;
    const double end = hostStart + (double) numSamples * ppqPerSample - halfSample;
    const double expectedStart = hostStart - halfSample;

    const bool jumped = ! running || res != lastResolution || std::abs (expectedStart - prevEnd) > stepLen * 0.25;
    const double start = jumped ? expectedStart : prevEnd;
    const int64_t hostStep = (int64_t) std::floor (hostStart / stepLen + 1.0e-6);

    if (end <= start)
        return 0;

    if (jumped)
    {
        resync (hostStep, bank, s);
        running = true;
        lastResolution = res;
    }

    int count = 0;
    auto emit = [&] (double t, double nominal, int voice, float velocity, const Step& st)
    {
        // After a (re)start, steps that nominally precede the new position
        // belong to music that was never played: skip their late tails.
        if (jumped && nominal < start - 1.0e-9)
            return;

        int offset = -1;
        if (t >= start && t < end)
            offset = (int) std::llround ((t - hostStart) / ppqPerSample);
        else if (jumped && t < start && nominal >= start - 1.0e-9)
            offset = 0; // negative microtiming right at a (re)start: play immediately

        if (offset < 0 || count >= maxEvents)
            return;

        TriggerEvent& e = out[count++];
        e.offset = std::clamp (offset, 0, numSamples - 1);
        e.voice = (uint8_t) voice;
        e.velocity = velocity;
        e.accent = st.accent();
        e.tune = st.tune;
        e.decay = st.decay;
    };

    const int64_t kLo = (int64_t) std::floor (start / stepLen) - 3;
    const int64_t kHi = (int64_t) std::floor (end / stepLen) + 1;

    for (int64_t k = kLo; k <= kHi; ++k)
    {
        const Location loc = locate (k, bank, s);
        if (! loc.valid)
            continue;

        const Pattern& pat = bank.patterns[(size_t) loc.pattern];
        if (loc.step >= (int) pat.length)
            continue;

        const double nominal = (double) k * stepLen;
        const double groove = grooveOffset (k, s);

        for (int v = 0; v < kNumVoices; ++v)
        {
            const Step& st = pat.tracks[(size_t) v].steps[(size_t) loc.step];
            if (! st.on)
                continue;

            const double t = nominal + (groove + (double) st.micro / 100.0) * stepLen;
            const int hits = std::clamp ((int) st.ratchet, 1, 4);
            const double lastHit = t + (double) (hits - 1) * stepLen / hits + stepLen * 0.12;
            if (lastHit < start - stepLen && ! jumped) continue; // entirely in the past
            if (t >= end) continue;                               // entirely in the future
            if (! probabilityPasses (k, v, st.probability, s.seed)) continue;

            const float vel = (float) st.velocity / 127.0f;
            for (int j = 0; j < hits; ++j)
            {
                const double te = t + (double) j * stepLen / hits;
                if (j == 0 && st.flam())
                {
                    emit (te, nominal, v, vel * 0.5f, st);
                    emit (te + stepLen * 0.12 / hits, nominal, v, vel, st);
                }
                else
                {
                    emit (te, nominal, v, j == 0 ? vel : vel * 0.85f, st);
                }
            }
        }
    }

    // Insertion sort by offset (event counts per block are small).
    for (int i = 1; i < count; ++i)
    {
        TriggerEvent tmp = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].offset > tmp.offset)
        {
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = tmp;
    }

    const Location now = locate (hostStep, bank, s);
    if (now.valid)
    {
        displayStep = now.step;
        displayPattern = now.pattern;
    }

    prevEnd = end;
    return count;
}

} // namespace fp
