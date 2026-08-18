#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class AudioEngine;

/**
 * ISamplePreview — the subset of preview-player operations SliceOperation
 * depends on. Extracted as an interface so tests can supply a fake without
 * spinning up a real TE preview Edit.
 */
class ISamplePreview
{
public:
    virtual ~ISamplePreview() = default;

    virtual void play(const juce::File& file) = 0;
    virtual void stop() = 0;
    virtual void seek(double seconds) = 0;
    virtual double getPositionSeconds() const = 0;
    virtual bool isPlaying() const noexcept = 0;
};

/**
 * SamplePreviewPlayer — previews audio files using TE's built-in
 * Edit::createEditForPreviewingFile(). No custom tracks or plugins.
 */
class SamplePreviewPlayer : public ISamplePreview
{
public:
    explicit SamplePreviewPlayer(AudioEngine& engine);
    ~SamplePreviewPlayer() override;

    void play(const juce::File& file) override;
    void stop() override;
    void seek(double seconds) override;
    double getPositionSeconds() const override;
    bool isPlaying() const noexcept override;

private:
    AudioEngine& engine_;
    std::unique_ptr<te::Edit> previewEdit_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePreviewPlayer)
};
