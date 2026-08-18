#include <gtest/gtest.h>

#include "../src/engine/commands/FillEuclideanCommand.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

namespace
{
bool stepHit(const SamplerInstrument::PadSnapshot& pad,
              int patternIndex, int step)
{
    for (const auto& ps : pad.patterns)
        if (ps.patternIndex == patternIndex)
            return ps.steps[step];
    return false;
}
}

TEST(FillEuclideanCommandTests, Fills8Of16StraightEighths)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& audio = h.audio();
    auto& sampler = h.pads();

    auto cmd = std::make_unique<FillEuclideanCommand>(
        audio, /*pattern*/ 0, /*pad*/ 0, /*pulses*/ 8, /*rotation*/ 0);
    EXPECT_TRUE(audio.getUndoManager().perform(cmd.release()));

    auto snap = sampler.getPadsSnapshot();
    ASSERT_FALSE(snap.empty());
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(stepHit(snap[0], 0, i), (i % 2 == 0)) << "step " << i;
}

TEST(FillEuclideanCommandTests, UndoRestoresPriorPattern)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& audio = h.audio();
    auto& sampler = h.pads();

    // Start with an off-beat hit on step 2. Force a transaction boundary
    // so this setup step isn't coalesced into the command's transaction
    // when we undo later.
    ASSERT_TRUE(sampler.setStep(0, 0, 2, true));
    audio.getUndoManager().beginNewTransaction();

    auto cmd = std::make_unique<FillEuclideanCommand>(
        audio, 0, 0, /*pulses*/ 4, /*rotation*/ 0);
    EXPECT_TRUE(audio.getUndoManager().perform(cmd.release()));

    // Pattern should now be euclidean(4, 16) — not just step 2.
    auto snap = sampler.getPadsSnapshot();
    EXPECT_TRUE(stepHit(snap[0], 0, 0));    // euclidean always starts with a hit

    EXPECT_TRUE(audio.getUndoManager().undo());

    snap = sampler.getPadsSnapshot();
    EXPECT_TRUE(stepHit(snap[0], 0, 2));    // the user's prior hit is back
    EXPECT_FALSE(stepHit(snap[0], 0, 0));   // the generator's hit is gone
}
