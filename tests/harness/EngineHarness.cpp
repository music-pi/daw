#include "EngineHarness.h"

#include "../../src/engine/SamplerInstrument.h"

namespace testharness
{

namespace
{
class TestEngineBehaviour final : public tracktion::engine::EngineBehaviour
{
public:
    int getNumberOfCPUsToUseForAudio() override
    {
        const auto available = juce::jmax(1, juce::SystemStats::getNumCpus());
        const auto requested = juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_TEST_AUDIO_CPUS", {}).getIntValue();

        return requested > 0 ? juce::jlimit(1, available, requested) : available;
    }
};

juce::File makeTempRoot()
{
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("maschinepi-tests")
                    .getChildFile(juce::Uuid().toString());
    root.createDirectory();
    return root;
}
}

EngineHarness::EngineHarness()
    : engineInstance("maschinepi-tests", nullptr,
                     std::make_unique<TestEngineBehaviour>())
    , harnessRoot(makeTempRoot())
{
    auto& dm = engineInstance.getDeviceManager().deviceManager;
    juce::String err = dm.initialise(0, 2, nullptr, true);
    if (err.isNotEmpty())
        juce::Logger::writeToLog("[EngineHarness] Audio device init: " + err);
}

EngineHarness::~EngineHarness()
{
    if (audioEngine)
        audioEngine->shutdown();

    if (harnessRoot.exists())
        harnessRoot.deleteRecursively();
}

AudioEngine& EngineHarness::audio()
{
    if (audioEngine == nullptr)
        audioEngine = std::make_unique<AudioEngine>(engineInstance);
    return *audioEngine;
}

SamplerInstrument& EngineHarness::pads()
{
    return audio().getSampler();
}

void EngineHarness::createEmptyEdit()
{
    audio().createEmptyEdit();
}

juce::File EngineHarness::createTemporarySampleFile(const juce::String& name,
                                                    int lengthSamples,
                                                    double sampleRate,
                                                    double frequencyHz)
{
    harnessRoot.createDirectory();

    juce::AudioBuffer<float> buffer(1, juce::jmax(1, lengthSamples));
    buffer.clear();

    const double resolvedFrequency = frequencyHz > 0.0
                                       ? frequencyHz
                                       : sampleRate / static_cast<double>(buffer.getNumSamples());
    const double step = juce::MathConstants<double>::twoPi * resolvedFrequency / sampleRate;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, std::sin(step * static_cast<double>(i)));

    const auto file = harnessRoot.getChildFile(name).withFileExtension("wav");
    file.deleteFile();

    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (stream == nullptr)
        return {};

    juce::WavAudioFormat wav;
    auto writerOptions = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(buffer.getNumChannels())
                             .withBitsPerSample(16);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream, writerOptions));

    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset(); // flush
    return file;
}

} // namespace testharness
