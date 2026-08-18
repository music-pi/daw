#pragma once

#include "EditorOperation.h"
#include <vector>

/**
 * Normalize operation for AudioEditorWidget.
 *
 * Adjusts audio levels to reach a target dB with optional evening across
 * the waveform. Provides live preview overlay showing normalized result.
 */
class NormalizeOperation : public EditorOperation
{
public:
    NormalizeOperation() = default;
    ~NormalizeOperation() override = default;

    // EditorOperation interface
    void activate(AudioEngine* engine) override;
    void deactivate() override;

    juce::String getName() const override { return "Normalize"; }
    std::vector<Knob> getKnobs(double totalSeconds) override;
    std::vector<Option> getOptions() override;

    void refreshFromSnapshot(const SamplerInstrument::PadSnapshot& snapshot) override;

    void paintOverlay(juce::Graphics& g,
                      const juce::Rectangle<int>& waveformArea,
                      double totalSeconds) override;

    bool canApply() const override;
    void apply(AudioEngine& engine, int padId, juce::UndoManager& undoManager) override;

    // Waveform data access (set by AudioEditor before paint)
    struct WaveformBin
    {
        float min { 0.0f };
        float max { 0.0f };
    };
    void setWaveformBins(const std::vector<WaveformBin>& bins) { waveformBins_ = bins; }

    // State accessors
    double getTargetDb() const { return targetDb_; }
    double getEvenFactor() const { return evenFactor_; }
    void setTargetDb(double db) { targetDb_ = juce::jlimit(-96.0, 6.0, db); }
    void setEvenFactor(double factor) { evenFactor_ = juce::jlimit(0.0, 1.0, factor); }

    // Callback support for throttled UI updates
    using ThrottleCallback = std::function<void()>;
    void setThrottleCallback(ThrottleCallback callback) { throttleCallback_ = std::move(callback); }

private:
    void notifyThrottledUpdate() {
        if (throttleCallback_)
            throttleCallback_();
        // Note: If no callback set, the caller should ensure repaint happens
    }

    ThrottleCallback throttleCallback_;
    // State
    double targetDb_ { -1.0 };       // Target level in dB (default -1dB)
    double evenFactor_ { 0.0 };      // 0.0 = no smoothing, 1.0 = max smoothing
    int lastPadId_ { -1 };           // Track which pad the values belong to
    std::vector<WaveformBin> waveformBins_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NormalizeOperation)
};
