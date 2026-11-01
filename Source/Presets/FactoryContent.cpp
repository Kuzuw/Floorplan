#include "Presets/FactoryContent.h"

namespace fp::factory
{

namespace
{
// Tiny pattern DSL: one string per track, one character per step.
//   '.' off   'x' normal (vel 100)   'X' accent (vel 120 + accent flag)
//   'o' soft (vel 70)   'g' ghost (vel 45)   '?' 50 % probability   'r' ratchet x2
Pattern build (int length, std::initializer_list<std::pair<int, const char*>> rows)
{
    Pattern p;
    p.length = (uint8_t) length;
    for (const auto& [voice, row] : rows)
    {
        for (int i = 0; row[i] != 0 && i < length; ++i)
        {
            auto& s = p.tracks[(size_t) voice].steps[(size_t) i];
            switch (row[i])
            {
                case 'x': s.on = 1; s.velocity = 100; break;
                case 'X': s.on = 1; s.velocity = 120; s.setFlag (Step::Accent, true); break;
                case 'o': s.on = 1; s.velocity = 70; break;
                case 'g': s.on = 1; s.velocity = 45; break;
                case '?': s.on = 1; s.velocity = 85; s.probability = 50; break;
                case 'r': s.on = 1; s.velocity = 80; s.ratchet = 2; break;
                default: break;
            }
        }
    }
    return p;
}

std::vector<NamedPattern> makePatterns()
{
    std::vector<NamedPattern> list;
    list.push_back ({ "House Classic", makeDefaultHousePattern() });

    list.push_back ({ "Deep House", build (16, {
        { Kick,      "x...x...x...x..." },
        { Clap,      "....x.......x..." },
        { ClosedHat, "..o...o...o...og" },
        { OpenHat,   "..x...x...x...x." },
        { Perc,      "...g..o....g.o.." },
        { Snare,     "...............g" } }) });

    list.push_back ({ "Tech House Roll", build (16, {
        { Kick,      "X...x...x...x..." },
        { Clap,      "....x.......x..." },
        { ClosedHat, "xoxoxoxoxoxoxoxr" },
        { OpenHat,   "..x...x...x...x." },
        { Perc,      ".g.o..g..o.g..o." },
        { Tom,       "..............?." } }) });

    list.push_back ({ "Minimal Groove", build (16, {
        { Kick,      "x...x...x...x..." },
        { ClosedHat, "..g...g.o.g...g." },
        { Perc,      "...x.....x....o." },
        { Clap,      "............x..." },
        { Snare,     ".......g......?." } }) });

    list.push_back ({ "Peak Techno", build (16, {
        { Kick,      "X...x...X...x..." },
        { ClosedHat, "x.x.x.x.x.x.x.x." },
        { OpenHat,   "..x...x...x...x." },
        { Clap,      "....x.......x..." },
        { Cymbal,    "x..............." },
        { Tom,       ".........o....o." } }) });

    list.push_back ({ "UK Garage 2-Step", build (16, {
        { Kick,      "x......x..x....." },
        { Snare,     "....X.......X..." },
        { ClosedHat, "x.ox.ox.x.ox.oxo" },
        { OpenHat,   "..............x." },
        { Perc,      "......g.....g..." } }) });

    list.push_back ({ "Electro Breaks", build (16, {
        { Kick,      "x.....x...x....." },
        { Snare,     "....X.......X..." },
        { ClosedHat, "xxxxxxxxxxxxxxxx" },
        { Clap,      "....x.......x..x" },
        { Tom,       "...........o.o.." } }) });

    list.push_back ({ "Afro Perc", build (16, {
        { Kick,      "x...x...x...x..." },
        { Perc,      "x..x..x...x..x.." },
        { Tom,       "..o....o..o...o." },
        { ClosedHat, ".ox.ox.ox.ox.ox." },
        { Clap,      "....x.......x..." } }) });

    list.push_back ({ "Rolling 32", build (32, {
        { Kick,      "x...x...x...x...x...x...x...x..." },
        { Clap,      "....x.......x.......x.......x..x" },
        { ClosedHat, "oxoxoxoxoxoxoxoxoxoxoxoxoxoxoxrr" },
        { OpenHat,   "..x...x...x...x...x...x...x...x." },
        { Perc,      "...o......o.....g.o.......o...?." } }) });

    list.push_back ({ "Empty", Pattern {} });
    return list;
}

std::vector<Kit> makeKits()
{
    return {
        { "Init House", {} },
        { "Deep Night", {
            { "kick_tune", 44.0f }, { "kick_decay", 650.0f }, { "kick_click", 15.0f }, { "kick_bend", 20.0f },
            { "snare_tone", 30.0f }, { "chh_bright", 6000.0f }, { "chh_decay", 70.0f },
            { "ohh_decay", 520.0f }, { "clap_decay", 400.0f }, { "clap_tone", 30.0f },
            { "perc_tune", 380.0f }, { "perc_metal", 10.0f }, { "master_clip", 15.0f } } },
        { "Tech Punch", {
            { "kick_tune", 52.0f }, { "kick_decay", 320.0f }, { "kick_click", 55.0f }, { "kick_punch", 60.0f },
            { "kick_drive", 25.0f }, { "clap_tune", 1400.0f }, { "clap_bursts", 3.0f }, { "clap_decay", 180.0f },
            { "chh_decay", 35.0f }, { "chh_bright", 9000.0f }, { "perc_metal", 60.0f }, { "master_comp", 30.0f } } },
        { "Warehouse Techno", {
            { "kick_tune", 46.0f }, { "kick_decay", 900.0f }, { "kick_bend", 30.0f }, { "kick_drive", 45.0f },
            { "kick_filter", -15.0f }, { "ohh_decay", 250.0f }, { "cym_decay", 3000.0f }, { "tom_tune", 80.0f },
            { "master_comp", 40.0f }, { "master_clip", 40.0f } } },
        { "Minimal Tick", {
            { "kick_tune", 55.0f }, { "kick_decay", 250.0f }, { "kick_click", 60.0f }, { "chh_decay", 20.0f },
            { "chh_bright", 11000.0f }, { "chh_metal", 30.0f }, { "perc_tune", 900.0f }, { "perc_decay", 60.0f },
            { "perc_metal", 70.0f }, { "clap_decay", 120.0f }, { "clap_spread", 5.0f } } },
        { "Garage Swing", {
            { "snare_tune", 220.0f }, { "snare_snappy", 80.0f }, { "snare_decay", 180.0f },
            { "chh_bright", 8000.0f }, { "clap_width", 70.0f }, { "kick_decay", 380.0f }, { "kick_bend", 32.0f } } },
        { "Lo-Fi Dust", {
            { "kick_drive", 35.0f }, { "snare_drive", 40.0f }, { "chh_filter", -30.0f }, { "ohh_filter", -35.0f },
            { "clap_drive", 30.0f }, { "perc_filter", -25.0f }, { "master_clip", 60.0f } } },
        { "Electro Snap", {
            { "kick_bend", 40.0f }, { "kick_bendtime", 15.0f }, { "snare_tone", 70.0f }, { "snare_decay", 300.0f },
            { "tom_tune", 90.0f }, { "tom_metal", 20.0f }, { "clap_bursts", 5.0f } } },
    };
}
} // namespace

const std::vector<NamedPattern>& patterns()
{
    static const std::vector<NamedPattern> list = makePatterns();
    return list;
}

const std::vector<Kit>& kits()
{
    static const std::vector<Kit> list = makeKits();
    return list;
}

PatternBank defaultBank()
{
    PatternBank bank;
    const auto& list = patterns();
    for (size_t i = 0; i < list.size() && i < 8; ++i)
        bank.patterns[i] = list[i].pattern;
    return bank;
}

} // namespace fp::factory
