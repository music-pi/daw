#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Widget.h"
#include "../../engine/SamplerInstrument.h"
#include "../../engine/SamplePreviewPlayer.h"

class EditorOperation;
class SampleRangeOperation;
class NormalizeOperation;
class SliceOperation;

/**
 * AudioEditorWidget -- waveform editor with sample-range/normalize/slicing
 * operations and JUCE AudioThumbnail-based waveform rendering. All operations
 * are non-destructive: the sample file is never rewritten.
 *
 * Workflow: user first sets the sample range (which portion of the file
 * constitutes "the sample"), then applies slice/normalize operations within
 * that range. The Slice operation reads/writes pad sample-range metadata
 * exactly like PadDetails does.
 */
class AudioEditorWidget : public Widget,
                          private SamplerInstrument::Listener
{
public:
    enum class Mode
    {
        Sampling
    };

    enum class Operation
    {
        SampleRange,
        Normalize,
        Slice
    };

    AudioEditorWidget();
    ~AudioEditorWidget() override;

    // -- Widget identity --
    WidgetDescriptor describe() const override;
    juce::String getTitle() const override { return "Sampling"; }
    juce::String getTitleSubtitle() const override;
    juce::Colour getAccentColour() const override;

    // -- Lifecycle --
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onActiveSamplerAboutToChange() override;
    void onActiveSamplerChanged() override;

    // -- Resources --
    std::vector<std::string> requiredResources(int page) override;

    // -- Options & Knobs --
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    // -- Rendering --
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    // -- Input --
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;
    void handleOption(int localIndex) override;
    void handleButton(const controller_events::ButtonEvent& e) override;

    // -- Accessors (for testing) --
    Mode getMode() const { return mode_; }
    Operation getCurrentOperation() const { return currentOperation_; }
    int getCurrentPadId() const { return currentPadId_; }
    bool hasSample() const { return hasSample_; }
    bool isRangePlayheadActive() const { return rangePlayheadActive_; }
    double getRangePlayheadSeconds() const { return rangePlayheadSeconds_; }

    struct SliceDetailsState
    {
        struct Range
        {
            double startSeconds { 0.0 };
            double endSeconds { 0.0 };
        };

        bool active { false };
        bool overlapping { false };
        int selectedSlice { -1 };
        std::vector<Range> ranges;
    };

    SliceDetailsState getSliceDetailsState() const;
    bool selectManualSlice(int sliceIndex);
    void setManualSliceOverlapping(bool overlapping);

    /** Set mode. */
    void setMode(Mode newMode);

    /** Cycle to next operation. */
    void nextOperation();

    /** Cycle to previous operation. */
    void prevOperation();

    /** Host hook used to continue directly into sequencing after a successful
        slice commit. Receives the number of populated slice pads. */
    void setSlicesAppliedCallback(std::function<void(int)> callback)
    {
        onSlicesApplied_ = std::move(callback);
    }

    void setSliceDetailsVisibilityCallback(std::function<void(bool)> callback)
    {
        onSliceDetailsVisibilityChanged_ = std::move(callback);
    }

private:
    void onUiHostTick() override;
    void refreshState();
    std::size_t computeVisualSignature() const;
    void rebuildOptions();
    void rebuildKnobs();

    juce::String operationName(Operation op) const;
    juce::String modeName() const;
    double getTotalSampleSeconds() const;

    // Waveform thumbnail
    juce::AudioFormatManager thumbnailFormatManager_;
    juce::AudioThumbnailCache thumbnailCache_ { 1 };
    juce::AudioThumbnail thumbnail_ { 512, thumbnailFormatManager_, thumbnailCache_ };
    juce::File thumbnailFile_;

    // Pad state
    int currentPadId_ { -1 };
    int currentPadIndex_ { -1 };
    SamplerInstrument::PadSnapshot currentSnapshot_;
    bool hasSample_ { false };

    // Range state
    double rangeStartSeconds_ { 0.0 };
    double rangeEndSeconds_ { 0.0 };
    double rangeZoom_ { 0.0 };
    int rangeZoomPadId_ { -1 };

    struct VisibleRange
    {
        double startSeconds { 0.0 };
        double endSeconds { 0.0 };
    };

    VisibleRange getRangeVisibleWindow(int waveformWidth) const;
    void freezeRangeViewport();
    void releaseRangeViewport();
    bool rangeViewportFrozen_ { false };
    VisibleRange frozenRangeView_;

    // Mode & operation
    Mode mode_ { Mode::Sampling };
    Operation currentOperation_ { Operation::SampleRange };

    // Operation plugins
    std::unique_ptr<SampleRangeOperation> sampleRangeOp_;
    std::unique_ptr<NormalizeOperation> normalizeOp_;
    std::unique_ptr<SliceOperation> sliceOp_;

    // Preview player: created on activation, injected into SliceOperation
    // so the Slice operation can audition slices via the TE preview Edit.
    std::unique_ptr<SamplePreviewPlayer> previewPlayer_;
    std::function<void(int)> onSlicesApplied_;
    std::function<void(bool)> onSliceDetailsVisibilityChanged_;
    bool sliceDetailsVisible_ { false };

    EditorOperation* getActiveOperation() const;
    void activateCurrentOperation();

    // Tick divider: onUiHostTick fires at 60 Hz, original poll was 10 Hz → N=6
    int tickDiv_ { 0 };

    // Options & knobs cache
    std::vector<Option> currentOptions_;
    std::vector<Knob> currentKnobs_;

    // Accumulates encoder deltas for ListModel knobs so a single encoder
    // tick doesn't skip multiple entries. One entry per local knob (0-3).
    std::array<int, 4> listKnobAccum_ { 0, 0, 0, 0 };
    juce::Colour cachedAccent_ { UiTheme::kTitlebarAccent };

    static juce::String formatTimeString(double seconds);

    // InputManager bindings active while Slice is the current operation. Pads
    // audition Straight/Transient proposals and capture/audition Manual cuts.
    static constexpr std::uint32_t kInvalidBinding = 0;
    std::uint32_t manualPadBinding_   { kInvalidBinding };
    std::uint32_t manualEraseBinding_ { kInvalidBinding };

    void claimManualInput();
    void releaseManualInput();
    void updateManualInputClaim();
    void updateManualPadLedClaim();
    void releaseManualPadLedClaim();

    void publishManualPadLeds();
    void updateSliceDetailsVisibility();
    int manualLedBlinkPhase_ { 0 };  // 60 Hz counter; /8 → toggles
    bool manualPadLedsClaimed_ { false };

    // SamplerInstrument::Listener — Range playhead follows the selected pad's
    // actual trigger/stop edges while keeping the existing audition path.
    void padTriggered(int padIndex) override;
    void padStopped(int padIndex) override;
    bool listeningForPadTriggers_ { false };
    bool rangePlayheadActive_ { false };
    double rangePlayheadStartedMs_ { 0.0 };
    double rangePlayheadStartSeconds_ { 0.0 };
    double rangePlayheadEndSeconds_ { 0.0 };
    double rangePlayheadSeconds_ { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEditorWidget)
};
