#include "SampleRangeOperation.h"

#include "../../../engine/AudioEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

void SampleRangeOperation::activate(AudioEngine* engine)
{
    audioEngine = engine;
}

void SampleRangeOperation::deactivate()
{
    audioEngine = nullptr;
}

std::vector<Knob> SampleRangeOperation::getKnobs(double totalSeconds)
{
    std::vector<Knob> knobs(4);

    totalSampleSeconds_ = totalSeconds;
    const double maxSeconds = juce::jmax(kMinRangeSeconds, totalSeconds);

    // Start knob
    Knob startKnob;
    startKnob.id = "range.start";
    startKnob.label = "Start";
    startKnob.isEnabled = hasSample;
    startKnob.continuousMode = true;

    Knob::NumericModel startModel;
    startModel.value = rangeStartSeconds_;
    startModel.minimum = 0.0;
    startModel.maximum = maxSeconds;
    startModel.step = kFineAdjustStepSeconds;
    startModel.formatter = [](double s) { return SampleRangeOperation::formatTimeString(s); };
    startModel.onChange = [this](double newValue) {
        const double total = juce::jmax(kMinRangeSeconds, totalSampleSeconds_);
        rangeStartSeconds_ = juce::jlimit(0.0, juce::jmax(0.0, total - kMinRangeSeconds), newValue);
        // Drag End forward if Start has crossed it.
        if (rangeEndSeconds_ < rangeStartSeconds_ + kMinRangeSeconds)
            rangeEndSeconds_ = juce::jmin(total, rangeStartSeconds_ + kMinRangeSeconds);
        applyRange();
        notifyThrottledUpdate();
    };
    startKnob.model = startModel;
    knobs[0] = std::move(startKnob);

    // End knob
    Knob endKnob;
    endKnob.id = "range.end";
    endKnob.label = "End";
    endKnob.isEnabled = hasSample;
    endKnob.continuousMode = true;

    Knob::NumericModel endModel;
    endModel.value = rangeEndSeconds_;
    endModel.minimum = 0.0;
    endModel.maximum = maxSeconds;
    endModel.step = kFineAdjustStepSeconds;
    endModel.formatter = [](double s) { return SampleRangeOperation::formatTimeString(s); };
    endModel.onChange = [this](double newValue) {
        const double total = juce::jmax(kMinRangeSeconds, totalSampleSeconds_);
        rangeEndSeconds_ = juce::jlimit(kMinRangeSeconds, total, newValue);
        // Drag Start backward if End has crossed it.
        if (rangeStartSeconds_ > rangeEndSeconds_ - kMinRangeSeconds)
            rangeStartSeconds_ = juce::jmax(0.0, rangeEndSeconds_ - kMinRangeSeconds);
        applyRange();
        notifyThrottledUpdate();
    };
    endKnob.model = endModel;
    knobs[1] = std::move(endKnob);

    // Placeholders
    for (int i = 2; i < 4; ++i)
    {
        Knob placeholder;
        placeholder.id = "unused." + juce::String(i);
        placeholder.label = {};
        placeholder.isEnabled = false;
        knobs[static_cast<size_t>(i)] = std::move(placeholder);
    }

    return knobs;
}

std::vector<Option> SampleRangeOperation::getOptions()
{
    return {};  // Options built by AudioEditorWidget
}

void SampleRangeOperation::refreshFromSnapshot(const SamplerInstrument::PadSnapshot& snapshot)
{
    hasSample = snapshot.hasSample;
    currentPadId = snapshot.id;
    totalSampleSeconds_ = snapshot.totalLengthSeconds;

    // Pull the window from the sampler (authoritative on the pad state).
    if (snapshot.windowEndSeconds > snapshot.windowStartSeconds)
    {
        rangeStartSeconds_ = snapshot.windowStartSeconds;
        rangeEndSeconds_   = snapshot.windowEndSeconds;
    }
    else if (rangeEndSeconds_ <= 0.0 && totalSampleSeconds_ > 0.0)
    {
        rangeStartSeconds_ = 0.0;
        rangeEndSeconds_   = totalSampleSeconds_;
    }
}

void SampleRangeOperation::paintOverlay(juce::Graphics& g,
                                         const juce::Rectangle<int>& waveformArea,
                                         double totalSeconds)
{
    if (totalSeconds <= 0.0)
        return;

    const double visibleStart = visibleEndSeconds_ > visibleStartSeconds_
        ? visibleStartSeconds_ : 0.0;
    const double visibleEnd = visibleEndSeconds_ > visibleStartSeconds_
        ? visibleEndSeconds_ : totalSeconds;
    const double visibleSpan = juce::jmax(kMinRangeSeconds, visibleEnd - visibleStart);

    const float rangeStartX = static_cast<float>(waveformArea.getX()) +
                              static_cast<float>(((rangeStartSeconds_ - visibleStart) / visibleSpan)
                                                 * waveformArea.getWidth());
    const float rangeEndX   = static_cast<float>(waveformArea.getX()) +
                              static_cast<float>(((rangeEndSeconds_ - visibleStart) / visibleSpan)
                                                 * waveformArea.getWidth());

    const float clippedStartX = juce::jlimit(static_cast<float>(waveformArea.getX()),
                                             static_cast<float>(waveformArea.getRight()),
                                             rangeStartX);
    const float clippedEndX = juce::jlimit(static_cast<float>(waveformArea.getX()),
                                           static_cast<float>(waveformArea.getRight()),
                                           rangeEndX);

    // Shade regions outside the range
    const auto fadeColour = juce::Colours::black.withAlpha(0.45f);
    if (clippedStartX > waveformArea.getX())
    {
        g.setColour(fadeColour);
        g.fillRect(juce::Rectangle<float>(static_cast<float>(waveformArea.getX()),
                                          static_cast<float>(waveformArea.getY()),
                                          clippedStartX - static_cast<float>(waveformArea.getX()),
                                          static_cast<float>(waveformArea.getHeight())));
    }
    if (clippedEndX < waveformArea.getRight())
    {
        g.setColour(fadeColour);
        g.fillRect(juce::Rectangle<float>(clippedEndX,
                                          static_cast<float>(waveformArea.getY()),
                                          static_cast<float>(waveformArea.getRight()) - clippedEndX,
                                          static_cast<float>(waveformArea.getHeight())));
    }

    // Range boundary lines
    const auto lineColour = juce::Colours::cornflowerblue.withAlpha(0.55f);
    g.setColour(lineColour);
    g.drawLine(clippedStartX, static_cast<float>(waveformArea.getY()),
               clippedStartX, static_cast<float>(waveformArea.getBottom()), 2.0f);
    g.drawLine(clippedEndX,   static_cast<float>(waveformArea.getY()),
               clippedEndX,   static_cast<float>(waveformArea.getBottom()), 2.0f);
}

void SampleRangeOperation::applyRange()
{
    if (!hasSample || audioEngine == nullptr || currentPadId < 0)
        return;

    audioEngine->getSampler().setPadSampleRange(currentPadId, rangeStartSeconds_, rangeEndSeconds_);
}

juce::String SampleRangeOperation::formatTimeString(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        seconds = 0.0;

    const int totalMs = static_cast<int>(std::round(seconds * 1000.0));
    const int mins = totalMs / 60000;
    const int secs = (totalMs / 1000) % 60;
    const int ms = totalMs % 1000;

    return juce::String::formatted("%d:%02d.%03d", mins, secs, ms);
}
