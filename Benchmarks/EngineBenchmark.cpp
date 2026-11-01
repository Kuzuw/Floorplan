// Offline CPU benchmark for the sequencer + engine (the whole audio path
// except the JUCE wrapper). Prints the cost per block and the realtime load.
//
// Usage: floorplan_bench [secondsOfAudio]

#include "Audio/DrumEngine.h"
#include "Sequencer/Pattern.h"
#include "Sequencer/Sequencer.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace fp;

namespace
{
struct Scenario
{
    const char* name;
    double sampleRate;
    int blockSize;
    bool playing;
    bool dense;
};

double runScenario (const Scenario& sc, double seconds, double& realtimeLoadPercent)
{
    DrumEngine engine;
    engine.prepare (sc.sampleRate, sc.blockSize);
    Sequencer seq;
    seq.prepare (sc.sampleRate);

    PatternBank bank;
    SequencerSettings settings;
    settings.swing = 56.0f;

    if (sc.dense)
    {
        // Worst case: every voice on every 1/32 step, ratchets, long tails,
        // full stereo width and every optional processor engaged.
        settings.resolution = 2;
        auto& p = bank.patterns[0];
        p.length = 64;
        for (auto& t : p.tracks)
            for (auto& s : t.steps) { s.on = 1; s.ratchet = 2; }
        for (int v = 0; v < kNumVoices; ++v)
        {
            auto& vp = engine.voiceParams (v);
            for (int i = 0; i < kNumSynthParams; ++i)
                vp.synth[(size_t) i] = kVoiceSpecs[(size_t) v].params[(size_t) i].max;
            vp.drive = 0.5f;
            vp.filter = (v & 1) ? 0.3f : -0.3f;
        }
        engine.voiceParams (ClosedHat).mute = false;
        engine.masterParams().comp = 0.6f;
        engine.masterParams().clip = 0.5f;
    }
    else
    {
        bank.patterns[0] = makeDefaultHousePattern();
    }

    std::vector<float> L ((size_t) sc.blockSize), R ((size_t) sc.blockSize);
    TriggerEvent events[Sequencer::kMaxEventsPerBlock];
    const double bpm = 124.0;
    const double ppqPerSample = bpm / (60.0 * sc.sampleRate);
    const long long totalBlocks = (long long) (seconds * sc.sampleRate / sc.blockSize);

    double ppq = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long long b = 0; b < totalBlocks; ++b)
    {
        TransportState t { sc.playing, ppq, bpm };
        const int n = seq.process (bank, t, settings, sc.blockSize, events, Sequencer::kMaxEventsPerBlock);
        engine.process (events, n, L.data(), R.data(), nullptr, nullptr, sc.blockSize);
        ppq += sc.blockSize * ppqPerSample;
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed = std::chrono::duration<double> (t1 - t0).count();
    const double audioSeconds = (double) totalBlocks * sc.blockSize / sc.sampleRate;
    realtimeLoadPercent = 100.0 * elapsed / audioSeconds;
    return elapsed * 1.0e6 / (double) totalBlocks; // microseconds per block
}
} // namespace

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 60.0;

    const Scenario scenarios[] = {
        { "idle (transport stopped)", 48000.0, 128, false, false },
        { "typical house groove",     48000.0, 128, true,  false },
        { "typical house groove",     44100.0, 512, true,  false },
        { "worst case dense",         48000.0, 128, true,  true  },
        { "worst case dense",         96000.0, 256, true,  true  },
    };

    std::printf ("Floorplan engine benchmark (%.0f s of audio per scenario, single thread)\n\n", seconds);
    std::printf ("%-28s %8s %6s %14s %12s\n", "scenario", "rate", "block", "us / block", "core load");
    for (const auto& sc : scenarios)
    {
        double load = 0.0;
        const double usPerBlock = runScenario (sc, seconds, load);
        std::printf ("%-28s %8.0f %6d %14.2f %11.3f%%\n", sc.name, sc.sampleRate, sc.blockSize, usPerBlock, load);
    }

    std::printf ("\nMemory (static footprint):\n");
    std::printf ("  DrumEngine object      %zu bytes (+ 2 x maxBlock floats scratch)\n", sizeof (DrumEngine));
    std::printf ("  Sequencer object       %zu bytes\n", sizeof (Sequencer));
    std::printf ("  PatternBank            %zu bytes (x3 for the triple buffer)\n", sizeof (PatternBank));
    return 0;
}
