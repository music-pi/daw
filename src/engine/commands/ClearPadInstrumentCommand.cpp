#include "ClearPadInstrumentCommand.h"
#include "../AudioEngine.h"
#include "../RoundRobinMidiPlugin.h"
#include "../SamplerInstrument.h"

// te alias comes transitively from SamplerInstrument.h.

ClearPadInstrumentCommand::ClearPadInstrumentCommand(AudioEngine& e, int idx)
    : engine_(e), padIndex_(idx) {}

bool ClearPadInstrumentCommand::perform()
{
    auto& sampler = engine_.getSampler();
    auto* pad = sampler.getPad(padIndex_);
    if (!pad || pad->instrument == nullptr)
        return false;

    if (instrumentPlugin_ != nullptr)
    {
        if (! pluginParentState_.isValid())
            return false;

        pluginParentState_.removeChild(pad->instrument->state, nullptr);
        pad->instrument = nullptr;
        if (roundRobinPlugin_ != nullptr && roundRobinTreeIndex_ >= 0)
        {
            pluginParentState_.addChild(
                roundRobinPlugin_->state, roundRobinTreeIndex_, nullptr);
            pad->roundRobinMidi = dynamic_cast<RoundRobinMidiPlugin*>(
                roundRobinPlugin_.get());
        }
        if (samplerPlugin_ != nullptr && samplerTreeIndex_ >= 0)
        {
            pluginParentState_.addChild(
                samplerPlugin_->state, samplerTreeIndex_, nullptr);
            pad->sampler = dynamic_cast<te::SamplerPlugin*>(
                samplerPlugin_.get());
        }

        pad->name = "Pad " + juce::String(padIndex_ + 1);
        pad->track->state.setProperty(te::IDs::name, pad->name, nullptr);
        return true;
    }

    instrumentPlugin_ = pad->instrument;
    pluginParentState_ = pad->instrument->state.getParent();
    instrumentTreeIndex_ = pluginParentState_.indexOf(pad->instrument->state);
    savedPadName_ = pad->name;

    if (! sampler.clearPadInstrumentRaw(padIndex_))
        return false;

    pad = sampler.getPad(padIndex_);
    if (pad == nullptr || pad->sampler == nullptr)
        return false;
    samplerPlugin_ = pad->sampler;
    samplerTreeIndex_ = pluginParentState_.indexOf(pad->sampler->state);
    if (pad->roundRobinMidi != nullptr)
    {
        roundRobinPlugin_ = pad->roundRobinMidi;
        roundRobinTreeIndex_ = pluginParentState_.indexOf(
            pad->roundRobinMidi->state);
    }
    return true;
}

bool ClearPadInstrumentCommand::undo()
{
    auto& sampler = engine_.getSampler();
    auto* pad = sampler.getPad(padIndex_);
    if (pad == nullptr || instrumentPlugin_ == nullptr
        || ! pluginParentState_.isValid() || instrumentTreeIndex_ < 0)
        return false;

    if (pad->sampler != nullptr)
        pluginParentState_.removeChild(pad->sampler->state, nullptr);
    if (pad->roundRobinMidi != nullptr)
        pluginParentState_.removeChild(pad->roundRobinMidi->state, nullptr);
    pad->sampler = nullptr;
    pad->roundRobinMidi = nullptr;

    pluginParentState_.addChild(
        instrumentPlugin_->state, instrumentTreeIndex_, nullptr);
    pad->instrument = dynamic_cast<te::ExternalPlugin*>(
        instrumentPlugin_.get());
    pad->name = savedPadName_;
    if (pad->track != nullptr)
        pad->track->state.setProperty(te::IDs::name, savedPadName_, nullptr);
    return true;
}
