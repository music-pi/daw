#include <gtest/gtest.h>
#include "../src/engine/commands/InsertEffectPluginCommand.h"
#include "../src/engine/commands/RemovePluginCommand.h"
#include "../src/engine/commands/SetPadInstrumentCommand.h"
#include "../src/engine/commands/ClearPadInstrumentCommand.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

TEST(PluginCommandTests, InsertEffectOnMasterListAddsPlugin)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& engine = harness.audio();

    // Use a TE built-in effect descriptor — guaranteed present without a scan.
    const auto desc = te::PluginManager::createBuiltInPluginDescription<te::ReverbPlugin>(false);

    auto& masterList = engine.getEdit()->getMasterPluginList();
    const int before = masterList.getPlugins().size();

    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new InsertEffectPluginCommand(engine, masterList, desc, /*index*/ before)));

    EXPECT_EQ(masterList.getPlugins().size(), before + 1);

    ASSERT_TRUE(engine.undo());
    EXPECT_EQ(masterList.getPlugins().size(), before);

    ASSERT_TRUE(engine.redo());
    EXPECT_EQ(masterList.getPlugins().size(), before + 1);
}

TEST(PluginCommandTests, RemovePluginRestoresOnUndo)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& engine = harness.audio();

    auto& masterList = engine.getEdit()->getMasterPluginList();
    const auto desc = te::PluginManager::createBuiltInPluginDescription<te::ReverbPlugin>(false);
    const int before = masterList.getPlugins().size();

    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new InsertEffectPluginCommand(engine, masterList, desc, before)));
    ASSERT_EQ(masterList.getPlugins().size(), before + 1);

    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new RemovePluginCommand(engine, masterList, before)));
    EXPECT_EQ(masterList.getPlugins().size(), before);

    ASSERT_TRUE(engine.undo());
    EXPECT_EQ(masterList.getPlugins().size(), before + 1);

    ASSERT_TRUE(engine.redo());
    EXPECT_EQ(masterList.getPlugins().size(), before);
}

TEST(PluginCommandTests, SetPadInstrumentReplacesSamplerPlugin)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& engine = harness.audio();
    auto& pads = engine.getSampler();

    // FourOsc is a TE built-in instrument — guaranteed present.
    const auto desc = te::PluginManager::createBuiltInPluginDescription<te::FourOscPlugin>(true);

    const int padIndex = 0;
    ASSERT_FALSE(pads.hasInstrument(padIndex));

    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new SetPadInstrumentCommand(engine, padIndex, desc)));

    EXPECT_TRUE(pads.hasInstrument(padIndex));

    auto* track = pads.getTrack(padIndex);
    ASSERT_NE(track, nullptr);
    const auto* instrumentPad = pads.getPad(padIndex);
    ASSERT_NE(instrumentPad, nullptr);
    EXPECT_EQ(track->getName(), instrumentPad->name);
    int samplerCount = 0;
    for (auto p : track->pluginList.getPlugins())
        if (dynamic_cast<te::SamplerPlugin*>(p)) ++samplerCount;
    EXPECT_EQ(samplerCount, 0);

    ASSERT_TRUE(engine.undo());
    EXPECT_FALSE(pads.hasInstrument(padIndex));
    const auto* restoredPad = pads.getPad(padIndex);
    ASSERT_NE(restoredPad, nullptr);
    EXPECT_EQ(restoredPad->name, "Pad 1");
    EXPECT_EQ(track->getName(), restoredPad->name);
    samplerCount = 0;
    for (auto p : track->pluginList.getPlugins())
        if (dynamic_cast<te::SamplerPlugin*>(p)) ++samplerCount;
    EXPECT_EQ(samplerCount, 1);

    ASSERT_TRUE(engine.redo());
    EXPECT_TRUE(pads.hasInstrument(padIndex));
    EXPECT_EQ(track->getName(), pads.getPad(padIndex)->name);
}

TEST(PluginCommandTests, ClearPadInstrumentRestoresSampler)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& engine = harness.audio();
    auto& pads = engine.getSampler();

    const auto desc = te::PluginManager::createBuiltInPluginDescription<te::FourOscPlugin>(true);
    const int padIndex = 0;

    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new SetPadInstrumentCommand(engine, padIndex, desc)));
    ASSERT_TRUE(pads.hasInstrument(padIndex));

    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new ClearPadInstrumentCommand(engine, padIndex)));
    EXPECT_FALSE(pads.hasInstrument(padIndex));

    auto* track = pads.getTrack(padIndex);
    int samplerCount = 0;
    for (auto p : track->pluginList.getPlugins())
        if (dynamic_cast<te::SamplerPlugin*>(p)) ++samplerCount;
    EXPECT_EQ(samplerCount, 1);

    ASSERT_TRUE(engine.undo());
    EXPECT_TRUE(pads.hasInstrument(padIndex));
}
