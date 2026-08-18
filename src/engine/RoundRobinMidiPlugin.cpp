#include "RoundRobinMidiPlugin.h"

#include <cmath>

namespace
{
const juce::Identifier kBaseNoteProp { "baseNote" };
const juce::Identifier kLayerCountProp { "layerCount" };
const juce::Identifier kPolicyProp { "policy" };
const juce::Identifier kVelocityMinimumProp { "velocityMinimum" };
const juce::Identifier kVelocityMaximumProp { "velocityMaximum" };
juce::Identifier weightProp(int layer)
{
    return juce::Identifier("weight" + juce::String(layer + 1));
}
juce::Identifier velocityCurveProp(int layer)
{
    return juce::Identifier("velocityCurve" + juce::String(layer + 1));
}
}

const char* RoundRobinMidiPlugin::xmlTypeName = "maschinepiRoundRobinMidi";

RoundRobinMidiPlugin::RoundRobinMidiPlugin(te::PluginCreationInfo info)
    : te::Plugin(info)
{
    loadConfiguration(state);
}

RoundRobinMidiPlugin::~RoundRobinMidiPlugin()
{
    notifyListenersOfDeletion();
}

void RoundRobinMidiPlugin::configure(int baseNote, int layerCount, Policy policy)
{
    configureWithUndoManager(baseNote, layerCount, policy, getUndoManager());
}

void RoundRobinMidiPlugin::configureRaw(int baseNote, int layerCount, Policy policy)
{
    configureWithUndoManager(baseNote, layerCount, policy, nullptr);
}

void RoundRobinMidiPlugin::configureWithUndoManager(
    int baseNote, int layerCount, Policy policy, juce::UndoManager* undoManager)
{
    const int safeBase = juce::jlimit(0, 127, baseNote);
    const int safeCount = juce::jlimit(1, kMaxLayers, layerCount);
    const auto safePolicy = policy == Policy::Ordered || policy == Policy::Random
        ? policy : Policy::Disabled;

    state.setProperty(kBaseNoteProp, safeBase, undoManager);
    state.setProperty(kLayerCountProp, safeCount, undoManager);
    state.setProperty(kPolicyProp, static_cast<int>(safePolicy), undoManager);

    baseNote_.store(safeBase, std::memory_order_release);
    layerCount_.store(safeCount, std::memory_order_release);
    policy_.store(static_cast<int>(safePolicy), std::memory_order_release);
    orderedCursor_.store(0, std::memory_order_release);
    lastRandomLayer_.store(-1, std::memory_order_release);
    velocityCursor_.store(0, std::memory_order_release);
    lastRandomVelocitySlot_.store(-1, std::memory_order_release);
}

void RoundRobinMidiPlugin::setLayerMix(
    const std::array<float, kMaxLayers>& weights,
    const std::array<int, kMaxLayers>& velocityCurves)
{
    setLayerMixWithUndoManager(weights, velocityCurves, getUndoManager());
}

void RoundRobinMidiPlugin::setLayerMixRaw(
    const std::array<float, kMaxLayers>& weights,
    const std::array<int, kMaxLayers>& velocityCurves)
{
    setLayerMixWithUndoManager(weights, velocityCurves, nullptr);
}

void RoundRobinMidiPlugin::setSingleLayerVelocityRange(float minimum,
                                                        float maximum)
{
    setSingleLayerVelocityRangeWithUndoManager(
        minimum, maximum, getUndoManager());
}

void RoundRobinMidiPlugin::setSingleLayerVelocityRangeRaw(float minimum,
                                                           float maximum)
{
    setSingleLayerVelocityRangeWithUndoManager(minimum, maximum, nullptr);
}

void RoundRobinMidiPlugin::setSingleLayerVelocityRangeWithUndoManager(
    float minimum, float maximum, juce::UndoManager* undoManager)
{
    const float safeMinimum = juce::jlimit(0.01f, 1.0f, minimum);
    const float safeMaximum = juce::jlimit(safeMinimum, 1.0f, maximum);
    state.setProperty(kVelocityMinimumProp, safeMinimum, undoManager);
    state.setProperty(kVelocityMaximumProp, safeMaximum, undoManager);
    velocityMinimum_.store(safeMinimum, std::memory_order_release);
    velocityMaximum_.store(safeMaximum, std::memory_order_release);
    velocityCursor_.store(0, std::memory_order_release);
    lastRandomVelocitySlot_.store(-1, std::memory_order_release);
}

void RoundRobinMidiPlugin::setLayerMixWithUndoManager(
    const std::array<float, kMaxLayers>& weights,
    const std::array<int, kMaxLayers>& velocityCurves,
    juce::UndoManager* undoManager)
{
    for (int i = 0; i < kMaxLayers; ++i)
    {
        const float weight = juce::jlimit(0.0f, 1.0f,
                                         weights[static_cast<size_t>(i)]);
        const int curve = juce::jlimit(0, 3,
                                      velocityCurves[static_cast<size_t>(i)]);
        state.setProperty(weightProp(i), weight, undoManager);
        state.setProperty(velocityCurveProp(i), curve, undoManager);
        weights_[static_cast<size_t>(i)].store(weight, std::memory_order_release);
        velocityCurves_[static_cast<size_t>(i)].store(
            curve, std::memory_order_release);
    }
}

RoundRobinMidiPlugin::Policy RoundRobinMidiPlugin::getPolicy() const noexcept
{
    return static_cast<Policy>(policy_.load(std::memory_order_acquire));
}

int RoundRobinMidiPlugin::getLayerCount() const noexcept
{
    return layerCount_.load(std::memory_order_acquire);
}

juce::String RoundRobinMidiPlugin::getName() const { return "Pad Round Robin"; }
juce::String RoundRobinMidiPlugin::getPluginType() { return xmlTypeName; }
juce::String RoundRobinMidiPlugin::getShortName(int) { return "RR"; }
juce::String RoundRobinMidiPlugin::getSelectableDescription()
{
    return "MusicPI pad round-robin MIDI router";
}

void RoundRobinMidiPlugin::initialise(const te::PluginInitialisationInfo&) {}
void RoundRobinMidiPlugin::deinitialise() {}
double RoundRobinMidiPlugin::getLatencySeconds() { return 0.0; }
int RoundRobinMidiPlugin::getNumOutputChannelsGivenInputs(int) { return 0; }
void RoundRobinMidiPlugin::getChannelNames(juce::StringArray*, juce::StringArray*) {}
bool RoundRobinMidiPlugin::takesAudioInput() { return false; }
bool RoundRobinMidiPlugin::canBeAddedToClip() { return false; }

void RoundRobinMidiPlugin::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.bufferForMidiMessages != nullptr)
        remapMidiMessages(*context.bufferForMidiMessages);
}

void RoundRobinMidiPlugin::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    loadConfiguration(source);
}

void RoundRobinMidiPlugin::midiPanic()
{
    orderedCursor_.store(0, std::memory_order_release);
    lastRandomLayer_.store(-1, std::memory_order_release);
    velocityCursor_.store(0, std::memory_order_release);
    lastRandomVelocitySlot_.store(-1, std::memory_order_release);
}

void RoundRobinMidiPlugin::remapMidiMessages(te::MidiMessageArray& messages) noexcept
{
    const auto policy = getPolicy();
    const int count = getLayerCount();
    if (policy == Policy::Disabled)
        return;

    const int baseNote = baseNote_.load(std::memory_order_acquire);
    for (auto& message : messages)
    {
        if (! message.isNoteOn() || message.getNoteNumber() != baseNote
            || message.getChannel() == kAuditionMidiChannel)
            continue;

        const int layer = count > 1 ? chooseLayer(count, policy) : 0;
        if (count > 1)
            message.setNoteNumber(baseNote + layer * kLayerNoteStride);
        float velocity = message.getFloatVelocity();
        if (count == 1)
            velocity *= chooseSingleLayerVelocityMultiplier(policy);
        switch (velocityCurves_[static_cast<size_t>(layer)].load(
                    std::memory_order_acquire))
        {
            case 1: velocity = std::sqrt(velocity); break;
            case 2: velocity *= velocity; break;
            case 3: velocity = 1.0f; break;
            default: break;
        }
        message.setVelocity(velocity);
    }
}

float RoundRobinMidiPlugin::chooseSingleLayerVelocityMultiplier(
    Policy policy) noexcept
{
    constexpr int kVelocitySlots = 4;
    int slot = 0;
    if (policy == Policy::Ordered)
    {
        slot = velocityCursor_.fetch_add(1, std::memory_order_relaxed)
             % kVelocitySlots;
    }
    else
    {
        auto next = randomState_.load(std::memory_order_relaxed);
        next ^= next << 13;
        next ^= next >> 17;
        next ^= next << 5;
        randomState_.store(next, std::memory_order_relaxed);
        const int previous = lastRandomVelocitySlot_.load(
            std::memory_order_relaxed);
        slot = static_cast<int>(next % (kVelocitySlots - 1));
        if (slot >= previous && previous >= 0)
            ++slot;
        lastRandomVelocitySlot_.store(slot, std::memory_order_relaxed);
    }

    const float minimum = velocityMinimum_.load(std::memory_order_acquire);
    const float maximum = velocityMaximum_.load(std::memory_order_acquire);
    const float proportion = static_cast<float>(slot)
                           / static_cast<float>(kVelocitySlots - 1);
    return minimum + (maximum - minimum) * proportion;
}

int RoundRobinMidiPlugin::chooseLayer(int layerCount, Policy policy) noexcept
{
    if (policy == Policy::Ordered)
    {
        const int selected = orderedCursor_.fetch_add(1, std::memory_order_relaxed)
                           % layerCount;
        return selected;
    }

    // Xorshift32 is allocation-free and lock-free on the audio thread. A
    // zero-weight layer is never selected while any positive-weight layer
    // exists. The previous layer is excluded only when another enabled layer
    // is available, so a single enabled layer still behaves predictably.
    auto next = randomState_.load(std::memory_order_relaxed);
    next ^= next << 13;
    next ^= next >> 17;
    next ^= next << 5;
    randomState_.store(next, std::memory_order_relaxed);

    const int previous = lastRandomLayer_.load(std::memory_order_relaxed);
    float enabledWeight = 0.0f;
    float alternativeWeight = 0.0f;
    for (int i = 0; i < layerCount; ++i)
    {
        const float weight = weights_[static_cast<size_t>(i)].load(
            std::memory_order_relaxed);
        enabledWeight += weight;
        if (i != previous)
            alternativeWeight += weight;
    }

    const bool useUniformWeights = enabledWeight <= 0.0f;
    const bool excludePrevious = useUniformWeights
        ? layerCount > 1 : alternativeWeight > 0.0f;
    const float selectableWeight = useUniformWeights
        ? static_cast<float>(layerCount - (excludePrevious ? 1 : 0))
        : (excludePrevious ? alternativeWeight : enabledWeight);
    float target = (static_cast<float>(next)
                  / static_cast<float>(UINT32_MAX)) * selectableWeight;
    int selected = excludePrevious && previous == 0 ? 1 : 0;
    for (int i = 0; i < layerCount; ++i)
    {
        if (excludePrevious && i == previous)
            continue;
        const float weight = useUniformWeights ? 1.0f
            : weights_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        if (weight <= 0.0f)
            continue;
        selected = i;
        target -= weight;
        if (target <= 0.0f)
            break;
    }

    lastRandomLayer_.store(selected, std::memory_order_relaxed);
    return selected;
}

void RoundRobinMidiPlugin::loadConfiguration(const juce::ValueTree& source) noexcept
{
    const int base = juce::jlimit(0, 127,
        static_cast<int>(source.getProperty(kBaseNoteProp, 36)));
    const int count = juce::jlimit(1, kMaxLayers,
        static_cast<int>(source.getProperty(kLayerCountProp, 1)));
    const int policy = juce::jlimit(
        static_cast<int>(Policy::Disabled), static_cast<int>(Policy::Random),
        static_cast<int>(source.getProperty(kPolicyProp,
                                            static_cast<int>(Policy::Disabled))));

    baseNote_.store(base, std::memory_order_release);
    layerCount_.store(count, std::memory_order_release);
    policy_.store(policy, std::memory_order_release);
    const float velocityMinimum = juce::jlimit(
        0.01f, 1.0f,
        static_cast<float>(source.getProperty(kVelocityMinimumProp, 0.85f)));
    const float velocityMaximum = juce::jlimit(
        velocityMinimum, 1.0f,
        static_cast<float>(source.getProperty(kVelocityMaximumProp, 1.0f)));
    velocityMinimum_.store(velocityMinimum, std::memory_order_release);
    velocityMaximum_.store(velocityMaximum, std::memory_order_release);
    for (int i = 0; i < kMaxLayers; ++i)
    {
        weights_[static_cast<size_t>(i)].store(
            juce::jlimit(0.0f, 1.0f, static_cast<float>(
                source.getProperty(weightProp(i), 1.0f))),
            std::memory_order_release);
        velocityCurves_[static_cast<size_t>(i)].store(
            juce::jlimit(0, 3, static_cast<int>(
                source.getProperty(velocityCurveProp(i), 0))),
            std::memory_order_release);
    }
    orderedCursor_.store(0, std::memory_order_release);
    lastRandomLayer_.store(-1, std::memory_order_release);
    velocityCursor_.store(0, std::memory_order_release);
    lastRandomVelocitySlot_.store(-1, std::memory_order_release);
}
