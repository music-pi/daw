#include "SetPanCommand.h"

#include "../AudioEngine.h"

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

SetPanCommand::SetPanCommand(AudioEngine& engine,
                             juce::String channelId,
                             float newPan)
    : engine(engine)
    , channelId(std::move(channelId))
    , newPan(newPan)
{
}

bool SetPanCommand::apply(float pan)
{
    auto* volume = resolveVolume(engine, channelId);
    if (volume == nullptr)
        return false;

    const float clamped = juce::jlimit(-1.0f, 1.0f, pan);
    const float centred = clamped >= -0.005f && clamped <= 0.005f
        ? 0.0f : clamped;
    volume->panParam->setParameter(centred, juce::dontSendNotification);
    volume->state.setProperty(te::IDs::pan, centred, nullptr);
    return true;
}

bool SetPanCommand::perform()
{
    if (! oldPanCaptured)
    {
        auto* volume = resolveVolume(engine, channelId);
        if (volume == nullptr)
            return false;
        oldPan = volume->getPan();
        oldPanCaptured = true;
    }
    return apply(newPan);
}

bool SetPanCommand::undo()
{
    return oldPanCaptured && apply(oldPan);
}
