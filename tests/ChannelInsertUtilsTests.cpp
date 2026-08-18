#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/ChannelInsertUtils.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/commands/InsertBuiltInPluginCommand.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

TEST(ChannelInsertUtilsTests, PadEffectSlotIsAfterInstrumentAndBeforeStripInfrastructure)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto* track = harness.pads().getTrack(0);
    ASSERT_NE(track, nullptr);
    const auto plugins = track->pluginList.getPlugins();
    const int insertAt = ChannelInsertUtils::insertionIndex(track->pluginList);

    ASSERT_GT(insertAt, 0);
    ASSERT_LT(insertAt, plugins.size());
    EXPECT_NE(dynamic_cast<te::SamplerPlugin*>(plugins[insertAt - 1].get()), nullptr);
    EXPECT_TRUE(ChannelInsertUtils::isInfrastructurePlugin(plugins[insertAt].get()));
}

TEST(ChannelInsertUtilsTests, NativeEqInsertRoundTripsAtPreFaderSlot)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto* track = harness.pads().getTrack(0);
    ASSERT_NE(track, nullptr);

    const int insertAt = ChannelInsertUtils::insertionIndex(track->pluginList);
    audio.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(audio.getUndoManager().perform(new InsertBuiltInPluginCommand(
        audio, track->pluginList, te::EqualiserPlugin::xmlTypeName, insertAt)));

    auto* equaliser = ChannelInsertUtils::findEqualiser(track->pluginList);
    ASSERT_NE(equaliser, nullptr);
    EXPECT_EQ(track->pluginList.getPlugins()[insertAt].get(), equaliser);
    ASSERT_LT(insertAt + 1, track->pluginList.getPlugins().size());
    EXPECT_TRUE(ChannelInsertUtils::isInfrastructurePlugin(
        track->pluginList.getPlugins()[insertAt + 1].get()));

    equaliser->setMidFreq1(1000.0f);
    equaliser->setMidGain1(-12.0f);
    ASSERT_TRUE(audio.undo());
    EXPECT_EQ(ChannelInsertUtils::findEqualiser(track->pluginList), nullptr);

    ASSERT_TRUE(audio.redo());
    equaliser = ChannelInsertUtils::findEqualiser(track->pluginList);
    ASSERT_NE(equaliser, nullptr);
    EXPECT_NEAR(equaliser->midFreq1->getCurrentValue(), 1000.0f, 0.01f);
    EXPECT_NEAR(equaliser->midGain1->getCurrentValue(), -12.0f, 0.01f);
}

TEST(ChannelInsertUtilsTests, GroupAndMasterResolveToTheirRealProcessingLists)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto* folder = harness.pads().getFolder();
    ASSERT_NE(folder, nullptr);
    EXPECT_EQ(ChannelInsertUtils::insertionIndex(folder->pluginList), 0);

    auto& master = harness.audio().getEdit()->getMasterPluginList();
    EXPECT_EQ(ChannelInsertUtils::insertionIndex(master), 0);
}
