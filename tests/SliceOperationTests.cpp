#include <gtest/gtest.h>
#include <type_traits>

#include "../src/engine/commands/SliceSampleCommand.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"

#include "harness/EngineHarness.h"

// ============================================================================
// SliceSampleCommand — unit-level boundary math
// ============================================================================

TEST(SliceSampleCommandTests, BoundaryMath_EqualDivision)
{
    // 1.6 second source / 16 slices → 0.1 second slices
    constexpr double kTotal = 1.6;
    constexpr int kCount = 16;

    for (int i = 0; i < kCount; ++i)
    {
        const double start = SliceSampleCommand::sliceStartSeconds(kTotal, kCount, i);
        const double end   = SliceSampleCommand::sliceEndSeconds  (kTotal, kCount, i);
        EXPECT_NEAR(end - start, 0.1, 1e-9) << "Slice " << i << " not 0.1 s";
        EXPECT_NEAR(start, i * 0.1, 1e-9)   << "Slice " << i << " wrong start";
    }
}

TEST(SliceSampleCommandTests, BoundaryMath_FirstAndLast)
{
    EXPECT_NEAR(SliceSampleCommand::sliceStartSeconds(1.6, 16, 0),  0.0, 1e-9);
    EXPECT_NEAR(SliceSampleCommand::sliceEndSeconds  (1.6, 16, 0),  0.1, 1e-9);
    EXPECT_NEAR(SliceSampleCommand::sliceStartSeconds(1.6, 16, 15), 1.5, 1e-9);
    EXPECT_NEAR(SliceSampleCommand::sliceEndSeconds  (1.6, 16, 15), 1.6, 1e-9);
}

TEST(SliceSampleCommandTests, BoundaryMath_CoversFullDurationWithoutGaps)
{
    const double totals[] = { 1.6, 4.0, 10.5 };
    const int counts[]    = { 4, 8, 16 };

    for (const double total : totals)
    {
        for (const int count : counts)
        {
            double prevEnd = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const double start = SliceSampleCommand::sliceStartSeconds(total, count, i);
                const double end   = SliceSampleCommand::sliceEndSeconds  (total, count, i);

                EXPECT_NEAR(start, prevEnd, 1e-9)
                    << "Gap before slice " << i << " (total=" << total << " count=" << count << ")";
                EXPECT_GT(end, start) << "Empty slice " << i;
                prevEnd = end;
            }
            EXPECT_NEAR(prevEnd, total, 1e-9) << "Did not cover full duration";
        }
    }
}

// ============================================================================
// SliceSampleCommand — integration via EngineHarness
// ============================================================================

TEST(SliceSampleCommandTests, PerformPointsAllSlicePadsAtSourceFile)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    // Load a 1-second sample onto pad 0
    const auto source = harness.createTemporarySampleFile("slice_perf", 44100);
    harness.pads().loadSample(0, source);

    SliceSampleCommand cmd(harness.audio(), 0, 4);
    ASSERT_TRUE(cmd.perform());

    // Non-destructive: every sliced pad points at the SAME source file,
    // differentiated by range. No files written.
    for (int i = 0; i < 4; ++i)
    {
        const auto* pad = harness.pads().getPad(i);
        ASSERT_NE(pad, nullptr) << "Pad " << i << " is null";
        EXPECT_TRUE(pad->hasSample) << "Pad " << i << " has no sample";
        EXPECT_EQ(pad->sampleFile, source) << "Pad " << i << " should reference the source file";
    }
}

TEST(SliceSampleCommandTests, PerformAssignsEqualRangesAcrossSlices)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto source = harness.createTemporarySampleFile("slice_ranges", 44100);
    harness.pads().loadSample(0, source);

    SliceSampleCommand cmd(harness.audio(), 0, 4);
    ASSERT_TRUE(cmd.perform());

    // For a 1-second source split into 4 slices, ranges should be 0.0-0.25,
    // 0.25-0.50, 0.50-0.75, 0.75-1.0.
    const auto* pad0 = harness.pads().getPad(0);
    const auto* pad1 = harness.pads().getPad(1);
    const auto* pad2 = harness.pads().getPad(2);
    const auto* pad3 = harness.pads().getPad(3);
    ASSERT_NE(pad0, nullptr);
    ASSERT_NE(pad1, nullptr);
    ASSERT_NE(pad2, nullptr);
    ASSERT_NE(pad3, nullptr);

    EXPECT_NEAR(pad0->rangeStartSeconds, 0.00, 0.01);
    EXPECT_NEAR(pad0->rangeEndSeconds,   0.25, 0.01);
    EXPECT_NEAR(pad1->rangeStartSeconds, 0.25, 0.01);
    EXPECT_NEAR(pad1->rangeEndSeconds,   0.50, 0.01);
    EXPECT_NEAR(pad2->rangeStartSeconds, 0.50, 0.01);
    EXPECT_NEAR(pad2->rangeEndSeconds,   0.75, 0.01);
    EXPECT_NEAR(pad3->rangeStartSeconds, 0.75, 0.01);
    EXPECT_NEAR(pad3->rangeEndSeconds,   1.00, 0.01);
}

TEST(SliceSampleCommandTests, ExplicitStartsPreserveTrimmedEditingWindow)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto source = harness.createTemporarySampleFile("slice_trimmed_window", 88200);
    harness.pads().loadSample(0, source);

    SliceSampleCommand cmd(harness.audio(), 0,
                           { 0.50, 1.00, 1.50 }, 1.75);
    ASSERT_TRUE(cmd.perform());

    const auto* pad0 = harness.pads().getPad(0);
    const auto* pad1 = harness.pads().getPad(1);
    const auto* pad2 = harness.pads().getPad(2);
    const auto* pad3 = harness.pads().getPad(3);
    ASSERT_NE(pad0, nullptr);
    ASSERT_NE(pad1, nullptr);
    ASSERT_NE(pad2, nullptr);
    ASSERT_NE(pad3, nullptr);

    EXPECT_NEAR(pad0->rangeStartSeconds, 0.50, 0.01);
    EXPECT_NEAR(pad0->rangeEndSeconds,   1.00, 0.01);
    EXPECT_NEAR(pad1->rangeStartSeconds, 1.00, 0.01);
    EXPECT_NEAR(pad1->rangeEndSeconds,   1.50, 0.01);
    EXPECT_NEAR(pad2->rangeStartSeconds, 1.50, 0.01);
    EXPECT_NEAR(pad2->rangeEndSeconds,   1.75, 0.01);
    EXPECT_FALSE(pad3->hasSample) << "trimmed lead-in must not create an extra slice";
}

TEST(SliceSampleCommandTests, ExplicitRangesPreserveOverlappingSlices)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto source =
        harness.createTemporarySampleFile("slice_overlapping_ranges", 88200);
    harness.pads().loadSample(0, source);

    SliceSampleCommand cmd(
        harness.audio(), 0,
        { 0.00, 0.40, 0.90 },
        { 0.60, 1.20, 1.75 },
        1.75);
    ASSERT_TRUE(cmd.perform());

    const auto* pad0 = harness.pads().getPad(0);
    const auto* pad1 = harness.pads().getPad(1);
    const auto* pad2 = harness.pads().getPad(2);
    ASSERT_NE(pad0, nullptr);
    ASSERT_NE(pad1, nullptr);
    ASSERT_NE(pad2, nullptr);
    EXPECT_NEAR(pad0->rangeStartSeconds, 0.00, 0.01);
    EXPECT_NEAR(pad0->rangeEndSeconds,   0.60, 0.01);
    EXPECT_NEAR(pad1->rangeStartSeconds, 0.40, 0.01);
    EXPECT_NEAR(pad1->rangeEndSeconds,   1.20, 0.01);
    EXPECT_NEAR(pad2->rangeStartSeconds, 0.90, 0.01);
    EXPECT_NEAR(pad2->rangeEndSeconds,   1.75, 0.01);
}

TEST(SliceSampleCommandTests, UndoRestoresPreviousPads)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    // Pre-load distinct samples onto pads 0..3
    const auto sample0 = harness.createTemporarySampleFile("slice_undo_0", 44100);
    const auto sample1 = harness.createTemporarySampleFile("slice_undo_1", 22050);
    const auto sample2 = harness.createTemporarySampleFile("slice_undo_2", 11025);
    const auto sample3 = harness.createTemporarySampleFile("slice_undo_3", 8000);

    harness.pads().loadSample(0, sample0);
    harness.pads().loadSample(1, sample1);
    harness.pads().loadSample(2, sample2);
    harness.pads().loadSample(3, sample3);

    SliceSampleCommand cmd(harness.audio(), 0, 4);
    ASSERT_TRUE(cmd.perform());

    // After perform: all four pads point at sample0 (the source).
    EXPECT_EQ(harness.pads().getPad(0)->sampleFile, sample0);
    EXPECT_EQ(harness.pads().getPad(1)->sampleFile, sample0);
    EXPECT_EQ(harness.pads().getPad(2)->sampleFile, sample0);
    EXPECT_EQ(harness.pads().getPad(3)->sampleFile, sample0);

    ASSERT_TRUE(cmd.undo());

    EXPECT_EQ(harness.pads().getPad(0)->sampleFile, sample0);
    EXPECT_EQ(harness.pads().getPad(1)->sampleFile, sample1);
    EXPECT_EQ(harness.pads().getPad(2)->sampleFile, sample2);
    EXPECT_EQ(harness.pads().getPad(3)->sampleFile, sample3);
}

TEST(SliceSampleCommandTests, UndoRestoresEmptyPads)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto sample = harness.createTemporarySampleFile("slice_empty_undo", 44100);
    harness.pads().loadSample(0, sample);

    SliceSampleCommand cmd(harness.audio(), 0, 4);
    ASSERT_TRUE(cmd.perform());
    EXPECT_TRUE(harness.pads().getPad(1)->hasSample);

    ASSERT_TRUE(cmd.undo());

    // Pads 1..3 were empty before slice — they should be empty again after undo.
    EXPECT_FALSE(harness.pads().getPad(1)->hasSample);
    EXPECT_FALSE(harness.pads().getPad(2)->hasSample);
    EXPECT_FALSE(harness.pads().getPad(3)->hasSample);
}

TEST(SliceSampleCommandTests, PerformFailsWhenSourceHasNoSample)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    SliceSampleCommand cmd(harness.audio(), 0, 4);
    EXPECT_FALSE(cmd.perform());
}

TEST(SliceSampleCommandTests, RedoAfterUndoRestoresSlicedState)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto sample = harness.createTemporarySampleFile("slice_redo", 44100);
    harness.pads().loadSample(0, sample);

    SliceSampleCommand cmd(harness.audio(), 0, 4);
    ASSERT_TRUE(cmd.perform());
    ASSERT_TRUE(cmd.undo());
    ASSERT_TRUE(cmd.perform());  // redo

    for (int i = 0; i < 4; ++i)
    {
        const auto* pad = harness.pads().getPad(i);
        ASSERT_NE(pad, nullptr);
        EXPECT_TRUE(pad->hasSample) << "Pad " << i << " missing sample after redo";
        EXPECT_EQ(pad->sampleFile, sample) << "Pad " << i << " should point at source after redo";
    }
}

TEST(SliceSampleCommandTests, UndoManagerTreatsAllGeneratedPadsAsOneTransaction)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto sample = harness.createTemporarySampleFile("slice_atomic_undo", 44100);
    harness.pads().loadSample(0, sample);

    auto& undo = harness.audio().getUndoManager();
    undo.clearUndoHistory();
    undo.beginNewTransaction("Slice Sample");
    ASSERT_TRUE(undo.perform(new SliceSampleCommand(harness.audio(), 0, 16)));
    ASSERT_EQ(undo.getUndoDescription(), "Slice Sample");
    ASSERT_TRUE(harness.pads().getPad(15)->hasSample);

    ASSERT_TRUE(undo.undo());
    for (int i = 1; i < 16; ++i)
        EXPECT_FALSE(harness.pads().getPad(i)->hasSample) << "pad " << i;

    ASSERT_TRUE(undo.redo());
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(harness.pads().getPad(i)->hasSample) << "pad " << i;
}

// ============================================================================
// ISamplePreview — interface + SamplePreviewPlayer conformance
// ============================================================================

#include "../src/engine/SamplePreviewPlayer.h"

TEST(SamplePreviewPlayerTests, SatisfiesISamplePreviewInterface)
{
    // Compile-time: SamplePreviewPlayer must be an ISamplePreview.
    static_assert(std::is_base_of_v<ISamplePreview, SamplePreviewPlayer>,
                  "SamplePreviewPlayer must implement ISamplePreview");

    // Runtime: default preview reports not playing and position 0.
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    SamplePreviewPlayer preview(harness.audio());
    ISamplePreview& iface = preview;
    EXPECT_FALSE(iface.isPlaying());
    EXPECT_NEAR(iface.getPositionSeconds(), 0.0, 1e-9);
    iface.seek(0.5);  // no-op on a stopped preview; must not crash
}

// ============================================================================
// SliceOperation — Manual mode (pure state, no playback yet)
// ============================================================================

#include "../src/ui/views/operations/SliceOperation.h"

TEST(SliceOperationManualTest, K0SelectsManualType)
{
    SliceOperation op;
    SamplerInstrument::PadSnapshot snap;
    snap.hasSample = true;
    snap.totalLengthSeconds = 4.0;
    snap.windowStartSeconds = 0.0;
    snap.windowEndSeconds   = 4.0;
    op.refreshFromSnapshot(snap);

    auto knobs = op.getKnobs(4.0);
    ASSERT_FALSE(knobs.empty());
    auto* listModel = std::get_if<Knob::ListModel>(&knobs[0].model);
    ASSERT_NE(listModel, nullptr) << "K0 must be a ListModel";
    ASSERT_EQ(listModel->entries.size(), 3u) << "K0 must now have 3 entries";
    EXPECT_EQ(listModel->entries[0], juce::String("Straight"));
    EXPECT_EQ(listModel->entries[1], juce::String("Transient"));
    EXPECT_EQ(listModel->entries[2], juce::String("Manual"));

    // Switching to Manual must set op.getType() == Manual.
    ASSERT_TRUE(listModel->onChange);
    listModel->onChange(2);
    EXPECT_EQ(op.getType(), SliceOperation::Type::Manual);
    EXPECT_TRUE(op.getCapturedStarts().empty());
    EXPECT_FALSE(op.isCapturing());
}

// Fake preview that records calls and exposes mutable state for tests.
class FakeSamplePreview : public ISamplePreview
{
public:
    void play(const juce::File& file) override { isPlaying_ = true; playedFile_ = file; }
    void stop() override                       { isPlaying_ = false; }
    void seek(double seconds) override         { position_ = seconds; seekCount_++; }
    double getPositionSeconds() const override { return position_; }
    bool isPlaying() const noexcept override   { return isPlaying_; }

    // Test helpers
    void setPosition(double s) { position_ = s; }

    bool isPlaying_ { false };
    double position_ { 0.0 };
    int seekCount_ { 0 };
    juce::File playedFile_;
};

static SamplerInstrument::PadSnapshot makeManualSnap(double windowStart = 0.0,
                                                     double windowEnd = 4.0,
                                                     int padId = 0);

TEST(SliceOperationAuditionTest, DeactivateStopsSlicePreview)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.setType(SliceOperation::Type::Straight);
    op.setSliceCount(4);
    op.refreshFromSnapshot(makeManualSnap(0.5, 2.5));

    op.onPadPressed(0);
    ASSERT_TRUE(preview.isPlaying_);
    ASSERT_EQ(op.getActiveSliceIndex(), 0);
    ASSERT_GT(op.getAuditionEndSeconds(), 0.0);

    op.deactivate();

    EXPECT_FALSE(preview.isPlaying_);
    EXPECT_EQ(op.getActiveSliceIndex(), -1);
    EXPECT_EQ(op.getAuditionEndSeconds(), 0.0);
}

TEST(SliceOperationAuditionTest, StraightPadsAuditionOnlyTheirProposedWindow)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.setType(SliceOperation::Type::Straight);
    op.setSliceCount(4);
    op.refreshFromSnapshot(makeManualSnap(0.5, 2.5));

    op.onPadPressed(2);

    EXPECT_TRUE(preview.isPlaying_);
    EXPECT_NEAR(preview.position_, 1.5, 1e-9);
    EXPECT_NEAR(op.getAuditionEndSeconds(), 2.0, 1e-9);
    EXPECT_EQ(op.getActiveSliceIndex(), 2);
}

TEST(SliceOperationAuditionTest, StraightPadsOutsideSliceCountAreIgnored)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.setType(SliceOperation::Type::Straight);
    op.setSliceCount(4);
    op.refreshFromSnapshot(makeManualSnap());

    op.onPadPressed(4);

    EXPECT_FALSE(preview.isPlaying_);
    EXPECT_EQ(op.getActiveSliceIndex(), -1);
    EXPECT_DOUBLE_EQ(op.getAuditionEndSeconds(), 0.0);
}

TEST(SliceOperationAuditionTest, TransientSensitivityUsesFineHardwareStep)
{
    SliceOperation op;
    op.setType(SliceOperation::Type::Transient);
    op.refreshFromSnapshot(makeManualSnap());

    auto knobs = op.getKnobs(4.0);
    ASSERT_GE(knobs.size(), 2u);
    auto* model = std::get_if<Knob::NumericModel>(&knobs[1].model);
    ASSERT_NE(model, nullptr);
    EXPECT_DOUBLE_EQ(model->step, 0.002);
}

static SamplerInstrument::PadSnapshot makeManualSnap(double windowStart,
                                                     double windowEnd,
                                                     int padId)
{
    SamplerInstrument::PadSnapshot s;
    s.id = padId;
    s.hasSample = true;
    s.totalLengthSeconds = windowEnd;
    s.windowStartSeconds = windowStart;
    s.windowEndSeconds   = windowEnd;
    // A stub file path — SliceOperation doesn't read it directly in tests,
    // it just forwards it to the preview fake.
    s.sampleFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("fake_manual.wav");
    return s;
}

TEST(SliceOperationManualTest, SequentialCapture)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);

    op.refreshFromSnapshot(makeManualSnap());
    op.setType(SliceOperation::Type::Manual);  // test-only mutator, see below

    // Pad 0: starts playback, drops boundary 0 at windowStart.
    op.onPadPressed(0);
    ASSERT_EQ(op.getCapturedStarts().size(), 1u);
    EXPECT_NEAR(op.getCapturedStarts()[0], 0.0, 1e-9);
    EXPECT_TRUE(preview.isPlaying_);
    EXPECT_TRUE(op.isCapturing());

    // Advance fake playhead, then press pad 1.
    preview.setPosition(1.25);
    op.onPadPressed(1);
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);
    EXPECT_NEAR(op.getCapturedStarts()[1], 1.25, 1e-9);

    // Pad 2 at 2.5 s.
    preview.setPosition(2.5);
    op.onPadPressed(2);
    ASSERT_EQ(op.getCapturedStarts().size(), 3u);
    EXPECT_NEAR(op.getCapturedStarts()[2], 2.5, 1e-9);
}

TEST(SliceOperationManualTest, UndoRedoRestoresTemporaryChopBoundaries)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap());
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0);
    op.onPadPressed(1);
    preview.setPosition(2.0);
    op.onPadPressed(2);
    ASSERT_EQ(op.getCapturedStarts().size(), 3u);

    EXPECT_TRUE(op.undoManualEdit());
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);
    EXPECT_NEAR(op.getCapturedStarts().back(), 1.0, 1e-9);

    EXPECT_TRUE(op.redoManualEdit());
    ASSERT_EQ(op.getCapturedStarts().size(), 3u);
    EXPECT_NEAR(op.getCapturedStarts().back(), 2.0, 1e-9);
}

TEST(SliceOperationManualTest, NewChopAfterUndoClearsTemporaryRedo)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap());
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0);
    op.onPadPressed(1);
    ASSERT_TRUE(op.undoManualEdit());

    preview.setPosition(1.5);
    op.onPadPressed(1);
    EXPECT_FALSE(op.redoManualEdit());
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);
    EXPECT_NEAR(op.getCapturedStarts().back(), 1.5, 1e-9);
}

TEST(SliceOperationManualTest, SelectedSliceExposesSharedStartAndEndBoundaries)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0);
    op.onPadPressed(1);
    preview.setPosition(2.0);
    op.onPadPressed(2);
    op.onPadPressed(1); // select the completed middle slice

    auto knobs = op.getKnobs(4.0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[2].label, juce::String("Start"));
    EXPECT_EQ(knobs[3].label, juce::String("End"));
    EXPECT_TRUE(knobs[2].isEnabled);
    EXPECT_TRUE(knobs[3].isEnabled);

    auto* start = std::get_if<Knob::NumericModel>(&knobs[2].model);
    auto* end = std::get_if<Knob::NumericModel>(&knobs[3].model);
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    ASSERT_TRUE(static_cast<bool>(start->onChange));
    ASSERT_TRUE(static_cast<bool>(end->onChange));

    start->onChange(1.25);
    end->onChange(1.75);
    ASSERT_EQ(op.getCapturedStarts().size(), 3u);
    EXPECT_NEAR(op.getCapturedStarts()[1], 1.25, 1e-9);
    EXPECT_NEAR(op.getCapturedStarts()[2], 1.75, 1e-9);
}

TEST(SliceOperationManualTest, FirstSliceStartAndLastSliceEndAreEditable)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.5, 4.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.5);
    op.onPadPressed(1);

    op.onPadPressed(0);
    auto firstKnobs = op.getKnobs(3.5);
    EXPECT_TRUE(firstKnobs[2].isEnabled);
    EXPECT_TRUE(firstKnobs[3].isEnabled);
    auto* firstStart = std::get_if<Knob::NumericModel>(&firstKnobs[2].model);
    ASSERT_NE(firstStart, nullptr);
    EXPECT_NEAR(firstStart->minimum, 0.5, 1e-9);
    ASSERT_TRUE(static_cast<bool>(firstStart->onChange));
    firstStart->onChange(0.75);
    EXPECT_NEAR(op.getCapturedStarts()[0], 0.75, 1e-9);

    op.onPadPressed(1);
    auto lastKnobs = op.getKnobs(3.5);
    EXPECT_TRUE(lastKnobs[2].isEnabled);
    EXPECT_TRUE(lastKnobs[3].isEnabled);
    auto* lastEnd = std::get_if<Knob::NumericModel>(&lastKnobs[3].model);
    ASSERT_NE(lastEnd, nullptr);
    EXPECT_NEAR(lastEnd->maximum, 4.0, 1e-9);
    ASSERT_TRUE(static_cast<bool>(lastEnd->onChange));
    lastEnd->onChange(3.5);
    EXPECT_NEAR(op.getCapturedEnds()[1], 3.5, 1e-9);
}

TEST(SliceOperationManualTest, OverlappingMakesSelectedRangeIndependent)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0);
    op.onPadPressed(1);
    preview.setPosition(2.0);
    op.onPadPressed(2);
    ASSERT_TRUE(op.selectManualSlice(1));

    op.setOverlapping(true);
    auto knobs = op.getKnobs(4.0);
    auto* start = std::get_if<Knob::NumericModel>(&knobs[2].model);
    auto* end = std::get_if<Knob::NumericModel>(&knobs[3].model);
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    start->onChange(0.75);
    end->onChange(2.25);

    const auto ranges = op.getManualSliceRanges();
    ASSERT_EQ(ranges.size(), 3u);
    EXPECT_NEAR(ranges[0].endSeconds, 1.0, 1e-9);
    EXPECT_NEAR(ranges[1].startSeconds, 0.75, 1e-9);
    EXPECT_NEAR(ranges[1].endSeconds, 2.25, 1e-9);
    EXPECT_NEAR(ranges[2].startSeconds, 2.0, 1e-9);
}

TEST(SliceOperationManualTest, DisablingOverlapRestoresContiguousRanges)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0);
    op.onPadPressed(1);
    preview.setPosition(2.0);
    op.onPadPressed(2);
    ASSERT_TRUE(op.selectManualSlice(1));
    op.setOverlapping(true);

    auto knobs = op.getKnobs(4.0);
    auto* start = std::get_if<Knob::NumericModel>(&knobs[2].model);
    auto* end = std::get_if<Knob::NumericModel>(&knobs[3].model);
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    start->onChange(0.75);
    end->onChange(2.25);

    op.setOverlapping(false);
    const auto ranges = op.getManualSliceRanges();
    ASSERT_EQ(ranges.size(), 3u);
    EXPECT_NEAR(ranges[0].endSeconds, ranges[1].startSeconds, 1e-9);
    EXPECT_NEAR(ranges[1].endSeconds, ranges[2].startSeconds, 1e-9);
    EXPECT_NEAR(ranges[2].endSeconds, 4.0, 1e-9);
}

TEST(SliceOperationManualTest, ArrowSelectionClampsToCapturedSlices)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0);
    op.onPadPressed(1);
    preview.setPosition(2.0);
    op.onPadPressed(2);
    ASSERT_EQ(op.getActiveSliceIndex(), 2);

    EXPECT_TRUE(op.selectAdjacentManualSlice(-1));
    EXPECT_EQ(op.getActiveSliceIndex(), 1);
    EXPECT_TRUE(op.selectAdjacentManualSlice(-1));
    EXPECT_EQ(op.getActiveSliceIndex(), 0);
    EXPECT_FALSE(op.selectAdjacentManualSlice(-1));
    EXPECT_TRUE(op.selectManualSlice(2));
    EXPECT_EQ(op.getActiveSliceIndex(), 2);
    EXPECT_FALSE(op.selectManualSlice(3));
}

TEST(SliceOperationManualTest, OutOfOrderIgnored)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap());
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);           // starts
    preview.setPosition(1.0);
    op.onPadPressed(5);           // K > N → ignored
    EXPECT_EQ(op.getCapturedStarts().size(), 1u);
}

TEST(SliceOperationManualTest, AuditionExistingPadSeeksPreview)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap());
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);                             // capture 0 at 0.0
    preview.setPosition(1.0); op.onPadPressed(1);   // capture 1 at 1.0
    preview.setPosition(2.0); op.onPadPressed(2);   // capture 2 at 2.0

    preview.stop();
    const int beforeSeeks = preview.seekCount_;
    op.onPadPressed(1);  // audition pad 1
    EXPECT_EQ(op.getCapturedStarts().size(), 3u) << "audition must not change captures";
    EXPECT_NEAR(preview.position_, 1.0, 1e-9) << "preview should seek to capture[1]";
    EXPECT_GT(preview.seekCount_, beforeSeeks);
}

TEST(SliceOperationManualTest, ExistingPadCannotRestartActiveCapturePlayback)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap());
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.25);

    const int seeksBeforeDuplicate = preview.seekCount_;
    op.onPadPressed(0); // duplicate Pad 1 down while capture is still playing
    EXPECT_NEAR(preview.position_, 1.25, 1e-9);
    EXPECT_EQ(preview.seekCount_, seeksBeforeDuplicate);

    op.onPadPressed(1);
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);
    EXPECT_NEAR(op.getCapturedStarts()[1], 1.25, 1e-9);
}

TEST(SliceOperationManualTest, WindowBoundsClamped)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0));  // window ends at 4 s
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);                    // first capture at windowStart
    preview.setPosition(9.0);              // past window end
    op.onPadPressed(1);
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);
    EXPECT_NEAR(op.getCapturedStarts()[1], 4.0, 1e-9) << "must clamp to windowEnd";
}

TEST(SliceOperationManualTest, MinimumSliceSeparation)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);           // at 0.0
    preview.setPosition(0.002);   // 2 ms later — below the 10 ms floor
    op.onPadPressed(1);
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);
    EXPECT_NEAR(op.getCapturedStarts()[1], 0.010, 1e-9)
        << "second capture must be bumped to 10 ms after the first";
}

TEST(SliceOperationManualTest, SixteenCaptureCap)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 20.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    for (int i = 1; i < 16; ++i)
    {
        preview.setPosition(i * 0.5);
        op.onPadPressed(i);
    }
    EXPECT_EQ(op.getCapturedStarts().size(), 16u);

    // 17th pad press (we only have pads 0..15 anyway, but guard still matters).
    preview.setPosition(10.0);
    op.onPadPressed(15);  // K == N-1 now: audition, not capture
    EXPECT_EQ(op.getCapturedStarts().size(), 16u);
}

TEST(SliceOperationManualTest, EraseSliceReindex)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    // Capture 5 starts: t0 = 0, 1, 2, 3, 4
    op.onPadPressed(0);
    for (int i = 1; i < 5; ++i) { preview.setPosition(i); op.onPadPressed(i); }
    ASSERT_EQ(op.getCapturedStarts().size(), 5u);

    int eraseAllCalls = 0;
    op.setOnEraseAll([&]{ eraseAllCalls++; });

    op.onEraseEvent(true);       // Erase press
    op.onPadPressed(2);          // delete slice 2
    op.onEraseEvent(false);      // Erase release — must NOT trigger erase-all

    EXPECT_EQ(eraseAllCalls, 0);
    ASSERT_EQ(op.getCapturedStarts().size(), 4u);
    EXPECT_NEAR(op.getCapturedStarts()[0], 0.0, 1e-9);
    EXPECT_NEAR(op.getCapturedStarts()[1], 1.0, 1e-9);
    EXPECT_NEAR(op.getCapturedStarts()[2], 3.0, 1e-9);  // formerly index 3
    EXPECT_NEAR(op.getCapturedStarts()[3], 4.0, 1e-9);
    EXPECT_FALSE(op.isEraseHeld());
}

TEST(SliceOperationManualTest, EraseFrontBoundaryReindex)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.5, 10.5));  // windowStart = 0.5
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);                               // capture 0 at 0.5
    preview.setPosition(1.5); op.onPadPressed(1);
    preview.setPosition(2.5); op.onPadPressed(2);

    op.setOnEraseAll([]{});
    op.onEraseEvent(true);
    op.onPadPressed(0);
    op.onEraseEvent(false);

    ASSERT_EQ(op.getCapturedStarts().size(), 2u);
    EXPECT_NEAR(op.getCapturedStarts()[0], 1.5, 1e-9) << "front entry must keep its absolute time";
    EXPECT_NEAR(op.getCapturedStarts()[1], 2.5, 1e-9);
}

TEST(SliceOperationManualTest, EraseAllOnTap)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    preview.setPosition(2.0); op.onPadPressed(2);
    preview.setPosition(3.0); op.onPadPressed(3);
    ASSERT_EQ(op.getCapturedStarts().size(), 4u);

    int eraseAllCalls = 0;
    op.setOnEraseAll([&]{ eraseAllCalls++; });

    op.onEraseEvent(true);       // Erase press (no pad)
    op.onEraseEvent(false);      // Erase release → erase-all fires
    EXPECT_EQ(eraseAllCalls, 1);
    EXPECT_FALSE(op.isEraseHeld());
}

TEST(SliceOperationManualTest, EraseHoldNoPadThenRelease)
{
    // Zero-capture state: Erase tap still counts as tap (erase-all callback fires,
    // but the callback itself may noop on an empty list — that's the caller's
    // concern, not SliceOperation's).
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap());
    op.setType(SliceOperation::Type::Manual);

    int calls = 0;
    op.setOnEraseAll([&]{ calls++; });

    op.onEraseEvent(true);
    op.onEraseEvent(false);
    EXPECT_EQ(calls, 1);
}

TEST(SliceOperationManualTest, StateResetOnPadChange)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0, /*padId=*/0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    preview.setPosition(2.0); op.onPadPressed(2);
    ASSERT_EQ(op.getCapturedStarts().size(), 3u);

    // Switch to a different pad
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0, /*padId=*/5));
    EXPECT_TRUE(op.getCapturedStarts().empty());
    EXPECT_FALSE(op.isCapturing());
}

TEST(SliceOperationManualTest, StateResetOnWindowChange)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 4.0, 0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);

    // Same pad, shorter window.
    auto tighter = makeManualSnap(0.0, 2.0, 0);
    op.refreshFromSnapshot(tighter);
    EXPECT_TRUE(op.getCapturedStarts().empty());
}

TEST(SliceOperationManualTest, ApplyWithTwoCapturesProducesTwoSlices)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto source = harness.createTemporarySampleFile("manual_apply", 44100);
    harness.pads().loadSample(0, source);

    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.activate(&harness.audio());

    auto snap = harness.pads().getPad(0);
    ASSERT_NE(snap, nullptr);
    // Populate a fresh snapshot object matching the loaded pad
    SamplerInstrument::PadSnapshot s;
    s.id = 0;
    s.hasSample = true;
    s.totalLengthSeconds = snap->cachedLengthSeconds;
    s.windowStartSeconds = 0.0;
    s.windowEndSeconds   = snap->cachedLengthSeconds;
    s.sampleFile = source;
    op.refreshFromSnapshot(s);
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);                    // capture 0 at 0.0
    preview.setPosition(s.totalLengthSeconds * 0.5);
    op.onPadPressed(1);                    // capture 1 mid-file
    ASSERT_EQ(op.getCapturedStarts().size(), 2u);

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    ASSERT_TRUE(op.canApply());
    op.apply(harness.audio(), 0, edit->getUndoManager());
    EXPECT_FALSE(op.didLastApplyFail());
    EXPECT_EQ(op.getLastAppliedSliceCount(), 2);

    // Pads 0 and 1 both point at the source with complementary ranges.
    EXPECT_TRUE(harness.pads().getPad(0)->hasSample);
    EXPECT_TRUE(harness.pads().getPad(1)->hasSample);
    EXPECT_EQ(harness.pads().getPad(0)->sampleFile, source);
    EXPECT_EQ(harness.pads().getPad(1)->sampleFile, source);

    // Capture resets after apply.
    EXPECT_TRUE(op.getCapturedStarts().empty());
}

// ============================================================================
// SliceOperation — Manual mode: active slice selection + nudge
// ============================================================================

TEST(SliceOperationManualTest, ActiveSliceSetOnCapture)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    EXPECT_EQ(op.getActiveSliceIndex(), 0);

    preview.setPosition(1.0); op.onPadPressed(1);
    EXPECT_EQ(op.getActiveSliceIndex(), 1);

    preview.setPosition(2.0); op.onPadPressed(2);
    EXPECT_EQ(op.getActiveSliceIndex(), 2);
}

TEST(SliceOperationManualTest, ActiveSliceSetOnAudition)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    preview.setPosition(2.0); op.onPadPressed(2);
    preview.setPosition(3.0); op.onPadPressed(3);
    EXPECT_EQ(op.getActiveSliceIndex(), 3);

    op.onPadPressed(1);  // audition -> select index 1
    EXPECT_EQ(op.getActiveSliceIndex(), 1);
}

TEST(SliceOperationManualTest, NudgeMiddleSliceWithinBounds)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);                               // [0.0]
    preview.setPosition(1.0); op.onPadPressed(1);     // [0.0, 1.0]
    preview.setPosition(2.0); op.onPadPressed(2);     // [0.0, 1.0, 2.0]
    preview.setPosition(3.0); op.onPadPressed(3);     // [0.0, 1.0, 2.0, 3.0]
    op.onPadPressed(1);                               // audition -> active = 1

    op.nudgeActiveSlice(+0.5);  // 1.0 -> 1.5 (in bounds: between 0.0 and 2.0)
    EXPECT_NEAR(op.getCapturedStarts()[1], 1.5, 1e-9);

    op.nudgeActiveSlice(+5.0);  // 1.5 -> clamped to 2.0 - 0.010 = 1.990
    EXPECT_NEAR(op.getCapturedStarts()[1], 1.990, 1e-9);

    op.nudgeActiveSlice(-5.0);  // back down -> clamped to 0.0 + 0.010 = 0.010
    EXPECT_NEAR(op.getCapturedStarts()[1], 0.010, 1e-9);
}

TEST(SliceOperationManualTest, NudgeFirstSliceClampedToWindowStart)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.5, 10.5));  // windowStart = 0.5
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);                               // [0.5]
    preview.setPosition(2.0); op.onPadPressed(1);     // [0.5, 2.0]
    op.onPadPressed(0);                               // audition -> active = 0

    op.nudgeActiveSlice(-5.0);  // clamp to windowStart (0.5)
    EXPECT_NEAR(op.getCapturedStarts()[0], 0.5, 1e-9);

    op.nudgeActiveSlice(+10.0);  // clamp to next - 0.010 = 1.990
    EXPECT_NEAR(op.getCapturedStarts()[0], 1.990, 1e-9);
}

TEST(SliceOperationManualTest, NudgeLastSliceClampedToWindowEnd)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    preview.setPosition(2.0); op.onPadPressed(2);
    // active = 2 (last capture)

    op.nudgeActiveSlice(+100.0);  // clamp to windowEnd (10.0) - 0.010
    EXPECT_NEAR(op.getCapturedStarts()[2], 9.990, 1e-9);
}

TEST(SliceOperationManualTest, NudgeNoOpWhenNoActiveSlice)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    EXPECT_EQ(op.getActiveSliceIndex(), -1);
    op.nudgeActiveSlice(+1.0);  // no-op, must not crash
    EXPECT_EQ(op.getActiveSliceIndex(), -1);
    EXPECT_TRUE(op.getCapturedStarts().empty());
}

TEST(SliceOperationManualTest, ActiveSliceClearedOnStateReset)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0, 0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    EXPECT_EQ(op.getActiveSliceIndex(), 1);

    // Pad change triggers reset
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0, 5));
    EXPECT_EQ(op.getActiveSliceIndex(), -1);
}

TEST(SliceOperationManualTest, EraseActiveSliceClearsActive)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    preview.setPosition(2.0); op.onPadPressed(2);
    op.onPadPressed(1);  // active = 1
    EXPECT_EQ(op.getActiveSliceIndex(), 1);

    op.onEraseEvent(true);
    op.onPadPressed(1);        // erase slice 1
    op.onEraseEvent(false);
    EXPECT_EQ(op.getActiveSliceIndex(), -1);
}

TEST(SliceOperationManualTest, EraseEarlierSliceDecrementsActiveIndex)
{
    SliceOperation op;
    FakeSamplePreview preview;
    op.setPreview(&preview);
    op.refreshFromSnapshot(makeManualSnap(0.0, 10.0));
    op.setType(SliceOperation::Type::Manual);

    op.onPadPressed(0);
    preview.setPosition(1.0); op.onPadPressed(1);
    preview.setPosition(2.0); op.onPadPressed(2);
    preview.setPosition(3.0); op.onPadPressed(3);
    op.onPadPressed(2);  // active = 2 (time = 2.0)
    EXPECT_EQ(op.getActiveSliceIndex(), 2);

    op.onEraseEvent(true);
    op.onPadPressed(0);        // erase slice 0 (time 0.0 gone)
    op.onEraseEvent(false);
    // The same boundary (time = 2.0) is now at index 1.
    EXPECT_EQ(op.getActiveSliceIndex(), 1);
    EXPECT_NEAR(op.getCapturedStarts()[1], 2.0, 1e-9);
}
