#include "Instruments/ClapVoice.h"
#include "Instruments/KickVoice.h"
#include "Instruments/MetalVoice.h"
#include "Instruments/SnareVoice.h"
#include "Instruments/ToneVoice.h"

namespace fp
{

std::unique_ptr<DrumVoice> createSynthVoice (int voiceIndex)
{
    switch (voiceIndex)
    {
        case Kick:      return std::make_unique<KickVoice>();
        case Snare:     return std::make_unique<SnareVoice>();
        case Clap:      return std::make_unique<ClapVoice>();
        case ClosedHat:
        case OpenHat:   return std::make_unique<MetalVoice> (std::array<float, 6> { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f });
        case Cymbal:    return std::make_unique<MetalVoice> (std::array<float, 6> { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f });
        case Perc:      return std::make_unique<ToneVoice> (1.41f);
        case Tom:       return std::make_unique<ToneVoice> (1.5f);
        default:        break;
    }

    return std::make_unique<KickVoice>();
}

namespace
{
template <typename Fn>
void fillCurve (EnvelopeCurves::Curve& c, const char* label, bool pitch, float seconds, Fn&& valueAt)
{
    c.label = label;
    c.pitch = pitch;
    for (int i = 0; i <= EnvelopeCurves::kPoints; ++i)
        c.y[(size_t) i] = std::clamp (valueAt (seconds * (float) i / (float) EnvelopeCurves::kPoints), 0.0f, 1.0f);
}
} // namespace

bool describeEnvelopes (int voiceIndex, const float* p, float seconds, EnvelopeCurves& out)
{
    out = {};
    switch (kVoiceSpecs[(size_t) voiceIndex].engine)
    {
        case EngineKind::Kick:
        {
            const float bendMax = kVoiceSpecs[(size_t) voiceIndex].params[KickP::Bend].max;
            out.count = 2;
            fillCurve (out.curves[0], "AMP", false, seconds, [p] (float t) { return KickVoice::ampEnvelope (p, t); });
            fillCurve (out.curves[1], "PITCH", true, seconds, [p, bendMax] (float t) { return KickVoice::pitchSemitones (p, t) / bendMax; });
            out.markers = { p[KickP::Attack] * 0.001f, p[KickP::Attack] * 0.001f + p[KickP::Punch] * 0.01f * 0.06f };
            return true;
        }

        case EngineKind::Snare:
            out.count = 3;
            fillCurve (out.curves[0], "BODY", false, seconds, [p] (float t) { return SnareVoice::bodyEnvelope (p, t); });
            fillCurve (out.curves[1], "WIRES", false, seconds, [p] (float t) { return SnareVoice::wireEnvelope (p, t); });
            fillCurve (out.curves[2], "PITCH", true, seconds, [p] (float t) { return SnareVoice::pitchDrop (p, t); });
            out.markers[0] = p[SnareP::WireDelay] * 0.001f;
            return true;

        case EngineKind::Clap:
        {
            // Run the voice's own envelope code and keep each point's peak, so
            // short hits show at full height.
            constexpr double rate = 24000.0;
            ClapEnvelope env;
            env.setup (p, rate, 1.0f, ClapVoice::kFirstJitterSeed);
            auto& c = out.curves[0];
            c.label = "AMP";
            const int total = std::max (1, (int) (seconds * rate));
            for (int s = 0; s < total; ++s)
            {
                const auto index = (size_t) (((int64_t) s * EnvelopeCurves::kPoints + total / 2) / total);
                c.y[index] = std::max (c.y[index], std::min (1.0f, env.next()));
            }
            out.count = 1;
            return true;
        }

        case EngineKind::Metal:
            out.count = 1;
            fillCurve (out.curves[0], "AMP", false, seconds, [p] (float t) { return MetalVoice::ampEnvelope (p, t); });
            out.markers = { p[MetalP::Attack] * 0.001f, (p[MetalP::Attack] + p[MetalP::Hold]) * 0.001f };
            return true;

        case EngineKind::Tone:
        {
            const float bendMax = kVoiceSpecs[(size_t) voiceIndex].params[ToneP::Bend].max;
            out.count = 3;
            fillCurve (out.curves[0], "AMP", false, seconds, [p] (float t) { return ToneVoice::ampEnvelope (p, t); });
            fillCurve (out.curves[1], "NOISE", false, seconds, [p] (float t) { return ToneVoice::noiseEnvelope (p, t); });
            fillCurve (out.curves[2], "PITCH", true, seconds, [p, bendMax] (float t) { return ToneVoice::pitchSemitones (p, t) / bendMax; });
            out.markers = { p[ToneP::Attack] * 0.001f, (p[ToneP::Attack] + p[ToneP::Hold]) * 0.001f };
            return true;
        }

        default:
            return false;
    }
}

float hitLengthSeconds (int voiceIndex, const float* p)
{
    switch (kVoiceSpecs[(size_t) voiceIndex].engine)
    {
        case EngineKind::Kick:
            return (p[KickP::Attack] + p[KickP::Punch] * 0.6f + p[KickP::Decay] * 1.1f) * 0.001f;
        case EngineKind::Snare:
            return std::max (p[SnareP::BodyDecay], p[SnareP::WireDelay] + p[SnareP::Decay]) * 1.1f * 0.001f;
        case EngineKind::Clap:
            return ((float) (std::lround (p[ClapP::Bursts]) - 1) * p[ClapP::Spread] + p[ClapP::Decay] * 1.1f) * 0.001f;
        case EngineKind::Metal:
        {
            using namespace MetalP;
            const float decay = p[Decay] * (p[Tail] > 0.0f ? MetalVoice::kTailTimeScale : 1.0f);
            return (p[Attack] + p[Hold] + decay * 1.1f) * 0.001f;
        }
        case EngineKind::Tone:
        {
            using namespace ToneP;
            return std::max (p[Attack] + p[Hold] + p[Decay], p[NoiseLength]) * 1.1f * 0.001f;
        }
        default:
            return 0.3f;
    }
}

} // namespace fp
