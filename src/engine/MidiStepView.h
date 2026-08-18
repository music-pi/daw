#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/** Stateless view over a te::MidiList that renders it as a step grid at a
    specific root pitch. Does not own the MidiList.

    Two construction modes:
    - Writable: pass a mutable `te::MidiList&`. All read + write operations
      work; writes mutate the MidiList in place with a null UndoManager
      (callers that need undo wrap in a command that re-invokes the same API).
    - Read-only: pass a `const te::MidiList&`. Only the const accessors
      (isStepOn, getStepVelocity, getNumSteps) are valid; calling setStep()
      trips a runtime assertion and does nothing.

    The read-only constructor lets callers on a `const te::MidiList&` drop
    the `const_cast` they'd otherwise need to instantiate the view. */
class MidiStepView
{
public:
    MidiStepView (te::MidiList& list,
                  int rootPitch,
                  int stepsPerBar,
                  int bars);

    MidiStepView (const te::MidiList& list,
                  int rootPitch,
                  int stepsPerBar,
                  int bars);

    bool isStepOn (int stepIndex) const;

    /** Sets a step on or off. On sets a note at (stepIndex * stepDuration)
        at rootPitch with the given velocity and duration = stepDuration/2.
        Off removes the note at that step whose pitch == rootPitch. Asserts
        and no-ops if the view was constructed on a const MidiList. */
    void setStep (int stepIndex, bool on, int velocity);

    /** Returns 0 if the step is off. */
    int getStepVelocity (int stepIndex) const;

    int getNumSteps() const { return stepsPerBar_ * bars_; }

private:
    const te::MidiList* readList_ { nullptr };
    te::MidiList* writeList_ { nullptr };  // nullptr => read-only view
    int rootPitch_;
    int stepsPerBar_;
    int bars_;

    te::MidiNote* findRootNoteAtStep (int stepIndex) const;
    tracktion::BeatPosition stepBeatPosition (int stepIndex) const;
    tracktion::BeatDuration stepDuration() const;
};
