#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

struct AudioInputSource
{
    juce::String nodeName;
    juce::String displayName;
    int channels { 1 };
};

class AudioInputRecorder
{
public:
    virtual ~AudioInputRecorder() = default;

    virtual bool refreshSources() = 0;
    virtual const std::vector<AudioInputSource>& getSources() const = 0;
    virtual bool startRecording(const AudioInputSource& source,
                                int channels,
                                const juce::File& destination) = 0;
    virtual void stopRecording() = 0;
    virtual bool isRecording() const = 0;
    virtual bool startMonitoring(const AudioInputSource& source,
                                 int channels) = 0;
    virtual void stopMonitoring() = 0;
    virtual bool isMonitoring() const = 0;
    virtual double getDurationSeconds() const = 0;
    virtual float getPeak(int channel) const = 0;
    virtual juce::String getLastError() const = 0;
};

/** Records a selected PipeWire Audio/Source node through pw-record.

    Capture is a separate child process and reader thread, so the application's
    normal JUCE/Tracktion output device remains output-only. Raw float samples
    are converted to a finalized 24-bit WAV by this class, which also makes a
    forced child-process shutdown safe for the resulting file. */
class PipeWireAudioRecorder final : public AudioInputRecorder,
                                    private juce::Thread
{
public:
    PipeWireAudioRecorder();
    ~PipeWireAudioRecorder() override;

    bool refreshSources() override;
    const std::vector<AudioInputSource>& getSources() const override;
    bool startRecording(const AudioInputSource& source,
                        int channels,
                        const juce::File& destination) override;
    void stopRecording() override;
    bool isRecording() const override;
    bool startMonitoring(const AudioInputSource& source, int channels) override;
    void stopMonitoring() override;
    bool isMonitoring() const override;
    double getDurationSeconds() const override;
    float getPeak(int channel) const override;
    juce::String getLastError() const override;

    static std::vector<AudioInputSource> parsePipeWireDump(
        const juce::String& json,
        juce::String* error = nullptr);

private:
    void run() override;
    void setError(const juce::String& message);

    std::vector<AudioInputSource> sources_;
    juce::ChildProcess captureProcess_;
    juce::ChildProcess monitorProcess_;
    std::unique_ptr<juce::AudioFormatWriter> writer_;
    std::atomic<bool> recording_ { false };
    std::atomic<juce::int64> framesWritten_ { 0 };
    std::array<std::atomic<float>, 2> peaks_ { 0.0f, 0.0f };
    int captureChannels_ { 0 };
    static constexpr double kSampleRate = 48000.0;

    mutable juce::CriticalSection errorLock_;
    juce::String lastError_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PipeWireAudioRecorder)
};

std::unique_ptr<AudioInputRecorder> createPipeWireAudioRecorder();
