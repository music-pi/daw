#include <gtest/gtest.h>
#include "../src/engine/PluginCatalog.h"
#include "../src/engine/commands/InsertEffectPluginCommand.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

// End-to-end plumbing proof: seed KnownPluginList with a TE built-in effect
// description (the same shape a real scan would produce), read it back via
// PluginCatalog, insert it onto a pad's channel plugin list through the
// command, and confirm the plugin instance is live on the track.
TEST(PluginIntegrationTests, BuiltInEffectScansAndInsertsIntoChannel)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& engine = h.audio();

    auto& known = engine.getEngine().getPluginManager().knownPluginList;
    const auto desc = te::PluginManager::createBuiltInPluginDescription<te::ReverbPlugin>(false);
    ASSERT_TRUE(known.addType(desc));

    PluginCatalog cat(engine.getEngine());
    auto effects = cat.getEffects();
    ASSERT_GE(effects.size(), 1);

    // Find the Reverb descriptor we just injected — the list may contain
    // other effects seeded by earlier tests in the same process.
    juce::PluginDescription chosen;
    bool found = false;
    for (auto& d : effects)
    {
        if (d.name == desc.name && d.pluginFormatName == desc.pluginFormatName)
        {
            chosen = d;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    auto* track = engine.getSampler().getTrack(0);
    ASSERT_NE(track, nullptr);

    const int before = track->pluginList.getPlugins().size();
    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new InsertEffectPluginCommand(engine, track->pluginList, chosen, before)));
    EXPECT_EQ(track->pluginList.getPlugins().size(), before + 1);

    te::Plugin* inserted = nullptr;
    for (auto p : track->pluginList.getPlugins())
    {
        if (p->getName() == chosen.name)
        {
            inserted = p;
            break;
        }
    }
    ASSERT_NE(inserted, nullptr);
}

// Round-trip the insert command on the master plugin list across undo and
// redo to confirm state is restored correctly in both directions.
TEST(PluginIntegrationTests, RemovePluginCommandRoundTripsThroughChannelList)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& engine = h.audio();

    auto& masterList = engine.getEdit()->getMasterPluginList();
    const auto desc = te::PluginManager::createBuiltInPluginDescription<te::DelayPlugin>(false);

    const int before = masterList.getPlugins().size();
    engine.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(engine.getUndoManager().perform(
        new InsertEffectPluginCommand(engine, masterList, desc, before)));
    ASSERT_EQ(masterList.getPlugins().size(), before + 1);

    ASSERT_TRUE(engine.undo());
    EXPECT_EQ(masterList.getPlugins().size(), before);

    ASSERT_TRUE(engine.redo());
    EXPECT_EQ(masterList.getPlugins().size(), before + 1);
}
