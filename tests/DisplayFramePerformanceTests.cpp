#include <gtest/gtest.h>

#include "../src/devices/DisplayFrameConversion.h"
#include "../src/devices/Mk3Device.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <vector>

extern "C" {
#include "mk3_output_map.h"
}

namespace
{
constexpr int Width = 480;
constexpr int Height = 272;
constexpr size_t PixelCount = static_cast<size_t>(Width) * Height;

uint16_t legacyRgb565(const juce::Colour& colour)
{
    const auto red = static_cast<uint16_t>((colour.getRed() >> 3) & 0x1f);
    const auto green = static_cast<uint16_t>((colour.getGreen() >> 2) & 0x3f);
    const auto blue = static_cast<uint16_t>((colour.getBlue() >> 3) & 0x1f);
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

void legacyConvert(const juce::Image& image, std::vector<uint16_t>& output)
{
    size_t index = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            output[index++] = legacyRgb565(image.getPixelAt(x, y));
}

juce::Image makeTestFrame()
{
    juce::Image image(juce::Image::ARGB, Width, Height, true,
                      juce::SoftwareImageType());
    juce::Graphics graphics(image);
    graphics.fillAll(juce::Colour(18, 22, 29));
    for (int y = 0; y < Height; y += 8)
    {
        graphics.setColour(juce::Colour::fromHSV(
            static_cast<float>(y) / Height, 0.8f, 0.9f, 1.0f));
        graphics.fillRect(0, y, Width, 4);
    }
    graphics.setColour(juce::Colours::white.withAlpha(0.7f));
    graphics.drawText("MusicPI display performance", image.getBounds(),
                      juce::Justification::centred);
    return image;
}

template<typename Callback>
double benchmarkMilliseconds(Callback&& callback, int iterations)
{
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
        callback();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count() / iterations;
}
} // namespace

TEST(DisplayFrameConversionTests, MatchesLegacyConversionForAllImageFormats)
{
    for (const auto format : { juce::Image::ARGB, juce::Image::RGB,
                               juce::Image::SingleChannel })
    {
        juce::Image image(format, 7, 5, true, juce::SoftwareImageType());
        if (format != juce::Image::SingleChannel)
        {
            juce::Graphics graphics(image);
            graphics.fillAll(juce::Colour(20, 40, 80));
            graphics.setColour(juce::Colour(230, 70, 110).withAlpha(0.45f));
            graphics.fillRect(1, 1, 5, 3);
        }

        std::vector<uint16_t> expected(35);
        std::vector<uint16_t> actual(35);
        legacyConvert(image, expected);

        ASSERT_TRUE(DisplayFrameConversion::toRgb565(image, actual));
        EXPECT_EQ(actual, expected);

        std::vector<uint8_t> bigEndian(70);
        ASSERT_TRUE(DisplayFrameConversion::toRgb565BigEndian(image, bigEndian));
        for (size_t i = 0; i < actual.size(); ++i)
        {
            EXPECT_EQ(bigEndian[i * 2], static_cast<uint8_t>(actual[i] >> 8));
            EXPECT_EQ(bigEndian[i * 2 + 1], static_cast<uint8_t>(actual[i] & 0xff));
        }
    }
}

TEST(DisplayFrameConversionTests, RejectsInvalidBuffers)
{
    const juce::Image image(juce::Image::ARGB, 4, 4, true,
                            juce::SoftwareImageType());
    std::vector<uint16_t> tooSmall(15);
    std::vector<uint8_t> tooSmallBytes(31);

    EXPECT_FALSE(DisplayFrameConversion::toRgb565({}, tooSmall));
    EXPECT_FALSE(DisplayFrameConversion::toRgb565(image, tooSmall));
    EXPECT_FALSE(DisplayFrameConversion::toRgb565BigEndian(image, tooSmallBytes));
}

TEST(DisplayFramePerformanceTests, BulkConversionIsAtLeastFourTimesFasterThanLegacy)
{
    const auto image = makeTestFrame();
    std::vector<uint16_t> legacy(PixelCount);
    std::vector<uint16_t> bulk(PixelCount);

    legacyConvert(image, legacy);
    ASSERT_TRUE(DisplayFrameConversion::toRgb565(image, bulk));
    ASSERT_EQ(bulk, legacy);

    constexpr int Iterations = 12;
    const double legacyMs = benchmarkMilliseconds(
        [&] { legacyConvert(image, legacy); }, Iterations);
    const double bulkMs = benchmarkMilliseconds(
        [&] { ASSERT_TRUE(DisplayFrameConversion::toRgb565(image, bulk)); },
        Iterations);

    std::printf("\n  [PERF] RGB565 conversion legacy=%.3fms bulk=%.3fms speedup=%.1fx\n",
                legacyMs, bulkMs, legacyMs / bulkMs);
    RecordProperty("legacy_ms", legacyMs);
    RecordProperty("bulk_ms", bulkMs);
    RecordProperty("speedup", legacyMs / bulkMs);

    EXPECT_LT(bulkMs, legacyMs * 0.25);
}

TEST(DisplayFramePerformanceTests, AttachedHardwareIncrementalFrameThroughput)
{
    if (!juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_HARDWARE_PERF", "0").equalsIgnoreCase("1"))
        GTEST_SKIP() << "Set MASCHINEPI_HARDWARE_PERF=1 to exercise the attached MK3";

    Mk3Device device;
    if (!device.isConnected())
        GTEST_SKIP() << "MK3 is not connected or accessible";

    auto image = makeTestFrame();
    ASSERT_TRUE(device.sendDisplayFrame(0, image));
    ASSERT_TRUE(device.sendDisplayFrame(1, image));
    ASSERT_TRUE(device.flushDisplayFrames());

    constexpr int Iterations = 60;
    std::vector<double> transferTimes;
    transferTimes.reserve(Iterations);
    for (int i = 0; i < Iterations; ++i)
    {
        image.setPixelAt(i % Width, i % Height,
                         i % 2 == 0 ? juce::Colours::white : juce::Colours::black);
        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(device.sendDisplayFrame(i % 2, image));
        ASSERT_TRUE(device.flushDisplayFrames());
        transferTimes.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }

    std::sort(transferTimes.begin(), transferTimes.end());
    const double mean = std::accumulate(transferTimes.begin(), transferTimes.end(), 0.0)
                      / transferTimes.size();
    const double p95 = transferTimes[static_cast<size_t>(Iterations * 0.95)];
    const double max = transferTimes.back();
    std::printf("\n  [PERF] MK3 incremental frame mean=%.3fms p95=%.3fms max=%.3fms "
                "dual-panel ceiling=%.1ffps\n",
                mean, p95, max, 1000.0 / (mean * 2.0));
    RecordProperty("mean_ms", mean);
    RecordProperty("p95_ms", p95);
    RecordProperty("max_ms", max);
    RecordProperty("dual_panel_fps_ceiling", 1000.0 / (mean * 2.0));

    // Exercise the worst case separately: every pixel changes, so libmk3's
    // dirty bounding box is the entire panel even though partial rendering is
    // enabled. This documents the physical USB limit alongside the normal
    // incremental path.
    constexpr int FullFrameIterations = 8;
    std::vector<double> fullFrameTimes;
    fullFrameTimes.reserve(FullFrameIterations);
    for (int i = 0; i < FullFrameIterations; ++i)
    {
        juce::Graphics graphics(image);
        graphics.fillAll((i / 2) % 2 == 0 ? juce::Colours::red : juce::Colours::blue);
        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(device.sendDisplayFrame(i % 2, image));
        ASSERT_TRUE(device.flushDisplayFrames());
        fullFrameTimes.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    const double fullMean = std::accumulate(
        fullFrameTimes.begin(), fullFrameTimes.end(), 0.0) / fullFrameTimes.size();
    std::printf("  [PERF] MK3 worst-case full-panel mean=%.3fms "
                "dual-panel ceiling=%.1ffps\n",
                fullMean, 1000.0 / (fullMean * 2.0));
    RecordProperty("full_panel_mean_ms", fullMean);
}

TEST(DisplayFramePerformanceTests, AsyncSubmissionDoesNotBlockOnFullPanelUsbTransfer)
{
    if (!juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_HARDWARE_PERF", "0").equalsIgnoreCase("1"))
        GTEST_SKIP() << "Set MASCHINEPI_HARDWARE_PERF=1 to exercise the attached MK3";

    Mk3Device device;
    if (!device.isConnected())
        GTEST_SKIP() << "MK3 is not connected or accessible";

    juce::Image red(juce::Image::ARGB, Width, Height, true,
                    juce::SoftwareImageType());
    juce::Image blue(juce::Image::ARGB, Width, Height, true,
                     juce::SoftwareImageType());
    {
        juce::Graphics graphics(red);
        graphics.fillAll(juce::Colours::red);
    }
    {
        juce::Graphics graphics(blue);
        graphics.fillAll(juce::Colours::blue);
    }

    ASSERT_TRUE(device.sendDisplayFrame(0, red));
    ASSERT_TRUE(device.sendDisplayFrame(1, red));
    ASSERT_TRUE(device.flushDisplayFrames());

    constexpr int Iterations = 120;
    std::vector<double> submissionTimes;
    submissionTimes.reserve(Iterations);
    for (int i = 0; i < Iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(device.sendDisplayFrame(i % 2, i % 4 < 2 ? blue : red));
        submissionTimes.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    ASSERT_TRUE(device.flushDisplayFrames());

    std::sort(submissionTimes.begin(), submissionTimes.end());
    const double mean = std::accumulate(submissionTimes.begin(), submissionTimes.end(), 0.0)
                      / submissionTimes.size();
    const double p95 = submissionTimes[static_cast<size_t>(Iterations * 0.95)];
    const double max = submissionTimes.back();
    std::printf("\n  [PERF] MK3 async full-frame submit mean=%.3fms p95=%.3fms "
                "max=%.3fms\n", mean, p95, max);
    RecordProperty("submit_mean_ms", mean);
    RecordProperty("submit_p95_ms", p95);
    RecordProperty("submit_max_ms", max);

    EXPECT_LT(p95, 2.0) << "USB display transfer leaked onto the caller thread";
}

TEST(DisplayFramePerformanceTests, AttachedHardwareLedBatchThroughput)
{
    if (!juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_HARDWARE_PERF", "0").equalsIgnoreCase("1"))
        GTEST_SKIP() << "Set MASCHINEPI_HARDWARE_PERF=1 to exercise the attached MK3";

    Mk3Device device;
    if (!device.isConnected())
        GTEST_SKIP() << "MK3 is not connected or accessible";

    constexpr int Iterations = 6;
    uint8_t immediateValue = 5;
    bool immediateSucceeded = true;
    const double immediateMs = benchmarkMilliseconds([&]
    {
        immediateValue = immediateValue == 5 ? 9 : 5;
        for (int pad = 1; pad <= 16; ++pad)
            immediateSucceeded = device.setIndexedLed(
                "p" + std::to_string(pad), immediateValue) && immediateSucceeded;
    }, Iterations);

    uint8_t batchValue = 13;
    bool batchSucceeded = true;
    const double batchMs = benchmarkMilliseconds([&]
    {
        batchValue = batchValue == 13 ? 17 : 13;
        device.beginLedBatch();
        for (int pad = 1; pad <= 16; ++pad)
            batchSucceeded = device.setIndexedLed(
                "p" + std::to_string(pad), batchValue) && batchSucceeded;
        device.endLedBatch();
    }, Iterations);

    device.beginLedBatch();
    for (int pad = 1; pad <= 16; ++pad)
        device.setIndexedLed("p" + std::to_string(pad), 0);
    device.endLedBatch();

    ASSERT_TRUE(immediateSucceeded);
    ASSERT_TRUE(batchSucceeded);
    std::printf("\n  [PERF] MK3 16-LED update immediate=%.3fms batch=%.3fms "
                "speedup=%.1fx\n",
                immediateMs, batchMs, immediateMs / batchMs);
    RecordProperty("immediate_ms", immediateMs);
    RecordProperty("batch_ms", batchMs);
    RecordProperty("speedup", immediateMs / batchMs);

    EXPECT_LT(batchMs, immediateMs * 0.75)
        << "A library-level change must improve real hardware throughput by at least 25%";
}

TEST(DisplayFramePerformanceTests, AttachedHardwareFullLedResetThroughput)
{
    if (!juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_HARDWARE_PERF", "0").equalsIgnoreCase("1"))
        GTEST_SKIP() << "Set MASCHINEPI_HARDWARE_PERF=1 to exercise the attached MK3";

    Mk3Device device;
    if (!device.isConnected())
        GTEST_SKIP() << "MK3 is not connected or accessible";

    constexpr int Iterations = 4;
    bool immediateSucceeded = true;
    const double immediateMs = benchmarkMilliseconds([&]
    {
        device.clearAllDisplays();
        for (int i = 0; i < mk3_leds_count; ++i)
        {
            const auto& definition = mk3_leds[i];
            if (definition.type == MK3_LED_TYPE_MONO)
                immediateSucceeded = device.setMonoLed(definition.name, 7)
                                  && immediateSucceeded;
            else
                immediateSucceeded = device.setIndexedLed(definition.name, 7)
                                  && immediateSucceeded;
        }
    }, Iterations);

    const double resetMs = benchmarkMilliseconds(
        [&] { device.resetAllLeds(); }, Iterations);

    ASSERT_TRUE(immediateSucceeded);
    std::printf("\n  [PERF] MK3 full LED reset immediate=%.3fms batch=%.3fms "
                "speedup=%.1fx\n",
                immediateMs, resetMs, immediateMs / resetMs);
    RecordProperty("immediate_ms", immediateMs);
    RecordProperty("batch_ms", resetMs);
    RecordProperty("speedup", immediateMs / resetMs);

    EXPECT_LT(resetMs, immediateMs * 0.75)
        << "A library-level change must improve real hardware throughput by at least 25%";
}
