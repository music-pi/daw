#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/PipeWireAudioRecorder.h"
#include "../src/ui/widget/AudioRecorderWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

namespace
{
class FakeAudioInputRecorder final : public AudioInputRecorder
{
public:
    bool refreshSources() override
    {
        ++refreshCount;
        return !sources.empty();
    }

    const std::vector<AudioInputSource>& getSources() const override
    {
        return sources;
    }

    bool startRecording(const AudioInputSource& source, int channels,
                        const juce::File& destination) override
    {
        startedSource = source.nodeName;
        startedChannels = channels;
        destination.getParentDirectory().createDirectory();
        std::array<char, 128> bytes {};
        destination.replaceWithData(bytes.data(), bytes.size());
        recording = true;
        duration = 1.25;
        return startSucceeds;
    }

    void stopRecording() override { recording = false; }
    bool isRecording() const override { return recording; }
    bool startMonitoring(const AudioInputSource& source, int channels) override
    {
        monitoring = true;
        monitoredSource = source.nodeName;
        monitoredChannels = channels;
        return true;
    }
    void stopMonitoring() override { monitoring = false; }
    bool isMonitoring() const override { return monitoring; }
    double getDurationSeconds() const override { return duration; }
    float getPeak(int channel) const override
    {
        return channel == 0 ? 0.4f : 0.6f;
    }
    juce::String getLastError() const override { return error; }

    std::vector<AudioInputSource> sources {
        { "source.a", "Input A", 2 },
        { "source.b", "Input B", 1 }
    };
    int refreshCount { 0 };
    bool recording { false };
    bool monitoring { false };
    bool startSucceeds { true };
    double duration { 0.0 };
    int startedChannels { 0 };
    juce::String startedSource;
    juce::String monitoredSource;
    int monitoredChannels { 0 };
    juce::String error;
};

class FakeSamplePreview final : public ISamplePreview
{
public:
    void play(const juce::File& file) override
    {
        playedFile = file;
        playing = true;
    }
    void stop() override { playing = false; }
    void seek(double seconds) override { position = seconds; }
    double getPositionSeconds() const override { return position; }
    bool isPlaying() const noexcept override { return playing; }

    bool playing { false };
    double position { 0.0 };
    juce::File playedFile;
};

class AudioRecorderWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        windowManager.setAudioEngine(&harness.audio());
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    WindowManager windowManager;
};
}

TEST(PipeWireAudioRecorderTests, ParsesOnlyAudioSourceNodes)
{
    const auto json = R"json([
      {"type":"PipeWire:Interface:Node","info":{"props":{
        "media.class":"Audio/Sink","node.name":"sink.a",
        "node.description":"Output","audio.channels":2}}},
      {"type":"PipeWire:Interface:Node","info":{"props":{
        "media.class":"Audio/Source","node.name":"source.b",
        "node.description":"Zulu Mic","audio.channels":2}}},
      {"type":"PipeWire:Interface:Node","info":{"props":{
        "media.class":"Audio/Source/Virtual","node.name":"source.a",
        "node.nick":"Alpha Bus","audio.channels":1}}}
    ])json";

    juce::String error;
    const auto sources = PipeWireAudioRecorder::parsePipeWireDump(json, &error);

    EXPECT_TRUE(error.isEmpty());
    ASSERT_EQ(sources.size(), 2u);
    EXPECT_EQ(sources[0].nodeName, "source.a");
    EXPECT_EQ(sources[0].displayName, "Alpha Bus");
    EXPECT_EQ(sources[0].channels, 1);
    EXPECT_EQ(sources[1].nodeName, "source.b");
}

TEST(PipeWireAudioRecorderTests, RejectsInvalidGraphData)
{
    juce::String error;
    EXPECT_TRUE(PipeWireAudioRecorder::parsePipeWireDump("not-json", &error).empty());
    EXPECT_TRUE(error.isNotEmpty());
}

TEST_F(AudioRecorderWidgetTests, SpansBothPanelsAndPublishesCaptureSuiteState)
{
    auto fake = std::make_unique<FakeAudioInputRecorder>();
    auto* fakePtr = fake.get();
    auto widget = std::make_unique<AudioRecorderWidget>(std::move(fake));
    auto* widgetPtr = widget.get();

    windowManager.open(std::move(widget), DisplaySide::Left);

    EXPECT_EQ(widgetPtr->describe().id, "audio_recorder");
    EXPECT_EQ(widgetPtr->describe().display, DisplayConstraint::Both);
    EXPECT_EQ(fakePtr->refreshCount, 1);
    EXPECT_TRUE(harness.audio().isAudioCaptureSuiteActive());
    EXPECT_FALSE(harness.audio().isAudioCaptureRecording());
    EXPECT_EQ(windowManager.getWidget(DisplaySide::Right), widgetPtr);

    windowManager.close("audio_recorder");
    EXPECT_FALSE(harness.audio().isAudioCaptureSuiteActive());
}

TEST_F(AudioRecorderWidgetTests, SourceKnobUsesDeliberateSteps)
{
    auto fake = std::make_unique<FakeAudioInputRecorder>();
    auto widget = std::make_unique<AudioRecorderWidget>(std::move(fake));
    auto* widgetPtr = widget.get();
    windowManager.open(std::move(widget), DisplaySide::Left);

    EXPECT_EQ(widgetPtr->getSelectedSourceIndex(), 0);
    widgetPtr->handleKnob(0, 11, 0, false);
    EXPECT_EQ(widgetPtr->getSelectedSourceIndex(), 0);
    widgetPtr->handleKnob(0, 1, 0, false);
    EXPECT_EQ(widgetPtr->getSelectedSourceIndex(), 1);
    EXPECT_EQ(widgetPtr->getCaptureChannels(), 1);
}

TEST_F(AudioRecorderWidgetTests, RecordsStopsAndHandsAcceptedTakeToHost)
{
    auto fake = std::make_unique<FakeAudioInputRecorder>();
    auto* fakePtr = fake.get();
    auto widget = std::make_unique<AudioRecorderWidget>(std::move(fake));
    auto* widgetPtr = widget.get();
    juce::File accepted;
    widgetPtr->setUseTakeCallback(
        [&accepted](const juce::File& file) { accepted = file; });
    windowManager.open(std::move(widget), DisplaySide::Left);

    widgetPtr->toggleRecording();
    EXPECT_TRUE(fakePtr->recording);
    EXPECT_EQ(fakePtr->startedSource, "source.a");
    EXPECT_EQ(fakePtr->startedChannels, 2);
    EXPECT_TRUE(harness.audio().isAudioCaptureRecording());

    widgetPtr->toggleRecording();
    EXPECT_FALSE(fakePtr->recording);
    ASSERT_TRUE(widgetPtr->hasTake());
    EXPECT_FALSE(harness.audio().isAudioCaptureRecording());

    widgetPtr->useTake();
    EXPECT_EQ(accepted, widgetPtr->getTakeFile());

    widgetPtr->discardTake();
    EXPECT_FALSE(widgetPtr->hasTake());
}

TEST_F(AudioRecorderWidgetTests, MonitorFollowsSelectedInputAndStopsOnClose)
{
    auto fake = std::make_unique<FakeAudioInputRecorder>();
    auto* fakePtr = fake.get();
    auto widget = std::make_unique<AudioRecorderWidget>(std::move(fake));
    auto* widgetPtr = widget.get();
    windowManager.open(std::move(widget), DisplaySide::Left);

    auto rightOptions = widgetPtr->getOptions(1);
    ASSERT_EQ(rightOptions[0].label, "Monitor");
    rightOptions[0].onInvoke();
    EXPECT_TRUE(fakePtr->monitoring);
    EXPECT_EQ(fakePtr->monitoredSource, "source.a");
    EXPECT_EQ(fakePtr->monitoredChannels, 2);

    widgetPtr->handleKnob(0, 12, 0, false);
    EXPECT_TRUE(fakePtr->monitoring);
    EXPECT_EQ(fakePtr->monitoredSource, "source.b");
    EXPECT_EQ(fakePtr->monitoredChannels, 1);

    widgetPtr->onDeactivated();
    EXPECT_FALSE(fakePtr->monitoring);
    windowManager.close("audio_recorder");
}

TEST_F(AudioRecorderWidgetTests, FinishedTakeCanBePreviewedBeforeUse)
{
    auto recorder = std::make_unique<FakeAudioInputRecorder>();
    auto preview = std::make_unique<FakeSamplePreview>();
    auto* previewPtr = preview.get();
    auto widget = std::make_unique<AudioRecorderWidget>(
        std::move(recorder), std::move(preview));
    auto* widgetPtr = widget.get();
    windowManager.open(std::move(widget), DisplaySide::Left);

    widgetPtr->toggleRecording();
    widgetPtr->toggleRecording();
    auto rightOptions = widgetPtr->getOptions(1);
    ASSERT_EQ(rightOptions[1].label, "Preview");

    rightOptions[1].onInvoke();
    EXPECT_TRUE(previewPtr->playing);
    EXPECT_EQ(previewPtr->playedFile, widgetPtr->getTakeFile());
    EXPECT_TRUE(widgetPtr->isPreviewPlaying());

    rightOptions = widgetPtr->getOptions(1);
    EXPECT_EQ(rightOptions[1].label, "Stop");
    rightOptions[1].onInvoke();
    EXPECT_FALSE(previewPtr->playing);

    widgetPtr->discardTake();
}
