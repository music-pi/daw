#include "SetStepVelocityCommand.h"

#include "../AudioEngine.h"
#include "../SamplerInstrument.h"

SetStepVelocityCommand::SetStepVelocityCommand(
    AudioEngine& engine,
    int patternIndex,
    int padId,
    int stepIndex,
    int newVelocity)
    : engine(engine)
    , patternIndex(patternIndex)
    , padId(padId)
    , stepIndex(stepIndex)
    , newVelocity(newVelocity)
{
}

bool SetStepVelocityCommand::perform()
{
    auto& sampler = engine.getSampler();
    if (! oldVelocityCaptured)
    {
        const auto snapshots = sampler.getPadsSnapshot(
            SamplerInstrument::SnapshotContent::Patterns);
        for (const auto& pad : snapshots)
        {
            if (pad.id != padId)
                continue;
            for (const auto& pattern : pad.patterns)
            {
                if (pattern.patternIndex != patternIndex
                    || ! juce::isPositiveAndBelow(
                        stepIndex, static_cast<int>(pattern.velocities.size())))
                    continue;
                oldVelocity = pattern.velocities[static_cast<size_t>(stepIndex)];
                oldVelocityCaptured = true;
                break;
            }
            break;
        }
    }

    return oldVelocityCaptured
        && sampler.setStepVelocity(patternIndex, padId, stepIndex, newVelocity);
}

bool SetStepVelocityCommand::undo()
{
    return oldVelocityCaptured
        && engine.getSampler().setStepVelocity(
            patternIndex, padId, stepIndex, oldVelocity);
}
