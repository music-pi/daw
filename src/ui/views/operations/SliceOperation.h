#pragma once

#include "EditorOperation.h"
#include <functional>
#include <vector>

class ISamplePreview;

/**
 * SliceOperation for AudioEditorWidget.
 *
 * Two modes (selected via K0):
 *   • Straight   — equal-duration slices. K1 picks count from {4, 8, 16}.
 *   • Transient  — slice at detected onsets. K1 is a 0..1 sensitivity knob
 *                  (higher = more slices). Boundaries are computed by
 *                  TransientDetector (Tracktion's BeatDetect).
 *
 * Overlay: vertical tick lines at each slice boundary (UiTheme::kAccentOrange).
 * Labels (1-N) are drawn when the count is small enough not to crowd.
 *
 * The Apply option is disabled when no sample is loaded, or (Transient) when
 * analysis found no boundaries.
 */
class SliceOperation : public EditorOperation
{
public:
    enum class Type { Straight, Transient, Manual };
    struct ManualSliceRange
    {
        double startSeconds { 0.0 };
        double endSeconds { 0.0 };
    };

    SliceOperation() = default;
    ~SliceOperation() override = default;

    // EditorOperation interface
    void activate(AudioEngine* engine) override;
    void deactivate() override;

    juce::String getName() const override { return "Slice"; }
    std::vector<Knob> getKnobs(double totalSeconds) override;
    std::vector<Option> getOptions() override;

    void refreshFromSnapshot(const SamplerInstrument::PadSnapshot& snapshot) override;

    void paintOverlay(juce::Graphics& g,
                      const juce::Rectangle<int>& waveformArea,
                      double totalSeconds) override;

    bool canApply() const override;
    void apply(AudioEngine& engine, int padId, juce::UndoManager& undoManager) override;

    // Accessors
    Type getType() const { return type_; }
    int getSliceCount() const { return sliceCount_; }
    void setSliceCount(int count);
    int getLastAppliedSliceCount() const { return lastAppliedSliceCount_; }

    /** True if the last apply() call's UndoableAction returned false. */
    bool didLastApplyFail() const { return lastApplyFailed_; }

    // Pad-audition/manual-mode accessors (test seams + consumer query)
    const std::vector<double>& getCapturedStarts() const { return capturedStarts_; }
    const std::vector<double>& getCapturedEnds() const { return capturedEnds_; }
    std::vector<ManualSliceRange> getManualSliceRanges() const;
    bool isCapturing() const { return capturing_; }
    bool isEraseHeld() const { return eraseHeld_; }
    int  getActiveSliceIndex() const { return activeSliceIndex_; }
    double getAuditionEndSeconds() const { return auditionEndSeconds_; }
    bool isOverlapping() const { return overlapping_; }
    void setOverlapping(bool overlapping);
    bool selectManualSlice(int index);
    bool selectAdjacentManualSlice(int direction);

    /** Adjust the currently-active captured start by deltaSeconds, clamped
        to [prev + kMinSliceSeconds_, next - kMinSliceSeconds_]. No-op if
        Manual is inactive or there is no active slice. */
    void nudgeActiveSlice(double deltaSeconds);

    /** Undo/redo temporary Manual-mode boundary edits before Apply commits
        them to the engine UndoManager. Returns false when no local edit is
        available in that direction. */
    bool undoManualEdit();
    bool redoManualEdit();

    // Preview player seam (borrowed, non-owning). Set by AudioEditorWidget on
    // activate(); set to nullptr on deactivate(). Tests inject a FakeSamplePreview.
    void setPreview(ISamplePreview* preview) { preview_ = preview; }

    // Test-only mutator: flip to a specific type without going through K0.
    void setType(Type t) { type_ = t; }

    // Pad press handler. Straight/Transient audition the corresponding proposed
    // slice; Manual preserves its capture/audition gesture. padIdx is 0-based.
    void onPadPressed(int padIdx);

    // Manual-mode Erase button handler. `pressed` is the hardware press/release
    // state for `eraseReplace`. Behaviour:
    //   press: arms eraseHeld_, clears erasePadActed_
    //   release without a pad in between: invokes onEraseAll_ (opens confirm dialog)
    void onEraseEvent(bool pressed);

    // Invoked when the user "taps" Erase (no pad pressed during the hold). The
    // host widget wires this to a confirm dialog.
    using EraseAllCallback = std::function<void()>;
    void setOnEraseAll(EraseAllCallback cb) { onEraseAll_ = std::move(cb); }

    // Host callback for the confirm dialog's "Erase" action: wipes captures +
    // stops preview. Public so the widget can call it on confirm.
    void confirmEraseAll();

    // Callback support for throttled UI updates
    using ThrottleCallback = std::function<void()>;
    void setThrottleCallback(ThrottleCallback callback) { throttleCallback_ = std::move(callback); }

private:
    static constexpr double kMinSliceSeconds_ = 0.010;

    /** Snap raw value to nearest valid count in {4, 8, 16}. */
    static int snapToValidCount(double raw);

    void notifyThrottledUpdate()
    {
        if (throttleCallback_)
            throttleCallback_();
    }

    /** Runs transient detection if (type=Transient) and the cached result is
        stale relative to the current file + sensitivity. Updates
        detectedStarts_ in place. Safe to call repeatedly. */
    void ensureAnalysisUpToDate();
    void beginManualEdit();
    void restoreManualState();
    void makeManualRangesContiguous();
    void clearManualHistory();

    ThrottleCallback throttleCallback_;

    Type type_ { Type::Straight };
    int sliceCount_ { 16 };              // Straight mode
    double sensitivity_ { 0.5 };         // Transient mode (0..1)

    // Window (the range set on the Sample Range step) — slicing operates
    // within this window, not the full file.
    double windowStartSeconds_ { 0.0 };
    double windowEndSeconds_ { 0.0 };

    // Transient analysis cache. Starts are absolute file seconds (first
    // entry == windowStartSeconds_).
    std::vector<double> detectedStarts_;
    juce::File currentSampleFile_;
    juce::File lastAnalysedFile_;
    double lastAnalysedSensitivity_ { -1.0 };
    double lastAnalysedStart_ { -1.0 };
    double lastAnalysedEnd_ { -1.0 };

    int lastAppliedSliceCount_ { 0 };
    bool lastApplyFailed_ { false };

    // Manual mode state
    std::vector<double> capturedStarts_;  // absolute file seconds
    std::vector<double> capturedEnds_;    // explicit per-slice end seconds
    bool capturing_   { false };
    bool overlapping_ { false };
    bool eraseHeld_   { false };
    bool erasePadActed_ { false };
    int  activeSliceIndex_ { -1 };  // -1 = none selected; 0..N-1 otherwise
    double auditionEndSeconds_ { 0.0 }; // host stops preview at this boundary
    struct ManualState
    {
        std::vector<double> starts;
        std::vector<double> ends;
        bool overlapping { false };
        int activeSliceIndex { -1 };
    };
    std::vector<ManualState> manualUndoStack_;
    std::vector<ManualState> manualRedoStack_;

    ISamplePreview* preview_ { nullptr };  // non-owning, injected

    EraseAllCallback onEraseAll_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceOperation)
};
