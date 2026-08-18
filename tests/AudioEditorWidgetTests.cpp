#include <gtest/gtest.h>

#include "../src/ui/widget/AudioEditorWidget.h"
#include "../src/ui/widget/PadOverviewWidget.h"
#include "../src/ui/widget/SliceDetailsWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/control/HardwareConstants.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/commands/NormalizeSampleCommand.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

class AudioEditorWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        wm.setAudioEngine(&harness.audio());
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost mockController;
    WindowManager wm;
};

// 1. Descriptor
TEST_F(AudioEditorWidgetTests, DescriptorIsCorrect)
{
    AudioEditorWidget widget;
    auto desc = widget.describe();

    EXPECT_EQ(desc.id, "audio_editor");
    EXPECT_EQ(desc.pageCount, 1);
    EXPECT_FALSE(desc.forceOnTop);
    EXPECT_EQ(desc.display, DisplayConstraint::Any);
}

// 2. Resource declaration -- left panel
TEST_F(AudioEditorWidgetTests, ResourceDeclarationLeftPanel)
{
    auto widget = std::make_unique<AudioEditorWidget>();
    auto* raw = widget.get();

    auto resources = raw->requiredResources(0);

    for (int i = 1; i <= 4; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }

    // Should NOT have pad LEDs
    for (int i = 1; i <= 16; ++i)
    {
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)), resources.end())
            << "Should not have p" << i;
    }
}

// 3. Resource declaration -- right panel
TEST_F(AudioEditorWidgetTests, ResourceDeclarationRightPanel)
{
    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Right);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(raw, nullptr);

    auto resources = raw->requiredResources(0);

    for (int i = 5; i <= 8; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }
}

// 4. SampleRange applies non-destructively (no file rewrite)
TEST_F(AudioEditorWidgetTests, SampleRangeIsNonDestructive)
{
    auto sample = harness.createTemporarySampleFile("audio_editor_test", 44100);
    harness.pads().loadSample(0, sample);

    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Left);
    wm.setFocus(DisplaySide::Left);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);
    EXPECT_TRUE(raw->hasSample());
    EXPECT_EQ(raw->getCurrentOperation(), AudioEditorWidget::Operation::SampleRange);

    const auto originalSize = sample.getSize();
    ASSERT_GT(originalSize, 0);

    // Set a sub-range via the sampler (same path SampleRangeOperation uses)
    harness.pads().setPadSampleRange(raw->getCurrentPadId(), 0.0, 0.5);

    // File must be untouched — non-destructive contract.
    EXPECT_EQ(sample.getSize(), originalSize);
    const auto* pad = harness.pads().getPad(raw->getCurrentPadId());
    ASSERT_NE(pad, nullptr);
    EXPECT_NEAR(pad->rangeEndSeconds, 0.5, 0.01);
}

TEST_F(AudioEditorWidgetTests, RangePlayheadFollowsSelectedPadTriggerAndStop)
{
    auto sample = harness.createTemporarySampleFile("range_playhead", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));
    ASSERT_TRUE(harness.pads().setPadSampleRange(0, 0.25, 0.75));

    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Left);
    auto* raw = static_cast<AudioEditorWidget*>(
        wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    harness.pads().trigger(0);
    EXPECT_TRUE(raw->isRangePlayheadActive());
    EXPECT_NEAR(raw->getRangePlayheadSeconds(), 0.25, 0.01);

    harness.pads().stopPad(0);
    EXPECT_FALSE(raw->isRangePlayheadActive());
}

// 5. Operation navigation
TEST_F(AudioEditorWidgetTests, OperationNavigation)
{
    AudioEditorWidget widget;
    EXPECT_EQ(widget.getCurrentOperation(), AudioEditorWidget::Operation::SampleRange);

    widget.nextOperation();
    EXPECT_EQ(widget.getCurrentOperation(), AudioEditorWidget::Operation::Normalize);

    widget.nextOperation();
    EXPECT_EQ(widget.getCurrentOperation(), AudioEditorWidget::Operation::Slice);

    widget.nextOperation();
    EXPECT_EQ(widget.getCurrentOperation(), AudioEditorWidget::Operation::SampleRange);

    widget.prevOperation();
    EXPECT_EQ(widget.getCurrentOperation(), AudioEditorWidget::Operation::Slice);

    widget.prevOperation();
    EXPECT_EQ(widget.getCurrentOperation(), AudioEditorWidget::Operation::Normalize);
}

// 7. Lifecycle -- activate/deactivate
TEST_F(AudioEditorWidgetTests, LifecycleActivateDeactivate)
{
    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto& hw = wm.getHardwareState();

    // After opening, panel options/knobs should be claimed
    EXPECT_TRUE(hw.isClaimed("d1"));
    EXPECT_TRUE(hw.isClaimed("k1"));
    EXPECT_EQ(hw.getOwner("d1"), "audio_editor");

    // Pad LEDs should NOT be claimed
    EXPECT_FALSE(hw.isClaimed("p1"));

    // Close
    wm.close("audio_editor");

    EXPECT_FALSE(hw.isClaimed("d1"));
    EXPECT_FALSE(hw.isClaimed("k1"));
}

// 8. Knobs have start/end when sample loaded
TEST_F(AudioEditorWidgetTests, KnobsForSampleRangeMode)
{
    auto sample = harness.createTemporarySampleFile("knob_test", 44100);
    harness.pads().loadSample(0, sample);

    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);

    EXPECT_EQ(knobs[0].id, "range.start");
    EXPECT_EQ(knobs[0].label, "Start");
    EXPECT_TRUE(knobs[0].isEnabled);

    EXPECT_EQ(knobs[1].id, "range.end");
    EXPECT_EQ(knobs[1].label, "End");
    EXPECT_TRUE(knobs[1].isEnabled);

    EXPECT_EQ(knobs[3].id, "range.zoom");
    EXPECT_EQ(knobs[3].label, "Zoom");
    EXPECT_TRUE(knobs[3].isEnabled);
}

TEST_F(AudioEditorWidgetTests, RangeTrimStepTracksVisibleZoomWindow)
{
    auto sample = harness.createTemporarySampleFile("range_zoom", 44100);
    harness.pads().loadSample(0, sample);
    harness.pads().setPadSampleRange(0, 0.25, 0.75);

    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto knobs = raw->getKnobs(0);
    auto* fullStart = std::get_if<Knob::NumericModel>(&knobs[0].model);
    ASSERT_NE(fullStart, nullptr);
    const double fullViewStep = fullStart->step;

    raw->handleKnob(3, 500, 0, false);
    for (int i = 0; i < 6; ++i)
        wm.tickActiveWidgets();

    knobs = raw->getKnobs(0);
    auto* zoomedStart = std::get_if<Knob::NumericModel>(&knobs[0].model);
    ASSERT_NE(zoomedStart, nullptr);
    EXPECT_LT(zoomedStart->step, fullViewStep * 0.6);
}

TEST_F(AudioEditorWidgetTests, ZoomedRangeViewportRecentresAfterTrimKnobRelease)
{
    auto sample = harness.createTemporarySampleFile("range_release", 44100);
    harness.pads().loadSample(0, sample);
    harness.pads().setPadSampleRange(0, 0.25, 0.75);

    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    raw->handleKnob(3, 500, 0, false);
    for (int i = 0; i < 6; ++i)
        wm.tickActiveWidgets();

    auto knobs = raw->getKnobs(0);
    auto* before = std::get_if<Knob::NumericModel>(&knobs[0].model);
    ASSERT_NE(before, nullptr);
    const double frozenStep = before->step;

    wm.handleButtonEvent({ "knobTouch1", true, false });
    raw->handleKnob(0, 50, 0, false);
    for (int i = 0; i < 6; ++i)
        wm.tickActiveWidgets();

    knobs = raw->getKnobs(0);
    auto* whileTurning = std::get_if<Knob::NumericModel>(&knobs[0].model);
    ASSERT_NE(whileTurning, nullptr);
    EXPECT_NEAR(whileTurning->step, frozenStep, 1e-9);

    wm.handleButtonEvent({ "knobTouch1", false, false });
    knobs = raw->getKnobs(0);
    auto* afterRelease = std::get_if<Knob::NumericModel>(&knobs[0].model);
    ASSERT_NE(afterRelease, nullptr);
    EXPECT_LT(afterRelease->step, frozenStep);
}

TEST_F(AudioEditorWidgetTests, OperationNavigationExposesEnabledNormalizeAndSliceControls)
{
    auto sample = harness.createTemporarySampleFile("operation_controls", 44100);
    harness.pads().loadSample(0, sample);

    auto widget = std::make_unique<AudioEditorWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    raw->nextOperation();
    auto knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].id, "normalize.target");
    EXPECT_EQ(knobs[1].id, "normalize.evenFactor");
    EXPECT_TRUE(knobs[0].isEnabled);
    EXPECT_TRUE(knobs[1].isEnabled);

    raw->nextOperation();
    knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].id, "slice.type");
    EXPECT_EQ(knobs[1].id, "slice.count");
    EXPECT_TRUE(knobs[0].isEnabled);
    EXPECT_TRUE(knobs[1].isEnabled);
}

TEST_F(AudioEditorWidgetTests, ManualSlicingTemporarilyOwnsPadLedsAndBlinksNextPad)
{
    auto sample = harness.createTemporarySampleFile("manual_leds", 44100);
    harness.pads().loadSample(0, sample);

    wm.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);
    wm.open(std::make_unique<AudioEditorWidget>(), DisplaySide::Left);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);
    auto& hardware = wm.getHardwareState();
    ASSERT_EQ(hardware.getOwner("p1"), "pad_overview");

    raw->nextOperation();
    raw->nextOperation();
    raw->handleKnob(0, 64, 0, false);
    raw->handleKnob(0, 64, 0, false);
    for (int i = 0; i < 6; ++i)
        wm.tickActiveWidgets();

    EXPECT_EQ(hardware.getOwner("p1"), "audio_editor");
    EXPECT_EQ(hardware.getLed("p1"), HardwareConstants::kColorGoldBright);
    EXPECT_EQ(hardware.getLed("p2"), HardwareConstants::kColorOff);

    raw->handleKnob(0, -64, 0, false);
    for (int i = 0; i < 6; ++i)
        wm.tickActiveWidgets();

    EXPECT_EQ(hardware.getOwner("p1"), "pad_overview");
}

TEST_F(AudioEditorWidgetTests, ManualModeRequestsDetailsAndD8TogglesOverlap)
{
    auto sample = harness.createTemporarySampleFile("manual_details", 44100);
    harness.pads().loadSample(0, sample);

    bool detailsVisible = false;
    auto widget = std::make_unique<AudioEditorWidget>();
    widget->setSliceDetailsVisibilityCallback(
        [&detailsVisible](bool visible) { detailsVisible = visible; });
    wm.open(std::move(widget), DisplaySide::Left);

    auto* editor =
        static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(editor, nullptr);
    editor->nextOperation();
    editor->nextOperation();
    editor->handleKnob(0, 64, 0, false);
    editor->handleKnob(0, 64, 0, false);
    for (int i = 0; i < 6; ++i)
        wm.tickActiveWidgets();
    ASSERT_TRUE(detailsVisible);
    ASSERT_TRUE(editor->getSliceDetailsState().active);

    auto details = std::make_unique<SliceDetailsWidget>(*editor);
    auto* rawDetails = details.get();
    wm.open(std::move(details), DisplaySide::Right);
    EXPECT_TRUE(rawDetails->getKnobs(0).empty());
    auto options = rawDetails->getOptions(0);
    ASSERT_EQ(options.size(), 4u);
    EXPECT_EQ(options[0].state, OptionState::Empty);
    EXPECT_EQ(options[1].state, OptionState::Empty);
    EXPECT_EQ(options[2].state, OptionState::Empty);
    EXPECT_EQ(options[3].label, "Overlapping");
    EXPECT_EQ(options[3].state, OptionState::Enabled);
    ASSERT_TRUE(static_cast<bool>(options[3].onToggle));

    options[3].onToggle(true);
    EXPECT_TRUE(editor->getSliceDetailsState().overlapping);
    options = rawDetails->getOptions(0);
    EXPECT_EQ(options[3].state, OptionState::Active);

    options[3].onToggle(false);
    EXPECT_FALSE(editor->getSliceDetailsState().overlapping);
}

TEST_F(AudioEditorWidgetTests, SuccessfulSliceApplyContinuesToSequencerWorkflow)
{
    auto sample = harness.createTemporarySampleFile("slice_continue", 44100);
    harness.pads().loadSample(0, sample);

    int continuedWithSlices = 0;
    auto widget = std::make_unique<AudioEditorWidget>();
    widget->setSlicesAppliedCallback([&continuedWithSlices](int count)
    {
        continuedWithSlices = count;
    });
    wm.open(std::move(widget), DisplaySide::Left);

    auto* raw = static_cast<AudioEditorWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);
    raw->nextOperation();
    raw->nextOperation();
    ASSERT_EQ(raw->getCurrentOperation(), AudioEditorWidget::Operation::Slice);

    wm.refreshBars();
    wm.handleOptionButton("d4");

    EXPECT_EQ(continuedWithSlices, 16);
    for (int i = 0; i < continuedWithSlices; ++i)
    {
        const auto* pad = harness.pads().getPad(i);
        ASSERT_NE(pad, nullptr);
        EXPECT_TRUE(pad->hasSample) << "slice pad " << i;
        EXPECT_EQ(pad->sampleFile, sample) << "slice pad " << i;
    }
}
