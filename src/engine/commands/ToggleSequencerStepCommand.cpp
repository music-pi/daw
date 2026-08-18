#include "ToggleSequencerStepCommand.h"
#include "../AudioEngine.h"
#include "../SamplerInstrument.h"

ToggleSequencerStepCommand::ToggleSequencerStepCommand(AudioEngine& engine, int patternIndex, int padId, int stepIndex, bool newState)
    : engine(engine)
    , patternIndex(patternIndex)
    , padId(padId)
    , stepIndex(stepIndex)
    , newState(newState)
{
}

bool ToggleSequencerStepCommand::perform()
{
    auto& sampler = engine.getSampler();

    // Capture old state on first execution
    if (!oldStateCaptured)
    {
        auto snapshots = sampler.getPadsSnapshot(
            SamplerInstrument::SnapshotContent::Patterns);
        for (const auto& pad : snapshots)
        {
            if (pad.id != padId)
                continue;

            for (const auto& pattern : pad.patterns)
            {
                if (pattern.patternIndex == patternIndex)
                {
                    if (stepIndex >= 0 && stepIndex < pattern.steps.getHighestBit() + 1)
                    {
                        oldState = pattern.steps[stepIndex];
                    }
                    oldStateCaptured = true;
                    break;
                }
            }
            if (oldStateCaptured)
                break;
        }
    }

    sampler.setStep(patternIndex, padId, stepIndex, newState);
    return true;
}

bool ToggleSequencerStepCommand::undo()
{
    if (!oldStateCaptured)
        return false;

    auto& sampler = engine.getSampler();
    sampler.setStep(patternIndex, padId, stepIndex, oldState);
    return true;
}
