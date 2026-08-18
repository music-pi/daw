#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

class AudioEngine;

/**
 * LoadSampleCommand — undoable load of a sample file onto a pad.
 *
 * Uses SamplerInstrument::loadSampleRaw / clearSampleRaw so the sampler's
 * ValueTree is mutated with a null UndoManager — otherwise the nested
 * state.removeChild(um) calls inside TE's SamplerPlugin::removeSound would
 * be blocked by UndoManager re-entrance and the caller's `while
 * (getNumSounds() > 0)` loop would livelock the app during undo.
 */
class LoadSampleCommand : public juce::UndoableAction
{
public:
    LoadSampleCommand(AudioEngine& engine, int padIndex, juce::File newFile);
    ~LoadSampleCommand() override = default;

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Load Sample"; }

private:
    struct PadState
    {
        bool hadSample { false };
        juce::File sampleFile;
        std::vector<juce::File> sampleLayers;
        std::vector<float> layerGainsDb;
        std::vector<float> layerWeights;
        std::vector<int> layerVelocityCurves;
        std::vector<float> layerVelocityMinimums;
        std::vector<float> layerVelocityMaximums;
        int triggerMode { 0 };
        double rangeStartSeconds { 0.0 };
        double rangeEndSeconds { 0.0 };
        float normalizationGainDb { 0.0f };
        float gainDb { 0.0f };
        int chokeGroup { 0 };
    };

    AudioEngine& engine_;
    const int padIndex_;
    const juce::File newFile_;

    PadState previous_;
    bool hasCapturedState_ { false };
    bool performed_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadSampleCommand)
};
