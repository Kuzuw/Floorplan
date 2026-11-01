#include "UI/SequencerComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

namespace
{
constexpr int kRuler = 16;
constexpr int kToolbar = 28;

// Step menu item ID bases (each range is far from the next).
constexpr int kRatchetId = 100, kProbId = 200, kVelId = 1000, kTuneId = 2100, kDecayId = 3300, kMicroId = 4200;

enum LaneMode { LaneVelocity, LaneProbability, LaneTiming, LaneTune, LaneDecay, LaneRatchet, NumLaneModes };
constexpr const char* kLaneNames[NumLaneModes] = { "VELOCITY", "PROBABILITY", "MICRO TIMING", "TUNE LOCK", "DECAY LOCK", "RATCHET" };

struct LaneRange { int min, max; bool bipolar; };
constexpr LaneRange kLaneRanges[NumLaneModes] = { { 1, 127, false }, { 0, 100, false }, { -50, 50, true },
                                                 { -24, 24, true }, { -100, 100, true }, { 1, 4, false } };

int laneValue (const Step& s, int mode)
{
    switch (mode)
    {
        case LaneVelocity:    return s.velocity;
        case LaneProbability: return s.probability;
        case LaneTiming:      return s.micro;
        case LaneTune:        return s.tune;
        case LaneDecay:       return s.decay;
        case LaneRatchet:     return s.ratchet;
        default:              return 0;
    }
}

void setLaneValue (Step& s, int mode, int v)
{
    switch (mode)
    {
        case LaneVelocity:    s.velocity = (uint8_t) v; break;
        case LaneProbability: s.probability = (uint8_t) v; break;
        case LaneTiming:      s.micro = (int8_t) v; break;
        case LaneTune:        s.tune = (int8_t) v; break;
        case LaneDecay:       s.decay = (int8_t) v; break;
        case LaneRatchet:     s.ratchet = (uint8_t) v; break;
        default: break;
    }
}

juce::String laneText (int mode, int v)
{
    switch (mode)
    {
        case LaneProbability: return juce::String (v) + "%";
        case LaneTiming:      return (v > 0 ? "+" : "") + juce::String (v) + "%";
        case LaneTune:        return (v > 0 ? "+" : "") + juce::String (v) + " st";
        case LaneDecay:       return "x" + juce::String (std::exp2 ((float) v / 50.0f), 2);
        case LaneRatchet:     return juce::String (v) + "x";
        default:              return juce::String (v);
    }
}

// Small callout with the Euclidean generator controls.
class EuclidPanel final : public juce::Component
{
public:
    EuclidPanel (int length, std::function<void (int, int)> apply) : onApply (std::move (apply))
    {
        for (auto* s : { &pulses, &rotation })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 18);
            addAndMakeVisible (s);
        }
        pulses.setRange (0, length, 1);
        pulses.setValue (juce::jmin (length, 5));
        rotation.setRange (0, juce::jmax (1, length - 1), 1);
        addAndMakeVisible (applyButton);
        applyButton.onClick = [this] { onApply ((int) pulses.getValue(), (int) rotation.getValue()); };
        setSize (260, 104);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::textDim);
        g.setFont (font (12.0f, true));
        g.drawText ("PULSES", 8, 8, 70, 22, juce::Justification::centredLeft);
        g.drawText ("ROTATE", 8, 38, 70, 22, juce::Justification::centredLeft);
    }

    void resized() override
    {
        pulses.setBounds (76, 8, getWidth() - 84, 22);
        rotation.setBounds (76, 38, getWidth() - 84, 22);
        applyButton.setBounds (getWidth() - 88, 70, 80, 26);
    }

private:
    juce::Slider pulses, rotation;
    juce::TextButton applyButton { "APPLY" };
    std::function<void (int, int)> onApply;
};
} // namespace

// ===========================================================================
// Grid
// ===========================================================================
class SequencerComponent::Grid final : public juce::Component
{
public:
    explicit Grid (SequencerComponent& o) : owner (o) {}

    float rowHeight() const { return (float) (getHeight() - kRuler) / (float) kNumVoices; }

    juce::Rectangle<float> cell (int voice, int step) const
    {
        return owner.geo.column (step, (float) kRuler + (float) voice * rowHeight(), rowHeight());
    }

    int voiceAt (float y) const
    {
        const int v = (int) std::floor ((y - (float) kRuler) / rowHeight());
        return juce::isPositiveAndBelow (v, kNumVoices) ? v : -1;
    }

    void repaintStep (int step)
    {
        if (step >= owner.geo.first && step < owner.geo.first + owner.geo.columns)
            repaint (owner.geo.column (step, 0.0f, (float) getHeight()).getSmallestIntegerContainer().expanded (1, 0));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& geom = owner.geo;
        const auto& pat = owner.cached;
        const auto& ui = owner.state;

        // Ruler with step numbers; beat starts are brighter.
        for (int c = 0; c < geom.columns; ++c)
        {
            const int s = geom.first + c;
            const auto r = geom.column (s, 0.0f, (float) kRuler);
            const bool isPlay = s == owner.playStep && owner.playPattern == owner.cachedIndex;
            g.setColour (isPlay ? colours::accentHot : (s % 4 == 0 ? colours::textDim : colours::textFaint));
            g.setFont (monoFont (10.5f, s % 4 == 0 || isPlay));
            g.drawText (geom.inPattern (s) ? juce::String (s + 1) : juce::String(), r, juce::Justification::centred);
        }

        for (int v = 0; v < kNumVoices; ++v)
        {
            const bool selectedRow = v == ui.voice;
            if (selectedRow)
            {
                g.setColour (colours::raised.withAlpha (0.6f));
                g.fillRect (juce::Rectangle<float> (geom.x0, (float) kRuler + (float) v * rowHeight(),
                                                    geom.colW * (float) geom.columns, rowHeight()));
            }

            for (int c = 0; c < geom.columns; ++c)
                paintCell (g, v, geom.first + c, pat);
        }

        // Playhead column overlay.
        if (owner.playPattern == owner.cachedIndex && owner.playStep >= geom.first && owner.playStep < geom.first + geom.columns)
        {
            const auto col = geom.column (owner.playStep, (float) kRuler, (float) getHeight() - (float) kRuler);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (col);
            g.setColour (colours::accentHot);
            g.fillRect (col.withHeight (2.0f).translated (0.0f, -3.0f).reduced (3.0f, 0.0f));
        }
    }

    void paintCell (juce::Graphics& g, int v, int s, const Pattern& pat)
    {
        const auto& geom = owner.geo;
        auto r = cell (v, s).reduced (2.0f, 2.5f);
        if (! geom.inPattern (s))
        {
            g.setColour (colours::well.withAlpha (0.5f));
            g.fillRect (r);
            return;
        }

        const auto& st = pat.tracks[(size_t) v].steps[(size_t) s];
        const bool beatGroup = (s / 4) % 2 == 0;
        g.setColour (beatGroup ? juce::Colour (0xff181816) : juce::Colour (0xff121211));
        g.fillRect (r);

        if (st.on)
        {
            // Bone pad (amber when accented); velocity is the solid height.
            const float vel = (float) st.velocity / 127.0f;
            const auto base = st.accent() ? colours::accentHot : colours::accent;
            g.setColour (base.withAlpha (0.3f));
            g.fillRect (r);
            g.setColour (base);
            g.fillRect (r.withTop (r.getBottom() - r.getHeight() * (0.3f + 0.7f * vel)));

            const auto ink = colours::ink;
            // Probability: small pie in the top-left corner.
            if (st.probability < 100)
            {
                const auto pie = juce::Rectangle<float> (r.getX() + 3.0f, r.getY() + 3.0f, 7.0f, 7.0f);
                g.setColour (ink.withAlpha (0.35f));
                g.fillEllipse (pie);
                juce::Path p;
                p.addPieSegment (pie, 0.0f, juce::MathConstants<float>::twoPi * (float) st.probability / 100.0f, 0.0f);
                g.setColour (ink);
                g.fillPath (p);
            }
            // Ratchet: dots in the top-right corner.
            for (int i = 1; i < st.ratchet; ++i)
            {
                g.setColour (ink);
                g.fillEllipse (r.getRight() - 4.0f - (float) i * 5.0f, r.getY() + 3.0f, 3.0f, 3.0f);
            }
            // Flam: double tick bottom-left.
            if (st.flam())
            {
                g.setColour (ink);
                g.fillRect (r.getX() + 3.0f, r.getBottom() - 8.0f, 1.5f, 5.0f);
                g.fillRect (r.getX() + 6.0f, r.getBottom() - 8.0f, 1.5f, 5.0f);
            }
            // Parameter locks: dot bottom-right.
            if (st.tune != 0 || st.decay != 0)
            {
                g.setColour (ink);
                g.fillEllipse (r.getRight() - 7.0f, r.getBottom() - 7.0f, 4.0f, 4.0f);
            }
            // Microtiming: tick showing the offset from the step start.
            if (st.micro != 0)
            {
                const float x = r.getCentreX() + r.getWidth() * 0.45f * (float) st.micro / 50.0f;
                g.setColour (ink);
                g.fillRect (x - 0.75f, r.getBottom() - 6.0f, 1.5f, 6.0f);
            }
        }
        else if (s % 4 == 0)
        {
            g.setColour (colours::borderLight.withAlpha (0.6f));
            g.fillRect (juce::Rectangle<float> (3.0f, 3.0f).withCentre (r.getCentre()));
        }

        g.setColour (st.on ? (st.accent() ? colours::accentHot : colours::accent) : colours::border);
        g.drawRect (r, 1.0f);

        if (v == owner.state.voice && s == owner.state.step)
        {
            g.setColour (colours::accentHot);
            g.drawRect (r.expanded (1.5f), 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int v = voiceAt ((float) e.y);
        const int s = owner.geo.stepAt ((float) e.x);
        if (v < 0 || ! owner.geo.inPattern (s))
            return;

        owner.state.selectVoice (v);
        owner.state.selectStep (s);

        if (e.mods.isPopupMenu())
        {
            showStepMenu (v, s);
            return;
        }

        owner.proc.patterns.pushUndoSnapshot (owner.cachedIndex);
        const auto& st = owner.cached.tracks[(size_t) v].steps[(size_t) s];

        if (e.mods.isShiftDown())
        {
            const bool acc = ! (st.on && st.accent());
            owner.editPattern ([v, s, acc] (Pattern& p)
            {
                auto& x = p.tracks[(size_t) v].steps[(size_t) s];
                x.on = 1;
                x.setFlag (Step::Accent, acc);
            }, false);
            painting = false;
            return;
        }

        paintValue = ! st.on;
        paintVoice = v;
        lastPainted = s;
        painting = true;
        setStep (v, s, paintValue);
        if (paintValue)
            owner.proc.triggerVoice (v, (float) st.velocity / 127.0f);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! painting)
            return;
        const int s = owner.geo.stepAt ((float) e.x);
        if (owner.geo.inPattern (s) && s != lastPainted)
        {
            lastPainted = s;
            setStep (paintVoice, s, paintValue);
        }
    }

    void mouseUp (const juce::MouseEvent&) override { painting = false; }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int v = voiceAt ((float) e.y);
        const int s = owner.geo.stepAt ((float) e.x);
        if (v < 0 || ! owner.geo.inPattern (s) || ! owner.cached.tracks[(size_t) v].steps[(size_t) s].on)
            return;
        const int delta = w.deltaY > 0 ? 6 : -6;
        owner.editPattern ([v, s, delta] (Pattern& p)
        {
            auto& x = p.tracks[(size_t) v].steps[(size_t) s];
            x.velocity = (uint8_t) juce::jlimit (1, 127, (int) x.velocity + delta);
        });
    }

private:
    void setStep (int v, int s, bool on)
    {
        owner.editPattern ([v, s, on] (Pattern& p) { p.tracks[(size_t) v].steps[(size_t) s].on = on ? 1 : 0; }, false);
    }

    void showStepMenu (int v, int s)
    {
        const auto st = owner.cached.tracks[(size_t) v].steps[(size_t) s];
        juce::PopupMenu menu, ratchet, prob, vel, tune, decay, micro;
        // Item IDs: base + offset per sub-menu so ranges never overlap.
        for (int r = 1; r <= 4; ++r) ratchet.addItem (kRatchetId + r, juce::String (r) + "x", true, st.ratchet == r);
        for (int p : { 100, 90, 75, 50, 33, 25, 10 }) prob.addItem (kProbId + p, juce::String (p) + "%", true, st.probability == p);
        for (int x : { 127, 110, 100, 85, 70, 50, 35 }) vel.addItem (kVelId + x, juce::String (x), true, st.velocity == x);
        for (int t : { -12, -7, -5, -2, 0, 2, 5, 7, 12 }) tune.addItem (kTuneId + t, t == 0 ? juce::String ("None") : laneText (LaneTune, t), true, st.tune == t);
        for (int d : { -100, -50, -25, 0, 25, 50, 100 }) decay.addItem (kDecayId + d, d == 0 ? juce::String ("None") : laneText (LaneDecay, d), true, st.decay == d);
        for (int m : { -25, -10, 0, 10, 25, 50 }) micro.addItem (kMicroId + m, laneText (LaneTiming, m), true, st.micro == m);

        menu.addSectionHeader (juce::String (kVoiceSpecs[(size_t) v].name) + "  step " + juce::String (s + 1));
        menu.addItem (1, st.on ? "Disable step" : "Enable step");
        menu.addItem (2, "Accent", true, st.accent());
        menu.addItem (3, "Flam", true, st.flam());
        menu.addSubMenu ("Ratchet", ratchet);
        menu.addSubMenu ("Velocity", vel);
        menu.addSubMenu ("Probability", prob);
        menu.addSubMenu ("Micro timing", micro);
        menu.addSubMenu ("Tune lock", tune);
        menu.addSubMenu ("Decay lock", decay);
        menu.addSeparator();
        menu.addItem (4, "Reset step settings");
        menu.addItem (5, "Preview");

        juce::Component::SafePointer<Grid> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safe, v, s, st] (int r)
        {
            if (safe == nullptr || r == 0)
                return;
            if (r == 5) { safe->owner.proc.triggerVoice (v, (float) st.velocity / 127.0f); return; }

            safe->owner.editPattern ([v, s, r] (Pattern& p)
            {
                auto& x = p.tracks[(size_t) v].steps[(size_t) s];
                if (r == 1) x.on = x.on ? 0 : 1;
                else if (r == 2) { x.on = 1; x.setFlag (Step::Accent, ! x.accent()); }
                else if (r == 3) { x.on = 1; x.setFlag (Step::Flam, ! x.flam()); }
                else if (r == 4) { const auto on = x.on; x = Step {}; x.on = on; }
                else if (r >= kMicroId - 100) x.micro = (int8_t) (r - kMicroId);
                else if (r >= kDecayId - 200) x.decay = (int8_t) (r - kDecayId);
                else if (r >= kTuneId - 100)  x.tune = (int8_t) (r - kTuneId);
                else if (r >= kVelId)         x.velocity = (uint8_t) (r - kVelId);
                else if (r >= kProbId)        x.probability = (uint8_t) (r - kProbId);
                else if (r > kRatchetId)      x.ratchet = (uint8_t) (r - kRatchetId);
            });
        });
    }

    SequencerComponent& owner;
    bool painting = false, paintValue = true;
    int paintVoice = 0, lastPainted = -1;
};

// ===========================================================================
// Lane: per-step values of the selected voice.
// ===========================================================================
class SequencerComponent::Lane final : public juce::Component
{
public:
    explicit Lane (SequencerComponent& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& geom = owner.geo;
        const int mode = owner.laneMode;
        const auto range = kLaneRanges[mode];
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (colours::well);
        g.fillRect (bounds);
        g.setColour (colours::border);
        g.drawRect (bounds, 1.0f);

        const float h = bounds.getHeight() - 6.0f;
        const float zeroY = range.bipolar ? bounds.getY() + 3.0f + h * 0.5f : bounds.getBottom() - 3.0f;
        if (range.bipolar)
        {
            g.setColour (colours::border);
            g.fillRect (bounds.getX(), zeroY, bounds.getWidth(), 1.0f);
        }

        const auto& track = owner.cached.tracks[(size_t) owner.state.voice];
        for (int c = 0; c < geom.columns; ++c)
        {
            const int s = geom.first + c;
            if (! geom.inPattern (s))
                continue;
            const auto& st = track.steps[(size_t) s];
            const int value = laneValue (st, mode);
            const float norm = (float) (value - range.min) / (float) (range.max - range.min);
            auto col = geom.column (s, bounds.getY() + 3.0f, h);
            col = col.withSizeKeepingCentre (juce::jmax (4.0f, col.getWidth() * 0.55f), col.getHeight());

            juce::Rectangle<float> bar;
            if (range.bipolar)
            {
                const float y = col.getY() + h * (1.0f - norm);
                bar = juce::Rectangle<float>::leftTopRightBottom (col.getX(), juce::jmin (y, zeroY), col.getRight(), juce::jmax (y, zeroY) + 1.0f);
            }
            else
            {
                bar = col.withTop (col.getBottom() - juce::jmax (2.0f, h * norm));
            }

            g.setColour (st.on ? colours::accent.withAlpha (s == owner.state.step ? 1.0f : 0.8f) : colours::textFaint.withAlpha (0.18f));
            g.fillRect (bar);

            if (s == hoverStep)
            {
                g.setColour (colours::text);
                g.setFont (font (10.5f, true));
                g.drawText (laneText (mode, value), col.withHeight (14.0f), juce::Justification::centred);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int s = owner.geo.stepAt ((float) e.x);
        if (s != hoverStep) { hoverStep = s; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override { hoverStep = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.proc.patterns.pushUndoSnapshot (owner.cachedIndex);
        apply (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override { apply (e); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Double-click resets the value to its neutral default.
        const int s = owner.geo.stepAt ((float) e.x);
        if (! owner.geo.inPattern (s))
            return;
        const int v = owner.state.voice, mode = owner.laneMode;
        const int neutral = laneValue (Step {}, mode);
        owner.editPattern ([v, s, mode, neutral] (Pattern& p) { setLaneValue (p.tracks[(size_t) v].steps[(size_t) s], mode, neutral); });
    }

private:
    void apply (const juce::MouseEvent& e)
    {
        const int s = owner.geo.stepAt ((float) e.x);
        if (! owner.geo.inPattern (s))
            return;
        const auto range = kLaneRanges[owner.laneMode];
        const float h = (float) getHeight() - 6.0f;
        const float norm = juce::jlimit (0.0f, 1.0f, 1.0f - ((float) e.y - 3.0f) / h);
        const int value = juce::roundToInt ((float) range.min + norm * (float) (range.max - range.min));
        const int v = owner.state.voice, mode = owner.laneMode;
        owner.editPattern ([v, s, mode, value] (Pattern& p) { setLaneValue (p.tracks[(size_t) v].steps[(size_t) s], mode, value); }, false);
        hoverStep = s;
        owner.state.selectStep (s);
    }

    SequencerComponent& owner;
    int hoverStep = -1;
};

// ===========================================================================
// SequencerComponent
// ===========================================================================
SequencerComponent::SequencerComponent (FloorplanProcessor& p, UiState& s) : proc (p), state (s)
{
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto* b = patternButtons.add (new juce::TextButton (juce::String (i + 1)));
        b->setTooltip ("Pattern " + juce::String (i + 1) + " (switches at the end of the current cycle while playing)");
        b->onClick = [this, i] { setEditPattern (i); };
        addAndMakeVisible (b);
    }

    for (int i = 0; i < 4; ++i)
    {
        auto* b = pageButtons.add (new juce::TextButton (juce::String (i * 16 + 1) + "-" + juce::String (i * 16 + 16)));
        b->onClick = [this, i] { state.page = i; state.follow = false; followButton.setToggleState (false, juce::dontSendNotification); updateGeometry(); };
        addAndMakeVisible (b);
    }

    for (int i = 0; i < (int) kLengthChoices.size(); ++i)
        lengthBox.addItem (juce::String (kLengthChoices[(size_t) i]) + " steps", i + 1);
    lengthBox.setTooltip ("Pattern length");
    lengthBox.onChange = [this]
    {
        const int idx = lengthBox.getSelectedItemIndex();
        if (idx >= 0)
        {
            const int len = kLengthChoices[(size_t) idx];
            if (len != cached.length)
                editPattern ([len] (Pattern& pat) { pat.length = (uint8_t) len; });
        }
    };
    addAndMakeVisible (lengthBox);

    followButton.setClickingTogglesState (true);
    followButton.setToggleState (state.follow, juce::dontSendNotification);
    followButton.setTooltip ("Page follows the playhead");
    followButton.onClick = [this] { state.follow = followButton.getToggleState(); };

    copyButton.onClick   = [this] { copyPattern(); };
    pasteButton.onClick  = [this] { pastePattern(); };
    clearButton.onClick  = [this] { clearPattern(); };
    doubleButton.onClick = [this] { doublePattern(); };
    randomButton.onClick = [this] { randomiseTrack(); };
    euclidButton.onClick = [this] { showEuclidean(); };
    copyButton.setTooltip ("Copy pattern (Ctrl+C)");
    pasteButton.setTooltip ("Paste pattern (Ctrl+V)");
    clearButton.setTooltip ("Clear pattern");
    doubleButton.setTooltip ("Duplicate: double the length and copy the content");
    randomButton.setTooltip ("Randomize the selected track (intensity in the Groove section)");
    euclidButton.setTooltip ("Euclidean rhythm generator for the selected track");
    for (auto* b : { &followButton, &copyButton, &pasteButton, &clearButton, &doubleButton, &randomButton, &euclidButton })
        addAndMakeVisible (b);

    for (int m = 0; m < NumLaneModes; ++m)
    {
        auto* b = laneButtons.add (new juce::TextButton (kLaneNames[m]));
        b->setRadioGroupId (4242);
        b->setClickingTogglesState (true);
        b->setToggleState (m == 0, juce::dontSendNotification);
        b->onClick = [this, m] { laneMode = m; lane->repaint(); };
        addAndMakeVisible (b);
    }

    grid = std::make_unique<Grid> (*this);
    lane = std::make_unique<Lane> (*this);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*lane);

    state.addChangeListener (this);
    patternChanged();
}

SequencerComponent::~SequencerComponent()
{
    state.removeChangeListener (this);
}

int SequencerComponent::getEditPattern() const
{
    return juce::jlimit (0, kNumPatterns - 1, (int) proc.apvts.getRawParameterValue (ids::pattern)->load() - 1);
}

void SequencerComponent::setEditPattern (int index)
{
    if (auto* p = proc.apvts.getParameter (ids::pattern))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (index + 1)));
        p->endChangeGesture();
    }
    patternChanged();
}

void SequencerComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    grid->repaint();
    lane->repaint();
}

void SequencerComponent::editPattern (const std::function<void (Pattern&)>& fn, bool pushUndo)
{
    proc.patterns.edit (cachedIndex, fn, pushUndo);
    patternChanged();
}

void SequencerComponent::patternChanged()
{
    cachedIndex = getEditPattern();
    cached = proc.patterns.getPattern (cachedIndex);
    cachedVersion = proc.patterns.version();
    updateToolbar();
    updateGeometry();
}

void SequencerComponent::updateToolbar()
{
    const auto bank = proc.patterns.getBank();
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto* b = patternButtons[i];
        b->setToggleState (i == cachedIndex, juce::dontSendNotification);
        const bool playing = i == playPattern && proc.isTransportRunning();
        b->setColour (juce::TextButton::textColourOffId,
                      playing ? colours::accentHot : (bank.patterns[(size_t) i].isEmpty() ? colours::textFaint : colours::text));
    }

    for (int i = 0; i < (int) kLengthChoices.size(); ++i)
        if (kLengthChoices[(size_t) i] == cached.length)
            lengthBox.setSelectedItemIndex (i, juce::dontSendNotification);
    if (std::find (kLengthChoices.begin(), kLengthChoices.end(), (int) cached.length) == kLengthChoices.end())
        lengthBox.setText (juce::String (cached.length) + " steps", juce::dontSendNotification);

    pasteButton.setEnabled (state.patternClipboard.has_value());
    doubleButton.setEnabled (cached.length * 2 <= kMaxSteps);
}

void SequencerComponent::updateGeometry()
{
    const int len = juce::jmax (1, (int) cached.length);
    const int pages = (len + 15) / 16;
    state.page = juce::jlimit (0, pages - 1, state.page);

    geo.length = len;
    geo.columns = juce::jmin (16, len);
    geo.first = state.page * 16;
    geo.x0 = 0.0f;
    geo.colW = (float) grid->getWidth() / (float) juce::jmax (1, geo.columns);

    for (int i = 0; i < pageButtons.size(); ++i)
    {
        pageButtons[i]->setVisible (pages > 1 && i < pages);
        pageButtons[i]->setToggleState (i == state.page, juce::dontSendNotification);
    }

    grid->repaint();
    lane->repaint();
}

void SequencerComponent::refresh()
{
    const int editIndex = getEditPattern();
    if (editIndex != cachedIndex || proc.patterns.version() != cachedVersion)
        patternChanged();

    const int newStep = proc.getPlayingStep();
    const int newPattern = proc.getPlayingPattern();
    if (newPattern != playPattern)
    {
        playPattern = newPattern;
        updateToolbar();
        grid->repaint();
    }

    if (newStep != playStep)
    {
        const int old = playStep;
        playStep = newStep;

        if (state.follow && newStep >= 0 && playPattern == cachedIndex && newStep / 16 != state.page)
        {
            state.page = newStep / 16;
            updateGeometry();
        }
        else
        {
            grid->repaintStep (old);
            grid->repaintStep (newStep);
        }
    }
}

int SequencerComponent::getGridTop() const noexcept { return grid->getY() + kRuler; }
int SequencerComponent::getRowHeight() const noexcept { return (grid->getHeight() - kRuler) / kNumVoices; }

void SequencerComponent::copyPattern()
{
    state.patternClipboard = cached;
    updateToolbar();
}

void SequencerComponent::pastePattern()
{
    if (state.patternClipboard)
    {
        const Pattern p = *state.patternClipboard;
        editPattern ([p] (Pattern& dst) { dst = p; });
    }
}

void SequencerComponent::clearPattern()   { editPattern ([] (Pattern& p) { p.clear(); }); }
void SequencerComponent::doublePattern()  { editPattern ([] (Pattern& p) { p.doubleLength(); }); }

void SequencerComponent::randomiseTrack()
{
    const int v = state.voice;
    const float amount = proc.apvts.getRawParameterValue (ids::randAmount)->load() * 0.01f;
    const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    editPattern ([v, amount, seed] (Pattern& p) { p.randomiseTrack (v, amount, seed); });
}

void SequencerComponent::rotateTrack (int amount)
{
    const int v = state.voice;
    editPattern ([v, amount] (Pattern& p) { p.rotateTrack (v, amount); });
}

void SequencerComponent::showEuclidean()
{
    const int v = state.voice;
    juce::Component::SafePointer<SequencerComponent> safe (this);
    auto panel = std::make_unique<EuclidPanel> ((int) cached.length, [safe, v] (int pulses, int rotation)
    {
        if (safe != nullptr)
            safe->editPattern ([v, pulses, rotation] (Pattern& p) { p.euclidean (v, pulses, rotation); });
    });
    juce::CallOutBox::launchAsynchronously (std::move (panel), euclidButton.getScreenBounds(), nullptr);
}

void SequencerComponent::moveSelection (int deltaStep, int deltaVoice)
{
    const int len = juce::jmax (1, (int) cached.length);
    state.selectVoice (juce::jlimit (0, kNumVoices - 1, state.voice + deltaVoice));
    state.selectStep (((state.step + deltaStep) % len + len) % len);
    if (state.step / 16 != state.page)
    {
        state.page = state.step / 16;
        updateGeometry();
    }
}

void SequencerComponent::toggleSelectedStep()
{
    const int v = state.voice, s = state.step;
    if (s < cached.length)
        editPattern ([v, s] (Pattern& p) { auto& x = p.tracks[(size_t) v].steps[(size_t) s]; x.on = x.on ? 0 : 1; });
}

void SequencerComponent::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (kToolbar);

    for (auto* b : patternButtons)
    {
        b->setBounds (bar.removeFromLeft (25));
        bar.removeFromLeft (2);
    }
    bar.removeFromLeft (10);
    lengthBox.setBounds (bar.removeFromLeft (92));
    bar.removeFromLeft (8);

    euclidButton.setBounds (bar.removeFromRight (58));
    bar.removeFromRight (4);
    randomButton.setBounds (bar.removeFromRight (48));
    bar.removeFromRight (4);
    doubleButton.setBounds (bar.removeFromRight (32));
    bar.removeFromRight (4);
    clearButton.setBounds (bar.removeFromRight (50));
    bar.removeFromRight (4);
    pasteButton.setBounds (bar.removeFromRight (50));
    bar.removeFromRight (4);
    copyButton.setBounds (bar.removeFromRight (48));
    bar.removeFromRight (10);
    followButton.setBounds (bar.removeFromRight (60));

    r.removeFromTop (6);
    auto laneArea = r.removeFromBottom (78);
    r.removeFromBottom (8);

    const int rowH = juce::jlimit (20, 44, (r.getHeight() - kRuler) / kNumVoices);
    grid->setBounds (r.removeFromTop (kRuler + rowH * kNumVoices));

    // Page buttons sit in the free space of the toolbar row under the grid ruler.
    auto tabs = laneArea.removeFromTop (20);
    for (auto* b : laneButtons)
    {
        b->setBounds (tabs.removeFromLeft (92));
        tabs.removeFromLeft (3);
    }
    for (int i = pageButtons.size(); --i >= 0;)
    {
        pageButtons[i]->setBounds (tabs.removeFromRight (58));
        tabs.removeFromRight (3);
    }
    laneArea.removeFromTop (4);
    lane->setBounds (laneArea);

    updateGeometry();
}

} // namespace fp::ui
