#include "SliceSampleCommand.h"

#include "../AudioEngine.h"
#include "../SamplerInstrument.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <cmath>

SliceSampleCommand::SliceSampleCommand(AudioEngine& engine, int sourcePadIndex, int sliceCount)
    : engine_(engine)
    , sourcePadIndex_(sourcePadIndex)
    , sliceCountPending_(juce::jlimit(1, 16, sliceCount))
{
}

SliceSampleCommand::SliceSampleCommand(AudioEngine& engine, int sourcePadIndex,
                                         std::vector<double> sliceStartSeconds,
                                         double endSeconds)
    : engine_(engine)
    , sourcePadIndex_(sourcePadIndex)
    , sliceStarts_(std::move(sliceStartSeconds))
    , explicitEndSeconds_(endSeconds > 0.0 ? endSeconds : 0.0)
{
    // Sanitise absolute file positions without changing the caller's editing
    // window. In particular, do not inject 0.0: explicit starts from the
    // audio editor may intentionally begin after trimmed lead-in audio.
    std::sort(sliceStarts_.begin(), sliceStarts_.end());
    for (auto& s : sliceStarts_) if (s < 0.0) s = 0.0;
    sliceStarts_.erase(
        std::unique(sliceStarts_.begin(), sliceStarts_.end(),
                    [](double a, double b) { return std::abs(a - b) < 1e-6; }),
        sliceStarts_.end());
    if (sliceStarts_.size() > 16)
        sliceStarts_.resize(16);
}

SliceSampleCommand::SliceSampleCommand(AudioEngine& engine, int sourcePadIndex,
                                         std::vector<double> sliceStartSeconds,
                                         std::vector<double> sliceEndSeconds,
                                         double endSeconds)
    : engine_(engine)
    , sourcePadIndex_(sourcePadIndex)
    , sliceStarts_(std::move(sliceStartSeconds))
    , sliceEnds_(std::move(sliceEndSeconds))
    , explicitEndSeconds_(endSeconds > 0.0 ? endSeconds : 0.0)
{
    const size_t count = juce::jmin(
        static_cast<size_t>(16),
        juce::jmin(sliceStarts_.size(), sliceEnds_.size()));
    sliceStarts_.resize(count);
    sliceEnds_.resize(count);
    for (auto& start : sliceStarts_)
        start = juce::jmax(0.0, start);
    for (auto& end : sliceEnds_)
        end = juce::jmax(0.0, end);
}

//==============================================================================
// Static boundary helpers
//==============================================================================

double SliceSampleCommand::sliceStartSeconds(double totalSeconds, int sliceCount, int i)
{
    if (sliceCount <= 0 || totalSeconds <= 0.0)
        return 0.0;
    return (totalSeconds * static_cast<double>(i)) / static_cast<double>(sliceCount);
}

double SliceSampleCommand::sliceEndSeconds(double totalSeconds, int sliceCount, int i)
{
    if (sliceCount <= 0 || totalSeconds <= 0.0)
        return totalSeconds;
    return (totalSeconds * static_cast<double>(i + 1)) / static_cast<double>(sliceCount);
}

//==============================================================================
// perform
//==============================================================================

bool SliceSampleCommand::perform()
{
    if (performed_)
        return true;

    auto& sampler = engine_.getSampler();

    // --- Capture state on first run ---
    if (!hasCapturedState_)
    {
        const auto* sourcePad = sampler.getPad(sourcePadIndex_);
        if (sourcePad == nullptr || !sourcePad->sampleFile.existsAsFile())
        {
            DBG("SliceSampleCommand::perform() - source pad has no valid sample file");
            return false;
        }

        sourceFile_ = sourcePad->sampleFile;

        // Determine the source's total length in seconds. Prefer the reader
        // (authoritative) but fall back to the pad's cached range if the
        // reader can't be opened — callers can still slice based on the
        // range the user is seeing.
        juce::AudioFormat* fmt = nullptr;
        std::unique_ptr<juce::AudioFormatReader> reader(
            tracktion::engine::AudioFileUtils::createReaderFindingFormat(engine_.getEngine(),
                                                                          sourceFile_, fmt));
        if (reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
        {
            sourceTotalSeconds_ = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        }
        else if (sourcePad->rangeEndSeconds > sourcePad->rangeStartSeconds)
        {
            // Fallback — the pad's own range if the reader failed. Not ideal
            // (slices would be relative to the pad's trim range, not the
            // full file), but preserves forward progress.
            sourceTotalSeconds_ = sourcePad->rangeEndSeconds;
        }

        if (sourceTotalSeconds_ <= 0.0)
        {
            DBG("SliceSampleCommand::perform() - source file has zero length");
            return false;
        }

        // If construction gave us a slice count (not explicit starts), fill
        // sliceStarts_ now that the source duration is known.
        if (sliceStarts_.empty() && sliceCountPending_ > 0)
        {
            sliceStarts_.reserve(static_cast<size_t>(sliceCountPending_));
            for (int i = 0; i < sliceCountPending_; ++i)
                sliceStarts_.push_back(sliceStartSeconds(sourceTotalSeconds_, sliceCountPending_, i));
        }

        // Clamp the explicit end (if any) to the source duration.
        const double effectiveEnd = (explicitEndSeconds_ > 0.0 &&
                                     explicitEndSeconds_ < sourceTotalSeconds_)
                                        ? explicitEndSeconds_
                                        : sourceTotalSeconds_;

        if (!sliceEnds_.empty())
        {
            std::vector<double> validStarts;
            std::vector<double> validEnds;
            validStarts.reserve(sliceStarts_.size());
            validEnds.reserve(sliceEnds_.size());
            for (size_t i = 0; i < sliceStarts_.size() && i < sliceEnds_.size(); ++i)
            {
                const double start = juce::jlimit(0.0, effectiveEnd, sliceStarts_[i]);
                const double end = juce::jlimit(start, effectiveEnd, sliceEnds_[i]);
                if (end > start + 1e-6)
                {
                    validStarts.push_back(start);
                    validEnds.push_back(end);
                }
            }
            sliceStarts_ = std::move(validStarts);
            sliceEnds_ = std::move(validEnds);
        }
        else
        {
            // Drop any starts at or past the effective end — they would
            // produce empty slices.
            while (!sliceStarts_.empty()
                   && sliceStarts_.back() >= effectiveEnd - 1e-6)
                sliceStarts_.pop_back();
        }
        if (sliceStarts_.empty())
        {
            DBG("SliceSampleCommand::perform() - no valid slice boundaries");
            return false;
        }
        // Use the effective end as the last slice's endpoint.
        sourceTotalSeconds_ = effectiveEnd;

        const int sliceCount = static_cast<int>(sliceStarts_.size());

        // Capture previous state for each pad we will overwrite (0..sliceCount-1)
        previousPads_.resize(static_cast<size_t>(sliceCount));
        for (int i = 0; i < sliceCount; ++i)
        {
            const auto* pad = sampler.getPad(i);
            PreviousPadState& prev = previousPads_[static_cast<size_t>(i)];
            if (pad != nullptr)
            {
                prev.hadSample           = pad->hasSample;
                prev.sampleFile          = pad->hasSample ? pad->sampleFile : juce::File();
                for (const auto& layer : pad->sampleLayers)
                {
                    prev.sampleLayers.push_back(layer.file);
                    prev.layerGainsDb.push_back(layer.gainDb);
                    prev.layerWeights.push_back(layer.randomWeight);
                    prev.layerVelocityCurves.push_back(
                        static_cast<int>(layer.velocityCurve));
                    prev.layerVelocityMinimums.push_back(
                        layer.velocityMinimum);
                    prev.layerVelocityMaximums.push_back(
                        layer.velocityMaximum);
                }
                prev.rangeStartSeconds   = pad->rangeStartSeconds;
                prev.rangeEndSeconds     = pad->rangeEndSeconds;
                prev.normalizationGainDb = pad->normalizationGainDb;
                prev.gainDb              = sampler.getGainDb(i);
                prev.chokeGroup          = sampler.getChokeGroup(i);
                prev.triggerMode         = static_cast<int>(pad->triggerMode);
            }
        }

        hasCapturedState_ = true;
    }

    // --- Apply slices: same file, per-pad ranges ---
    // Non-destructive: no audio is written. Each pad gets pointed at the same
    // source file and its sample-range set to the slice boundary. Playback
    // uses TE's SamplerPlugin sound-range (same mechanism Trim uses).
    //
    // Raw variants: TE's loadSample/setPadSampleRange re-enter UndoManager
    // via SamplerPlugin::removeSound/setSoundExcerpt. That's fine on the
    // initial perform() (isPerformingUndoRedo == false) but livelocks on
    // undo() / redo(). Using the raw path is correct either way because
    // this command is itself on the undo stack and undo() restores via
    // the raw path too.
    const int sliceCount = static_cast<int>(sliceStarts_.size());
    for (int i = 0; i < sliceCount; ++i)
    {
        const double startSec = sliceStarts_[static_cast<size_t>(i)];
        const double endSec = !sliceEnds_.empty()
            ? sliceEnds_[static_cast<size_t>(i)]
            : (i + 1 < sliceCount)
                ? sliceStarts_[static_cast<size_t>(i + 1)]
                : sourceTotalSeconds_;

        if (!sampler.loadSampleRaw(i, sourceFile_))
        {
            DBG("SliceSampleCommand::perform() - loadSample failed for pad " + juce::String(i));
            // Continue — partial state is acceptable; undo will restore.
        }
        sampler.setPadSampleRangeRaw(i, startSec, endSec);
    }

    performed_ = true;
    return true;
}

//==============================================================================
// undo
//==============================================================================

bool SliceSampleCommand::undo()
{
    if (!performed_)
        return false;

    auto& sampler = engine_.getSampler();

    const int sliceCount = static_cast<int>(previousPads_.size());
    for (int i = 0; i < sliceCount; ++i)
    {
        const auto& prev = previousPads_[static_cast<size_t>(i)];
        if (prev.hadSample && prev.sampleFile.existsAsFile())
        {
            sampler.loadSampleRaw(i, prev.sampleFile);
            for (size_t layer = 1; layer < prev.sampleLayers.size(); ++layer)
                sampler.addSampleLayerRaw(i, prev.sampleLayers[layer]);
            for (size_t layer = 0; layer < prev.sampleLayers.size(); ++layer)
            {
                sampler.setSampleLayerGainDbRaw(i, static_cast<int>(layer),
                                                prev.layerGainsDb[layer]);
                sampler.setSampleLayerRandomWeightRaw(i, static_cast<int>(layer),
                                                      prev.layerWeights[layer]);
                sampler.setSampleLayerVelocityCurveRaw(
                    i, static_cast<int>(layer),
                    static_cast<SamplerInstrument::LayerVelocityCurve>(
                        prev.layerVelocityCurves[layer]));
                sampler.setSampleLayerVelocityRangeRaw(
                    i, static_cast<int>(layer),
                    prev.layerVelocityMinimums[layer],
                    prev.layerVelocityMaximums[layer]);
            }
            // loadSample() resets range/normalization to defaults. Restore
            // the captured pad-level state so undo truly round-trips.
            if (prev.rangeEndSeconds > prev.rangeStartSeconds)
                sampler.setPadSampleRangeRaw(i, prev.rangeStartSeconds, prev.rangeEndSeconds);
            sampler.setPadSampleNormalizationGainDbRaw(i, prev.normalizationGainDb);
        }
        else
        {
            sampler.clearSampleRaw(i);
        }
        // gainDb / chokeGroup restoration is best-effort: these APIs route
        // through UM-tracked ValueTree writes (VolumeAndPanPlugin + track
        // properties) which silently no-op during um.undo(). Left here for
        // the common case where gain/choke match defaults; non-default
        // values won't round-trip until raw helpers exist for these too.
        sampler.setGainDb(i, prev.gainDb);
        sampler.setChokeGroupDirect(i, prev.chokeGroup);
        sampler.setTriggerModeDirect(
            i, static_cast<SamplerInstrument::TriggerMode>(prev.triggerMode));
    }

    performed_ = false;
    return true;
}
