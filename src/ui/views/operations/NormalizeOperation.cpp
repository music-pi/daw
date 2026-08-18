#include "NormalizeOperation.h"

#include "../../../engine/AudioEngine.h"
#include "../../../engine/commands/NormalizeSampleCommand.h"

#include <juce_audio_basics/juce_audio_basics.h>

void NormalizeOperation::activate(AudioEngine* engine)
{
    audioEngine = engine;
}

void NormalizeOperation::deactivate()
{
    audioEngine = nullptr;
}

std::vector<Knob> NormalizeOperation::getKnobs(double /*totalSeconds*/)
{
    std::vector<Knob> knobs(4);

    // Target dB knob
    Knob targetKnob;
    targetKnob.id = "normalize.target";
    targetKnob.label = "Target";
    targetKnob.isEnabled = hasSample;

    Knob::NumericModel targetModel;
    targetModel.value = targetDb_;
    targetModel.minimum = -96.0;
    targetModel.maximum = 6.0;
    targetModel.step = 0.1;
    targetModel.formatter = [](double db) { return juce::String::formatted("%.1f dB", db); };
    targetModel.onChange = [this](double newValue) {
        targetDb_ = juce::jlimit(-96.0, 6.0, newValue);
        notifyThrottledUpdate();
    };
    targetKnob.model = targetModel;
    targetKnob.continuousMode = true;
    knobs[0] = std::move(targetKnob);

    // Even Factor knob
    Knob evenFactorKnob;
    evenFactorKnob.id = "normalize.evenFactor";
    evenFactorKnob.label = "Even Factor";
    evenFactorKnob.isEnabled = hasSample;

    Knob::NumericModel evenFactorModel;
    evenFactorModel.value = evenFactor_;
    evenFactorModel.minimum = 0.0;
    evenFactorModel.maximum = 1.0;
    evenFactorModel.step = 0.005;
    evenFactorModel.formatter = [](double value) { return juce::String::formatted("%.0f%%", value * 100.0); };
    evenFactorModel.onChange = [this](double newValue) {
        evenFactor_ = juce::jlimit(0.0, 1.0, newValue);
        notifyThrottledUpdate();
    };
    evenFactorKnob.model = evenFactorModel;
    evenFactorKnob.continuousMode = true;
    knobs[1] = std::move(evenFactorKnob);

    // Unused knobs (placeholders)
    Knob placeholderA;
    placeholderA.id = "unused.1";
    placeholderA.label = {};
    placeholderA.isEnabled = false;
    knobs[2] = std::move(placeholderA);

    Knob placeholderB;
    placeholderB.id = "unused.2";
    placeholderB.label = {};
    placeholderB.isEnabled = false;
    knobs[3] = std::move(placeholderB);

    return knobs;
}

std::vector<Option> NormalizeOperation::getOptions()
{
    // Options are handled by AudioEditor's configureOptions
    // This method returns an empty vector as the operation-specific
    // options are combined with common options by the orchestrator
    return {};
}

void NormalizeOperation::refreshFromSnapshot(const SamplerInstrument::PadSnapshot& snapshot)
{
    hasSample = snapshot.hasSample;
    currentPadId = snapshot.id;

    // Reset values when pad changes
    if (lastPadId_ != snapshot.id)
    {
        lastPadId_ = snapshot.id;
        targetDb_ = -1.0;
        evenFactor_ = 0.0;
    }
}

void NormalizeOperation::paintOverlay(juce::Graphics& g,
                                       const juce::Rectangle<int>& waveformArea,
                                       double /*totalSeconds*/)
{
    if (waveformBins_.empty())
        return;

    const float midY = static_cast<float>(waveformArea.getCentreY());
    const float halfHeight = static_cast<float>(waveformArea.getHeight()) / 2.0f;
    const int binCount = static_cast<int>(waveformBins_.size());

    // Calculate current peak level
    float currentPeak = 0.0f;
    for (const auto& bin : waveformBins_)
    {
        currentPeak = juce::jmax(currentPeak, std::abs(bin.max), std::abs(bin.min));
    }

    if (currentPeak <= 0.0f)
        return;

    // Calculate base gain to reach target dB
    const float targetLinear = juce::Decibels::decibelsToGain(static_cast<float>(targetDb_));
    float baseGain = targetLinear / currentPeak;

    // Apply even factor: analyze waveform in chunks and smooth gain variations
    const int chunkSize = juce::jmax(1, binCount / 32); // 32 chunks for smoothing
    std::vector<float> chunkGains;

    const float evenAmount = juce::jlimit(0.0f, 1.0f, static_cast<float>(evenFactor_));

    if (evenAmount > 0.0f && chunkSize > 0)
    {
        // Calculate gain per chunk based on local peak
        for (int chunk = 0; chunk < (binCount + chunkSize - 1) / chunkSize; ++chunk)
        {
            const int startBin = chunk * chunkSize;
            const int endBin = juce::jmin(startBin + chunkSize, binCount);

            float chunkPeak = 0.0f;
            for (int i = startBin; i < endBin; ++i)
            {
                chunkPeak = juce::jmax(chunkPeak, std::abs(waveformBins_[static_cast<size_t>(i)].max),
                                       std::abs(waveformBins_[static_cast<size_t>(i)].min));
            }

            if (chunkPeak > 0.0f)
            {
                const float chunkGain = targetLinear / chunkPeak;
                chunkGains.push_back(chunkGain);
            }
            else
            {
                chunkGains.push_back(baseGain);
            }
        }

        // Smooth chunk gains based on even factor
        if (chunkGains.size() > 1)
        {
            std::vector<float> smoothedGains = chunkGains;
            for (size_t i = 1; i < chunkGains.size() - 1; ++i)
            {
                const float avg = (chunkGains[i - 1] + chunkGains[i] + chunkGains[i + 1]) / 3.0f;
                smoothedGains[i] = chunkGains[i] * (1.0f - evenAmount) + avg * evenAmount;
            }
            chunkGains = smoothedGains;
        }
    }

    // Draw normalized waveform preview
    auto toX = [waveformArea, binCount](int index)
    {
        if (binCount <= 1)
            return static_cast<float>(waveformArea.getX());
        const double proportion = static_cast<double>(index) / static_cast<double>(binCount - 1);
        return static_cast<float>(waveformArea.getX() + proportion * waveformArea.getWidth());
    };

    juce::Path normalizedWaveformPath;
    for (int i = 0; i < binCount; ++i)
    {
        const float x = toX(i);

        // Calculate gain for this bin
        float gain = baseGain;
        if (evenAmount > 0.0f && !chunkGains.empty() && chunkSize > 0)
        {
            const int chunk = i / chunkSize;
            const int clampedChunk = juce::jlimit(0, static_cast<int>(chunkGains.size()) - 1, chunk);
            gain = chunkGains[static_cast<size_t>(clampedChunk)];
        }

        const float adjustedMax = waveformBins_[static_cast<size_t>(i)].max * gain;
        const float y = midY - adjustedMax * halfHeight;

        if (i == 0)
            normalizedWaveformPath.startNewSubPath(x, y);
        else
            normalizedWaveformPath.lineTo(x, y);
    }
    for (int i = binCount - 1; i >= 0; --i)
    {
        const float x = toX(i);

        // Calculate gain for this bin
        float gain = baseGain;
        if (evenAmount > 0.0f && !chunkGains.empty() && chunkSize > 0)
        {
            const int chunk = i / chunkSize;
            const int clampedChunk = juce::jlimit(0, static_cast<int>(chunkGains.size()) - 1, chunk);
            gain = chunkGains[static_cast<size_t>(clampedChunk)];
        }

        const float adjustedMin = waveformBins_[static_cast<size_t>(i)].min * gain;
        const float y = midY - adjustedMin * halfHeight;
        normalizedWaveformPath.lineTo(x, y);
    }
    normalizedWaveformPath.closeSubPath();

    g.setColour(juce::Colours::cyan.withAlpha(0.4f));
    g.fillPath(normalizedWaveformPath);
    g.setColour(juce::Colours::cyan.withAlpha(0.7f));
    g.strokePath(normalizedWaveformPath, juce::PathStrokeType(1.5f));
}

bool NormalizeOperation::canApply() const
{
    return hasSample && currentPadId >= 0 && audioEngine != nullptr;
}

void NormalizeOperation::apply(AudioEngine& engine, int padId, juce::UndoManager& undoManager)
{
    if (!canApply())
        return;

    undoManager.beginNewTransaction("Normalize Sample");
    undoManager.perform(new NormalizeSampleCommand(engine, padId, targetDb_));
}
