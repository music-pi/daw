#pragma once

#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include "../../src/engine/AudioEngine.h"

namespace testharness
{

class EngineHarness
{
public:
    EngineHarness();
    ~EngineHarness();

    tracktion::engine::Engine& engine() { return engineInstance; }
    AudioEngine& audio();
    SamplerInstrument& pads();

    void createEmptyEdit();

    juce::File createTemporarySampleFile(const juce::String& name,
                                         int lengthSamples,
                                         double sampleRate = 44100.0,
                                         double frequencyHz = 0.0);

private:
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::engine::Engine engineInstance;
    std::unique_ptr<AudioEngine> audioEngine;
    juce::File harnessRoot;
};

} // namespace testharness
