#include "SamplePreviewPlayer.h"
#include "AudioEngine.h"

SamplePreviewPlayer::SamplePreviewPlayer(AudioEngine& engine)
    : engine_(engine)
{
}

SamplePreviewPlayer::~SamplePreviewPlayer()
{
    stop();
}

void SamplePreviewPlayer::play(const juce::File& file)
{
    stop();

    if (!file.existsAsFile())
        return;

    // Use TE's built-in preview Edit facility
    previewEdit_ = te::Edit::createEditForPreviewingFile(
        engine_.getEngine(),
        file,
        engine_.getEdit(),  // match output config from main edit
        false,              // don't match tempo
        false,              // don't match pitch
        nullptr,            // couldMatchTempo out-param
        {}                  // no MIDI preview plugin
    );

    if (previewEdit_ == nullptr)
        return;

    // Preview at ~75 % gain so browsed samples match the perceived loudness of
    // step playback (steps default to MIDI velocity 96 ≈ 0.756) and moderate
    // hardware pad hits. Keeps previewing a bright sample from being jarringly
    // louder than the same sample once it's actually triggered.
    constexpr float kPreviewGain = 0.75f;
    if (auto master = previewEdit_->getMasterVolumePlugin())
        master->setVolumeDb(juce::Decibels::gainToDecibels(kPreviewGain));

    auto& transport = previewEdit_->getTransport();
    transport.ensureContextAllocated();
    transport.play(false);
}

void SamplePreviewPlayer::stop()
{
    if (previewEdit_ != nullptr)
    {
        previewEdit_->getTransport().stop(false, false);
        previewEdit_.reset();
    }
}

bool SamplePreviewPlayer::isPlaying() const noexcept
{
    return previewEdit_ != nullptr && previewEdit_->getTransport().isPlaying();
}

void SamplePreviewPlayer::seek(double seconds)
{
    if (previewEdit_ == nullptr)
        return;
    auto& transport = previewEdit_->getTransport();
    transport.setPosition(tracktion::core::TimePosition::fromSeconds(juce::jmax(0.0, seconds)));
}

double SamplePreviewPlayer::getPositionSeconds() const
{
    if (previewEdit_ == nullptr)
        return 0.0;
    return previewEdit_->getTransport().getPosition().inSeconds();
}
