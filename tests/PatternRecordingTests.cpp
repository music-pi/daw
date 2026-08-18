#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"

#include "../src/ui/widget/PatternWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class PatternRecordingTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        wm.setAudioEngine(&harness.audio());
    }

    PatternWidget* openPatternWidget()
    {
        wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);
        return static_cast<PatternWidget*>(wm.getWidget(DisplaySide::Left));
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    WindowManager wm;
};

TEST_F(PatternRecordingTests, StepModeOffWithNoSamples)
{
    auto* pw = openPatternWidget();
    ASSERT_NE(pw, nullptr);
    EXPECT_FALSE(pw->isStepModeActive());
}

TEST_F(PatternRecordingTests, LoadedSamplesDoNotAutoEnableStepMode)
{
    auto sample = harness.createTemporarySampleFile("auto_test", 44100);
    harness.pads().loadSample(0, sample);

    auto* pw = openPatternWidget();
    ASSERT_NE(pw, nullptr);
    EXPECT_FALSE(pw->isStepModeActive());
}

TEST_F(PatternRecordingTests, ToggleStepModeOnOff)
{
    auto sample = harness.createTemporarySampleFile("toggle_test", 44100);
    harness.pads().loadSample(0, sample);

    auto* pw = openPatternWidget();
    ASSERT_NE(pw, nullptr);
    EXPECT_FALSE(pw->isStepModeActive());

    pw->toggleStepMode();
    EXPECT_TRUE(pw->isStepModeActive());

    pw->toggleStepMode();
    EXPECT_FALSE(pw->isStepModeActive());
}

TEST_F(PatternRecordingTests, StepModePadEditsGridWhileRecordArmedAndPlaying)
{
    auto sample = harness.createTemporarySampleFile("step_record_guard", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));

    auto* pw = openPatternWidget();
    ASSERT_NE(pw, nullptr);
    pw->toggleStepMode();
    ASSERT_TRUE(pw->isStepModeActive());

    harness.audio().setRecordArmed(true);
    harness.audio().getEdit()->getTransport().play(false);
    ASSERT_TRUE(harness.audio().isPlaying());

    ControllerHost::PadEvent press;
    press.pad = 5;
    press.pressed = true;
    press.pressure = 12000;
    pw->handlePad(press);

    const auto snapshots = harness.pads().getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    ASSERT_FALSE(snapshots.empty());
    ASSERT_FALSE(snapshots[0].patterns.empty());
    EXPECT_TRUE(snapshots[0].patterns[0].steps[5])
        << "Step-mode press must edit the selected channel at the pressed step";
    ASSERT_GT(snapshots.size(), 5u);
    ASSERT_FALSE(snapshots[5].patterns.empty());
    EXPECT_FALSE(snapshots[5].patterns[0].steps[0])
        << "Step-mode press must not record the pressed pad at the playhead";
}

TEST_F(PatternRecordingTests, RemovingLaterStepDoesNotRemoveFirstStep)
{
    auto sample = harness.createTemporarySampleFile("step_removal_isolation", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));

    auto* pw = openPatternWidget();
    ASSERT_NE(pw, nullptr);
    pw->toggleStepMode();
    ASSERT_TRUE(pw->isStepModeActive());

    const auto pressStep = [pw](int step)
    {
        ControllerHost::PadEvent press;
        press.pad = static_cast<uint32_t>(step);
        press.pressed = true;
        press.pressure = 12000;
        pw->handlePad(press);
    };
    const auto stepIsOn = [this](int step)
    {
        const auto snapshots = harness.pads().getPadsSnapshot(
            SamplerInstrument::SnapshotContent::Patterns);
        return !snapshots.empty()
            && !snapshots[0].patterns.empty()
            && snapshots[0].patterns[0].steps[step];
    };

    pressStep(0);
    pressStep(14);
    ASSERT_TRUE(stepIsOn(0));
    ASSERT_TRUE(stepIsOn(14));

    pressStep(14);
    EXPECT_TRUE(stepIsOn(0))
        << "Removing step 15 must preserve step 1";
    EXPECT_FALSE(stepIsOn(14));

    if (!stepIsOn(0))
        pressStep(0);
    pressStep(5);
    ASSERT_TRUE(stepIsOn(0));
    ASSERT_TRUE(stepIsOn(5));

    pressStep(5);
    EXPECT_TRUE(stepIsOn(0))
        << "Removing step 6 must preserve step 1";
    EXPECT_FALSE(stepIsOn(5));
}

TEST_F(PatternRecordingTests, EraseSelectedChannel)
{
    auto sample = harness.createTemporarySampleFile("erase_test", 44100);
    harness.pads().loadSample(0, sample);

    auto* pw = openPatternWidget();
    ASSERT_NE(pw, nullptr);
    ASSERT_GE(pw->getSelectedChannelPadId(), 0);

    auto& sampler = harness.pads();
    int patternIdx = pw->getSelectedPatternIndex();
    int padId = pw->getSelectedChannelPadId();
    sampler.setStep(patternIdx, padId, 0, true);
    sampler.setStep(patternIdx, padId, 4, true);
    sampler.setStep(patternIdx, padId, 8, true);

    auto snap = sampler.getPadsSnapshot();
    ASSERT_FALSE(snap.empty());
    ASSERT_FALSE(snap[0].patterns.empty());
    EXPECT_TRUE(snap[0].patterns[0].steps[0]);

    ControllerHost::ButtonEvent erase { "eraseReplace", true, false };
    pw->handleButton(erase);

    snap = sampler.getPadsSnapshot();
    ASSERT_FALSE(snap.empty());
    ASSERT_FALSE(snap[0].patterns.empty());
    for (int i = 0; i < 16; ++i)
        EXPECT_FALSE(snap[0].patterns[0].steps[i]) << "Step " << i << " should be cleared";
}
