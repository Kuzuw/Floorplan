#pragma once

#include "Audio/TripleBuffer.h"
#include "Sequencer/Pattern.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

namespace fp
{

// Owns the editable pattern bank and publishes it to the audio thread.
//
// Threads:
//  - Message thread edits through edit()/undo()/redo().
//  - Host state calls (get/setStateInformation) may arrive on other non-audio
//    threads, so the model is guarded by a CriticalSection. The audio thread
//    never takes that lock: it reads the latest published copy via
//    acquireForAudio(), backed by a wait-free triple buffer.
class PatternStore
{
public:
    PatternStore();

    // Non-audio threads ------------------------------------------------------
    Pattern getPattern (int index) const;
    PatternBank getBank() const;
    void setBank (const PatternBank& bank);                   // clears undo history
    void setPattern (int index, const Pattern& pattern);      // undoable
    void edit (int index, const std::function<void (Pattern&)>& fn, bool pushUndo = true);
    void pushUndoSnapshot (int index);                        // for drag gestures

    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // Incremented on every change, so editors can cheaply detect updates.
    uint32_t version() const noexcept { return changeCounter.load (std::memory_order_acquire); }

    // Audio thread -----------------------------------------------------------
    const PatternBank& acquireForAudio() noexcept { return realtime.acquire(); }

    static constexpr size_t kMaxUndo = 128;

private:
    struct Snapshot { int index; Pattern pattern; };

    void publishLocked();

    mutable juce::CriticalSection lock;
    PatternBank model;
    TripleBuffer<PatternBank> realtime;
    std::vector<Snapshot> undoStack, redoStack;
    std::atomic<uint32_t> changeCounter { 0 };
};

} // namespace fp
