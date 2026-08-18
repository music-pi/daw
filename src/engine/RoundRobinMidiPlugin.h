#pragma once

#include <atomic>
#include <array>
#include <cstdint>

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/** Audio-thread MIDI router used by round-robin sample pads.

    A pad's pattern and live input continue to emit the pad's canonical MIDI
    note. This plugin remaps each note-on to one of four layer notes before the
    track's SamplerPlugin sees it, preserving Tracktion-native sample-accurate
    sequencing. */
class RoundRobinMidiPlugin final : public te::Plugin
{
public:
    enum class Policy
    {
        Disabled = 0,
        Ordered,
        Random
    };

    explicit RoundRobinMidiPlugin(te::PluginCreationInfo);
    ~RoundRobinMidiPlugin() override;

    static constexpr int kLayerNoteStride = 16;
    static constexpr int kMaxLayers = 4;
    static constexpr int kAuditionMidiChannel = 16;
    static const char* getPluginName() { return "Pad Round Robin"; }
    static const char* xmlTypeName;

    void configure(int baseNote, int layerCount, Policy);
    void configureRaw(int baseNote, int layerCount, Policy);
    void setLayerMix(const std::array<float, kMaxLayers>& weights,
                     const std::array<int, kMaxLayers>& velocityCurves);
    void setLayerMixRaw(const std::array<float, kMaxLayers>& weights,
                        const std::array<int, kMaxLayers>& velocityCurves);
    void setSingleLayerVelocityRange(float minimum, float maximum);
    void setSingleLayerVelocityRangeRaw(float minimum, float maximum);
    [[nodiscard]] Policy getPolicy() const noexcept;
    [[nodiscard]] int getLayerCount() const noexcept;

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    double getLatencySeconds() override;
    int getNumOutputChannelsGivenInputs(int) override;
    void getChannelNames(juce::StringArray*, juce::StringArray*) override;
    bool takesAudioInput() override;
    bool canBeAddedToClip() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;
    void midiPanic() override;

    /** Public for deterministic engine tests; applyToBuffer delegates here. */
    void remapMidiMessages(te::MidiMessageArray&) noexcept;

private:
    int chooseLayer(int layerCount, Policy) noexcept;
    void configureWithUndoManager(int baseNote, int layerCount, Policy,
                                  juce::UndoManager*);
    void setLayerMixWithUndoManager(
        const std::array<float, kMaxLayers>&,
        const std::array<int, kMaxLayers>&, juce::UndoManager*);
    void setSingleLayerVelocityRangeWithUndoManager(
        float minimum, float maximum, juce::UndoManager*);
    float chooseSingleLayerVelocityMultiplier(Policy) noexcept;
    void loadConfiguration(const juce::ValueTree&) noexcept;

    std::atomic<int> baseNote_ { 36 };
    std::atomic<int> layerCount_ { 1 };
    std::atomic<int> policy_ { static_cast<int>(Policy::Disabled) };
    std::array<std::atomic<float>, kMaxLayers> weights_ {
        1.0f, 1.0f, 1.0f, 1.0f
    };
    std::array<std::atomic<int>, kMaxLayers> velocityCurves_ {
        0, 0, 0, 0
    };
    std::atomic<int> orderedCursor_ { 0 };
    std::atomic<int> lastRandomLayer_ { -1 };
    std::atomic<float> velocityMinimum_ { 0.85f };
    std::atomic<float> velocityMaximum_ { 1.0f };
    std::atomic<int> velocityCursor_ { 0 };
    std::atomic<int> lastRandomVelocitySlot_ { -1 };
    std::atomic<uint32_t> randomState_ { 0x6d2b79f5u };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoundRobinMidiPlugin)
};
