#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/ChannelInsertUtils.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/commands/InsertBuiltInPluginCommand.h"
#include "../src/engine/commands/InsertEffectPluginCommand.h"
#include "../src/ui/widget/ChannelDetailsWidget.h"
#include "../src/ui/widget/MixerStateKeys.h"
#include "harness/EngineHarness.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace
{
    // Activate the widget against the master channel. The widget reads the
    // mixer ValueTree to pick an active channel; pointing it at "master" is
    // the simplest setup that guarantees a non-null target PluginList
    // (edit.getMasterPluginList()).
    void activateOnMaster(ChannelDetailsWidget& w, AudioEngine& audio)
    {
        w.setAudioEngine(&audio);
        auto mixerState = audio.getMixerState();
        ASSERT_TRUE(mixerState.isValid());
        mixerState.setProperty(MixerStateKeys::Level, "global", nullptr);
        mixerState.setProperty(MixerStateKeys::ActiveChannel, "master", nullptr);
        w.onActivated(0);
    }
}

TEST(ChannelDetailsPluginInsertTests, AddInsertFiresCallbackWithMasterPluginList)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();

    ChannelDetailsWidget w;
    ASSERT_NO_FATAL_FAILURE(activateOnMaster(w, audio));

    te::PluginList* seen = nullptr;
    w.setOnAddInsertRequested([&](te::PluginList* list) { seen = list; });

    w.invokeAddInsertForTesting();
    ASSERT_NE(seen, nullptr);
    EXPECT_EQ(seen, &audio.getEdit()->getMasterPluginList());
}

TEST(ChannelDetailsPluginInsertTests, SelectedEffectLandsOnChannelList)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();

    ChannelDetailsWidget w;
    ASSERT_NO_FATAL_FAILURE(activateOnMaster(w, audio));

    te::PluginList* target = nullptr;
    w.setOnAddInsertRequested([&](te::PluginList* list) { target = list; });
    w.invokeAddInsertForTesting();
    ASSERT_NE(target, nullptr);

    const int before = target->getPlugins().size();

    // Simulate the downstream path UiHost will run when the browser
    // callback delivers a PluginDescription.
    const auto desc = te::PluginManager::createBuiltInPluginDescription<te::ReverbPlugin>(false);
    audio.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(audio.getUndoManager().perform(
        new InsertEffectPluginCommand(audio, *target, desc, before)));

    EXPECT_EQ(target->getPlugins().size(), before + 1);
}

TEST(ChannelDetailsPluginInsertTests, AddInsertNoopWhenNoCallback)
{
    // Guard the happy-path from crashing when no callback is registered yet
    // (e.g. widget instantiated but not yet wired by UiHost).
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();

    ChannelDetailsWidget w;
    activateOnMaster(w, audio);
    EXPECT_NO_FATAL_FAILURE(w.invokeAddInsertForTesting());
}

TEST(ChannelDetailsPluginInsertTests, EqRequestTargetsMasterProcessingList)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();

    ChannelDetailsWidget widget;
    ASSERT_NO_FATAL_FAILURE(activateOnMaster(widget, audio));

    te::PluginList* seenList = nullptr;
    te::EqualiserPlugin* seenEq = reinterpret_cast<te::EqualiserPlugin*>(1);
    widget.setOnEqRequested([&](te::PluginList* list, te::EqualiserPlugin* equaliser) {
        seenList = list;
        seenEq = equaliser;
    });
    widget.invokeEqForTesting();

    EXPECT_EQ(seenList, &audio.getEdit()->getMasterPluginList());
    EXPECT_EQ(seenEq, nullptr);
}

TEST(ChannelDetailsPluginInsertTests, ExistingMasterEqIsOfferedForOpening)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto& list = audio.getEdit()->getMasterPluginList();
    const int insertAt = ChannelInsertUtils::insertionIndex(list);
    audio.getUndoManager().beginNewTransaction();
    ASSERT_TRUE(audio.getUndoManager().perform(new InsertBuiltInPluginCommand(
        audio, list, te::EqualiserPlugin::xmlTypeName, insertAt)));
    auto* expected = ChannelInsertUtils::findEqualiser(list);
    ASSERT_NE(expected, nullptr);

    ChannelDetailsWidget widget;
    ASSERT_NO_FATAL_FAILURE(activateOnMaster(widget, audio));
    te::EqualiserPlugin* seen = nullptr;
    widget.setOnEqRequested(
        [&](te::PluginList*, te::EqualiserPlugin* equaliser) { seen = equaliser; });

    const auto options = widget.getOptions(0);
    ASSERT_EQ(options.size(), 2u);
    EXPECT_EQ(options[1].label, "Open EQ");
    widget.invokeEqForTesting();
    EXPECT_EQ(seen, expected);
}

TEST(ChannelDetailsPluginInsertTests, BuiltInInsertUndoUsesTheClampedPluginIdentity)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto& list = audio.getEdit()->getMasterPluginList();
    const int initialSize = list.getPlugins().size();

    auto& undo = audio.getUndoManager();
    undo.beginNewTransaction();
    ASSERT_TRUE(undo.perform(new InsertBuiltInPluginCommand(
        audio, list, te::EqualiserPlugin::xmlTypeName, 999)));
    ASSERT_EQ(list.getPlugins().size(), initialSize + 1);
    EXPECT_NE(ChannelInsertUtils::findEqualiser(list), nullptr);

    EXPECT_TRUE(undo.undo());
    EXPECT_EQ(list.getPlugins().size(), initialSize);
    EXPECT_TRUE(undo.redo());
    EXPECT_EQ(list.getPlugins().size(), initialSize + 1);
    EXPECT_NE(ChannelInsertUtils::findEqualiser(list), nullptr);
}

TEST(ChannelDetailsPluginInsertTests, EqRequestResolvesGroupAndPadProcessingLists)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& audio = harness.audio();
    auto* folder = harness.pads().getFolder();
    auto* padTrack = harness.pads().getTrack(0);
    ASSERT_NE(folder, nullptr);
    ASSERT_NE(padTrack, nullptr);

    auto state = audio.getMixerState();
    state.setProperty(MixerStateKeys::Level, "global", nullptr);
    state.setProperty(MixerStateKeys::ActiveChannel, folder->itemID.toString(), nullptr);

    ChannelDetailsWidget groupWidget;
    groupWidget.setAudioEngine(&audio);
    groupWidget.onActivated(0);
    te::PluginList* groupList = nullptr;
    groupWidget.setOnEqRequested(
        [&](te::PluginList* list, te::EqualiserPlugin*) { groupList = list; });
    groupWidget.invokeEqForTesting();
    EXPECT_EQ(groupList, &folder->pluginList);
    groupWidget.onDeactivated();

    state.setProperty(MixerStateKeys::Level, "group", nullptr);
    state.setProperty(MixerStateKeys::FocusedGroup, folder->itemID.toString(), nullptr);
    state.setProperty(MixerStateKeys::ActiveChannel, padTrack->itemID.toString(), nullptr);

    ChannelDetailsWidget padWidget;
    padWidget.setAudioEngine(&audio);
    padWidget.onActivated(0);
    te::PluginList* padList = nullptr;
    padWidget.setOnEqRequested(
        [&](te::PluginList* list, te::EqualiserPlugin*) { padList = list; });
    padWidget.invokeEqForTesting();
    EXPECT_EQ(padList, &padTrack->pluginList);
    padWidget.onDeactivated();
}
