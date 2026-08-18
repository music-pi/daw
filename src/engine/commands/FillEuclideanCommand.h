#pragma once

#include <array>

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

/** Fill a pattern's steps for one pad with an Euclidean distribution
    (N pulses across K steps, optionally rotated). Overwrites whatever was
    on those steps; undoable — captures the prior step mask + restores it
    on undo. */
class FillEuclideanCommand : public juce::UndoableAction
{
public:
    FillEuclideanCommand(AudioEngine& engine,
                         int patternIndex,
                         int padIndex,
                         int pulses,
                         int rotation,
                         int velocity = 100);

    bool perform() override;
    bool undo() override;
    int  getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Euclidean Fill"; }

private:
    AudioEngine& engine_;
    const int patternIndex_;
    const int padIndex_;
    const int pulses_;
    const int rotation_;
    const int velocity_;

    // Step state captured pre-perform for undo. std::array keeps the command
    // trivially-copyable and bounds the memory footprint regardless of
    // stepCount — the sampler caps at 64 steps per pattern.
    static constexpr int kMaxSteps = 64;
    std::array<bool, kMaxSteps> priorSteps_ {};
    std::array<int,  kMaxSteps> priorVelocities_ {};
    int priorStepCount_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FillEuclideanCommand)
};
