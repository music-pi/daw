#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/KeyboardInstrumentBank.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

namespace
{
// Fabricate the slot-track layout the bank expects so we can exercise the
// rediscover / cursor / delete paths without loading real VST plugins.
// Mirrors the "Instruments" folder + instrumentSlot property layout the
// bank itself uses.
struct FabricatedBank
{
    te::FolderTrack::Ptr folder;
    std::vector<te::AudioTrack::Ptr> tracks;
};

FabricatedBank buildBank(te::Edit& edit, int slotCount,
                         std::function<int(int)> slotIndexMap = {})
{
    FabricatedBank fb;
    fb.folder = edit.insertNewFolderTrack(
        te::TrackInsertPoint{ nullptr, nullptr }, nullptr, false);
    if (fb.folder != nullptr) fb.folder->setName("Instruments");

    for (int i = 0; i < slotCount; ++i)
    {
        auto track = edit.insertNewAudioTrack(
            te::TrackInsertPoint{ fb.folder.get(), nullptr }, nullptr);
        if (track == nullptr) continue;
        const int idx = slotIndexMap ? slotIndexMap(i) : i;
        track->state.setProperty("instrumentSlot", idx, nullptr);
        track->state.setProperty("slotName", "Inst " + juce::String(idx + 1), nullptr);
        track->setName("Inst " + juce::String(idx + 1));
        fb.tracks.push_back(std::move(track));
    }
    return fb;
}
}

TEST(KeyboardInstrumentBankTests, AttachDiscoversExistingSlots)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    auto fb = buildBank(*edit, 3);
    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);

    EXPECT_EQ(bank.getNumSlots(), 3);
    EXPECT_EQ(bank.getActiveSlot(), 0);
}

TEST(KeyboardInstrumentBankTests, LoadsBuiltInInstrumentOnCleanInstallation)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& bank = h.audio().getKeyboardBank();
    ASSERT_EQ(bank.getNumSlots(), 0);
    ASSERT_TRUE(bank.isOnEmptySlot());

    const auto description = te::PluginManager::
        createBuiltInPluginDescription<te::FourOscPlugin>(true);
    ASSERT_TRUE(bank.loadInstrument(description));

    ASSERT_EQ(bank.getNumSlots(), 1);
    EXPECT_EQ(bank.getActiveSlot(), 0);
    EXPECT_FALSE(bank.isOnEmptySlot());
    EXPECT_NE(bank.getTrack(), nullptr);
    EXPECT_NE(bank.getMidiClip(), nullptr);
    const auto* slot = bank.getSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_NE(slot->plugin, nullptr);
    EXPECT_EQ(slot->displayName, description.name);

    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    te::FolderTrack* instruments = nullptr;
    for (auto* track : te::getTopLevelTracks(*edit))
    {
        if (auto* folder = dynamic_cast<te::FolderTrack*>(track);
            folder != nullptr && folder->getName() == "Instruments")
        {
            instruments = folder;
            break;
        }
    }
    ASSERT_NE(instruments, nullptr);
    EXPECT_NE(instruments->getVolumePlugin(), nullptr);
    EXPECT_NE(instruments->pluginList.findFirstPluginOfType<te::LevelMeterPlugin>(),
              nullptr);

    auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(slot->plugin);
    ASSERT_NE(fourOsc, nullptr);
    static constexpr int ExpectedWaveShapes[] = { 1, 3, 2, 4 };
    static constexpr float ExpectedLevelsDb[] = { -6.0f, -12.0f, -18.0f, -24.0f };
    for (int i = 0; i < 4; ++i)
    {
        const auto suffix = juce::String(i + 1);
        EXPECT_EQ(static_cast<int>(fourOsc->state.getProperty("waveShape" + suffix)),
                  ExpectedWaveShapes[i]);

        te::AutomatableParameter* level = nullptr;
        for (auto* param : fourOsc->getAutomatableParameters())
            if (param != nullptr && param->paramID == "level" + suffix)
                level = param;
        ASSERT_NE(level, nullptr);
        EXPECT_NEAR(level->getCurrentValue(), ExpectedLevelsDb[i], 0.001f);
    }

    te::AutomatableParameter* master = nullptr;
    for (auto* param : fourOsc->getAutomatableParameters())
        if (param != nullptr && param->paramID == "masterLevel")
            master = param;
    ASSERT_NE(master, nullptr);
    EXPECT_NEAR(master->getCurrentValue(), -3.0f, 0.001f);
}

TEST(KeyboardInstrumentBankTests, AttachAddsMissingInstrumentBusPlugins)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    auto fabricated = buildBank(*edit, 1);
    ASSERT_NE(fabricated.folder, nullptr);
    EXPECT_EQ(fabricated.folder->getVolumePlugin(), nullptr);
    EXPECT_EQ(fabricated.folder->pluginList
                  .findFirstPluginOfType<te::LevelMeterPlugin>(), nullptr);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);

    EXPECT_NE(fabricated.folder->getVolumePlugin(), nullptr);
    EXPECT_NE(fabricated.folder->pluginList
                  .findFirstPluginOfType<te::LevelMeterPlugin>(), nullptr);
}

TEST(KeyboardInstrumentBankTests, RediscoverSortsBySlotIndexProperty)
{
    // Regression guard: subtrack iteration order is not guaranteed to match
    // the saved slot index after in-place inserts/deletes. If the bank reads
    // them out of order, the cursor cursor no longer aligns with the
    // corresponding preset on reload.
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    // Build 3 tracks with shuffled slot indices so the track iteration
    // order differs from the index order.
    auto fb = buildBank(*edit, 3, [](int i) {
        static const int shuffled[] = { 2, 0, 1 };
        return shuffled[i];
    });

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);

    ASSERT_EQ(bank.getNumSlots(), 3);
    // After sorting by instrumentSlot, slots should be in index order 0, 1, 2.
    for (int i = 0; i < 3; ++i)
    {
        auto* slot = bank.getSlot(i);
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->displayName, "Inst " + juce::String(i + 1))
            << "slot " << i;
    }
}

TEST(KeyboardInstrumentBankTests, SetActiveSlotClampsToRange)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto fb = buildBank(*h.audio().getEdit(), 2);
    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*h.audio().getEdit());

    bank.setActiveSlot(100);
    EXPECT_EQ(bank.getActiveSlot(), 2);  // N slots → max cursor is N (empty cursor)
    bank.setActiveSlot(-5);
    EXPECT_EQ(bank.getActiveSlot(), 0);
}

TEST(KeyboardInstrumentBankTests, NextPrevSlotCyclesIncludingEmptyCursor)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto fb = buildBank(*h.audio().getEdit(), 2);
    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*h.audio().getEdit());

    bank.setActiveSlot(0);
    bank.nextSlot();  EXPECT_EQ(bank.getActiveSlot(), 1);
    bank.nextSlot();  EXPECT_EQ(bank.getActiveSlot(), 2);  // empty cursor
    bank.nextSlot();  EXPECT_EQ(bank.getActiveSlot(), 0);  // wraps to 0

    bank.prevSlot();  EXPECT_EQ(bank.getActiveSlot(), 2);
    bank.prevSlot();  EXPECT_EQ(bank.getActiveSlot(), 1);
    bank.prevSlot();  EXPECT_EQ(bank.getActiveSlot(), 0);
}

TEST(KeyboardInstrumentBankTests, DeleteOnEmptyCursorIsNoOp)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto fb = buildBank(*h.audio().getEdit(), 2);
    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*h.audio().getEdit());

    bank.setActiveSlot(2);  // empty cursor
    ASSERT_TRUE(bank.isOnEmptySlot());
    bank.deleteActiveSlot();
    EXPECT_EQ(bank.getNumSlots(), 2);  // unchanged
}

TEST(KeyboardInstrumentBankTests, GetTrackAndClipMatchActiveSlot)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto fb = buildBank(*h.audio().getEdit(), 3);
    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*h.audio().getEdit());

    bank.setActiveSlot(1);
    auto* track = bank.getTrack();
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->getName(), "Inst 2");

    // A fresh MidiClip should have been ensured on the track.
    auto* clip = bank.getMidiClip();
    EXPECT_NE(clip, nullptr);
}

TEST(KeyboardInstrumentBankTests, GetTrackReturnsNullOnEmptyCursor)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto fb = buildBank(*h.audio().getEdit(), 1);
    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*h.audio().getEdit());

    bank.setActiveSlot(1);  // virtual empty cursor
    EXPECT_EQ(bank.getTrack(), nullptr);
    EXPECT_EQ(bank.getMidiClip(), nullptr);
    EXPECT_EQ(bank.getActiveName(), juce::String());
}
