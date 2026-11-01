#include "Audio/DrumEngine.h"

#include <cmath>

namespace fp
{

// --------------------------------------------------------------------------
// SampleSlots
// --------------------------------------------------------------------------
SampleSlots::~SampleSlots()
{
    for (int v = 0; v < kNumVoices; ++v)
    {
        delete pending[(size_t) v].exchange (nullptr);
        delete retired[(size_t) v].exchange (nullptr);
        delete active[(size_t) v];
    }
}

void SampleSlots::publish (int voice, std::unique_ptr<SampleData> data)
{
    // Whoever wins the exchange owns the previous pending object: if the audio
    // thread had not picked it up yet, it is ours to delete.
    delete pending[(size_t) voice].exchange (data.release());
}

void SampleSlots::collectGarbage()
{
    for (auto& r : retired)
        delete r.exchange (nullptr);
}

const SampleData* SampleSlots::acquire (int voice) noexcept
{
    auto& p = pending[(size_t) voice];
    auto& r = retired[(size_t) voice];

    // Only swap when the retired slot is free, so it never holds two objects.
    if (p.load (std::memory_order_acquire) != nullptr && r.load (std::memory_order_acquire) == nullptr)
    {
        if (auto* incoming = p.exchange (nullptr, std::memory_order_acq_rel))
        {
            r.store (active[(size_t) voice], std::memory_order_release);
            active[(size_t) voice] = incoming;
        }
    }

    auto* s = active[(size_t) voice];
    return (s != nullptr && ! s->empty()) ? s : nullptr;
}

// --------------------------------------------------------------------------
// DrumEngine
// --------------------------------------------------------------------------
DrumEngine::DrumEngine()
{
    for (int v = 0; v < kNumVoices; ++v)
    {
        synths[(size_t) v] = createSynthVoice (v);

        const auto& spec = kVoiceSpecs[(size_t) v];
        auto& vp = voiceSettings[(size_t) v];
        for (int i = 0; i < numVoiceParams (v); ++i)
            vp.synth[(size_t) i] = voiceParamSpec (v, i).def;
        vp.levelDb = spec.defaultLevelDb;
        vp.pan = spec.defaultPan;
    }

    voiceSettings[OpenHat].chokedBy = ClosedHat; // classic hi-hat choke group (the parameter's default)
}

void DrumEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maxBlockSize);
    scratchL.assign ((size_t) maxBlock, 0.0f);
    scratchR.assign ((size_t) maxBlock, 0.0f);
    dsp::SineTable::warmUp();

    for (auto& s : synths) s->prepare (sampleRate);
    for (auto& s : samplers) s.prepare (sampleRate);
    character.prepare (sampleRate);
    compressor.prepare (sampleRate);
    reset();
}

void DrumEngine::reset()
{
    for (auto& s : synths) s->reset();
    for (auto& s : samplers) s.reset();
    for (auto& st : strips)
    {
        st.filterL.reset();
        st.filterR.reset();
        st.cachedRate = 0.0;
        st.gainL.initialised = st.gainR.initialised = false;
    }
    character.reset();
    compressor.reset();
    masterGain.initialised = false;
    nextFreeHitId = 0;
    voicePeaks.fill (0.0f);
    peakL = peakR = 0.0f;
}

void DrumEngine::chokeAll() noexcept
{
    for (auto& s : synths) s->choke();
    for (auto& s : samplers) s.choke();
}

float DrumEngine::velocityToGain (float velocity, bool accent, float accentAmount) noexcept
{
    velocity = std::clamp (velocity, 0.0f, 1.0f);
    float g = 0.1f + 0.9f * std::pow (velocity, 1.6f);
    if (accent)
        g *= 1.0f + accentAmount * 0.8f;
    return g;
}

namespace
{
inline uint64_t mix64 (uint64_t x) noexcept
{
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}
} // namespace

void DrumEngine::applyCharacter (TriggerInfo& info, float velocity, int voice, uint32_t hitId, const MasterParams& m) noexcept
{
    if (! m.character)
        return;

    // Softer hits are duller as well as quieter, as with a struck drum.
    float tone = 1.0f - std::clamp (m.velTone, 0.0f, 1.0f) * (1.0f - std::clamp (velocity, 0.0f, 1.0f));

    const float vary = std::clamp (m.vary, 0.0f, 1.0f);
    if (vary > 0.0f)
    {
        // Four independent values in [-1, 1] from one hash of (seed, hit, voice).
        const uint64_t h = mix64 (((uint64_t) m.seed << 32) ^ hitId ^ ((uint64_t) (voice + 1) * 0x9E3779B97F4A7C15ull));
        auto r = [h] (int i) { return (float) ((h >> (16 * i)) & 0xFFFF) * (2.0f / 65535.0f) - 1.0f; };
        info.tuneSemis += vary * kVaryTuneSemis * r (0);
        info.decayMul  *= 1.0f + vary * kVaryDecay * r (1);
        info.gain      *= dsp::dbToGain (vary * kVaryLevelDb * r (2));
        tone           += vary * kVaryTone * r (3);
    }

    info.tone = std::clamp (tone, 0.0f, 1.0f + kVaryTone);
}

void DrumEngine::renderVoice (int v, const TriggerEvent* events, int numEvents, int numSamples)
{
    const bool useSample = samplers[(size_t) v].hasSample();
    DrumVoice& voice = useSample ? static_cast<DrumVoice&> (samplers[(size_t) v]) : *synths[(size_t) v];
    const auto& vp = voiceSettings[(size_t) v];
    const float sampleParams[2] = { vp.sampleTune, vp.sampleDecay };
    const float* params = useSample ? sampleParams : vp.synth.data();
    const int chokeSource = vp.chokedBy != v ? vp.chokedBy : -1;

    float* L = scratchL.data();
    float* R = scratchR.data();
    int pos = 0;

    for (int e = 0; e < numEvents; ++e)
    {
        const auto& ev = events[e];
        const bool isTrigger = ev.voice == v;
        const bool isChoke = chokeSource >= 0 && ev.voice == chokeSource;
        if (! isTrigger && ! isChoke)
            continue;

        const int at = std::clamp (ev.offset, pos, numSamples);
        voice.render (L + pos, R + pos, at - pos);
        pos = at;

        if (isTrigger)
        {
            TriggerInfo info;
            info.gain = velocityToGain (ev.velocity, ev.accent, master.accent);
            info.tuneSemis = (float) ev.tune;
            info.decayMul = std::exp2 ((float) ev.decay / 50.0f);
            if (master.character)
                applyCharacter (info, ev.velocity, v, ev.hitId != 0 ? ev.hitId : (++nextFreeHitId * 2u + 1u) * 0x2545F491u, master);
            voice.trigger (params, info);
        }
        else
        {
            voice.choke();
        }
    }

    voice.render (L + pos, R + pos, numSamples - pos);
}

void DrumEngine::process (const TriggerEvent* events, int numEvents,
                          float* mainL, float* mainR,
                          float* const* auxL, float* const* auxR,
                          int numSamples)
{
    if (numSamples <= 0)
        return;

    // Hosts may send blocks larger than announced; process them in chunks.
    if (numSamples > maxBlock)
    {
        int done = 0, e0 = 0;
        float* subAuxL[kNumAuxBuses] {};
        float* subAuxR[kNumAuxBuses] {};
        float masterPeakL = 0.0f, masterPeakR = 0.0f;
        std::array<float, kNumVoices> peaks {};

        while (done < numSamples)
        {
            const int n = std::min (maxBlock, numSamples - done);
            int e1 = e0;
            while (e1 < numEvents && events[e1].offset < done + n)
                ++e1;

            TriggerEvent local[256];
            int count = 0;
            for (int e = e0; e < e1 && count < 256; ++e)
            {
                local[count] = events[e];
                local[count++].offset -= done;
            }

            for (int b = 0; b < kNumAuxBuses; ++b)
            {
                subAuxL[b] = (auxL != nullptr && auxL[b] != nullptr) ? auxL[b] + done : nullptr;
                subAuxR[b] = (auxR != nullptr && auxR[b] != nullptr) ? auxR[b] + done : nullptr;
            }

            process (local, count, mainL + done, mainR + done, subAuxL, subAuxR, n);
            masterPeakL = std::max (masterPeakL, peakL);
            masterPeakR = std::max (masterPeakR, peakR);
            for (int v = 0; v < kNumVoices; ++v)
                peaks[(size_t) v] = std::max (peaks[(size_t) v], voicePeaks[(size_t) v]);

            done += n;
            e0 = e1;
        }

        peakL = masterPeakL;
        peakR = masterPeakR;
        voicePeaks = peaks;
        return;
    }

    std::fill (mainL, mainL + numSamples, 0.0f);
    std::fill (mainR, mainR + numSamples, 0.0f);
    for (int b = 0; b < kNumAuxBuses; ++b)
    {
        if (auxL != nullptr && auxL[b] != nullptr) std::fill (auxL[b], auxL[b] + numSamples, 0.0f);
        if (auxR != nullptr && auxR[b] != nullptr) std::fill (auxR[b], auxR[b] + numSamples, 0.0f);
    }

    bool anySolo = false;
    for (const auto& vp : voiceSettings)
        anySolo = anySolo || vp.solo;

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& vp = voiceSettings[(size_t) v];
        auto& strip = strips[(size_t) v];

        // Strip gains: level, balance pan, mute and solo.
        const bool audible = ! vp.mute && (! anySolo || vp.solo);
        const float level = audible ? dsp::dbToGain (vp.levelDb) : 0.0f;
        const float pan = std::clamp (vp.pan, -1.0f, 1.0f);
        const float targetL = level * (pan > 0.0f ? std::cos (pan * dsp::kPi * 0.5f) : 1.0f);
        const float targetR = level * (pan < 0.0f ? std::cos (-pan * dsp::kPi * 0.5f) : 1.0f);

        bool hasEvents = false;
        for (int e = 0; e < numEvents && ! hasEvents; ++e)
            hasEvents = events[e].voice == v || (vp.chokedBy >= 0 && vp.chokedBy != v && events[e].voice == vp.chokedBy);

        DrumVoice& current = samplers[(size_t) v].hasSample() ? static_cast<DrumVoice&> (samplers[(size_t) v])
                                                              : *synths[(size_t) v];
        if (! hasEvents && ! current.isActive())
        {
            // Idle voice: skip all DSP, just keep the gain ramps settled.
            strip.gainL.current = targetL;
            strip.gainR.current = targetR;
            strip.gainL.initialised = strip.gainR.initialised = true;
            voicePeaks[(size_t) v] = 0.0f;
            continue;
        }

        renderVoice (v, events, numEvents, numSamples);

        float* L = scratchL.data();
        float* R = scratchR.data();

        if (vp.drive > 0.001f)
        {
            const float pre = 1.0f + vp.drive * 15.0f;
            const float post = 1.0f / (1.0f + vp.drive * 2.0f);
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] = dsp::softClip (L[i] * pre) * post;
                R[i] = dsp::softClip (R[i] * pre) * post;
            }
        }

        const float f = std::clamp (vp.filter, -1.0f, 1.0f);
        if (std::abs (f) > 0.01f)
        {
            if (f != strip.cachedFilter || strip.cachedRate != sampleRate)
            {
                const float cutoff = f < 0.0f ? 20000.0f * std::pow (80.0f / 20000.0f, -f)
                                              : 20.0f * std::pow (8000.0f / 20.0f, f);
                strip.filterL.setup (cutoff, 0.75f, sampleRate);
                strip.filterR.setup (cutoff, 0.75f, sampleRate);
                strip.cachedFilter = f;
                strip.cachedRate = sampleRate;
            }

            if (f < 0.0f)
                for (int i = 0; i < numSamples; ++i) { L[i] = strip.filterL.lowpass (L[i]);  R[i] = strip.filterR.lowpass (R[i]); }
            else
                for (int i = 0; i < numSamples; ++i) { L[i] = strip.filterL.highpass (L[i]); R[i] = strip.filterR.highpass (R[i]); }
        }

        const float incL = strip.gainL.begin (targetL, numSamples);
        const float incR = strip.gainR.begin (targetR, numSamples);

        const int bus = vp.output;
        float* dstL = mainL;
        float* dstR = mainR;
        if (bus > 0 && bus <= kNumAuxBuses && auxL != nullptr && auxR != nullptr
            && auxL[bus - 1] != nullptr && auxR[bus - 1] != nullptr)
        {
            dstL = auxL[bus - 1];
            dstR = auxR[bus - 1];
        }

        float gL = strip.gainL.current, gR = strip.gainR.current, peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            gL += incL;
            gR += incR;
            const float l = L[i] * gL;
            const float r = R[i] * gR;
            dstL[i] += l;
            dstR[i] += r;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }
        strip.gainL.current = targetL;
        strip.gainR.current = targetR;
        voicePeaks[(size_t) v] = peak;
    }

    // Master bus: volume ramp -> optional Character -> optional compressor -> optional soft clip.
    const float mg = dsp::dbToGain (master.volumeDb);
    const float inc = masterGain.begin (mg, numSamples);
    float g = masterGain.current;
    for (int i = 0; i < numSamples; ++i)
    {
        g += inc;
        mainL[i] *= g;
        mainR[i] *= g;
    }
    masterGain.current = mg;

    character.process (mainL, mainR, numSamples, master.character,
                       std::clamp (master.color, 0.0f, 1.0f), std::clamp (master.punch, -1.0f, 1.0f));

    if (master.comp > 0.001f)
        compressor.process (mainL, mainR, numSamples, master.comp);

    if (master.clip > 0.001f)
    {
        const float pre = dsp::dbToGain (master.clip * 12.0f);
        const float post = dsp::dbToGain (-master.clip * 6.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            mainL[i] = dsp::softClip (mainL[i] * pre) * post;
            mainR[i] = dsp::softClip (mainR[i] * pre) * post;
        }
    }

    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        pl = std::max (pl, std::abs (mainL[i]));
        pr = std::max (pr, std::abs (mainR[i]));
    }
    peakL = pl;
    peakR = pr;
}

} // namespace fp
