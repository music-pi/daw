#include "PipeWireAudioRecorder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace
{
juce::String propertyString(const juce::DynamicObject& object,
                            const juce::Identifier& property)
{
    return object.getProperty(property).toString();
}
}

PipeWireAudioRecorder::PipeWireAudioRecorder()
    : juce::Thread("PipeWire audio recorder")
{
}

PipeWireAudioRecorder::~PipeWireAudioRecorder()
{
    stopRecording();
    stopMonitoring();
}

bool PipeWireAudioRecorder::refreshSources()
{
    juce::ChildProcess process;
    if (!process.start(juce::StringArray { "pw-dump" },
                       juce::ChildProcess::wantStdOut))
    {
        setError("PipeWire tools are not installed");
        sources_.clear();
        return false;
    }

    const auto output = process.readAllProcessOutput();
    if (process.getExitCode() != 0)
    {
        setError("Could not read the PipeWire graph");
        sources_.clear();
        return false;
    }

    juce::String parseError;
    auto parsed = parsePipeWireDump(output, &parseError);
    if (parseError.isNotEmpty())
    {
        setError(parseError);
        sources_.clear();
        return false;
    }

    sources_ = std::move(parsed);
    setError(sources_.empty() ? "No PipeWire audio inputs found" : juce::String());
    return !sources_.empty();
}

const std::vector<AudioInputSource>& PipeWireAudioRecorder::getSources() const
{
    return sources_;
}

bool PipeWireAudioRecorder::startRecording(const AudioInputSource& source,
                                           int channels,
                                           const juce::File& destination)
{
    stopRecording();
    setError({});

    captureChannels_ = juce::jlimit(1, 2, juce::jmin(channels, source.channels));
    if (source.nodeName.isEmpty())
    {
        setError("Select an input first");
        return false;
    }

    auto directory = destination.getParentDirectory();
    if (!directory.exists() && !directory.createDirectory())
    {
        setError("Could not create the recordings folder");
        return false;
    }

    destination.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();
    if (stream == nullptr)
    {
        setError("Could not create the recording file");
        return false;
    }

    juce::WavAudioFormat format;
    writer_ = format.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions {}
            .withSampleRate(kSampleRate)
            .withNumChannels(captureChannels_)
            .withBitsPerSample(24));
    if (writer_ == nullptr)
    {
        setError("Could not initialise the WAV writer");
        return false;
    }

    const juce::StringArray arguments {
        "pw-record",
        "--target", source.nodeName,
        "--rate", juce::String(static_cast<int>(kSampleRate)),
        "--channels", juce::String(captureChannels_),
        "--format", "f32",
        "--latency", "128/48000",
        "-"
    };
    if (!captureProcess_.start(arguments, juce::ChildProcess::wantStdOut))
    {
        writer_.reset();
        destination.deleteFile();
        setError("Could not start pw-record");
        return false;
    }

    framesWritten_.store(0, std::memory_order_relaxed);
    for (auto& peak : peaks_)
        peak.store(0.0f, std::memory_order_relaxed);
    recording_.store(true, std::memory_order_release);
    startThread(juce::Thread::Priority::normal);
    return true;
}

void PipeWireAudioRecorder::stopRecording()
{
    if (isThreadRunning())
    {
        signalThreadShouldExit();
        if (!waitForThreadToExit(500))
        {
            captureProcess_.kill();
            waitForThreadToExit(1000);
        }
    }

    if (captureProcess_.isRunning())
    {
        captureProcess_.kill();
        captureProcess_.waitForProcessToFinish(500);
    }

    recording_.store(false, std::memory_order_release);
    writer_.reset();
}

bool PipeWireAudioRecorder::isRecording() const
{
    return recording_.load(std::memory_order_acquire);
}

bool PipeWireAudioRecorder::startMonitoring(const AudioInputSource& source,
                                            int channels)
{
    stopMonitoring();
    setError({});

    if (source.nodeName.isEmpty())
    {
        setError("Select an input first");
        return false;
    }

    const int monitorChannels = juce::jlimit(
        1, 2, juce::jmin(channels, source.channels));
    const auto channelMap = monitorChannels == 1 ? "[ MONO ]" : "[ FL, FR ]";
    const juce::StringArray arguments {
        "pw-loopback",
        "-n", "MusicPI Input Monitor",
        "-c", juce::String(monitorChannels),
        "-m", channelMap,
        "-l", "10",
        "-C", source.nodeName
    };

    if (!monitorProcess_.start(arguments, juce::ChildProcess::wantStdErr))
    {
        setError("Could not start PipeWire monitoring");
        return false;
    }

    // A successfully spawned pw-loopback remains alive. Give immediate
    // argument/connection failures a moment to surface before reporting ON.
    monitorProcess_.waitForProcessToFinish(20);
    if (!monitorProcess_.isRunning())
    {
        setError("PipeWire could not monitor this input");
        return false;
    }
    return true;
}

void PipeWireAudioRecorder::stopMonitoring()
{
    if (!monitorProcess_.isRunning())
        return;
    monitorProcess_.kill();
    monitorProcess_.waitForProcessToFinish(500);
}

bool PipeWireAudioRecorder::isMonitoring() const
{
    return monitorProcess_.isRunning();
}

double PipeWireAudioRecorder::getDurationSeconds() const
{
    return static_cast<double>(framesWritten_.load(std::memory_order_relaxed))
        / kSampleRate;
}

float PipeWireAudioRecorder::getPeak(int channel) const
{
    if (!juce::isPositiveAndBelow(channel, static_cast<int>(peaks_.size())))
        return 0.0f;
    return peaks_[static_cast<size_t>(channel)].load(std::memory_order_relaxed);
}

juce::String PipeWireAudioRecorder::getLastError() const
{
    const juce::ScopedLock lock(errorLock_);
    return lastError_;
}

std::vector<AudioInputSource> PipeWireAudioRecorder::parsePipeWireDump(
    const juce::String& json,
    juce::String* error)
{
    juce::var root;
    const auto result = juce::JSON::parse(json, root);
    if (result.failed() || !root.isArray())
    {
        if (error != nullptr)
            *error = "PipeWire returned invalid device data";
        return {};
    }

    std::vector<AudioInputSource> resultSources;
    for (const auto& item : *root.getArray())
    {
        auto* object = item.getDynamicObject();
        if (object == nullptr
            || propertyString(*object, "type") != "PipeWire:Interface:Node")
            continue;

        auto* info = object->getProperty("info").getDynamicObject();
        if (info == nullptr)
            continue;
        auto* props = info->getProperty("props").getDynamicObject();
        if (props == nullptr)
            continue;

        const auto mediaClass = propertyString(*props, "media.class");
        if (!mediaClass.startsWith("Audio/Source"))
            continue;

        AudioInputSource source;
        source.nodeName = propertyString(*props, "node.name");
        source.displayName = propertyString(*props, "node.description");
        if (source.displayName.isEmpty())
            source.displayName = propertyString(*props, "node.nick");
        if (source.displayName.isEmpty())
            source.displayName = source.nodeName;
        source.channels = juce::jmax(
            1, static_cast<int>(props->getProperty("audio.channels")));

        if (source.nodeName.isNotEmpty())
            resultSources.push_back(std::move(source));
    }

    std::sort(resultSources.begin(), resultSources.end(),
              [](const AudioInputSource& lhs, const AudioInputSource& rhs)
              {
                  return lhs.displayName.compareIgnoreCase(rhs.displayName) < 0;
              });
    resultSources.erase(
        std::unique(resultSources.begin(), resultSources.end(),
                    [](const AudioInputSource& lhs, const AudioInputSource& rhs)
                    {
                        return lhs.nodeName == rhs.nodeName;
                    }),
        resultSources.end());

    if (error != nullptr)
        error->clear();
    return resultSources;
}

void PipeWireAudioRecorder::run()
{
    constexpr int kReadBytes = 32768;
    alignas(float) std::array<char, kReadBytes + 16> bytes {};
    int carryBytes = 0;

    while (!threadShouldExit())
    {
        const int bytesRead = captureProcess_.readProcessOutput(
            bytes.data() + carryBytes,
            kReadBytes - carryBytes);
        if (bytesRead <= 0)
        {
            if (!captureProcess_.isRunning())
                break;
            wait(2);
            continue;
        }

        const int availableBytes = carryBytes + bytesRead;
        const int bytesPerFrame = captureChannels_ * static_cast<int>(sizeof(float));
        const int frames = availableBytes / bytesPerFrame;
        const int consumedBytes = frames * bytesPerFrame;
        if (frames <= 0)
        {
            carryBytes = availableBytes;
            continue;
        }

        juce::AudioBuffer<float> buffer(captureChannels_, frames);
        for (int channel = 0; channel < captureChannels_; ++channel)
        {
            auto* destination = buffer.getWritePointer(channel);
            float peak = 0.0f;
            for (int frame = 0; frame < frames; ++frame)
            {
                float sample = 0.0f;
                const auto sampleOffset = static_cast<size_t>(
                    (frame * captureChannels_ + channel) * static_cast<int>(sizeof(float)));
                std::memcpy(&sample, bytes.data() + sampleOffset, sizeof(sample));
                destination[frame] = sample;
                peak = juce::jmax(peak, std::abs(sample));
            }
            peaks_[static_cast<size_t>(channel)].store(
                juce::jlimit(0.0f, 1.0f, peak), std::memory_order_relaxed);
        }
        if (captureChannels_ == 1)
            peaks_[1].store(peaks_[0].load(std::memory_order_relaxed),
                            std::memory_order_relaxed);

        if (writer_ != nullptr)
            writer_->writeFromAudioSampleBuffer(buffer, 0, frames);
        framesWritten_.fetch_add(frames, std::memory_order_relaxed);

        carryBytes = availableBytes - consumedBytes;
        if (carryBytes > 0)
            std::memmove(bytes.data(), bytes.data() + consumedBytes,
                         static_cast<size_t>(carryBytes));
    }

    if (!threadShouldExit() && framesWritten_.load(std::memory_order_relaxed) == 0)
        setError("The PipeWire input stopped before audio arrived");
    recording_.store(false, std::memory_order_release);
}

void PipeWireAudioRecorder::setError(const juce::String& message)
{
    const juce::ScopedLock lock(errorLock_);
    lastError_ = message;
}

std::unique_ptr<AudioInputRecorder> createPipeWireAudioRecorder()
{
    return std::make_unique<PipeWireAudioRecorder>();
}
