#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_core/juce_core.h>
#include <vector>

class AudioEngine;

/**
 * SliceSampleCommand — undoable action that slices a source pad sample into N
 * slices, **non-destructively**.
 *
 * Two input shapes:
 *   • Equal division — pass a slice count; perform() computes N equal-duration
 *     boundaries from the source file's length.
 *   • Explicit starts — pass a vector of absolute file start times (seconds).
 *     Slice i runs from starts[i] to starts[i+1], and the last slice ends at
 *     the supplied editing-window end (or EOF when no end is supplied).
 *     Used by transient-based slicing where boundaries come from onset
 *     detection.
 *
 * Each of pads 0..N-1 gets pointed at the SAME source file as the source pad,
 * but with a different `[startSeconds, endSeconds]` range. No audio data is
 * written to disk; slicing is pure metadata on the TE SamplerPlugin (the same
 * mechanism Trim uses).
 *
 * Undo restores each pad's previous (file, range, gain, normalization,
 * chokeGroup) state.
 */
class SliceSampleCommand : public juce::UndoableAction
{
public:
    SliceSampleCommand(AudioEngine& engine, int sourcePadIndex, int sliceCount);

    /** Explicit-starts constructor.
        @param sliceStartSeconds  Absolute file times (seconds) for each slice
                                   start. The first value is preserved so a
                                   trimmed editing window never regains lead-in.
        @param endSeconds          Absolute end time (seconds) for the last
                                   slice. If <= 0 or > source duration, falls
                                   back to the source file EOF. */
    SliceSampleCommand(AudioEngine& engine, int sourcePadIndex,
                       std::vector<double> sliceStartSeconds,
                       double endSeconds = 0.0);

    /** Explicit independent ranges, used by Manual slicing when overlapping
        slices are enabled. Starts and ends are paired by index. */
    SliceSampleCommand(AudioEngine& engine, int sourcePadIndex,
                       std::vector<double> sliceStartSeconds,
                       std::vector<double> sliceEndSeconds,
                       double endSeconds);
    ~SliceSampleCommand() override = default;

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Slice Sample"; }

    /**
     * Compute the start time (seconds) for slice i of a sample with
     * `totalSeconds` duration split into `sliceCount` equal pieces. Exposed
     * as a static helper so tests can verify boundary math without a full
     * engine.
     */
    static double sliceStartSeconds(double totalSeconds, int sliceCount, int i);

    /** End time (seconds, exclusive) for slice i. */
    static double sliceEndSeconds(double totalSeconds, int sliceCount, int i);

private:
    struct PreviousPadState
    {
        juce::File sampleFile;          // empty if pad had no sample
        std::vector<juce::File> sampleLayers;
        std::vector<float> layerGainsDb;
        std::vector<float> layerWeights;
        std::vector<int> layerVelocityCurves;
        std::vector<float> layerVelocityMinimums;
        std::vector<float> layerVelocityMaximums;
        bool hadSample { false };
        // Pad state that loadSample() / setPadSampleRange() touch.
        double rangeStartSeconds { 0.0 };
        double rangeEndSeconds { 0.0 };
        float normalizationGainDb { 0.0f };
        float gainDb { 0.0f };
        int chokeGroup { 0 };
        int triggerMode { 0 };
    };

    AudioEngine& engine_;
    const int sourcePadIndex_;

    // Slice layout. Either filled at construction (explicit-starts ctor) or
    // computed on first perform() from sliceCountPending_ (count-based ctor).
    std::vector<double> sliceStarts_;
    std::vector<double> sliceEnds_;
    const int sliceCountPending_ { 0 };  // >0 ⇒ compute equal division in perform()
    const double explicitEndSeconds_ { 0.0 };  // <= 0 ⇒ use source EOF

    // Captured on first perform()
    juce::File sourceFile_;
    double sourceTotalSeconds_ { 0.0 };
    std::vector<PreviousPadState> previousPads_;
    bool hasCapturedState_ { false };
    bool performed_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceSampleCommand)
};
