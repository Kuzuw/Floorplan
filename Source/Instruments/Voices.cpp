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

} // namespace fp
