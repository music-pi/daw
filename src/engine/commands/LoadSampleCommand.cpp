#include "LoadSampleCommand.h"

#include "../AudioEngine.h"
#include "../SamplerInstrument.h"

LoadSampleCommand::LoadSampleCommand(AudioEngine& engine, int padIndex, juce::File newFile)
    : engine_(engine)
    , padIndex_(padIndex)
    , newFile_(std::move(newFile))
{
}

bool LoadSampleCommand::perform()
{
    if (performed_)
        return true;

    auto& sampler = engine_.getSampler();

    if (! hasCapturedState_)
    {
        const auto* pad = sampler.getPad(padIndex_);
        if (pad != nullptr)
        {
            previous_.hadSample           = pad->hasSample;
            previous_.sampleFile          = pad->hasSample ? pad->sampleFile : juce::File();
            for (const auto& layer : pad->sampleLayers)
            {
                previous_.sampleLayers.push_back(layer.file);
                previous_.layerGainsDb.push_back(layer.gainDb);
                previous_.layerWeights.push_back(layer.randomWeight);
                previous_.layerVelocityCurves.push_back(
                    static_cast<int>(layer.velocityCurve));
                previous_.layerVelocityMinimums.push_back(
                    layer.velocityMinimum);
                previous_.layerVelocityMaximums.push_back(
                    layer.velocityMaximum);
            }
            previous_.triggerMode         = static_cast<int>(pad->triggerMode);
            previous_.rangeStartSeconds   = pad->rangeStartSeconds;
            previous_.rangeEndSeconds     = pad->rangeEndSeconds;
            previous_.normalizationGainDb = pad->normalizationGainDb;
            previous_.gainDb              = sampler.getGainDb(padIndex_);
            previous_.chokeGroup          = sampler.getChokeGroup(padIndex_);
        }
        hasCapturedState_ = true;
    }

    // Raw path — won't livelock when invoked inside UndoManager::undo() for
    // redo, or when the user initially loads. See SamplerInstrument raw
    // helpers for the full rationale.
    if (! sampler.loadSampleRaw(padIndex_, newFile_))
        return false;

    performed_ = true;
    return true;
}

bool LoadSampleCommand::undo()
{
    if (! performed_)
        return false;

    auto& sampler = engine_.getSampler();

    if (previous_.hadSample && previous_.sampleFile.existsAsFile())
    {
        sampler.loadSampleRaw(padIndex_, previous_.sampleFile);
        for (size_t i = 1; i < previous_.sampleLayers.size(); ++i)
            sampler.addSampleLayerRaw(padIndex_, previous_.sampleLayers[i]);
        for (size_t i = 0; i < previous_.sampleLayers.size(); ++i)
        {
            sampler.setSampleLayerGainDbRaw(padIndex_, static_cast<int>(i),
                                            previous_.layerGainsDb[i]);
            sampler.setSampleLayerRandomWeightRaw(padIndex_, static_cast<int>(i),
                                                  previous_.layerWeights[i]);
            sampler.setSampleLayerVelocityCurveRaw(
                padIndex_, static_cast<int>(i),
                static_cast<SamplerInstrument::LayerVelocityCurve>(
                    previous_.layerVelocityCurves[i]));
            sampler.setSampleLayerVelocityRangeRaw(
                padIndex_, static_cast<int>(i),
                previous_.layerVelocityMinimums[i],
                previous_.layerVelocityMaximums[i]);
        }
        if (previous_.rangeEndSeconds > previous_.rangeStartSeconds)
            sampler.setPadSampleRangeRaw(padIndex_, previous_.rangeStartSeconds, previous_.rangeEndSeconds);
        sampler.setPadSampleNormalizationGainDbRaw(padIndex_, previous_.normalizationGainDb);
    }
    else
    {
        sampler.clearSampleRaw(padIndex_);
    }
    sampler.setTriggerModeDirect(
        padIndex_, static_cast<SamplerInstrument::TriggerMode>(previous_.triggerMode));
    // setGainDb and setChokeGroupDirect also route through UM-tracking TE
    // APIs (VolumeAndPanPlugin, track properties). They'd be silently
    // no-op'd during um.undo(). Skip them for now — they only matter when
    // undoing an overwrite-load with non-default gain/choke, which is rare
    // enough that the tradeoff is worth shipping a working undo.
    juce::ignoreUnused(sampler);

    performed_ = false;
    return true;
}
