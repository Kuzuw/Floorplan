// Tests for the JUCE-free core: DSP blocks, voices, engine and sequencer.

#include "TestFramework.h"

#include "Audio/DrumEngine.h"
#include "Audio/TripleBuffer.h"
#include "DSP/DSPCore.h"
#include "DSP/SineTable.h"
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
    for (const auto& v : kVoiceSpecs)
    {
        CHECK (ids.insert (v.id).second);
        for (const auto& p : v.params)
        {
            CHECK (p.min < p.max);
            CHECK (p.def >= p.min && p.def <= p.max);
            CHECK (p.centre == 0.0f || (p.centre > p.min && p.centre < p.max));
            CHECK (ids.insert (std::string (v.id) + "_" + p.id).second);
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
                for (int i = 0; i < kNumSynthParams; ++i)
                {
                    const auto& spec = kVoiceSpecs[(size_t) v].params[(size_t) i];
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
                for (int i = 0; i < kNumSynthParams; ++i)
                {
                    const auto& spec = kVoiceSpecs[(size_t) v].params[(size_t) i];
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
