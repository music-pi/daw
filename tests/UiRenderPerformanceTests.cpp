#include <gtest/gtest.h>

#include "../src/ui/IRefreshable.h"
#include "../src/devices/Mk3Device.h"
#include "../src/ui/theme/UiTheme.h"
#include "../src/ui/widget/MixerWidget.h"
#include "../src/ui/widget/PadOverviewWidget.h"
#include "../src/ui/widget/PatternWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

namespace
{
class RenderHost final : public juce::Component, public IRefreshable
{
public:
    RenderHost(AudioEngine& audio, testharness::MockControllerHost& controller)
    {
        setBounds(0, 0, UiTheme::kTotalWidth, UiTheme::kPanelHeight);
        addAndMakeVisible(windowManager);
        windowManager.setBounds(getLocalBounds());
        windowManager.setAudioEngine(&audio);
        windowManager.setControllerHost(&controller);
    }

    void requestDisplayRefresh() noexcept override {}

    WindowManager windowManager;
};

struct RenderMetrics
{
    double meanMs { 0.0 };
    double p95Ms { 0.0 };
    double maximumMs { 0.0 };
};

void populatePads(testharness::EngineHarness& harness)
{
    auto& pads = harness.pads();
    for (int pad = 0; pad < 16; ++pad)
    {
        const auto sample = harness.createTemporarySampleFile(
            "ui_render_pad_" + juce::String(pad), 4096, 48000.0);
        ASSERT_TRUE(pads.loadSample(pad, sample));
        for (int step = pad % 4; step < pads.getStepCount(); step += 4)
            pads.setStep(0, pad, step, true);
    }
}

RenderMetrics benchmarkRender(RenderHost& host)
{
    juce::Image frame(juce::Image::ARGB,
                      UiTheme::kTotalWidth,
                      UiTheme::kPanelHeight,
                      true,
                      juce::SoftwareImageType());
    const auto render = [&]
    {
        host.windowManager.refreshBars();
        host.windowManager.tickActiveWidgets();
        juce::Graphics graphics(frame);
        graphics.fillAll(juce::Colours::transparentBlack);
        host.paintEntireComponent(graphics, false);
    };

    for (int i = 0; i < 12; ++i)
        render();

    constexpr int Iterations = 180;
    std::vector<double> samples;
    samples.reserve(Iterations);
    for (int i = 0; i < Iterations; ++i)
    {
        const auto started = std::chrono::steady_clock::now();
        render();
        samples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count());
    }

    std::sort(samples.begin(), samples.end());
    return {
        std::accumulate(samples.begin(), samples.end(), 0.0)
            / static_cast<double>(samples.size()),
        samples[static_cast<size_t>(samples.size() * 95 / 100)],
        samples.back()
    };
}

bool imagesDiffer(const juce::Image& first, const juce::Image& second)
{
    juce::Image::BitmapData firstData(first, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData secondData(second, juce::Image::BitmapData::readOnly);
    const auto rowBytes = static_cast<size_t>(first.getWidth() * firstData.pixelStride);
    for (int y = 0; y < first.getHeight(); ++y)
        if (std::memcmp(firstData.getLinePointer(y), secondData.getLinePointer(y), rowBytes) != 0)
            return true;
    return false;
}

bool renderAndFlushHardware(RenderHost& host, Mk3Device& device, juce::Image& frame)
{
    host.windowManager.refreshBars();
    host.windowManager.tickActiveWidgets();
    juce::Graphics graphics(frame);
    graphics.fillAll(juce::Colours::transparentBlack);
    host.paintEntireComponent(graphics, false);

    const auto left = frame.getClippedImage(
        { 0, 0, UiTheme::kPanelWidth, UiTheme::kPanelHeight });
    const auto right = frame.getClippedImage(
        { UiTheme::kPanelWidth, 0,
          UiTheme::kPanelWidth, UiTheme::kPanelHeight });
    return device.sendDisplayFrame(0, left)
        && device.sendDisplayFrame(1, right)
        && device.flushDisplayFrames();
}

void reportRenderMetrics(const char* name, const RenderMetrics& metrics)
{
    std::printf("\n  [PERF] %-28s mean=%.3fms p95=%.3fms max=%.3fms "
                "ceiling=%.1ffps\n",
                name, metrics.meanMs, metrics.p95Ms, metrics.maximumMs,
                1000.0 / metrics.p95Ms);
    EXPECT_LT(metrics.p95Ms, 1000.0 / 60.0)
        << "The software UI pipeline should retain a 60 fps frame budget";
}
} // namespace

TEST(UiRenderPerformanceTests, PopulatedDualPanelUpdateAndPaintFitsSixtyFpsBudget)
{
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost controller;
    harness.createEmptyEdit();

    populatePads(harness);

    RenderHost host(harness.audio(), controller);
    host.windowManager.open(std::make_unique<PatternWidget>(), DisplaySide::Left);
    host.windowManager.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);
    juceContext.flushMessageQueue(20);

    const auto metrics = benchmarkRender(host);
    reportRenderMetrics("pattern + pad overview", metrics);
    RecordProperty("mean_ms", metrics.meanMs);
    RecordProperty("p95_ms", metrics.p95Ms);
    RecordProperty("max_ms", metrics.maximumMs);
    RecordProperty("p95_fps_ceiling", 1000.0 / metrics.p95Ms);
}

TEST(UiRenderPerformanceTests, MixerDualPanelUpdateAndPaintFitsSixtyFpsBudget)
{
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost controller;
    harness.createEmptyEdit();
    populatePads(harness);
    harness.pads().selectPad(0);

    RenderHost host(harness.audio(), controller);
    host.windowManager.open(std::make_unique<MixerWidget>(), DisplaySide::Left);
    host.windowManager.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);
    juceContext.flushMessageQueue(20);

    const auto metrics = benchmarkRender(host);
    reportRenderMetrics("mixer + pad overview", metrics);
    RecordProperty("mean_ms", metrics.meanMs);
    RecordProperty("p95_ms", metrics.p95Ms);
    RecordProperty("max_ms", metrics.maximumMs);
    RecordProperty("p95_fps_ceiling", 1000.0 / metrics.p95Ms);
}

TEST(UiRenderPerformanceTests, AttachedHardwareActiveDualPanelThroughput)
{
    if (!juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_HARDWARE_PERF", "0").equalsIgnoreCase("1"))
        GTEST_SKIP() << "Set MASCHINEPI_HARDWARE_PERF=1 to exercise the attached MK3";

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost controller;
    harness.createEmptyEdit();
    populatePads(harness);
    harness.pads().selectPad(0);

    Mk3Device device;
    if (!device.isConnected())
        GTEST_SKIP() << "MK3 is not connected or accessible";

    RenderHost host(harness.audio(), controller);
    host.windowManager.open(std::make_unique<MixerWidget>(), DisplaySide::Left);
    host.windowManager.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);
    juceContext.flushMessageQueue(20);

    juce::Image frame(juce::Image::ARGB,
                      UiTheme::kTotalWidth,
                      UiTheme::kPanelHeight,
                      true,
                      juce::SoftwareImageType());
    const auto renderAndSend = [&]
    {
        EXPECT_TRUE(renderAndFlushHardware(host, device, frame));
    };

    renderAndSend();
    auto previousFrame = frame.createCopy();
    constexpr int Iterations = 48;
    int changedFrameCount = 0;
    std::vector<double> samples;
    samples.reserve(Iterations);
    for (int i = 0; i < Iterations; ++i)
    {
        harness.pads().setGainDb(0, i % 2 == 0 ? -6.0f : -12.0f);
        const auto started = std::chrono::steady_clock::now();
        renderAndSend();
        samples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count());
        if (imagesDiffer(frame, previousFrame))
            ++changedFrameCount;
        previousFrame = frame.createCopy();
    }

    std::sort(samples.begin(), samples.end());
    const auto mean = std::accumulate(samples.begin(), samples.end(), 0.0)
                    / static_cast<double>(samples.size());
    const auto p95 = samples[static_cast<size_t>(samples.size() * 95 / 100)];
    const auto maximum = samples.back();
    std::printf("\n  [PERF] MK3 active dual-panel UI mean=%.3fms p95=%.3fms "
                "max=%.3fms throughput=%.1ffps\n",
                mean, p95, maximum, 1000.0 / mean);
    RecordProperty("mean_ms", mean);
    RecordProperty("p95_ms", p95);
    RecordProperty("max_ms", maximum);
    RecordProperty("mean_fps", 1000.0 / mean);
    RecordProperty("changed_frames", changedFrameCount);
    EXPECT_EQ(changedFrameCount, Iterations)
        << "The hardware benchmark must exercise visually distinct frames";
    EXPECT_LT(p95, 1000.0 / 60.0)
        << "Active dual-panel updates should retain a 60 fps hardware budget";
}

TEST(UiRenderPerformanceTests, AttachedHardwarePatternPlaybackSustainsFortyFps)
{
    if (!juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_HARDWARE_PERF", "0").equalsIgnoreCase("1"))
        GTEST_SKIP() << "Set MASCHINEPI_HARDWARE_PERF=1 to exercise the attached MK3";

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost controller;
    harness.createEmptyEdit();
    harness.audio().initialiseAudioDevice(48000, 128);
    populatePads(harness);

    Mk3Device device;
    if (!device.isConnected())
        GTEST_SKIP() << "MK3 is not connected or accessible";

    RenderHost host(harness.audio(), controller);
    host.windowManager.open(std::make_unique<PatternWidget>(), DisplaySide::Left);
    host.windowManager.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);
    juceContext.flushMessageQueue(20);

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto& transport = edit->getTransport();
    transport.ensureContextAllocated();
    transport.setLoopRange(tracktion::TimeRange(
        tracktion::TimePosition::fromSeconds(0.0),
        tracktion::TimePosition::fromSeconds(4.0)));
    transport.looping.setValue(true, nullptr);
    harness.audio().play();
    ASSERT_TRUE(harness.audio().isPlaying());
    juce::Thread::sleep(50);
    auto* audioDevice = harness.audio().getAudioDeviceManager().getCurrentAudioDevice();
    const int xrunsBefore = audioDevice != nullptr ? audioDevice->getXRunCount() : -1;

    juce::Image frame(juce::Image::ARGB,
                      UiTheme::kTotalWidth,
                      UiTheme::kPanelHeight,
                      true,
                      juce::SoftwareImageType());
    ASSERT_TRUE(renderAndFlushHardware(host, device, frame));
    auto previousFrame = frame.createCopy();

    constexpr int Iterations = 60;
    int changedFrameCount = 0;
    double maximumPlayheadStep = 0.0;
    std::vector<double> samples;
    samples.reserve(Iterations);
    auto nextFrame = std::chrono::steady_clock::now();
    for (int i = 0; i < Iterations; ++i)
    {
        nextFrame += std::chrono::milliseconds(16);
        std::this_thread::sleep_until(nextFrame);
        transport.setPosition(tracktion::TimePosition::fromSeconds(
            static_cast<double>(i + 1) * 0.016));
        const auto started = std::chrono::steady_clock::now();
        ASSERT_TRUE(renderAndFlushHardware(host, device, frame));
        samples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count());
        if (imagesDiffer(frame, previousFrame))
            ++changedFrameCount;
        maximumPlayheadStep = juce::jmax(
            maximumPlayheadStep, harness.pads().getPlayheadStep());
        previousFrame = frame.createCopy();
    }
    const double finalPositionSeconds = transport.getPosition().inSeconds();
    harness.audio().stop();
    const int xrunsAfter = audioDevice != nullptr ? audioDevice->getXRunCount() : -1;

    std::sort(samples.begin(), samples.end());
    const auto mean = std::accumulate(samples.begin(), samples.end(), 0.0)
                    / static_cast<double>(samples.size());
    const auto p95 = samples[static_cast<size_t>(samples.size() * 95 / 100)];
    const auto maximum = samples.back();
    std::printf("\n  [PERF] MK3 Pattern playback mean=%.3fms p95=%.3fms "
                "max=%.3fms changed=%d/%d playhead=%.2f position=%.3fs\n",
                mean, p95, maximum, changedFrameCount, Iterations,
                maximumPlayheadStep, finalPositionSeconds);
    RecordProperty("mean_ms", mean);
    RecordProperty("p95_ms", p95);
    RecordProperty("max_ms", maximum);
    RecordProperty("p95_fps", 1000.0 / p95);
    RecordProperty("changed_frames", changedFrameCount);
    if (xrunsBefore >= 0 && xrunsAfter >= 0)
    {
        RecordProperty("xruns", xrunsAfter - xrunsBefore);
        EXPECT_EQ(xrunsAfter - xrunsBefore, 0);
    }

    EXPECT_GE(changedFrameCount, Iterations * 9 / 10)
        << "Playback should produce a visually distinct frame on each 60 Hz tick";
    EXPECT_LT(p95, 1000.0 / 40.0)
        << "Pattern playback should sustain at least 40 fps on hardware";
}
