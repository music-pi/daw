#include "SetPadInstrumentCommand.h"
#include "../AudioEngine.h"
#include "../RoundRobinMidiPlugin.h"
#include "../SamplerInstrument.h"

SetPadInstrumentCommand::SetPadInstrumentCommand(AudioEngine& e, int idx,
                                                 juce::PluginDescription d)
    : engine_(e), padIndex_(idx), desc_(std::move(d)) {}

bool SetPadInstrumentCommand::perform()
{
    auto& sampler = engine_.getSampler();
    auto* pad = sampler.getPad(padIndex_);
    if (!pad) return false;

    if (instrumentPlugin_ != nullptr)
    {
        if (! pluginParentState_.isValid() || instrumentTreeIndex_ < 0)
            return false;

        if (pad->sampler != nullptr)
            pluginParentState_.removeChild(pad->sampler->state, nullptr);
        if (pad->roundRobinMidi != nullptr)
            pluginParentState_.removeChild(
                pad->roundRobinMidi->state, nullptr);
        pad->sampler = nullptr;
        pad->roundRobinMidi = nullptr;

        pluginParentState_.addChild(
            instrumentPlugin_->state, instrumentTreeIndex_, nullptr);
        pad->instrument = dynamic_cast<te::ExternalPlugin*>(
            instrumentPlugin_.get());
        pad->name = pad->instrument != nullptr
            ? pad->instrument->getName() : savedPadName_;
        if (pad->track != nullptr)
            pad->track->state.setProperty(te::IDs::name, pad->name, nullptr);
        return pad->instrument != nullptr;
    }

    if (pad->sampler != nullptr)
    {
        savedSamplerPlugin_ = pad->sampler;
        pluginParentState_ = pad->sampler->state.getParent();
        samplerTreeIndex_ = pluginParentState_.indexOf(pad->sampler->state);
    }
    if (pad->roundRobinMidi != nullptr)
    {
        savedRoundRobinPlugin_ = pad->roundRobinMidi;
        pluginParentState_ = pad->roundRobinMidi->state.getParent();
        roundRobinTreeIndex_ = pluginParentState_.indexOf(
            pad->roundRobinMidi->state);
    }
    savedPadName_ = pad->name;

    if (! sampler.setPadInstrumentRaw(padIndex_, desc_))
        return false;

    pad = sampler.getPad(padIndex_);
    if (pad == nullptr || pad->instrument == nullptr)
        return false;
    instrumentPlugin_ = pad->instrument;
    instrumentTreeIndex_ = pluginParentState_.indexOf(
        pad->instrument->state);
    return instrumentTreeIndex_ >= 0;
}

bool SetPadInstrumentCommand::undo()
{
    auto& sampler = engine_.getSampler();
    auto* pad = sampler.getPad(padIndex_);
    if (pad == nullptr || pad->instrument == nullptr
        || ! pluginParentState_.isValid())
        return false;

    te::Plugin::Ptr instrument(pad->instrument);
    auto instrumentParent = pad->instrument->state.getParent();
    instrumentParent.removeChild(pad->instrument->state, nullptr);
    pad->instrument = nullptr;

    if (savedRoundRobinPlugin_ != nullptr && roundRobinTreeIndex_ >= 0)
    {
        pluginParentState_.addChild(
            savedRoundRobinPlugin_->state, roundRobinTreeIndex_, nullptr);
        pad->roundRobinMidi = dynamic_cast<RoundRobinMidiPlugin*>(
            savedRoundRobinPlugin_.get());
    }
    if (savedSamplerPlugin_ != nullptr && samplerTreeIndex_ >= 0)
    {
        pluginParentState_.addChild(
            savedSamplerPlugin_->state, samplerTreeIndex_, nullptr);
        pad->sampler = dynamic_cast<te::SamplerPlugin*>(
            savedSamplerPlugin_.get());
    }

    pad->name = savedPadName_;
    if (pad->track != nullptr)
        pad->track->state.setProperty(te::IDs::name, savedPadName_, nullptr);
    return true;
}
