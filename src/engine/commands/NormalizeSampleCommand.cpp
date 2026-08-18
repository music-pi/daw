#include "NormalizeSampleCommand.h"

#include "../AudioEngine.h"
#include "../SamplerInstrument.h"

NormalizeSampleCommand::NormalizeSampleCommand(AudioEngine& engine, int padId, double targetDb)
    : engine(engine)
    , padId(padId)
    , targetDb(targetDb)
{
}

bool NormalizeSampleCommand::perform()
{
    if (performed)
        return true;

    auto& sampler = engine.getSampler();

    if (!hasCapturedState)
    {
        const auto* pad = sampler.getPad(padId);
        if (pad == nullptr || !pad->sampleFile.existsAsFile())
            return false;

        previousGainDb = pad->normalizationGainDb;
        hasCapturedState = true;
    }

    if (!sampler.normalizePadSample(padId, targetDb))
        return false;

    performed = true;
    return true;
}

bool NormalizeSampleCommand::undo()
{
    if (!performed)
        return false;

    if (!engine.getSampler().setPadSampleNormalizationGainDb(padId, previousGainDb))
        return false;

    performed = false;
    return true;
}
