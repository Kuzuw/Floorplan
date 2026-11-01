// Tests for the JUCE-free core: DSP blocks, voices, engine and sequencer.

#include "TestFramework.h"

#include "Audio/DrumEngine.h"
#include "Audio/TripleBuffer.h"
#include "DSP/DSPCore.h"
#include "DSP/SineTable.h"
#include "Instruments/ClapVoice.h"
#include "Instruments/KickVoice.h"
#include "Instruments/MetalVoice.h"
#include "Instruments/SnareVoice.h"
#include "Instruments/ToneVoice.h"
#include "Sequencer/Pattern.h"
#include "Sequencer/Sequencer.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace fp;

namespace
{
struct Render
{
    std::vector<float> L, R;
    float peak() const
    {
        float p = 0.0f;
        for (size_t i = 0; i < L.size(); ++i) p = std::max ({ p, std::abs (L[i]), std::abs (R[i]) });
        return p;
    }
    bool finite() const
    {
        for (size_t i = 0; i < L.size(); ++i)
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) return false;
        return true;
    }
};

// Renders `seconds` of audio, triggering `voice` once at sample 0.
Render renderHit (DrumEngine& engine, int voice, double sr, double seconds, int block = 256, float velocity = 1.0f)
{
    Render r;
    const int total = (int) (sr * seconds);
    r.L.resize ((size_t) total);
    r.R.resize ((size_t) total);
    TriggerEvent ev;
    ev.voice = (uint8_t) voice;
    ev.velocity = velocity;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        engine.process (&ev, pos == 0 ? 1 : 0, r.L.data() + pos, r.R.data() + pos, nullptr, nullptr, n);
    }
    return r;
}

DrumEngine makeEngine (double sr, int block)
{
    DrumEngine e;
    e.prepare (sr, block);
    e.masterParams().clip = 0.0f;
    for (int v = 0; v < kNumVoices; ++v)
        e.voiceParams (v).levelDb = 0.0f;
    return e;
}

struct SeqRun
{
    std::vector<std::pair<int64_t, int>> events; // absolute sample, voice
};

// Drives the sequencer like a host with a continuous transport.
SeqRun runSequencer (const PatternBank& bank, SequencerSettings settings, double sr, double bpm,
                     int blockSize, int64_t totalSamples, double startPpq = 0.0)
{
    Sequencer seq;
    seq.prepare (sr);
    SeqRun run;
    TriggerEvent events[Sequencer::kMaxEventsPerBlock];
    const double ppqPerSample = bpm / (60.0 * sr);

    for (int64_t pos = 0; pos < totalSamples; pos += blockSize)
    {
        const int n = (int) std::min<int64_t> (blockSize, totalSamples - pos);
        TransportState t { true, startPpq + (double) pos * ppqPerSample, bpm };
        const int count = seq.process (bank, t, settings, n, events, Sequencer::kMaxEventsPerBlock);
        for (int i = 0; i < count; ++i)
            run.events.push_back ({ pos + events[i].offset, events[i].voice });
    }
    return run;
}

PatternBank singleStepBank (int voice, int every)
{
    PatternBank bank;
    auto& p = bank.patterns[0];
    p.length = 16;
    for (int i = 0; i < 16; i += every)
        p.tracks[(size_t) voice].steps[(size_t) i].on = 1;
    return bank;
}
} // namespace

// ---------------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------------
FP_TEST (sine_table_accuracy)
{
    double maxErr = 0.0;
    for (int i = -5000; i < 5000; ++i)
    {
        const float ph = (float) i * 0.000731f;
        maxErr = std::max (maxErr, std::abs ((double) dsp::SineTable::sin2pi (ph) - std::sin (2.0 * 3.141592653589793 * ph)));
    }
    CHECK (maxErr < 1.0e-5);
}

FP_TEST (decay_coefficient_reaches_minus_60db)
{
    const double sr = 48000.0;
    const float coef = dsp::decayCoef (0.25f, sr);
    float env = 1.0f;
    for (int i = 0; i < (int) (0.25 * sr); ++i) env *= coef;
    CHECK_NEAR (20.0 * std::log10 (env), -60.0, 0.5);
}

FP_TEST (svf_lowpass_attenuates_highs)
{
    auto steadyPeak = [] (float freq)
    {
        dsp::Svf f;
        f.setup (500.0f, 0.707f, 48000.0);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            const float y = f.lowpass (std::sin (2.0f * dsp::kPi * freq * (float) i / 48000.0f));
            if (i > 4800) peak = std::max (peak, std::abs (y));
        }
        return peak;
    };
    CHECK (steadyPeak (100.0f) > 0.9f);
    CHECK (steadyPeak (8000.0f) < 0.01f);
}

FP_TEST (soft_clip_is_bounded_and_transparent_near_zero)
{
    CHECK_NEAR (dsp::softClip (0.01f), 0.01f, 1.0e-4);
    for (float x = -100.0f; x <= 100.0f; x += 0.37f)
        CHECK (std::abs (dsp::softClip (x)) <= 1.0f + 1.0e-6f);
}

FP_TEST (noise_is_zero_mean_and_bounded)
{
    dsp::Noise n;
    double sum = 0.0;
    for (int i = 0; i < 200000; ++i)
    {
        const float v = n.next();
        CHECK (v >= -1.0f && v < 1.0f);
        sum += v;
    }
    CHECK (std::abs (sum / 200000.0) < 0.01);
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
FP_TEST (parameter_specs_are_valid)
{
    std::set<std::string> ids;
    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string voiceId = kVoiceSpecs[(size_t) v].id;
        CHECK (ids.insert (voiceId).second);
        // Suffixes taken by the channel strip parameters.
        for (const char* s : { "level", "pan", "mute", "solo", "filter", "drive", "out", "stune", "sdecay" })
            ids.insert (voiceId + "_" + s);

        CHECK (numVoiceParams (v) <= kMaxVoiceParams);
        for (int i = 0; i < numVoiceParams (v); ++i)
        {
            const auto& p = voiceParamSpec (v, i);
            CHECK (p.min < p.max);
            CHECK (p.def >= p.min && p.def <= p.max);
            CHECK (p.centre == 0.0f || (p.centre > p.min && p.centre < p.max));
            CHECK (ids.insert (voiceId + "_" + p.id).second);
        }
    }
}

FP_TEST (extreme_parameters_stay_finite)
{
    for (double sr : { 44100.0, 96000.0 })
    {
        for (int corner = 0; corner < 3; ++corner)
        {
            auto engine = makeEngine (sr, 512);
            for (int v = 0; v < kNumVoices; ++v)
            {
                auto& vp = engine.voiceParams (v);
                for (int i = 0; i < numVoiceParams (v); ++i)
                {
                    const auto& spec = voiceParamSpec (v, i);
                    vp.synth[(size_t) i] = corner == 0 ? spec.min : corner == 1 ? spec.max : ((i & 1) ? spec.min : spec.max);
                }
                vp.drive = corner == 1 ? 1.0f : 0.0f;
                vp.filter = corner == 0 ? -1.0f : corner == 1 ? 1.0f : 0.0f;
            }
            for (int v = 0; v < kNumVoices; ++v)
            {
                auto r = renderHit (engine, v, sr, 0.5);
                CHECK (r.finite());
                CHECK (r.peak() < 4.0f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Voices and engine
// ---------------------------------------------------------------------------
FP_TEST (every_voice_makes_sound_and_goes_idle)
{
    const double sr = 48000.0;
    auto engine = makeEngine (sr, 256);
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto r = renderHit (engine, v, sr, 6.0);
        CHECK (r.finite());
        const float peak = r.peak();
        if (! (peak > 0.05f && peak < 2.0f))
            std::printf ("    voice %d peak %f\n", v, peak);
        CHECK (peak > 0.05f && peak < 2.0f);

        // Last 100 ms must be silent: the voice decayed and stopped.
        float tail = 0.0f;
        for (size_t i = r.L.size() - 4800; i < r.L.size(); ++i) tail = std::max (tail, std::abs (r.L[i]));
        CHECK (tail < 1.0e-3f);
    }
}

FP_TEST (idle_engine_outputs_silence)
{
    auto engine = makeEngine (48000.0, 512);
    std::vector<float> L (512, 1.0f), R (512, 1.0f);
    engine.process (nullptr, 0, L.data(), R.data(), nullptr, nullptr, 512);
    for (int i = 0; i < 512; ++i) CHECK (L[(size_t) i] == 0.0f && R[(size_t) i] == 0.0f);
}

FP_TEST (velocity_scales_output)
{
    auto e1 = makeEngine (48000.0, 256);
    auto e2 = makeEngine (48000.0, 256);
    const float loud = renderHit (e1, Kick, 48000.0, 0.3, 256, 1.0f).peak();
    const float soft = renderHit (e2, Kick, 48000.0, 0.3, 256, 0.3f).peak();
    CHECK (soft < loud * 0.5f);
    CHECK (DrumEngine::velocityToGain (1.0f, true, 1.0f) > DrumEngine::velocityToGain (1.0f, false, 1.0f));
}

FP_TEST (trigger_is_sample_accurate)
{
    auto engine = makeEngine (48000.0, 512);
    std::vector<float> L (512), R (512);
    TriggerEvent ev;
    ev.voice = Snare; // noise starts immediately
    ev.offset = 300;
    engine.process (&ev, 1, L.data(), R.data(), nullptr, nullptr, 512);
    for (int i = 0; i < 300; ++i) CHECK (L[(size_t) i] == 0.0f);
    float after = 0.0f;
    for (int i = 300; i < 340; ++i) after = std::max (after, std::abs (L[(size_t) i]));
    CHECK (after > 0.01f);
}

FP_TEST (mute_and_solo)
{
    auto engine = makeEngine (48000.0, 256);
    engine.voiceParams (Kick).mute = true;
    // First block ramps from unity to zero; after that the kick must be silent.
    auto r = renderHit (engine, Kick, 48000.0, 0.05);
    float lateMax = 0.0f;
    for (size_t i = 256; i < r.L.size(); ++i) lateMax = std::max (lateMax, std::abs (r.L[i]));
    CHECK (lateMax == 0.0f);

    auto soloEngine = makeEngine (48000.0, 256);
    soloEngine.voiceParams (Snare).solo = true;
    auto k = renderHit (soloEngine, Kick, 48000.0, 0.05);
    lateMax = 0.0f;
    for (size_t i = 256; i < k.L.size(); ++i) lateMax = std::max (lateMax, std::abs (k.L[i]));
    CHECK (lateMax == 0.0f);
}

FP_TEST (open_hat_is_choked_by_closed_hat)
{
    const double sr = 48000.0;
    auto engine = makeEngine (sr, 256);
    engine.voiceParams (ClosedHat).mute = true; // listen to the open hat only
    std::vector<float> L (24000), R (24000);
    TriggerEvent evs[2];
    evs[0].voice = OpenHat; evs[0].offset = 0;
    engine.process (evs, 1, L.data(), R.data(), nullptr, nullptr, 256);
    for (int pos = 256; pos < 24000; pos += 256)
    {
        evs[1].voice = ClosedHat; evs[1].offset = 0;
        engine.process (pos == 4864 ? &evs[1] : nullptr, pos == 4864 ? 1 : 0, L.data() + pos, R.data() + pos, nullptr, nullptr, std::min (256, 24000 - pos));
    }
    float lateMax = 0.0f;
    for (int i = 4864 + 2400; i < 24000; ++i) lateMax = std::max (lateMax, std::abs (L[(size_t) i]));
    CHECK (lateMax < 1.0e-3f);
}

FP_TEST (retrigger_is_click_free)
{
    // Retrigger a long kick mid-cycle and check there is no large sample jump.
    const double sr = 48000.0;
    auto engine = makeEngine (sr, 64);
    engine.voiceParams (Kick).synth[KickP::Click] = 0.0f; // the click layer is a deliberate transient
    std::vector<float> L (9600), R (9600);
    TriggerEvent ev; ev.voice = Kick;
    for (int pos = 0; pos < 9600; pos += 64)
    {
        ev.offset = 17;
        const bool hit = pos == 0 || pos == 3200; // second hit at sample 3217
        engine.process (&ev, hit ? 1 : 0, L.data() + pos, R.data() + pos, nullptr, nullptr, 64);
    }
    float maxJump = 0.0f;
    for (int i = 1; i < 9600; ++i) maxJump = std::max (maxJump, std::abs (L[(size_t) i] - L[(size_t) i - 1]));
    CHECK (maxJump < 0.25f);
}

// ---------------------------------------------------------------------------
// Kick: Advanced parameters
// ---------------------------------------------------------------------------
namespace
{
using VoiceArray = std::array<float, kMaxVoiceParams>;

VoiceArray defaultParams (int voice)
{
    VoiceArray p {};
    for (int i = 0; i < numVoiceParams (voice); ++i)
        p[(size_t) i] = voiceParamSpec (voice, i).def;
    return p;
}

// One hit of a voice, rendered directly (no channel strip), left channel.
std::vector<float> renderVoiceDirect (int voice, const VoiceArray& p, double sr, double seconds,
                                      std::vector<float>* right = nullptr)
{
    auto v = createSynthVoice (voice);
    v->prepare (sr);
    v->trigger (p.data(), TriggerInfo {});
    std::vector<float> L ((size_t) (sr * seconds)), R (L.size());
    v->render (L.data(), R.data(), (int) L.size());
    if (right != nullptr)
        *right = R;
    return L;
}

using Stereo = std::pair<std::vector<float>, std::vector<float>>;

float maxDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0.0f;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) d = std::max (d, std::abs (a[i] - b[i]));
    return d;
}

// The snare as 0.1.0 rendered it.
Stereo renderOriginalSnare (const float* p, double sr, int total)
{
    using namespace SnareP;
    const float inv = (float) (1.0 / sr);
    const float freq = p[Tune], tone = p[Tone] * 0.01f;
    const float bodyMix = 1.0f - 0.8f * tone, noiseMix = 0.3f + 0.9f * tone, width = p[Width] * 0.01f;
    const float bodyCoef = dsp::decayCoef (p[BodyDecay] * 0.001f, sr), noiseCoef = dsp::decayCoef (p[Decay] * 0.001f, sr);
    const float pitchCoef = dsp::timeConstantCoef (0.006f, sr);
    float bodyEnv = 1.0f, noiseEnv = 1.0f, pitchEnv = 1.0f, ph1 = 0.0f, ph2 = 0.0f;
    const float cutoff = 600.0f * std::pow (10.0f, p[Snappy] * 0.01f);
    dsp::Svf hpL, hpR;
    hpL.setup (cutoff, 0.7f, sr);
    hpR.setup (cutoff, 0.7f, sr);
    dsp::Noise noiseA, noiseB { 0x1234567u };
    const bool stereo = width > 0.001f;

    Stereo out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
    for (size_t i = 0; i < (size_t) total; ++i)
    {
        const float sweep = 1.0f + 0.5f * pitchEnv;
        pitchEnv *= pitchCoef;
        ph1 += freq * sweep * inv;
        ph2 += freq * 1.62f * sweep * inv;
        if (ph1 >= 1.0f) ph1 -= 1.0f;
        if (ph2 >= 1.0f) ph2 -= 1.0f;
        const float body = (dsp::SineTable::sin2pi (ph1) + 0.45f * dsp::SineTable::sin2pi (ph2)) * bodyEnv * bodyMix;
        bodyEnv *= bodyCoef;
        const float n1 = noiseA.next();
        const float wiresL = hpL.highpass (n1) * noiseEnv * noiseMix;
        const float wiresR = stereo ? hpR.highpass (n1 + width * (noiseB.next() - n1)) * noiseEnv * noiseMix : wiresL;
        noiseEnv *= noiseCoef;
        out.first[i] = (body + wiresL) * 0.7f;
        out.second[i] = (body + wiresR) * 0.7f;
    }
    return out;
}

// The clap as 0.1.0 rendered it.
Stereo renderOriginalClap (const float* p, double sr, int total)
{
    using namespace ClapP;
    dsp::Svf bpL, bpR;
    bpL.setup (p[Tune], 0.8f + p[Tone] * 0.03f, sr);
    bpR.setup (p[Tune], 0.8f + p[Tone] * 0.03f, sr);
    const float bandGain = bpL.k * 2.2f;
    const int bursts = std::clamp ((int) std::lround (p[Bursts]), 1, 6);
    const int spreadSamples = std::max (1, dsp::msToSamples (p[Spread], sr));
    const float tailCoef = dsp::decayCoef (p[Decay] * 0.001f, sr);
    const float burstCoef = dsp::decayCoef (std::min (p[Spread] * 1.6f, 30.0f) * 0.001f, sr);
    const float attackInc = 1.0f / std::max (1.0f, 0.0004f * (float) sr);
    const float width = p[Width] * 0.01f;
    const bool stereo = width > 0.001f;
    int burstIndex = 0, nextBurstAt = 0, counter = 0;
    float envCoef = burstCoef, env = 0.0f;
    bool attacking = false;
    dsp::Noise noiseA { 0xC1A9u }, noiseB { 0xBEEFu };

    Stereo out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
    for (size_t i = 0; i < (size_t) total; ++i)
    {
        if (burstIndex < bursts && counter >= nextBurstAt)
        {
            envCoef = (burstIndex == bursts - 1) ? tailCoef : burstCoef;
            attacking = true;
            ++burstIndex;
            nextBurstAt += spreadSamples;
        }
        ++counter;
        if (attacking)
        {
            env += attackInc;
            if (env >= 1.0f) { env = 1.0f; attacking = false; }
        }
        else
            env *= envCoef;
        const float n1 = noiseA.next();
        out.first[i] = bpL.bandpass (n1) * bandGain * env;
        out.second[i] = stereo ? bpR.bandpass (n1 + width * (noiseB.next() - n1)) * bandGain * env : out.first[i];
    }
    return out;
}

// The references below are the 0.1.0 voices, with one change also made to
// the voices since: millisecond times become whole samples with
// dsp::msToSamples (rounded) instead of a truncating cast, which differed by
// one sample between compilers under fast-math.
//
// The voices are built with fast-math and the references are not, so allow
// rounding noise. A real change is far larger: 1% more snare Ring is ~4e-3.
constexpr float kReferenceTolerance = 5.0e-4f;

// The kick as 0.1.0 rendered it, before it had Advanced parameters.
std::vector<float> renderOriginalKick (const float* p, double sr, int total)
{
    using namespace KickP;
    const float inv = (float) (1.0 / sr);
    const float baseFreq = p[Tune], sweepDepth = dsp::semitonesToRatio (p[Bend]) - 1.0f;
    const float sweepCoef = dsp::timeConstantCoef (p[BendTime] * 0.001f, sr);
    const float ampCoef = dsp::decayCoef (p[Decay] * 0.001f, sr);
    const float clickAmt = p[Click] * 0.01f, clickCoef = dsp::decayCoef (0.007f, sr);
    float sweepEnv = 1.0f, amp = 1.0f, clickEnv = 1.0f, phase = 0.0f;
    int holdLeft = dsp::msToSamples (p[Punch] * 0.6f, sr);
    dsp::OnePoleHp hp;
    hp.setup (1800.0f, sr);
    dsp::Noise noise;

    std::vector<float> out ((size_t) total);
    for (auto& o : out)
    {
        phase += baseFreq * (1.0f + sweepDepth * sweepEnv) * inv;
        sweepEnv *= sweepCoef;
        if (phase >= 1.0f) phase -= 1.0f;
        const float body = dsp::SineTable::sin2pi (phase) * amp;
        if (holdLeft > 0) --holdLeft; else amp *= ampCoef;
        float click = 0.0f;
        if (clickEnv > 1.0e-4f)
        {
            click = hp.process (noise.next()) * clickEnv * clickAmt * 0.6f;
            clickEnv *= clickCoef;
        }
        o = body + click;
    }
    return out;
}

float peakIn (const std::vector<float>& x, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

double energyIn (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i) e += (double) x[i] * x[i];
    return e;
}

// Energy of the first difference relative to the signal's energy, over the
// first 2400 samples: higher means more top end.
double highShare (const std::vector<float>& x)
{
    double diff = 0.0, all = 0.0;
    for (size_t i = 1; i < std::min ((size_t) 2400, x.size()); ++i)
    {
        diff += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        all += (double) x[i] * x[i];
    }
    return diff / all;
}
} // namespace

FP_TEST (kick_defaults_match_the_original_kick)
{
    // Older projects and kits do not store the new parameters, so the kick at
    // their defaults must still be the 0.1.0 kick.
    const double sr = 48000.0;
    auto p = defaultParams (Kick);
    for (int variant = 0; variant < 3; ++variant)
    {
        if (variant == 1) { p[KickP::Punch] = 90.0f; p[KickP::Click] = 100.0f; p[KickP::Bend] = 40.0f; }
        if (variant == 2) { p[KickP::Tune] = 31.0f; p[KickP::Decay] = 1800.0f; p[KickP::BendTime] = 150.0f; }
        const auto now = renderVoiceDirect (Kick, p, sr, 1.0);
        const auto ref = renderOriginalKick (p.data(), sr, (int) now.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < now.size(); ++i) maxDiff = std::max (maxDiff, std::abs (now[i] - ref[i]));
        CHECK (maxDiff < kReferenceTolerance);
    }
}

FP_TEST (kick_attack_softens_the_onset)
{
    const double sr = 48000.0;
    auto p = defaultParams (Kick);
    p[KickP::Click] = 0.0f;
    p[KickP::Bend] = 0.0f;
    const auto hard = renderVoiceDirect (Kick, p, sr, 0.2);
    p[KickP::Attack] = 20.0f;
    const auto soft = renderVoiceDirect (Kick, p, sr, 0.2);

    CHECK (peakIn (soft, 0, 96) < 0.2f * peakIn (hard, 0, 96));          // first 2 ms
    CHECK (peakIn (soft, 1440, 2880) > 0.9f * peakIn (hard, 1440, 2880)); // 30-60 ms: full level
}

FP_TEST (kick_envelope_model_matches_rendered_output)
{
    // The editor draws KickVoice::ampEnvelope; it must describe what is heard.
    const double sr = 48000.0;
    auto p = defaultParams (Kick);
    p[KickP::Click] = 0.0f;
    p[KickP::Bend] = 0.0f;
    p[KickP::Tune] = 100.0f; // 10 ms period
    p[KickP::Attack] = 10.0f;
    p[KickP::Punch] = 50.0f; // 30 ms hold
    p[KickP::Decay] = 1500.0f;
    const auto out = renderVoiceDirect (Kick, p, sr, 1.2);

    for (size_t i = 0; i < out.size(); ++i)
        if (std::abs (out[i]) > KickVoice::ampEnvelope (p.data(), (float) ((double) i / sr)) + 1.0e-3f)
        {
            CHECK (false); // output exceeds the drawn envelope
            break;
        }

    for (float t : { 0.03f, 0.2f, 0.5f, 1.0f })
    {
        const auto centre = (size_t) (t * sr);
        const float measured = peakIn (out, centre - 240, centre + 240); // one period
        const float model = KickVoice::ampEnvelope (p.data(), t);
        CHECK_NEAR (measured, model, 0.05f * model);
    }

    // Pitch model: no sweep without bend; the full bend at t = 0.
    CHECK_NEAR (KickVoice::pitchSemitones (p.data(), 0.0f), 0.0f, 1.0e-4f);
    p[KickP::Bend] = 24.0f;
    CHECK_NEAR (KickVoice::pitchSemitones (p.data(), 0.0f), 24.0f, 1.0e-3f);
    CHECK (KickVoice::pitchSemitones (p.data(), 0.2f) < 1.0f);
}

FP_TEST (kick_shape_squares_the_body)
{
    const double sr = 48000.0;
    auto p = defaultParams (Kick);
    p[KickP::Click] = 0.0f;
    p[KickP::Bend] = 0.0f;
    p[KickP::Punch] = 100.0f; // 60 ms at full level
    auto crest = [&] (float shape)
    {
        p[KickP::Shape] = shape;
        const auto out = renderVoiceDirect (Kick, p, sr, 0.06);
        const double rms = std::sqrt (energyIn (out, 0, out.size()) / (double) out.size());
        return std::make_pair ((float) rms / peakIn (out, 0, out.size()), peakIn (out, 0, out.size()));
    };
    const auto [clean, cleanPeak] = crest (0.0f);
    const auto [shaped, shapedPeak] = crest (100.0f);
    CHECK (clean < 0.75f);           // a sine: rms/peak = 0.707
    CHECK (shaped > 0.85f);          // close to a square
    CHECK (shapedPeak < 1.001f);     // normalised, no level jump
    CHECK (cleanPeak > 0.99f);
}

FP_TEST (kick_sub_lengthens_the_tail)
{
    const double sr = 48000.0;
    auto p = defaultParams (Kick); // decay 420 ms
    p[KickP::Click] = 0.0f;
    const auto dry = renderVoiceDirect (Kick, p, sr, 0.8);
    p[KickP::Sub] = 100.0f;
    const auto sub = renderVoiceDirect (Kick, p, sr, 0.8);
    const auto from = (size_t) (0.45 * sr), to = (size_t) (0.6 * sr);
    CHECK (energyIn (sub, from, to) > 4.0 * energyIn (dry, from, to));
}

FP_TEST (kick_click_tone_and_length)
{
    // The click layer alone: the same kick with and without click.
    const double sr = 48000.0;
    auto clickOnly = [&] (float tone, float length)
    {
        auto p = defaultParams (Kick);
        p[KickP::ClickTone] = tone;
        p[KickP::ClickLength] = length;
        p[KickP::Click] = 0.0f;
        const auto without = renderVoiceDirect (Kick, p, sr, 0.1);
        p[KickP::Click] = 100.0f;
        auto with = renderVoiceDirect (Kick, p, sr, 0.1);
        for (size_t i = 0; i < with.size(); ++i) with[i] -= without[i];
        return with;
    };

    const auto shortClick = clickOnly (1800.0f, 2.0f), longClick = clickOnly (1800.0f, 40.0f);
    CHECK (energyIn (longClick, 480, 1920) > 100.0 * energyIn (shortClick, 480, 1920)); // 10-40 ms

    // Share of the click's energy below a few kHz (8-sample moving average).
    auto lowShare = [] (const std::vector<float>& x)
    {
        double low = 0.0, all = 0.0;
        for (size_t i = 8; i < 480; ++i)
        {
            float avg = 0.0f;
            for (size_t k = 0; k < 8; ++k) avg += x[i - k] * 0.125f;
            low += (double) avg * avg;
            all += (double) x[i] * x[i];
        }
        return low / all;
    };
    CHECK (lowShare (clickOnly (600.0f, 7.0f)) > 2.0 * lowShare (clickOnly (8000.0f, 7.0f)));
}

// ---------------------------------------------------------------------------
// Snare and clap: Advanced parameters
// ---------------------------------------------------------------------------
FP_TEST (snare_and_clap_defaults_match_the_originals)
{
    const double sr = 48000.0;
    for (int variant = 0; variant < 3; ++variant)
    {
        auto s = defaultParams (Snare);
        auto c = defaultParams (Clap);
        if (variant == 1)
        {
            s[SnareP::Tone] = 90.0f; s[SnareP::Snappy] = 10.0f; s[SnareP::Width] = 0.0f;
            c[ClapP::Bursts] = 6.0f; c[ClapP::Spread] = 22.0f; c[ClapP::Width] = 0.0f;
        }
        if (variant == 2)
        {
            s[SnareP::Tune] = 320.0f; s[SnareP::BodyDecay] = 350.0f; s[SnareP::Width] = 100.0f;
            c[ClapP::Bursts] = 1.0f; c[ClapP::Tone] = 100.0f; c[ClapP::Decay] = 900.0f;
        }

        std::vector<float> R;
        auto L = renderVoiceDirect (Snare, s, sr, 1.0, &R);
        auto ref = renderOriginalSnare (s.data(), sr, (int) L.size());
        CHECK (maxDifference (L, ref.first) < kReferenceTolerance);
        CHECK (maxDifference (R, ref.second) < kReferenceTolerance);

        L = renderVoiceDirect (Clap, c, sr, 1.0, &R);
        ref = renderOriginalClap (c.data(), sr, (int) L.size());
        CHECK (maxDifference (L, ref.first) < kReferenceTolerance);
        CHECK (maxDifference (R, ref.second) < kReferenceTolerance);
    }
}

FP_TEST (snare_envelopes_bound_the_output)
{
    // The editor draws SnareVoice's body and wire envelopes. The output can
    // never exceed what they allow (noise through the high-pass stays < 4).
    const double sr = 48000.0;
    auto p = defaultParams (Snare);
    p[SnareP::Width] = 0.0f;
    p[SnareP::WireDelay] = 12.0f;
    p[SnareP::BodyDecay] = 60.0f;
    p[SnareP::Decay] = 300.0f;
    p[SnareP::Ring] = 100.0f;
    const float tone = p[SnareP::Tone] * 0.01f, bodyMix = 1.0f - 0.8f * tone, noiseMix = 0.3f + 0.9f * tone;
    const auto out = renderVoiceDirect (Snare, p, sr, 0.5);

    bool bounded = true;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) ((double) i / sr);
        // 0.1 ms of slack on the wires' onset (sample rounding of the delay).
        const float limit = 0.7f * (2.0f * bodyMix * SnareVoice::bodyEnvelope (p.data(), t)
                                    + 4.0f * noiseMix * SnareVoice::wireEnvelope (p.data(), t + 1.0e-4f));
        bounded = bounded && std::abs (out[i]) <= limit + 1.0e-3f;
    }
    CHECK (bounded);
    CHECK (SnareVoice::wireEnvelope (p.data(), 0.011f) == 0.0f);
    CHECK_NEAR (SnareVoice::pitchDrop (p.data(), 0.0f), 0.5f, 1.0e-6f);
}

FP_TEST (snare_wire_delay_and_tone)
{
    const double sr = 48000.0;
    auto p = defaultParams (Snare);
    p[SnareP::Tone] = 100.0f;      // mostly wires
    p[SnareP::BodyDecay] = 20.0f;
    const auto now = renderVoiceDirect (Snare, p, sr, 0.1);
    p[SnareP::WireDelay] = 10.0f;
    const auto late = renderVoiceDirect (Snare, p, sr, 0.1);
    CHECK (energyIn (late, 48, 432) < 0.3 * energyIn (now, 48, 432));     // 1-9 ms: wires not yet in
    CHECK (energyIn (late, 576, 1920) > 0.7 * energyIn (now, 576, 1920)); // 12-40 ms: they are

    // Wire Tone: a low-pass on the wires removes their top end.
    p[SnareP::WireDelay] = 0.0f;
    const auto open = renderVoiceDirect (Snare, p, sr, 0.1);
    p[SnareP::WireTone] = 2500.0f;
    const auto dark = renderVoiceDirect (Snare, p, sr, 0.1);
    CHECK (highShare (dark) < 0.5 * highShare (open));
}

FP_TEST (snare_drop_and_ring_change_the_shell)
{
    // Tone 0 keeps the shell loud. The wires' noise is the same in every
    // render, so any difference comes from the shell.
    const double sr = 48000.0;
    auto p = defaultParams (Snare);
    p[SnareP::Tone] = 0.0f;
    p[SnareP::Width] = 0.0f;
    const auto base = renderVoiceDirect (Snare, p, sr, 0.05);

    auto noDrop = p;
    noDrop[SnareP::Drop] = 0.0f;
    // A pitch drop runs the shell faster at first, so the phase leads.
    CHECK (maxDifference (renderVoiceDirect (Snare, noDrop, sr, 0.05), base) > 0.05f);

    auto noRing = p;
    noRing[SnareP::Ring] = 0.0f;
    CHECK (maxDifference (renderVoiceDirect (Snare, noRing, sr, 0.05), base) > 0.05f);
}

namespace
{
std::vector<float> clapEnvelope (const VoiceArray& p, uint32_t seed, double sr, int samples)
{
    ClapEnvelope env;
    env.setup (p.data(), sr, 1.0f, seed);
    std::vector<float> out ((size_t) samples);
    for (auto& v : out) v = env.next();
    return out;
}

// Hits = points where the envelope starts rising again.
int countHits (const std::vector<float>& env)
{
    int hits = 0;
    for (size_t i = 1; i < env.size(); ++i)
        if (env[i] > env[i - 1] && (i < 2 || env[i - 1] <= env[i - 2]))
            ++hits;
    return hits;
}
} // namespace

FP_TEST (clap_envelope_controls)
{
    const double sr = 48000.0;
    auto p = defaultParams (Clap); // 4 bursts, 9 ms apart
    const int n = (int) (0.6 * sr);
    CHECK (countHits (clapEnvelope (p, 1, sr, n)) == 4);

    // Jitter 0: the same spacing whatever the seed. Jitter: a new spacing per
    // seed, still four hits.
    CHECK (clapEnvelope (p, 1, sr, n) == clapEnvelope (p, 2, sr, n));
    p[ClapP::Jitter] = 100.0f;
    CHECK (clapEnvelope (p, 1, sr, n) != clapEnvelope (p, 2, sr, n));
    CHECK (countHits (clapEnvelope (p, 2, sr, n)) == 4);
    p[ClapP::Jitter] = 0.0f;

    // Burst Length: longer hits leave more level before the next one.
    const auto beforeSecondHit = (size_t) (0.008 * sr);
    const float normal = clapEnvelope (p, 1, sr, n)[beforeSecondHit];
    p[ClapP::BurstLength] = 400.0f;
    CHECK (clapEnvelope (p, 1, sr, n)[beforeSecondHit] > 2.0f * normal);
    p[ClapP::BurstLength] = 100.0f;

    // Attack: a slower rise.
    p[ClapP::Attack] = 4.0f;
    CHECK (clapEnvelope (p, 1, sr, n)[(size_t) (0.002 * sr)] < 0.6f);
    p[ClapP::Attack] = 0.4f;

    // Tail Level: the tail peaks at that level, and the audio follows.
    p[ClapP::TailLevel] = 30.0f;
    const auto quiet = clapEnvelope (p, 1, sr, n);
    const auto tailStart = (size_t) (0.027 * sr) + 5, tailEnd = (size_t) (0.2 * sr);
    CHECK_NEAR (*std::max_element (quiet.begin() + (long) tailStart, quiet.begin() + (long) tailEnd), 0.3f, 1.0e-4f);
    const auto quietAudio = renderVoiceDirect (Clap, p, sr, 0.3);
    p[ClapP::TailLevel] = 100.0f;
    const auto loudAudio = renderVoiceDirect (Clap, p, sr, 0.3);
    const auto from = (size_t) (0.04 * sr), to = (size_t) (0.25 * sr);
    CHECK (energyIn (quietAudio, from, to) < 0.15 * energyIn (loudAudio, from, to));
}

FP_TEST (envelope_curves_describe_the_voices)
{
    // What the Pitch/Env page draws, for each voice that has one.
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto p = defaultParams (v);
        EnvelopeCurves curves;
        const bool has = describeEnvelopes (v, p.data(), 0.5f, curves);
        CHECK (has == hasEnvelopePage (kVoiceSpecs[(size_t) v].engine));
        if (! has)
            continue;
        CHECK (curves.count >= 1 && curves.count <= EnvelopeCurves::kMaxCurves);
        for (int c = 0; c < curves.count; ++c)
            for (float y : curves.curves[(size_t) c].y)
                CHECK (y >= 0.0f && y <= 1.0f);
        CHECK (hitLengthSeconds (v, p.data()) > 0.05f);
    }

    // The clap curve is the voice's own envelope: four hits at full height.
    auto p = defaultParams (Clap);
    EnvelopeCurves curves;
    describeEnvelopes (Clap, p.data(), 0.3f, curves);
    std::vector<float> y (curves.curves[0].y.begin(), curves.curves[0].y.end());
    CHECK (*std::max_element (y.begin(), y.end()) > 0.99f);
}

// ---------------------------------------------------------------------------
// Hats, cymbal, percussion and tom: Advanced parameters
// ---------------------------------------------------------------------------
namespace
{
constexpr std::array<float, 6> kHatPartials    { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
constexpr std::array<float, 6> kCymbalPartials { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f };

// The hats and cymbal as 0.1.1 rendered them.
Stereo renderOriginalMetal (const float* p, const std::array<float, 6>& partials, double sr, int total)
{
    using namespace MetalP;
    const float inv = (float) (1.0 / sr);
    std::array<dsp::BlepSquare, 6> oscs {};
    for (size_t i = 0; i < oscs.size(); ++i)
    {
        oscs[i].inc = std::min (0.45f, partials[i] * p[Tune] * inv);
        oscs[i].phase = 0.13f * (float) i;
    }
    const float envCoef = dsp::decayCoef (p[Decay] * 0.001f, sr);
    const float metalMix = p[Metal] * 0.01f, toneAmt = p[Tone] * 0.01f, width = p[Width] * 0.01f;
    const float bright = p[Bright] * std::sqrt (p[Tune]);
    dsp::Svf hpL, hpR, bpL, bpR;
    hpL.setup (bright, 0.7f, sr);
    hpR.setup (bright, 0.7f, sr);
    bpL.setup (bright * 1.2f, 1.0f + toneAmt * 5.0f, sr);
    bpR.setup (bright * 1.2f, 1.0f + toneAmt * 5.0f, sr);
    const float bandGain = bpL.k * toneAmt * 1.5f;
    const float noiseMix = (1.0f - metalMix) * 0.8f;
    const bool stereo = width > 0.001f;
    dsp::Noise noiseA { 0x51A7u }, noiseB { 0x7E11u };
    float env = 1.0f;

    Stereo out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
    for (size_t i = 0; i < (size_t) total; ++i)
    {
        float metallic = 0.0f;
        for (auto& o : oscs) metallic += o.next();
        metallic *= metalMix * (1.0f / 6.0f);
        const float srcL = metallic + noiseMix * noiseA.next();
        const auto fL = hpL.process (srcL);
        const float outL = (fL.high + bpL.bandpass (srcL) * bandGain) * env;
        float outR = outL;
        if (stereo)
        {
            const float srcR = metallic + noiseMix * noiseB.next();
            const float mixR = srcR + (1.0f - width) * (srcL - srcR);
            outR = (hpR.highpass (mixR) + bpR.bandpass (mixR) * bandGain) * env;
        }
        env *= envCoef;
        out.first[i] = outL * 0.8f;
        out.second[i] = outR * 0.8f;
    }
    return out;
}

// The percussion and tom as 0.1.1 rendered them.
std::vector<float> renderOriginalTone (const float* p, float modRatio, double sr, int total)
{
    using namespace ToneP;
    const float inv = (float) (1.0 / sr);
    const float baseFreq = p[Tune], sweepDepth = dsp::semitonesToRatio (p[Bend]) - 1.0f;
    const float sweepCoef = dsp::timeConstantCoef (p[BendTime] * 0.001f, sr);
    const float ampCoef = dsp::decayCoef (p[Decay] * 0.001f, sr);
    const float noiseAmt = p[Noise] * 0.01f, noiseCoef = dsp::decayCoef (0.03f, sr);
    const float fmIndex = p[Metal] * 0.01f * 0.7f;
    const bool fm = fmIndex > 0.0001f;
    dsp::OnePoleHp hp;
    hp.setup (1500.0f, sr);
    dsp::Noise noise { 0xD00Du };
    float sweepEnv = 1.0f, amp = 1.0f, noiseEnv = 1.0f, phC = 0.0f, phM = 0.0f;

    std::vector<float> out ((size_t) total);
    for (auto& o : out)
    {
        const float freq = baseFreq * (1.0f + sweepDepth * sweepEnv);
        sweepEnv *= sweepCoef;
        phC += freq * inv;
        if (phC >= 1.0f) phC -= 1.0f;
        float mod = 0.0f;
        if (fm)
        {
            phM += freq * modRatio * inv;
            if (phM >= 1.0f) phM -= 1.0f;
            mod = dsp::SineTable::sin2pi (phM) * fmIndex * amp;
        }
        float v = dsp::SineTable::sin2pi (phC + mod) * amp;
        amp *= ampCoef;
        if (noiseEnv > 1.0e-4f)
        {
            v += hp.process (noise.next()) * noiseEnv * noiseAmt * 0.6f;
            noiseEnv *= noiseCoef;
        }
        o = v;
    }
    return out;
}
} // namespace

FP_TEST (metal_and_tone_defaults_match_the_originals)
{
    const double sr = 48000.0;
    for (int variant = 0; variant < 3; ++variant)
    {
        for (int voice : { (int) ClosedHat, (int) OpenHat, (int) Cymbal })
        {
            auto p = defaultParams (voice);
            if (variant == 1) { p[MetalP::Metal] = 0.0f; p[MetalP::Tone] = 100.0f; p[MetalP::Width] = 0.0f; p[MetalP::Tune] = 1.7f; }
            if (variant == 2) { p[MetalP::Metal] = 100.0f; p[MetalP::Tune] = 0.6f; p[MetalP::Bright] = 13000.0f; p[MetalP::Width] = 100.0f; }

            std::vector<float> R;
            const auto L = renderVoiceDirect (voice, p, sr, 1.0, &R);
            const auto ref = renderOriginalMetal (p.data(), voice == Cymbal ? kCymbalPartials : kHatPartials, sr, (int) L.size());
            CHECK (maxDifference (L, ref.first) < kReferenceTolerance);
            CHECK (maxDifference (R, ref.second) < kReferenceTolerance);
        }

        for (int voice : { (int) Perc, (int) Tom })
        {
            auto p = defaultParams (voice);
            if (variant == 1) { p[ToneP::Bend] = 24.0f; p[ToneP::BendTime] = 200.0f; p[ToneP::Metal] = 100.0f; p[ToneP::Noise] = 100.0f; }
            if (variant == 2) { p[ToneP::Tune] = voice == Perc ? 1500.0f : 280.0f; p[ToneP::Decay] = 1400.0f; p[ToneP::Metal] = 60.0f; }

            const auto now = renderVoiceDirect (voice, p, sr, 1.0);
            const auto ref = renderOriginalTone (p.data(), voice == Perc ? 1.41f : 1.5f, sr, (int) now.size());
            CHECK (maxDifference (now, ref) < kReferenceTolerance);
        }
    }
}

FP_TEST (metal_attack_hold_and_tail_follow_the_envelope)
{
    // The filters run before the envelope, so dividing a hit by the same hit
    // held at full level gives exactly the envelope the voice applied. It must
    // be what MetalVoice::ampEnvelope (and so the editor) shows.
    const double sr = 48000.0;
    auto flat = defaultParams (ClosedHat);
    flat[MetalP::Width] = 0.0f;
    flat[MetalP::Decay] = 200.0f;
    flat[MetalP::Hold] = 500.0f;
    auto p = flat;
    p[MetalP::Attack] = 20.0f;
    p[MetalP::Hold] = 30.0f;
    p[MetalP::Tail] = 60.0f;

    const auto held = renderVoiceDirect (ClosedHat, flat, sr, 0.45);
    const auto out = renderVoiceDirect (ClosedHat, p, sr, 0.45);
    float worst = 0.0f;
    int checked = 0;
    for (size_t i = 0; i < out.size(); ++i)
        if (std::abs (held[i]) > 0.02f)
        {
            const float model = MetalVoice::ampEnvelope (p.data(), (float) ((double) i / sr));
            worst = std::max (worst, std::abs (out[i] / held[i] - model));
            ++checked;
        }
    CHECK (checked > 1000);
    CHECK (worst < 0.01f);

    // Tail keeps the hit going after the main decay.
    auto noTail = p;
    noTail[MetalP::Tail] = 0.0f;
    const auto from = (size_t) (0.3 * sr), to = (size_t) (0.45 * sr);
    CHECK (energyIn (out, from, to) > 10.0 * energyIn (renderVoiceDirect (ClosedHat, noTail, sr, 0.45), from, to));
}

FP_TEST (metal_detune_high_cut_and_stick)
{
    const double sr = 48000.0;
    auto p = defaultParams (OpenHat);
    p[MetalP::Width] = 0.0f;
    const auto base = renderVoiceDirect (OpenHat, p, sr, 0.1);

    auto detuned = p;
    detuned[MetalP::Detune] = 100.0f;
    CHECK (maxDifference (renderVoiceDirect (OpenHat, detuned, sr, 0.1), base) > 0.05f);

    auto dark = p;
    dark[MetalP::HighCut] = 3000.0f;
    CHECK (highShare (renderVoiceDirect (OpenHat, dark, sr, 0.1)) < 0.5 * highShare (base));

    // Stick: a louder first millisecond, and nothing left of it after 10 ms.
    auto stick = p;
    stick[MetalP::Stick] = 100.0f;
    const auto withStick = renderVoiceDirect (OpenHat, stick, sr, 0.1);
    CHECK (energyIn (withStick, 0, 48) > 1.5 * energyIn (base, 0, 48));
    float lateDiff = 0.0f;
    for (size_t i = 480; i < base.size(); ++i) lateDiff = std::max (lateDiff, std::abs (withStick[i] - base[i]));
    CHECK (lateDiff < 1.0e-6f);
}

FP_TEST (tone_envelope_model_matches_rendered_output)
{
    // The editor draws ToneVoice::ampEnvelope; it must describe what is heard.
    const double sr = 48000.0;
    auto p = defaultParams (Tom);
    p[ToneP::Noise] = 0.0f;
    p[ToneP::Bend] = 0.0f;
    p[ToneP::Tune] = 100.0f; // 10 ms period
    p[ToneP::Attack] = 10.0f;
    p[ToneP::Hold] = 30.0f;
    p[ToneP::Decay] = 1500.0f;
    const auto out = renderVoiceDirect (Tom, p, sr, 1.2);

    bool bounded = true;
    for (size_t i = 0; i < out.size(); ++i)
        bounded = bounded && std::abs (out[i]) <= ToneVoice::ampEnvelope (p.data(), (float) ((double) i / sr)) + 1.0e-3f;
    CHECK (bounded);

    for (float t : { 0.03f, 0.2f, 0.5f, 1.0f })
    {
        const auto centre = (size_t) (t * sr);
        const float measured = peakIn (out, centre - 240, centre + 240); // one period
        const float model = ToneVoice::ampEnvelope (p.data(), t);
        CHECK_NEAR (measured, model, 0.05f * model);
    }

    CHECK_NEAR (ToneVoice::pitchSemitones (p.data(), 0.0f), 0.0f, 1.0e-4f);
    p[ToneP::Bend] = 12.0f;
    CHECK_NEAR (ToneVoice::pitchSemitones (p.data(), 0.0f), 12.0f, 1.0e-3f);
    CHECK_NEAR (ToneVoice::noiseEnvelope (p.data(), 0.03f), 0.001f, 1.0e-4f); // -60 dB at Noise Len
}

FP_TEST (tone_noise_tone_and_length)
{
    // The noise layer alone: the same hit with and without noise.
    const double sr = 48000.0;
    auto noiseOnly = [&] (float tone, float length)
    {
        auto p = defaultParams (Perc);
        p[ToneP::NoiseTone] = tone;
        p[ToneP::NoiseLength] = length;
        p[ToneP::Noise] = 0.0f;
        const auto without = renderVoiceDirect (Perc, p, sr, 0.1);
        p[ToneP::Noise] = 100.0f;
        auto with = renderVoiceDirect (Perc, p, sr, 0.1);
        for (size_t i = 0; i < with.size(); ++i) with[i] -= without[i];
        return with;
    };

    CHECK (energyIn (noiseOnly (1500.0f, 200.0f), 1440, 4800) > 100.0 * energyIn (noiseOnly (1500.0f, 10.0f), 1440, 4800)); // 30-100 ms
    // Noise Tone is the high-pass cutoff: a higher one removes more of the noise.
    CHECK (energyIn (noiseOnly (10000.0f, 30.0f), 0, 1440) < 0.3 * energyIn (noiseOnly (300.0f, 30.0f), 0, 1440));
}

FP_TEST (tone_metal_tune_and_shape)
{
    const double sr = 48000.0;
    auto p = defaultParams (Perc); // Metal 25
    const auto base = renderVoiceDirect (Perc, p, sr, 0.1);
    auto up = p;
    up[ToneP::MetalTune] = 7.0f;
    CHECK (maxDifference (renderVoiceDirect (Perc, up, sr, 0.1), base) > 0.05f);

    // Without Metal there is no modulator to tune.
    p[ToneP::Metal] = 0.0f;
    const auto plain = renderVoiceDirect (Perc, p, sr, 0.1);
    p[ToneP::MetalTune] = 7.0f;
    CHECK (maxDifference (renderVoiceDirect (Perc, p, sr, 0.1), plain) == 0.0f);

    // Shape: from a sine towards a square, at the same peak level.
    auto t = defaultParams (Tom);
    t[ToneP::Noise] = 0.0f;
    t[ToneP::Bend] = 0.0f;
    t[ToneP::Hold] = 60.0f; // 60 ms at full level
    auto crest = [&] (float shape)
    {
        t[ToneP::Shape] = shape;
        const auto out = renderVoiceDirect (Tom, t, sr, 0.06);
        const double rms = std::sqrt (energyIn (out, 0, out.size()) / (double) out.size());
        return std::make_pair ((float) rms / peakIn (out, 0, out.size()), peakIn (out, 0, out.size()));
    };
    const auto [clean, cleanPeak] = crest (0.0f);
    const auto [shaped, shapedPeak] = crest (100.0f);
    CHECK (clean < 0.75f);
    CHECK (shaped > 0.85f);
    CHECK (shapedPeak < 1.001f);
    CHECK (cleanPeak > 0.99f);
}

FP_TEST (buffer_size_edge_cases)
{
    for (int block : { 1, 2, 3, 17, 64, 1000, 4096 })
    {
        auto engine = makeEngine (44100.0, block);
        auto r = renderHit (engine, Clap, 44100.0, 0.2, block);
        CHECK (r.finite());
        CHECK (r.peak() > 0.05f);
    }

    // Zero-length blocks and blocks larger than announced must be safe.
    auto engine = makeEngine (48000.0, 128);
    std::vector<float> L (2048), R (2048);
    engine.process (nullptr, 0, L.data(), R.data(), nullptr, nullptr, 0);
    TriggerEvent ev; ev.voice = Kick; ev.offset = 1500;
    engine.process (&ev, 1, L.data(), R.data(), nullptr, nullptr, 2048);
    float peakBefore = 0.0f, peakAfter = 0.0f;
    for (int i = 0; i < 1500; ++i) peakBefore = std::max (peakBefore, std::abs (L[(size_t) i]));
    for (int i = 1500; i < 2048; ++i) peakAfter = std::max (peakAfter, std::abs (L[(size_t) i]));
    CHECK (peakBefore == 0.0f);
    CHECK (peakAfter > 0.05f);
}

FP_TEST (sample_rate_change_preserves_decay_time)
{
    // The kick's -60 dB point should be at the same time regardless of rate.
    auto measure = [] (double sr)
    {
        auto engine = makeEngine (sr, 512);
        auto r = renderHit (engine, Kick, sr, 1.5, 512);
        const float peak = r.peak();
        size_t last = 0;
        for (size_t i = 0; i < r.L.size(); ++i)
            if (std::abs (r.L[i]) > peak * 0.01f) last = i;
        return (double) last / sr;
    };
    const double t44 = measure (44100.0), t48 = measure (48000.0), t96 = measure (96000.0), t882 = measure (88200.0);
    CHECK_NEAR (t44, t96, 0.01);
    CHECK_NEAR (t48, t882, 0.01);
}

FP_TEST (aux_routing)
{
    auto engine = makeEngine (48000.0, 256);
    engine.voiceParams (Snare).output = 2;
    std::vector<float> L (256), R (256);
    std::vector<std::vector<float>> aux (8, std::vector<float> (256));
    float* auxL[8]; float* auxR[8];
    for (int b = 0; b < 8; ++b) { auxL[b] = aux[(size_t) b].data(); auxR[b] = aux[(size_t) b].data(); }
    // Separate buffers for R to avoid aliasing L and R of the same bus.
    std::vector<std::vector<float>> auxRight (8, std::vector<float> (256));
    for (int b = 0; b < 8; ++b) auxR[b] = auxRight[(size_t) b].data();

    TriggerEvent ev; ev.voice = Snare;
    engine.process (&ev, 1, L.data(), R.data(), auxL, auxR, 256);
    float mainPeak = 0.0f, auxPeak = 0.0f;
    for (int i = 0; i < 256; ++i) { mainPeak = std::max (mainPeak, std::abs (L[(size_t) i])); auxPeak = std::max (auxPeak, std::abs (aux[1][(size_t) i])); }
    CHECK (mainPeak == 0.0f);
    CHECK (auxPeak > 0.01f);

    // Unavailable bus falls back to the main mix.
    auto engine2 = makeEngine (48000.0, 256);
    engine2.voiceParams (Snare).output = 2;
    engine2.process (&ev, 1, L.data(), R.data(), nullptr, nullptr, 256);
    mainPeak = 0.0f;
    for (int i = 0; i < 256; ++i) mainPeak = std::max (mainPeak, std::abs (L[(size_t) i]));
    CHECK (mainPeak > 0.01f);
}

FP_TEST (random_fuzz_never_produces_nan)
{
    uint32_t s = 12345;
    auto rnd = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) / 16777216.0f; };
    auto engine = makeEngine (48000.0, 128);
    engine.masterParams().comp = 0.7f;
    engine.masterParams().clip = 0.5f;
    std::vector<float> L (128), R (128);

    for (int block = 0; block < 4000; ++block)
    {
        if (block % 50 == 0)
            for (int v = 0; v < kNumVoices; ++v)
            {
                auto& vp = engine.voiceParams (v);
                for (int i = 0; i < numVoiceParams (v); ++i)
                {
                    const auto& spec = voiceParamSpec (v, i);
                    vp.synth[(size_t) i] = spec.min + rnd() * (spec.max - spec.min);
                }
                vp.filter = rnd() * 2.0f - 1.0f;
                vp.drive = rnd();
                vp.pan = rnd() * 2.0f - 1.0f;
            }

        TriggerEvent evs[4];
        int n = 0;
        while (n < 4 && rnd() < 0.3f)
        {
            evs[n].voice = (uint8_t) (rnd() * 7.99f);
            evs[n].offset = (int) (rnd() * 127.0f);
            evs[n].velocity = rnd();
            evs[n].tune = (int8_t) (rnd() * 48.0f - 24.0f);
            evs[n].decay = (int8_t) (rnd() * 200.0f - 100.0f);
            ++n;
        }
        std::sort (evs, evs + n, [] (auto& a, auto& b) { return a.offset < b.offset; });
        engine.process (evs, n, L.data(), R.data(), nullptr, nullptr, 128);
        for (int i = 0; i < 128; ++i)
            CHECK (std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]) && std::abs (L[(size_t) i]) <= 1.01f);
    }
}

FP_TEST (sample_voice_plays_and_slots_hand_off)
{
    SampleSlots slots;
    auto data = std::make_unique<SampleData>();
    data->sampleRate = 48000.0;
    data->left.assign (4800, 0.5f);
    slots.publish (Kick, std::move (data));

    auto engine = makeEngine (48000.0, 256);
    engine.setSample (Kick, slots.acquire (Kick));
    CHECK (engine.voiceUsesSample (Kick));
    auto r = renderHit (engine, Kick, 48000.0, 0.2);
    CHECK (r.peak() > 0.2f);
    float tail = 0.0f;
    for (size_t i = 5000; i < r.L.size(); ++i) tail = std::max (tail, std::abs (r.L[i]));
    CHECK (tail < 1.0e-3f);

    // Clearing hands the old sample to the retired slot for deletion.
    slots.publish (Kick, std::make_unique<SampleData>());
    engine.setSample (Kick, slots.acquire (Kick));
    CHECK (! engine.voiceUsesSample (Kick));
    slots.collectGarbage();
}

// ---------------------------------------------------------------------------
// Sequencer
// ---------------------------------------------------------------------------
FP_TEST (sequencer_step_timing_is_exact)
{
    // 120 BPM, 48 kHz, 1/16 => 6000 samples per step.
    auto bank = singleStepBank (Kick, 1);
    SequencerSettings s;
    auto run = runSequencer (bank, s, 48000.0, 120.0, 512, 6000 * 32);
    CHECK (run.events.size() == 32);
    for (size_t i = 0; i < run.events.size(); ++i)
        CHECK (run.events[i].first == (int64_t) i * 6000);
}

FP_TEST (sequencer_is_block_size_invariant)
{
    auto bank = PatternBank {};
    bank.patterns[0] = makeDefaultHousePattern();
    for (auto& t : bank.patterns[0].tracks) for (auto& st : t.steps) st.probability = 100;
    SequencerSettings s;
    s.swing = 62.0f;
    const auto reference = runSequencer (bank, s, 44100.0, 124.0, 64, 44100 * 8).events;
    CHECK (! reference.empty());
    for (int block : { 1, 7, 100, 333, 1024, 4096 })
    {
        const auto other = runSequencer (bank, s, 44100.0, 124.0, block, 44100 * 8).events;
        CHECK (other.size() == reference.size());
        bool same = other.size() == reference.size();
        for (size_t i = 0; same && i < other.size(); ++i)
        {
            // Allow 1 sample of rounding difference between block layouts.
            same = std::abs (other[i].first - reference[i].first) <= 1 && other[i].second == reference[i].second;
        }
        CHECK (same);
    }
}

FP_TEST (swing_delays_odd_steps)
{
    auto bank = singleStepBank (ClosedHat, 1);
    SequencerSettings s;
    s.swing = 75.0f; // odd steps delayed by half a step
    auto run = runSequencer (bank, s, 48000.0, 120.0, 256, 6000 * 4);
    CHECK (run.events.size() == 4);
    if (run.events.size() == 4)
    {
        CHECK (run.events[0].first == 0);
        CHECK_NEAR (run.events[1].first, 6000 + 3000, 1);
        CHECK (run.events[2].first == 12000);
        CHECK_NEAR (run.events[3].first, 18000 + 3000, 1);
    }
}

FP_TEST (microtiming_shifts_steps)
{
    PatternBank bank;
    auto& st = bank.patterns[0].tracks[Snare].steps;
    st[4].on = 1; st[4].micro = 25;   // +1/4 step late
    st[8].on = 1; st[8].micro = -50;  // half a step early
    auto run = runSequencer (bank, SequencerSettings {}, 48000.0, 120.0, 128, 6000 * 16);
    CHECK (run.events.size() == 2);
    if (run.events.size() == 2)
    {
        CHECK_NEAR (run.events[0].first, 4 * 6000 + 1500, 1);
        CHECK_NEAR (run.events[1].first, 8 * 6000 - 3000, 1);
    }
}

FP_TEST (probability_is_statistical_and_deterministic)
{
    PatternBank bank;
    auto& p = bank.patterns[0];
    for (int i = 0; i < 16; ++i) { p.tracks[0].steps[(size_t) i].on = 1; p.tracks[0].steps[(size_t) i].probability = 50; }
    p.tracks[1].steps[0].on = 1; p.tracks[1].steps[0].probability = 0;
    SequencerSettings s;
    const int64_t total = 6000LL * 16 * 64; // 64 bars
    auto a = runSequencer (bank, s, 48000.0, 120.0, 512, total);
    auto b = runSequencer (bank, s, 48000.0, 120.0, 1024, total);
    const double ratio = (double) a.events.size() / (16.0 * 64.0);
    CHECK (ratio > 0.42 && ratio < 0.58);
    CHECK (a.events == b.events);
    for (auto& e : a.events) CHECK (e.second == 0); // probability 0 never fires

    s.seed = 99;
    auto c = runSequencer (bank, s, 48000.0, 120.0, 512, total);
    CHECK (c.events != a.events);
}

FP_TEST (ratchet_and_flam_add_hits)
{
    PatternBank bank;
    auto& st = bank.patterns[0].tracks[ClosedHat].steps;
    st[0].on = 1; st[0].ratchet = 4;
    st[4].on = 1; st[4].setFlag (Step::Flam, true);
    auto run = runSequencer (bank, SequencerSettings {}, 48000.0, 120.0, 256, 6000 * 16);
    CHECK (run.events.size() == 6);
    if (run.events.size() == 6)
    {
        CHECK (run.events[1].first == 1500);
        CHECK (run.events[3].first == 4500);
        CHECK (run.events[4].first == 24000);
        CHECK (run.events[5].first > 24000);
    }
}

FP_TEST (transport_loop_does_not_duplicate_or_drop)
{
    // Host loops a single bar (4 beats) three times with jumps back to 0.
    auto bank = singleStepBank (Kick, 1);
    Sequencer seq;
    seq.prepare (48000.0);
    TriggerEvent events[Sequencer::kMaxEventsPerBlock];
    const double ppqPerSample = 120.0 / (60.0 * 48000.0);
    const int block = 500;
    const int64_t barSamples = 96000;
    std::vector<int64_t> hits;
    int64_t timeline = 0;
    for (int loop = 0; loop < 3; ++loop)
    {
        for (int64_t pos = 0; pos < barSamples; pos += block)
        {
            const int n = (int) std::min<int64_t> (block, barSamples - pos);
            TransportState t { true, (double) pos * ppqPerSample, 120.0 };
            const int c = seq.process (bank, t, SequencerSettings {}, n, events, Sequencer::kMaxEventsPerBlock);
            for (int i = 0; i < c; ++i) hits.push_back (timeline + pos + events[i].offset);
        }
        timeline += barSamples;
    }
    CHECK (hits.size() == 48);
    for (size_t i = 0; i < hits.size(); ++i) CHECK (hits[i] == (int64_t) i * 6000);
}

FP_TEST (start_mid_pattern_and_stop)
{
    auto bank = singleStepBank (Kick, 1);
    Sequencer seq;
    seq.prepare (48000.0);
    TriggerEvent ev[64];
    // Start exactly on beat 2 (step 4): the step must fire at sample 0.
    TransportState t { true, 1.0, 120.0 };
    int c = seq.process (bank, t, SequencerSettings {}, 256, ev, 64);
    CHECK (c == 1 && ev[0].offset == 0);
    CHECK (seq.currentStep() == 4);

    // Start slightly after a step: nothing until the next one.
    seq.reset();
    t.ppq = 1.01;
    c = seq.process (bank, t, SequencerSettings {}, 256, ev, 64);
    CHECK (c == 0);

    // Stopping produces nothing and resets the display.
    t.playing = false;
    c = seq.process (bank, t, SequencerSettings {}, 256, ev, 64);
    CHECK (c == 0 && seq.currentStep() == -1);
}

FP_TEST (tempo_change_follows_host)
{
    auto bank = singleStepBank (Kick, 4); // quarter notes
    Sequencer seq;
    seq.prepare (48000.0);
    TriggerEvent ev[64];
    std::vector<int64_t> hits;
    double ppq = 0.0;
    int64_t pos = 0;
    for (int b = 0; b < 1000; ++b)
    {
        const double bpm = pos < 96000 ? 120.0 : 150.0; // tempo jump after 2 s
        TransportState t { true, ppq, bpm };
        const int c = seq.process (bank, t, SequencerSettings {}, 256, ev, 64);
        for (int i = 0; i < c; ++i) hits.push_back (pos + ev[i].offset);
        ppq += 256 * bpm / (60.0 * 48000.0);
        pos += 256;
    }
    // Beats at 120 BPM are 24000 samples apart, at 150 BPM 19200.
    CHECK (hits.size() > 6);
    CHECK (hits[1] - hits[0] == 24000);
    const size_t n = hits.size();
    CHECK_NEAR ((double) (hits[n - 1] - hits[n - 2]), 19200.0, 1.0);
}

FP_TEST (pattern_switch_waits_for_cycle_end)
{
    PatternBank bank;
    bank.patterns[0].tracks[Kick].steps[0].on = 1;
    bank.patterns[1].tracks[Snare].steps[0].on = 1;
    Sequencer seq;
    seq.prepare (48000.0);
    SequencerSettings s;
    TriggerEvent ev[64];
    std::vector<std::pair<int64_t, int>> hits;
    const double pps = 120.0 / (60.0 * 48000.0);
    for (int64_t pos = 0; pos < 96000 * 3; pos += 480)
    {
        if (pos == 48000) s.pattern = 1; // request mid-bar
        TransportState t { true, (double) pos * pps, 120.0 };
        const int c = seq.process (bank, t, s, 480, ev, 64);
        for (int i = 0; i < c; ++i) hits.push_back ({ pos + ev[i].offset, ev[i].voice });
    }
    CHECK (hits.size() == 3);
    if (hits.size() == 3)
    {
        CHECK (hits[0].second == Kick && hits[0].first == 0);
        CHECK (hits[1].second == Snare && hits[1].first == 96000);
        CHECK (hits[2].second == Snare);
    }
}

FP_TEST (pattern_chain_advances)
{
    PatternBank bank;
    for (int p = 0; p < 3; ++p) bank.patterns[(size_t) p].tracks[(size_t) p].steps[0].on = 1;
    SequencerSettings s;
    s.chainEnabled = true;
    s.chain = { 0, 2, 1, -1, -1, -1, -1, -1 };
    auto run = runSequencer (bank, s, 48000.0, 120.0, 512, 96000 * 4);
    CHECK (run.events.size() == 4);
    if (run.events.size() == 4)
    {
        CHECK (run.events[0].second == 0);
        CHECK (run.events[1].second == 2);
        CHECK (run.events[2].second == 1);
        CHECK (run.events[3].second == 0);
    }
}

FP_TEST (euclidean_patterns)
{
    int hits = 0;
    std::string s;
    for (int i = 0; i < 8; ++i) { const bool h = euclideanHit (i, 8, 3, 0); hits += h; s += h ? 'x' : '.'; }
    CHECK (hits == 3);
    CHECK (s == "x..x..x.");
    CHECK (! euclideanHit (0, 16, 0, 0));
    int all = 0;
    for (int i = 0; i < 16; ++i) all += euclideanHit (i, 16, 16, 0);
    CHECK (all == 16);

    Pattern p;
    p.length = 16;
    p.euclidean (Kick, 5, 2);
    int count = 0;
    for (int i = 0; i < 16; ++i) count += p.tracks[Kick].steps[(size_t) i].on;
    CHECK (count == 5);
}

FP_TEST (pattern_operations)
{
    Pattern p = makeDefaultHousePattern();
    CHECK (! p.isEmpty());
    p.rotateTrack (Kick, 1);
    CHECK (p.tracks[Kick].steps[1].on && ! p.tracks[Kick].steps[0].on);
    p.rotateTrack (Kick, -1);
    CHECK (p.tracks[Kick].steps[0].on);

    p.doubleLength();
    CHECK (p.length == 32 && p.tracks[Kick].steps[16].on);

    Pattern r;
    r.randomiseTrack (Kick, 0.0f, 7);
    // At zero intensity the kick keeps its four-on-the-floor backbone.
    for (int i = 0; i < 16; i += 4) CHECK (r.tracks[Kick].steps[(size_t) i].on);
    Pattern r2;
    r2.randomiseTrack (Kick, 0.0f, 7);
    CHECK (std::memcmp (&r, &r2, sizeof (Pattern)) == 0);

    Step bad; bad.velocity = 0; bad.probability = 200; bad.micro = 99; bad.ratchet = 9;
    bad.sanitise();
    CHECK (bad.velocity == 1 && bad.probability == 100 && bad.micro == 50 && bad.ratchet == 4);
}

FP_TEST (triple_buffer_is_consistent_under_concurrency)
{
    struct Block { int64_t a[64]; };
    Block init {};
    TripleBuffer<Block> tb (init);
    std::atomic<bool> done { false };
    std::atomic<int> torn { 0 };

    std::thread reader ([&]
    {
        int64_t lastSeen = 0;
        while (! done.load())
        {
            const Block& b = tb.acquire();
            for (int i = 1; i < 64; ++i)
                if (b.a[i] != b.a[0]) { torn++; break; }
            if (b.a[0] < lastSeen) torn++; // must never go backwards
            lastSeen = b.a[0];
        }
    });

    for (int64_t v = 1; v <= 200000; ++v)
    {
        auto& back = tb.backBuffer();
        for (auto& x : back.a) x = v;
        tb.publish();
    }
    done = true;
    reader.join();
    CHECK (torn.load() == 0);
    CHECK (tb.acquire().a[0] == 200000);
}

int main (int argc, char** argv)
{
    return fptest::runMain (argc, argv);
}
