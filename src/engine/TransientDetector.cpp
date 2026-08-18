#include "TransientDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>

namespace TransientDetector
{

std::vector<double> detectSliceStarts(tracktion::engine::Engine& engine,
                                       const juce::File& audioFile,
                                       double sensitivity,
                                       double startSeconds,
                                       double endSeconds,
                                       int maxSlices)
{
    std::vector<double> starts;

    if (! audioFile.existsAsFile() || maxSlices < 1)
        return starts;

    std::unique_ptr<juce::AudioFormatReader> reader(
        tracktion::engine::AudioFileUtils::createReaderFor(engine, audioFile));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return starts;

    const auto sampleRate = reader->sampleRate;
    const auto totalSamples = reader->lengthInSamples;
    const auto fileDuration = static_cast<double>(totalSamples) / sampleRate;

    // Clamp analysis window to the file.
    const double winStart = juce::jlimit(0.0, fileDuration, startSeconds);
    const double winEnd   = (endSeconds > winStart) ? juce::jmin(endSeconds, fileDuration)
                                                    : fileDuration;
    if (winEnd <= winStart + 1e-6)
    {
        starts.push_back(winStart);
        return starts;
    }

    // Widen the knob→threshold mapping so the user sees a bigger spread
    // between "many slices" (permissive) and "few" (strict). BeatDetect's
    // built-in setSensitivity maps 0..1 → 0.8..4.0; we pre-compute a wider
    // range and drive it via a virtual 0..1 input.
    //    knob 0.0 → threshold 0.4  (very permissive, lots of onsets)
    //    knob 0.5 → threshold 2.5
    //    knob 1.0 → threshold 8.0  (only the strongest peaks)
    const double knob = juce::jlimit(0.0, 1.0, sensitivity);
    const double thresholdMultiplier = 0.4 + knob * 7.6;
    const double beatDetectInput = (thresholdMultiplier - 0.8) / (4.0 - 0.8);

    tracktion::engine::BeatDetect detect;
    detect.setSensitivity(beatDetectInput);
    detect.setSampleRate(sampleRate);

    const auto blockLength = detect.getBlockSize();
    const auto startSample = static_cast<juce::int64>(winStart * sampleRate);
    const auto endSample   = static_cast<juce::int64>(winEnd   * sampleRate);

    if (blockLength <= 0 || endSample - startSample < static_cast<juce::int64>(sampleRate))
    {
        // Window too short to analyse reliably — fall back to "first slice only".
        starts.push_back(winStart);
        return starts;
    }

    auto bufferSize = choc::buffer::Size::create(reader->numChannels, blockLength);
    choc::buffer::ChannelArrayBuffer<float> buffer(bufferSize);

    juce::int64 pos = startSample;
    while (pos + blockLength < endSample)
    {
        if (! reader->read(buffer.getView().data.channels,
                           static_cast<int>(reader->numChannels),
                           pos, static_cast<int>(blockLength)))
            break;

        detect.audioProcess(buffer);
        pos += blockLength;
    }

    // BeatDetect returns beat positions as (blockIndex * blockSize), relative
    // to where we started processing — convert to absolute file seconds.
    const auto& beats = detect.getBeats();
    starts.push_back(winStart);
    for (auto beatSample : beats)
    {
        const double relativeSec = static_cast<double>(beatSample) / sampleRate;
        const double absoluteSec = winStart + relativeSec;
        if (absoluteSec >= winEnd - 1e-6)
            break;
        // Skip anything too close to the previous start (< 20 ms).
        if (absoluteSec - starts.back() < 0.020)
            continue;
        starts.push_back(absoluteSec);
    }

    // Cap at maxSlices by even downsampling. Keep the first entry (window
    // start) so slice 0 always begins at winStart.
    if (static_cast<int>(starts.size()) > maxSlices)
    {
        std::vector<double> thinned;
        thinned.reserve(static_cast<size_t>(maxSlices));
        const double step = static_cast<double>(starts.size()) / static_cast<double>(maxSlices);
        for (int i = 0; i < maxSlices; ++i)
        {
            const size_t idx = static_cast<size_t>(static_cast<double>(i) * step);
            thinned.push_back(starts[juce::jmin(idx, starts.size() - 1)]);
        }
        starts = std::move(thinned);
    }

    return starts;
}

} // namespace TransientDetector
