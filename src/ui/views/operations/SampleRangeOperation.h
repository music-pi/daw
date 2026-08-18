#pragma once

#include "EditorOperation.h"
#include <vector>

/**
 * SampleRangeOperation — non-destructive sample trimming for AudioEditorWidget.
 *
 * The Start/End knobs set the pad's sample-range metadata (same mechanism
 * PadDetails uses, via SamplerInstrument::setPadSampleRange). No file is
 * rewritten; downstream operations (Slice, Normalize) see only the window
 * defined here. Changes are applied live on every knob tick.
 *
 * This is the first step in the editor workflow: decide what portion of the
 * sample is "the sample" going forward.
 */
class SampleRangeOperation : public EditorOperation
{
public:
    SampleRangeOperation() = default;
    ~SampleRangeOperation() override = default;

    // EditorOperation interface
    void activate(AudioEngine* engine) override;
    void deactivate() override;

    juce::String getName() const override { return "Range"; }
    std::vector<Knob> getKnobs(double totalSeconds) override;
    std::vector<Option> getOptions() override;

    void refreshFromSnapshot(const SamplerInstrument::PadSnapshot& snapshot) override;

    void paintOverlay(juce::Graphics& g,
                      const juce::Rectangle<int>& waveformArea,
                      double totalSeconds) override;

    bool canApply() const override { return false; }  // live, no-op apply
    void apply(AudioEngine& /*engine*/, int /*padId*/, juce::UndoManager& /*undoManager*/) override {}

    // State accessors
    double getRangeStart() const { return rangeStartSeconds_; }
    double getRangeEnd() const { return rangeEndSeconds_; }

    /** Set the absolute file-time window currently visible in the editor. */
    void setVisibleRange(double startSeconds, double endSeconds)
    {
        visibleStartSeconds_ = startSeconds;
        visibleEndSeconds_ = endSeconds;
    }

    // Callback support for throttled UI updates
    using ThrottleCallback = std::function<void()>;
    void setThrottleCallback(ThrottleCallback callback) { throttleCallback_ = std::move(callback); }

private:
    void notifyThrottledUpdate()
    {
        if (throttleCallback_)
            throttleCallback_();
    }

    /** Push the current range to the sampler as sample-range metadata. */
    void applyRange();

    ThrottleCallback throttleCallback_;

    static constexpr double kMinRangeSeconds = 0.001;
    static constexpr double kFineAdjustStepSeconds = 0.001;

    double rangeStartSeconds_ { 0.0 };
    double rangeEndSeconds_ { 0.0 };
    double totalSampleSeconds_ { 0.0 };
    double visibleStartSeconds_ { 0.0 };
    double visibleEndSeconds_ { 0.0 };

    static juce::String formatTimeString(double seconds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleRangeOperation)
};
