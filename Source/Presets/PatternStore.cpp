#include "Presets/PatternStore.h"
#include "Presets/FactoryContent.h"

namespace fp
{

PatternStore::PatternStore()
{
    model = factory::defaultBank();
    realtime.write (model);
    realtime.write (model); // fill every slot so the first acquire is valid
    undoStack.reserve (kMaxUndo);
    redoStack.reserve (kMaxUndo);
}

void PatternStore::publishLocked()
{
    realtime.write (model);
    changeCounter.fetch_add (1, std::memory_order_acq_rel);
}

Pattern PatternStore::getPattern (int index) const
{
    const juce::ScopedLock sl (lock);
    return model.patterns[(size_t) juce::jlimit (0, kNumPatterns - 1, index)];
}

PatternBank PatternStore::getBank() const
{
    const juce::ScopedLock sl (lock);
    return model;
}

void PatternStore::setBank (const PatternBank& bank)
{
    const juce::ScopedLock sl (lock);
    model = bank;
    for (auto& p : model.patterns)
        p.sanitise();
    undoStack.clear();
    redoStack.clear();
    publishLocked();
}

void PatternStore::pushUndoSnapshot (int index)
{
    const juce::ScopedLock sl (lock);
    if (undoStack.size() >= kMaxUndo)
        undoStack.erase (undoStack.begin());
    undoStack.push_back ({ index, model.patterns[(size_t) index] });
    redoStack.clear();
}

void PatternStore::setPattern (int index, const Pattern& pattern)
{
    edit (index, [&pattern] (Pattern& p) { p = pattern; });
}

void PatternStore::edit (int index, const std::function<void (Pattern&)>& fn, bool pushUndo)
{
    index = juce::jlimit (0, kNumPatterns - 1, index);
    if (pushUndo)
        pushUndoSnapshot (index);

    const juce::ScopedLock sl (lock);
    fn (model.patterns[(size_t) index]);
    model.patterns[(size_t) index].sanitise();
    publishLocked();
}

bool PatternStore::undo()
{
    const juce::ScopedLock sl (lock);
    if (undoStack.empty())
        return false;

    auto snap = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back ({ snap.index, model.patterns[(size_t) snap.index] });
    model.patterns[(size_t) snap.index] = snap.pattern;
    publishLocked();
    return true;
}

bool PatternStore::redo()
{
    const juce::ScopedLock sl (lock);
    if (redoStack.empty())
        return false;

    auto snap = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back ({ snap.index, model.patterns[(size_t) snap.index] });
    model.patterns[(size_t) snap.index] = snap.pattern;
    publishLocked();
    return true;
}

bool PatternStore::canUndo() const
{
    const juce::ScopedLock sl (lock);
    return ! undoStack.empty();
}

bool PatternStore::canRedo() const
{
    const juce::ScopedLock sl (lock);
    return ! redoStack.empty();
}

} // namespace fp
