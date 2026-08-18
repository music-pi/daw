#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/commands/SetGainDbCommand.h"
#include "../src/engine/commands/SetPanCommand.h"
#include "harness/EngineHarness.h"

TEST(SetPanCommandTests, PerformUndoAndRedoResolveMasterAtExecutionTime)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto* volume = audio.getEdit()->getMasterVolumePlugin().get();
    ASSERT_NE(volume, nullptr);
    volume->setPan(-0.25f);

    auto& undoManager = audio.getUndoManager();
    undoManager.clearUndoHistory();
    undoManager.beginNewTransaction("Set pan");
    ASSERT_TRUE(undoManager.perform(
        new SetPanCommand(audio, "master", 0.4f)));
    EXPECT_NEAR(volume->getPan(), 0.4f, 0.001f);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_NEAR(volume->getPan(), -0.25f, 0.001f);

    ASSERT_TRUE(undoManager.redo());
    EXPECT_NEAR(volume->getPan(), 0.4f, 0.001f);
}

TEST(SetPanCommandTests, GeneralGainCommandSupportsMasterUndoAndRedo)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto* volume = audio.getEdit()->getMasterVolumePlugin().get();
    ASSERT_NE(volume, nullptr);
    volume->setVolumeDb(-3.0f);

    auto& undoManager = audio.getUndoManager();
    undoManager.clearUndoHistory();
    undoManager.beginNewTransaction("Set gain");
    ASSERT_TRUE(undoManager.perform(
        new SetGainDbCommand(audio, "master", -9.0f)));
    EXPECT_NEAR(volume->getVolumeDb(), -9.0f, 0.01f);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_NEAR(volume->getVolumeDb(), -3.0f, 0.01f);

    ASSERT_TRUE(undoManager.redo());
    EXPECT_NEAR(volume->getVolumeDb(), -9.0f, 0.01f);
}
