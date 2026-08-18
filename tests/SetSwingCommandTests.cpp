#include <gtest/gtest.h>
#include "../src/engine/commands/SetSwingCommand.h"
#include "../src/engine/AudioEngine.h"
#include "harness/EngineHarness.h"

TEST(SetSwingCommandTests, PerformAndUndoRoundTrip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto& um = audio.getUndoManager();

    audio.setSwingPercent(50.0);
    um.beginNewTransaction("Set Swing");
    EXPECT_TRUE(um.perform(new SetSwingCommand(audio, 50.0, 60.0)));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 60.0);

    EXPECT_TRUE(um.undo());
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 50.0);
}

TEST(SetSwingCommandTests, ConsecutiveAdjustmentsCoalesce)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto& um = audio.getUndoManager();

    audio.setSwingPercent(50.0);
    um.beginNewTransaction("Set Swing");
    um.perform(new SetSwingCommand(audio, 50.0, 51.0));
    um.perform(new SetSwingCommand(audio, 51.0, 52.0));
    um.perform(new SetSwingCommand(audio, 52.0, 53.0));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 53.0);

    EXPECT_TRUE(um.undo());
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 50.0);
}
