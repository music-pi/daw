#include <gtest/gtest.h>

#include <algorithm>

#include "../src/ui/widget/MixerWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/KeyboardInstrumentBank.h"
#include "../src/control/ControllerHost.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class MixerWidgetTests : public ::testing::Test
{
protected:
    class WindowManagerEditListener : public AudioEngine::Listener
    {
    public:
        explicit WindowManagerEditListener(WindowManager& manager) : manager_(manager) {}
        void editAboutToBeReplaced() override { manager_.notifyEditAboutToBeReplaced(); }
        void editReplaced() override { manager_.notifyEditReplaced(); }

    private:
        WindowManager& manager_;
    };

    void SetUp() override
    {
        harness.createEmptyEdit();

        wm.setAudioEngine(&harness.audio());
        harness.audio().addListener(&editListener);

        widget = std::make_unique<MixerWidget>();
        raw = widget.get();

        wm.open(std::move(widget), DisplaySide::Left);
    }

    void TearDown() override
    {
        harness.audio().removeListener(&editListener);
        wm.closeAll();
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    WindowManager wm;
    WindowManagerEditListener editListener { wm };

    std::unique_ptr<MixerWidget> widget;
    MixerWidget* raw { nullptr };
};

TEST_F(MixerWidgetTests, RebindsToReplacementEditBeforeHandlingKnob)
{
    auto* previousEdit = harness.audio().getEdit();
    ASSERT_NE(previousEdit, nullptr);

    harness.audio().createEmptyEdit();
    auto* replacementEdit = harness.audio().getEdit();
    ASSERT_NE(replacementEdit, nullptr);
    ASSERT_NE(replacementEdit, previousEdit);

    auto* replacementVolume = replacementEdit->getMasterVolumePlugin().get();
    ASSERT_NE(replacementVolume, nullptr);
    const float initialGain = replacementVolume->getVolumeDb();

    raw->handleKnob(0, 4, 0, false);

    EXPECT_GT(replacementVolume->getVolumeDb(), initialGain);
}

TEST_F(MixerWidgetTests, DescriptorIdAndPageCount)
{
    auto desc = raw->describe();
    EXPECT_EQ(desc.id, "mixer");
    EXPECT_EQ(desc.pageCount, 1);
    EXPECT_FALSE(desc.forceOnTop);
    EXPECT_EQ(desc.display, DisplayConstraint::Any);
}

TEST_F(MixerWidgetTests, RequiredResourcesIncludeModifiersAndNav)
{
    auto resources = raw->requiredResources(0);
    const auto has = [&](const std::string& n) {
        return std::find(resources.begin(), resources.end(), n) != resources.end();
    };
    EXPECT_TRUE(has("solo"));
    EXPECT_TRUE(has("muteChoke"));
    EXPECT_TRUE(has("navUp"));
    EXPECT_TRUE(has("navDown"));
    EXPECT_TRUE(has("arrowLeft"));
    EXPECT_TRUE(has("arrowRight"));
    EXPECT_TRUE(has("d1"));
    EXPECT_TRUE(has("k1"));
    EXPECT_FALSE(has("d5"));
}

TEST_F(MixerWidgetTests, RequiredResourcesInvalidPage)
{
    auto resources = raw->requiredResources(1);
    EXPECT_TRUE(resources.empty());
}

TEST_F(MixerWidgetTests, OpensAtGlobalWhenNoPadSelected)
{
    raw->onDeactivated();
    raw->onActivated(0);
    EXPECT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Global);
}

TEST_F(MixerWidgetTests, OpensAtSoundLevelWhenPadSelected)
{
    auto& pads = harness.pads();
    auto sampleFile = harness.createTemporarySampleFile("ctx.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));
    pads.selectPad(0);

    wm.closeAll();
    auto replacement = std::make_unique<MixerWidget>();
    raw = replacement.get();
    wm.open(std::move(replacement), DisplaySide::Left);
    EXPECT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Sound);
    EXPECT_TRUE(raw->getFocusedGroup().has_value());
}

TEST_F(MixerWidgetTests, DrillDownAndUp)
{
    auto& pads = harness.pads();
    auto sampleFile = harness.createTemporarySampleFile("drill.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(MixerWidget::MixerLevel::Global, std::nullopt);
    ASSERT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Global);

    // At Global, channel 0 is Master (not drillable). Activate channel 1
    // (the Sampler folder) by pressing D2 before drilling into its sounds.
    raw->handleOption(1);
    ASSERT_EQ(raw->getActiveChannelIndex(), 1);

    ControllerHost::ButtonEvent downEvt{"navDown", true, false};
    raw->handleButton(downEvt);
    EXPECT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Sound);

    ControllerHost::ButtonEvent upEvt{"navUp", true, false};
    raw->handleButton(upEvt);
    EXPECT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Global);
}

TEST_F(MixerWidgetTests, MuteModifierTogglesMuteOnDPress)
{
    auto& pads = harness.pads();
    auto sampleFile = harness.createTemporarySampleFile("m1.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(MixerWidget::MixerLevel::Sound,
                         pads.getFolder() != nullptr
                             ? std::optional<te::EditItemID>(pads.getFolder()->itemID)
                             : std::nullopt);
    juceContext.flushMessageQueue(10);

    ControllerHost::ButtonEvent hold{"muteChoke", true, false};
    raw->handleButton(hold);
    EXPECT_TRUE(raw->isMuteHeld());

    EXPECT_FALSE(pads.isMuted(0));
    raw->handleOption(0);
    EXPECT_TRUE(pads.isMuted(0));

    raw->handleOption(0);
    EXPECT_FALSE(pads.isMuted(0));

    ControllerHost::ButtonEvent release{"muteChoke", false, false};
    raw->handleButton(release);
    EXPECT_FALSE(raw->isMuteHeld());
}

TEST_F(MixerWidgetTests, SoloModifierTogglesSoloOnDPress)
{
    auto& pads = harness.pads();
    auto sampleFile = harness.createTemporarySampleFile("s1.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(MixerWidget::MixerLevel::Sound,
                         pads.getFolder() != nullptr
                             ? std::optional<te::EditItemID>(pads.getFolder()->itemID)
                             : std::nullopt);
    juceContext.flushMessageQueue(10);

    ControllerHost::ButtonEvent hold{"solo", true, false};
    raw->handleButton(hold);
    EXPECT_TRUE(raw->isSoloHeld());

    EXPECT_FALSE(pads.isSoloed(0));
    raw->handleOption(0);
    EXPECT_TRUE(pads.isSoloed(0));
}

TEST_F(MixerWidgetTests, DPressWithoutModifierChangesActiveChannel)
{
    auto& pads = harness.pads();
    auto s1 = harness.createTemporarySampleFile("a.wav", 2048, 44100.0);
    auto s2 = harness.createTemporarySampleFile("b.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, s1));
    ASSERT_TRUE(pads.loadSample(1, s2));

    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(MixerWidget::MixerLevel::Sound,
                         pads.getFolder() != nullptr
                             ? std::optional<te::EditItemID>(pads.getFolder()->itemID)
                             : std::nullopt);
    juceContext.flushMessageQueue(10);

    const bool startingMute0 = pads.isMuted(0);
    const bool startingMute1 = pads.isMuted(1);

    raw->handleOption(1);
    EXPECT_EQ(raw->getActiveChannelIndex(), 1);

    EXPECT_EQ(pads.isMuted(0), startingMute0);
    EXPECT_EQ(pads.isMuted(1), startingMute1);
}

TEST_F(MixerWidgetTests, KnobWithoutShiftAdjustsGain)
{
    auto& pads = harness.pads();
    auto sampleFile = harness.createTemporarySampleFile("g.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(MixerWidget::MixerLevel::Sound,
                         pads.getFolder() != nullptr
                             ? std::optional<te::EditItemID>(pads.getFolder()->itemID)
                             : std::nullopt);
    juceContext.flushMessageQueue(10);

    const float start = pads.getGainDb(0);
    raw->handleKnob(0, 4, 0, /*shift=*/false);
    EXPECT_GT(pads.getGainDb(0), start);
}

TEST_F(MixerWidgetTests, GainKnobGestureUndoesAllDeltasInOneStep)
{
    auto& pads = harness.pads();
    auto sampleFile = harness.createTemporarySampleFile(
        "gain_gesture.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(
        MixerWidget::MixerLevel::Sound,
        pads.getFolder() != nullptr
            ? std::optional<te::EditItemID>(pads.getFolder()->itemID)
            : std::nullopt);

    auto& undoManager = harness.audio().getUndoManager();
    undoManager.clearUndoHistory();
    const float start = pads.getGainDb(0);
    raw->handleKnob(0, 4, 0, false);
    raw->handleKnob(0, 4, 0, false);
    ASSERT_GT(pads.getGainDb(0), start);

    auto knobs = raw->getKnobs(0);
    ASSERT_FALSE(knobs.empty());
    ASSERT_TRUE(static_cast<bool>(knobs[0].onInputResolved));
    knobs[0].onInputResolved(pads.getGainDb(0));

    ASSERT_TRUE(undoManager.undo());
    EXPECT_NEAR(pads.getGainDb(0), start, 0.01f);
    EXPECT_FALSE(undoManager.canUndo());
}

TEST_F(MixerWidgetTests, KnobWithShiftAdjustsPan)
{
    auto& pads = harness.pads();
    auto sampleFile = harness.createTemporarySampleFile("p.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(MixerWidget::MixerLevel::Sound,
                         pads.getFolder() != nullptr
                             ? std::optional<te::EditItemID>(pads.getFolder()->itemID)
                             : std::nullopt);
    juceContext.flushMessageQueue(10);

    auto* track = pads.getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* volume = track->getVolumePlugin();
    ASSERT_NE(volume, nullptr);

    const float startPan = volume->getPan();
    raw->handleKnob(0, 5, 0, /*shift=*/true);
    EXPECT_GT(volume->getPan(), startPan);
}

TEST_F(MixerWidgetTests, ArrowScrollPagesBy4)
{
    auto& pads = harness.pads();
    for (int i = 0; i < 6; ++i)
    {
        auto f = harness.createTemporarySampleFile(
            juce::String("scroll") + juce::String(i) + ".wav", 2048, 44100.0);
        ASSERT_TRUE(pads.loadSample(i, f));
    }
    raw->onDeactivated();
    raw->onActivated(0);
    raw->setLevelForTest(MixerWidget::MixerLevel::Sound,
                         pads.getFolder() != nullptr
                             ? std::optional<te::EditItemID>(pads.getFolder()->itemID)
                             : std::nullopt);
    juceContext.flushMessageQueue(10);

    EXPECT_EQ(raw->getScrollOffset(), 0);

    ControllerHost::ButtonEvent right{"arrowRight", true, false};
    raw->handleButton(right);
    EXPECT_EQ(raw->getScrollOffset(), 4);
    EXPECT_EQ(pads.getSelectedPad(), 4);

    ControllerHost::ButtonEvent left{"arrowLeft", true, false};
    raw->handleButton(left);
    EXPECT_EQ(raw->getScrollOffset(), 0);
    EXPECT_EQ(pads.getSelectedPad(), 0);
}

TEST_F(MixerWidgetTests, SelectingSoundFollowsTheSamplerPad)
{
    auto& pads = harness.pads();
    auto first = harness.createTemporarySampleFile("select-a.wav", 2048, 44100.0);
    auto second = harness.createTemporarySampleFile("select-b.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, first));
    ASSERT_TRUE(pads.loadSample(1, second));
    pads.selectPad(0);

    raw->setLevelForTest(MixerWidget::MixerLevel::Sound,
                         pads.getFolder()->itemID);
    raw->handleOption(1);

    EXPECT_EQ(pads.getSelectedPad(), 1);
}

TEST_F(MixerWidgetTests, PreferredInstrumentContextOpensAtActiveSound)
{
    auto& bank = harness.audio().getKeyboardBank();
    const auto description = te::PluginManager::
        createBuiltInPluginDescription<te::FourOscPlugin>(true);
    ASSERT_TRUE(bank.loadInstrument(description));
    bank.nextSlot();
    ASSERT_TRUE(bank.loadInstrument(description));
    ASSERT_EQ(bank.getActiveSlot(), 1);
    ASSERT_NE(bank.getFolder(), nullptr);
    ASSERT_NE(bank.getTrack(), nullptr);

    const auto groupId = bank.getFolder()->itemID;
    const auto channelId = bank.getTrack()->itemID;
    wm.closeAll();
    auto replacement = std::make_unique<MixerWidget>(groupId, channelId);
    raw = replacement.get();
    wm.open(std::move(replacement), DisplaySide::Left);

    EXPECT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Sound);
    EXPECT_EQ(raw->getFocusedGroup(), groupId);
    EXPECT_EQ(raw->getActiveChannelIndex(), 1);
    EXPECT_TRUE(raw->getTitleSubtitle().contains("Instruments"));

    raw->handleOption(0);
    EXPECT_EQ(bank.getActiveSlot(), 0);

    auto* instrumentTrack = bank.getTrack();
    ASSERT_NE(instrumentTrack, nullptr);
    auto* volume = instrumentTrack->getVolumePlugin();
    ASSERT_NE(volume, nullptr);
    const float startingGain = volume->getVolumeDb();
    raw->handleKnob(0, 4, 0, false);
    EXPECT_GT(volume->getVolumeDb(), startingGain);

    ControllerHost::ButtonEvent muteDown { "muteChoke", true, false };
    ControllerHost::ButtonEvent muteUp { "muteChoke", false, false };
    raw->handleButton(muteDown);
    raw->handleOption(0);
    raw->handleButton(muteUp);
    EXPECT_TRUE(instrumentTrack->isMuted(false));
}

TEST_F(MixerWidgetTests, ReactivationPreservesInstrumentSoundAndPage)
{
    auto& bank = harness.audio().getKeyboardBank();
    const auto description = te::PluginManager::
        createBuiltInPluginDescription<te::FourOscPlugin>(true);
    for (int slot = 0; slot < 5; ++slot)
    {
        ASSERT_TRUE(bank.loadInstrument(description));
        if (slot < 4)
            bank.nextSlot();
    }
    ASSERT_EQ(bank.getActiveSlot(), 4);
    ASSERT_NE(bank.getFolder(), nullptr);
    ASSERT_NE(bank.getTrack(), nullptr);

    wm.closeAll();
    auto replacement = std::make_unique<MixerWidget>(
        bank.getFolder()->itemID, bank.getTrack()->itemID);
    raw = replacement.get();
    wm.open(std::move(replacement), DisplaySide::Left);
    ASSERT_EQ(raw->getScrollOffset(), 4);

    auto stashed = wm.takeWidget(DisplaySide::Left);
    ASSERT_NE(stashed, nullptr);
    wm.open(std::move(stashed), DisplaySide::Left);

    EXPECT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Sound);
    EXPECT_EQ(raw->getFocusedGroup(), bank.getFolder()->itemID);
    EXPECT_EQ(raw->getScrollOffset(), 4);
    EXPECT_EQ(raw->getActiveChannelIndex(), 0);
    EXPECT_EQ(bank.getActiveSlot(), 4);
}

TEST_F(MixerWidgetTests, ReactivationPreservesGlobalLevelAndPage)
{
    auto& pads = harness.pads();
    auto sample = harness.createTemporarySampleFile(
        "global-context.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sample));
    pads.selectPad(0);

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    for (int i = 0; i < 5; ++i)
    {
        auto folder = edit->insertNewFolderTrack(
            te::TrackInsertPoint { nullptr, nullptr }, nullptr, false);
        ASSERT_NE(folder, nullptr);
        folder->setName("Group " + juce::String(i + 1));
    }

    raw->setLevelForTest(MixerWidget::MixerLevel::Global, std::nullopt);
    ControllerHost::ButtonEvent right { "arrowRight", true, false };
    raw->handleButton(right);
    ASSERT_EQ(raw->getScrollOffset(), 4);

    auto stashed = wm.takeWidget(DisplaySide::Left);
    ASSERT_NE(stashed, nullptr);
    wm.open(std::move(stashed), DisplaySide::Left);

    EXPECT_EQ(raw->getLevel(), MixerWidget::MixerLevel::Global);
    EXPECT_EQ(raw->getScrollOffset(), 4);
    EXPECT_EQ(raw->getActiveChannelIndex(), 0);
    EXPECT_EQ(pads.getSelectedPad(), 0);
}

TEST_F(MixerWidgetTests, TrackRemovalIsSafeBeforeTickOrDeactivation)
{
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto track = edit->insertNewAudioTrack(
        te::TrackInsertPoint { nullptr, nullptr }, nullptr);
    ASSERT_NE(track, nullptr);
    track->setName("Temporary track");
    auto meter = edit->getPluginCache().createNewPlugin(
        te::LevelMeterPlugin::xmlTypeName, {});
    ASSERT_NE(meter, nullptr);
    track->pluginList.insertPlugin(meter, -1, nullptr);

    raw->setLevelForTest(MixerWidget::MixerLevel::Global, std::nullopt);
    raw->onUiHostTick();
    const int countBeforeRemoval = raw->getChannelCountForTest();
    ASSERT_GT(countBeforeRemoval, 1);

    edit->deleteTrack(track.get());
    track = nullptr;
    auto stashed = wm.takeWidget(DisplaySide::Left);
    ASSERT_NE(stashed, nullptr);
    wm.open(std::move(stashed), DisplaySide::Left);

    EXPECT_EQ(raw->getChannelCountForTest(), countBeforeRemoval - 1);

    auto secondTrack = edit->insertNewAudioTrack(
        te::TrackInsertPoint { nullptr, nullptr }, nullptr);
    ASSERT_NE(secondTrack, nullptr);
    secondTrack->setName("Second temporary track");
    auto secondMeter = edit->getPluginCache().createNewPlugin(
        te::LevelMeterPlugin::xmlTypeName, {});
    ASSERT_NE(secondMeter, nullptr);
    secondTrack->pluginList.insertPlugin(secondMeter, -1, nullptr);
    raw->setLevelForTest(MixerWidget::MixerLevel::Global, std::nullopt);
    raw->onUiHostTick();
    const int secondCountBeforeRemoval = raw->getChannelCountForTest();

    edit->deleteTrack(secondTrack.get());
    secondTrack = nullptr;
    raw->onUiHostTick();

    EXPECT_EQ(raw->getChannelCountForTest(), secondCountBeforeRemoval - 1);
}

TEST_F(MixerWidgetTests, ShortFinalPageStartsAtNextMultipleOfFour)
{
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto folder = edit->insertNewFolderTrack(
        te::TrackInsertPoint { nullptr, nullptr }, nullptr, false);
    ASSERT_NE(folder, nullptr);
    folder->setName("Six Sounds");
    for (int i = 0; i < 6; ++i)
    {
        auto track = edit->insertNewAudioTrack(
            te::TrackInsertPoint { folder.get(), nullptr }, nullptr);
        ASSERT_NE(track, nullptr);
        track->setName("Sound " + juce::String(i + 1));
    }

    wm.closeAll();
    auto replacement = std::make_unique<MixerWidget>(folder->itemID);
    raw = replacement.get();
    wm.open(std::move(replacement), DisplaySide::Left);

    ControllerHost::ButtonEvent right { "arrowRight", true, false };
    raw->handleButton(right);
    EXPECT_EQ(raw->getScrollOffset(), 4);
    EXPECT_EQ(raw->getActiveChannelIndex(), 0);
}

TEST_F(MixerWidgetTests, LifecycleActivateDeactivate)
{
    raw->onDeactivated();
    raw->onActivated(0);
}

TEST_F(MixerWidgetTests, DestructorStopsTimer)
{
    auto temp = std::make_unique<MixerWidget>();
    temp->setAudioEngine(&harness.audio());
    temp->setHardware(&wm.getHardwareState());
    temp->onActivated(0);
    temp.reset();
}
