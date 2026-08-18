#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/devices/Mk3Device.h"
#include "harness/EngineHarness.h"

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>

#include <chrono>
#include <ctime>
#include <thread>

namespace
{

struct PerfMetrics
{
    double cpuMean { 0.0 };
    double cpuMax { 0.0 };
    int xrunCount { -1 };
    int latencySamples { 0 };
    int blockSize { 0 };
    double sampleRate { 0.0 };
    double processCpuCores { 0.0 };
};

void printMetrics(const char* name, const PerfMetrics& m)
{
    printf("\n  [PERF] %-30s  CPU mean=%.2f%%  max=%.2f%%  xruns=%s  "
           "process=%.2f cores  latency=%d samples  block=%d  rate=%.0f Hz\n",
           name,
           m.cpuMean * 100.0,
           m.cpuMax * 100.0,
           m.xrunCount >= 0 ? std::to_string(m.xrunCount).c_str() : "n/a",
           m.processCpuCores,
           m.latencySamples,
           m.blockSize,
           m.sampleRate);
}

const char* threadPoolStrategyName(tracktion::graph::ThreadPoolStrategy strategy)
{
    using Strategy = tracktion::graph::ThreadPoolStrategy;
    switch (strategy)
    {
        case Strategy::conditionVariable:    return "condition_variable";
        case Strategy::realTime:             return "realtime";
        case Strategy::hybrid:               return "hybrid";
        case Strategy::semaphore:            return "semaphore";
        case Strategy::lightweightSemaphore: return "lightweight_semaphore";
        case Strategy::lightweightSemHybrid: return "lightweight_semaphore_hybrid";
    }
    return "unknown";
}

int hardwareStressDurationMs()
{
    const auto requested = juce::SystemStats::getEnvironmentVariable(
        "MASCHINEPI_HARDWARE_PERF_DURATION_MS", "10000").getIntValue();
    return juce::jlimit(1000, 6 * 60 * 60 * 1000, requested);
}

int hardwareStressBufferSize()
{
    const auto requested = juce::SystemStats::getEnvironmentVariable(
        "MASCHINEPI_HARDWARE_PERF_BUFFER_SIZE", "128").getIntValue();

    switch (requested)
    {
        case 32:
        case 64:
        case 128:
        case 256:
        case 512:
            return requested;
        default:
            return 128;
    }
}

} // namespace

class RealTimePerfTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        te::EditPlaybackContext::setThreadPoolStrategy(
            static_cast<int>(tracktion::graph::ThreadPoolStrategy::realTime));
    }

    void initialiseForPerf(int sampleRate, int bufferSize)
    {
        harness.createEmptyEdit();
        harness.audio().initialiseAudioDevice(sampleRate, bufferSize);

        auto* device = harness.audio().getAudioDeviceManager().getCurrentAudioDevice();
        if (device == nullptr)
            GTEST_SKIP() << "No audio device available - skipping perf test";

        auto* edit = harness.audio().getEdit();
        ASSERT_NE(edit, nullptr);

        edit->getTransport().ensureContextAllocated();

        auto* context = edit->getTransport().getCurrentPlaybackContext();
        if (context == nullptr)
            GTEST_SKIP() << "Playback context could not be allocated - skipping perf test";
    }

    void loadPadsWithPattern(int numPads)
    {
        auto& pads = harness.pads();
        for (int i = 0; i < numPads; ++i)
        {
            auto sample = harness.createTemporarySampleFile(
                "perf_pad_" + juce::String(i), 48000, 48000.0);
            pads.loadSample(i, sample);
        }

        if (numPads > 0)
        {
            // Use default pattern 0 (created by ensureInfrastructure)
            int stepCount = pads.getStepCount();
            for (int pad = 0; pad < numPads; ++pad)
            {
                for (int step = 0; step < stepCount; step += 4)
                    pads.setStep(0, pad, step, true);
            }
        }
    }

    PerfMetrics collectMetrics(int durationMs)
    {
        PerfMetrics m;
        const auto wallStart = std::chrono::steady_clock::now();
        const auto cpuStart = std::clock();

        auto& teDeviceManager = harness.engine().getDeviceManager();
        auto& juceDeviceManager = harness.audio().getAudioDeviceManager();
        auto* juceDevice = juceDeviceManager.getCurrentAudioDevice();

        m.blockSize = teDeviceManager.getBlockSize();
        m.sampleRate = teDeviceManager.getSampleRate();

        int xrunsBefore = juceDevice ? juceDevice->getXRunCount() : -1;

        auto& transport = harness.audio().getEdit()->getTransport();

        using namespace tracktion;
        transport.setLoopRange(TimeRange(TimePosition::fromSeconds(0.0),
                                         TimePosition::fromSeconds(4.0)));
        transport.looping.setValue(true, nullptr);
        transport.play(false);

        // Let audio settle for one buffer cycle before sampling
        juce::Thread::sleep(50);

        // Sample CPU usage periodically during measurement window
        constexpr int sampleIntervalMs = 100;
        int numSamples = (durationMs - 50) / sampleIntervalMs;
        if (numSamples < 1)
            numSamples = 1;

        double cpuSum = 0.0;
        double cpuMax = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            juce::Thread::sleep(sampleIntervalMs);
            double cpu = juceDeviceManager.getCpuUsage();
            cpuSum += cpu;
            if (cpu > cpuMax)
                cpuMax = cpu;
        }

        m.cpuMean = cpuSum / static_cast<double>(numSamples);
        m.cpuMax = cpuMax;

        transport.stop(false, false);

        int xrunsAfter = juceDevice ? juceDevice->getXRunCount() : -1;
        if (xrunsBefore >= 0 && xrunsAfter >= 0)
            m.xrunCount = xrunsAfter - xrunsBefore;
        else
            m.xrunCount = -1;

        if (auto* context = transport.getCurrentPlaybackContext())
            m.latencySamples = context->getLatencySamples();

        const auto cpuElapsed = static_cast<double>(std::clock() - cpuStart)
                              / static_cast<double>(CLOCKS_PER_SEC);
        const auto wallElapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - wallStart).count();
        if (wallElapsed > 0.0)
            m.processCpuCores = cpuElapsed / wallElapsed;

        return m;
    }

    void TearDown() override
    {
        harness.audio().stop();
        if (auto* edit = harness.audio().getEdit())
            edit->getTransport().freePlaybackContext();

        te::EditPlaybackContext::setThreadPoolStrategy(
            static_cast<int>(tracktion::graph::ThreadPoolStrategy::lightweightSemHybrid));
    }

    testharness::EngineHarness harness;
};

// ============================================================================
// Progressive Load Tests (48kHz / 256 buffer)
// ============================================================================

TEST_F(RealTimePerfTest, EmptyEditBaseline)
{
    initialiseForPerf(48000, 256);

    auto m = collectMetrics(3000);
    printMetrics("EmptyEditBaseline", m);

    RecordProperty("cpu_mean_pct", m.cpuMean * 100.0);
    RecordProperty("cpu_max_pct", m.cpuMax * 100.0);

    EXPECT_LT(m.cpuMean, 0.10) << "Empty edit CPU should be < 10%";
    if (m.xrunCount >= 0)
    {
        EXPECT_EQ(m.xrunCount, 0) << "Zero underruns expected";
    }
}

TEST_F(RealTimePerfTest, SinglePadLoad)
{
    initialiseForPerf(48000, 256);
    loadPadsWithPattern(1);

    auto m = collectMetrics(3000);
    printMetrics("SinglePadLoad", m);

    RecordProperty("cpu_mean_pct", m.cpuMean * 100.0);

    EXPECT_LT(m.cpuMean, 0.30) << "1-pad CPU should be < 30%";
    if (m.xrunCount >= 0)
    {
        EXPECT_EQ(m.xrunCount, 0);
    }
}

TEST_F(RealTimePerfTest, EightPadLoad)
{
    initialiseForPerf(48000, 256);
    loadPadsWithPattern(8);

    auto m = collectMetrics(3000);
    printMetrics("EightPadLoad", m);

    RecordProperty("cpu_mean_pct", m.cpuMean * 100.0);

    EXPECT_LT(m.cpuMean, 0.60) << "8-pad CPU should be < 60%";
    if (m.xrunCount >= 0)
    {
        EXPECT_EQ(m.xrunCount, 0);
    }
}

TEST_F(RealTimePerfTest, SixteenPadMaxLoad)
{
    initialiseForPerf(48000, 256);
    loadPadsWithPattern(16);

    auto m = collectMetrics(3000);
    printMetrics("SixteenPadMaxLoad", m);

    RecordProperty("cpu_mean_pct", m.cpuMean * 100.0);

    EXPECT_LT(m.cpuMean, 0.80) << "16-pad CPU should be < 80%";
    if (m.xrunCount >= 0)
    {
        EXPECT_EQ(m.xrunCount, 0);
    }
}

// ============================================================================
// Buffer Size Sweep (48kHz / 16 pads)
// ============================================================================

class BufferSizeSweepTest : public RealTimePerfTest,
                            public ::testing::WithParamInterface<int>
{
};

TEST_P(BufferSizeSweepTest, SixteenPadsAtBufferSize)
{
    int bufferSize = GetParam();
    initialiseForPerf(48000, bufferSize);
    loadPadsWithPattern(16);

    auto m = collectMetrics(3000);

    char name[64];
    snprintf(name, sizeof(name), "16pads_buf%d", bufferSize);
    printMetrics(name, m);

    RecordProperty("buffer_size", bufferSize);
    RecordProperty("cpu_mean_pct", m.cpuMean * 100.0);
    RecordProperty("latency_samples", m.latencySamples);

    double cpuThreshold = (bufferSize <= 128) ? 0.90
                        : (bufferSize <= 256) ? 0.80
                        :                       0.60;

    EXPECT_LT(m.cpuMean, cpuThreshold)
        << "CPU at buffer " << bufferSize << " should be < " << cpuThreshold * 100 << "%";
    if (m.xrunCount >= 0)
    {
        EXPECT_EQ(m.xrunCount, 0);
    }
}

INSTANTIATE_TEST_SUITE_P(
    RealTimePerf,
    BufferSizeSweepTest,
    ::testing::Values(32, 64, 128, 256, 512),
    [](const ::testing::TestParamInfo<int>& info) {
        return "Buffer" + std::to_string(info.param);
    });

// ============================================================================
// Tracktion graph worker strategy sweep
// ============================================================================

class ThreadPoolStrategyTest : public RealTimePerfTest,
                               public ::testing::WithParamInterface<tracktion::graph::ThreadPoolStrategy>
{
protected:
    void TearDown() override
    {
        RealTimePerfTest::TearDown();
        te::EditPlaybackContext::setThreadPoolStrategy(
            static_cast<int>(tracktion::graph::ThreadPoolStrategy::lightweightSemHybrid));
    }
};

TEST_P(ThreadPoolStrategyTest, SixteenPadsAt128Samples)
{
    const auto strategy = GetParam();
    te::EditPlaybackContext::setThreadPoolStrategy(static_cast<int>(strategy));
    initialiseForPerf(48000, 128);
    loadPadsWithPattern(16);

    auto metrics = collectMetrics(5000);
    printMetrics(threadPoolStrategyName(strategy), metrics);
    RecordProperty("strategy", static_cast<int>(strategy));
    RecordProperty("process_cpu_cores", metrics.processCpuCores);
    RecordProperty("audio_cpu_mean_pct", metrics.cpuMean * 100.0);
    if (metrics.xrunCount >= 0)
    {
        EXPECT_EQ(metrics.xrunCount, 0);
    }
}

INSTANTIATE_TEST_SUITE_P(
    TracktionStrategies,
    ThreadPoolStrategyTest,
    ::testing::Values(
        tracktion::graph::ThreadPoolStrategy::conditionVariable,
        tracktion::graph::ThreadPoolStrategy::realTime,
        tracktion::graph::ThreadPoolStrategy::hybrid,
        tracktion::graph::ThreadPoolStrategy::semaphore,
        tracktion::graph::ThreadPoolStrategy::lightweightSemaphore,
        tracktion::graph::ThreadPoolStrategy::lightweightSemHybrid));

// ============================================================================
// Sustained Max Load Stress Test
// ============================================================================

TEST_F(RealTimePerfTest, SustainedMaxLoad)
{
    initialiseForPerf(48000, 256);
    loadPadsWithPattern(16);

    auto m = collectMetrics(10000);
    printMetrics("SustainedMaxLoad (10s)", m);

    RecordProperty("cpu_mean_pct", m.cpuMean * 100.0);
    RecordProperty("cpu_max_pct", m.cpuMax * 100.0);

    EXPECT_LT(m.cpuMean, 0.50) << "Sustained CPU mean should be < 50% (Pi4 headroom)";
    EXPECT_LT(m.cpuMax, 0.90) << "Sustained CPU max should be < 90%";
    if (m.xrunCount >= 0)
    {
        EXPECT_EQ(m.xrunCount, 0) << "Zero underruns over 10s sustained load";
    }
}

TEST_F(RealTimePerfTest, SustainedMaxLoadWithContinuousHardwareDisplayTraffic)
{
    if (!juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_HARDWARE_PERF", "0").equalsIgnoreCase("1"))
        GTEST_SKIP() << "Set MASCHINEPI_HARDWARE_PERF=1 to exercise the attached MK3";

    const int bufferSize = hardwareStressBufferSize();
    initialiseForPerf(48000, bufferSize);
    loadPadsWithPattern(16);

    Mk3Device display;
    if (!display.isConnected())
        GTEST_SKIP() << "MK3 is not connected or accessible";

    std::jthread displayLoad([&display](std::stop_token stopToken)
    {
        juce::Image frame(juce::Image::ARGB, 480, 272, true,
                          juce::SoftwareImageType());
        int frameNumber = 0;
        while (!stopToken.stop_requested())
        {
            juce::Graphics graphics(frame);
            graphics.fillAll((frameNumber / 2) % 2 == 0
                                 ? juce::Colour(10, 18, 30)
                                 : juce::Colour(34, 12, 18));
            graphics.setColour(juce::Colours::white);
            graphics.drawText("Audio stability under display load "
                                  + juce::String(frameNumber),
                              frame.getBounds(), juce::Justification::centred);
            display.sendDisplayFrame(frameNumber % 2, frame);
            ++frameNumber;
        }
    });

    const int durationMs = hardwareStressDurationMs();
    auto metrics = collectMetrics(durationMs);
    displayLoad.request_stop();
    displayLoad.join();
    EXPECT_TRUE(display.flushDisplayFrames());
    const auto name = "16pads_" + std::to_string(bufferSize)
                    + " + MK3 full-frame traffic";
    printMetrics(name.c_str(), metrics);

    RecordProperty("cpu_mean_pct", metrics.cpuMean * 100.0);
    RecordProperty("cpu_max_pct", metrics.cpuMax * 100.0);
    RecordProperty("duration_ms", durationMs);
    RecordProperty("buffer_size", bufferSize);
    if (metrics.xrunCount >= 0)
    {
        EXPECT_EQ(metrics.xrunCount, 0)
            << "Display traffic must never cause audio underruns";
    }
}
