#include "SetGainDbCommand.h"
#include "../AudioEngine.h"
#include "../SamplerInstrument.h"

namespace
{
te::VolumeAndPanPlugin* resolveVolume(AudioEngine& engine,
                                      const juce::String& channelId)
{
    auto* edit = engine.getEdit();
    if (edit == nullptr)
        return nullptr;
    if (channelId == "master")
        return edit->getMasterVolumePlugin().get();

    auto* track = te::findTrackForID(
        *edit, te::EditItemID::fromString(channelId));
    if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track))
        return audioTrack->getVolumePlugin();
    if (auto* folderTrack = dynamic_cast<te::FolderTrack*>(track))
        return folderTrack->getVolumePlugin();
    return nullptr;
}
}

SetGainDbCommand::SetGainDbCommand(SamplerInstrument& sampler, int padIndex, float oldDb, float newDb)
    : sampler(&sampler)
    , padIndex(padIndex)
    , oldDb(oldDb)
    , newDb(newDb)
    , oldDbCaptured(true)
{
}

SetGainDbCommand::SetGainDbCommand(AudioEngine& engine,
                                   juce::String channelId,
                                   float newDb)
    : engine(&engine)
    , channelId(std::move(channelId))
    , newDb(newDb)
{
}

bool SetGainDbCommand::apply(float db)
{
    if (sampler != nullptr)
    {
        sampler->setGainDbRaw(padIndex, db);
        return true;
    }

    auto* volume = engine != nullptr ? resolveVolume(*engine, channelId) : nullptr;
    if (volume == nullptr)
        return false;
    const float clamped = juce::jlimit(-48.0f, 12.0f, db);
    const float sliderPosition = te::decibelsToVolumeFaderPosition(clamped);
    volume->volParam->setParameter(
        sliderPosition, juce::dontSendNotification);
    volume->state.setProperty(
        te::IDs::volume, sliderPosition, nullptr);
    return true;
}

bool SetGainDbCommand::perform()
{
    if (! oldDbCaptured)
    {
        auto* volume = engine != nullptr ? resolveVolume(*engine, channelId) : nullptr;
        if (volume == nullptr)
            return false;
        oldDb = volume->getVolumeDb();
        oldDbCaptured = true;
    }
    return apply(newDb);
}

bool SetGainDbCommand::undo()
{
    return oldDbCaptured && apply(oldDb);
}
