#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/commands/SetStepVelocityCommand.h"
#include "harness/EngineHarness.h"

namespace
{
int stepVelocity(SamplerInstrument& sampler,
                 int patternIndex,
                 int padId,
                 int stepIndex)
{
    const auto snapshots = sampler.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    for (const auto& pad : snapshots)
    {
        if (pad.id != padId)
            continue;
        for (const auto& pattern : pad.patterns)
            if (pattern.patternIndex == patternIndex
                && juce::isPositiveAndBelow(
                    stepIndex, static_cast<int>(pattern.velocities.size())))
                return pattern.velocities[static_cast<size_t>(stepIndex)];
    }
    return 0;
}
}

TEST(SetStepVelocityCommandTests, PerformUndoAndRedoPreservePreviousVelocity)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& sampler = harness.pads();
    constexpr int patternIndex = 0;
    constexpr int padId = 0;
    constexpr int stepIndex = 3;
    ASSERT_TRUE(sampler.setStep(patternIndex, padId, stepIndex, true));
    ASSERT_TRUE(sampler.setStepVelocity(patternIndex, padId, stepIndex, 64));

    auto& undoManager = harness.audio().getUndoManager();
    undoManager.clearUndoHistory();
    undoManager.beginNewTransaction("Set step velocity");
    ASSERT_TRUE(undoManager.perform(new SetStepVelocityCommand(
        harness.audio(), patternIndex, padId, stepIndex, 111)));
    EXPECT_EQ(stepVelocity(sampler, patternIndex, padId, stepIndex), 111);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(stepVelocity(sampler, patternIndex, padId, stepIndex), 64);

    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(stepVelocity(sampler, patternIndex, padId, stepIndex), 111);
}
