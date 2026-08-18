#include <gtest/gtest.h>

#include <chrono>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

// Regression guard: getPadsSnapshot() must not take a blocking mutex on the
// meter read path. It's called from the message thread (via widget ticks)
// while the audio thread is writing into the LevelMeasurer. TE's meter
// machinery has its own juce::SpinLock, so no outer mutex is needed. If
// someone reintroduces one, this test might deadlock or slow dramatically.
//
// We hammer getPadsSnapshot() 1000x and assert the whole loop completes in
// well under a few seconds. Regression guard against a blocking mutex on the
// meter read path — not a tight perf contract. The loose bound avoids CI
// flakes on slow/loaded runners; a reintroduced blocking lock would still
// serialize behind audio-buffer meter updates and blow well past this cap.
TEST(MeterReadNoContentionTest, ThousandSnapshotsCompleteQuickly)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        auto snapshot = pads.getPadsSnapshot();
        (void) snapshot;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    EXPECT_LT(ms, 3000) << "getPadsSnapshot() 1000x took " << ms
                        << "ms — did a blocking mutex come back?";
}

TEST(MeterReadNoContentionTest, LevelAccessorReadsPerPadMeterClient)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->meterClient, nullptr);

    pad->meterClient->updateAudioLevel(0, te::DbTimePair { 1, -18.0f });
    pad->meterClient->updateAudioLevel(1, te::DbTimePair { 1, -6.0f });

    EXPECT_FLOAT_EQ(harness.pads().getLevelDb(0), -6.0f);
}

TEST(MeterReadNoContentionTest, StateSnapshotDoesNotMaterialisePatterns)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    ASSERT_TRUE(pads.setStep(0, 0, 0, true));

    const auto state = pads.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::State);
    const auto patterns = pads.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);

    ASSERT_FALSE(state.empty());
    ASSERT_FALSE(patterns.empty());
    EXPECT_TRUE(state[0].patterns.empty());
    ASSERT_FALSE(patterns[0].patterns.empty());
    EXPECT_TRUE(patterns[0].patterns[0].steps[0]);
}

TEST(MeterReadNoContentionTest, StateSnapshotDoesNotConsumeMeterLevel)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    auto* pad = pads.getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->meterClient, nullptr);
    pad->meterClient->updateAudioLevel(0, te::DbTimePair { 1, -12.0f });
    pad->meterClient->updateAudioLevel(1, te::DbTimePair { 1, -6.0f });

    const auto state = pads.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::State);
    const auto levels = pads.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Levels);

    ASSERT_FALSE(state.empty());
    ASSERT_FALSE(levels.empty());
    EXPECT_FLOAT_EQ(state[0].levelPeakDbfs, -100.0f);
    EXPECT_FLOAT_EQ(levels[0].levelPeakDbfs, -6.0f);
}

TEST(MeterReadNoContentionTest, LevelsSnapshotKeepsCoreMixerFieldsOnly)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    const auto sample = harness.createTemporarySampleFile(
        "levels-fast-path.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sample));
    pads.setGainDbRaw(0, -7.5f);

    auto* pad = pads.getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->meterClient, nullptr);
    pad->meterClient->updateAudioLevel(0, te::DbTimePair { 1, -9.0f });

    const auto levels = pads.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Levels);

    ASSERT_FALSE(levels.empty());
    EXPECT_EQ(levels[0].id, 0);
    EXPECT_TRUE(levels[0].hasSample);
    EXPECT_FLOAT_EQ(levels[0].gainDb, -7.5f);
    EXPECT_FLOAT_EQ(levels[0].levelPeakDbfs, -9.0f);
    EXPECT_TRUE(levels[0].name.isEmpty());
    EXPECT_EQ(levels[0].sampleLayerCount, 0);
    EXPECT_TRUE(levels[0].sampleLayers.empty());
    EXPECT_EQ(levels[0].totalSamples, 0);
}

TEST(MeterReadNoContentionTest, StateSnapshotAvoidsPatternMaterialisationCost)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    for (int i = 0; i < 7; ++i)
        ASSERT_GE(pads.createPattern(), 0);
    for (int pattern = 0; pattern < 8; ++pattern)
        for (int pad = 0; pad < 16; ++pad)
            ASSERT_TRUE(pads.setStep(pattern, pad, (pattern + pad) % 16, true));

    const auto measure = [&pads](SamplerInstrument::SnapshotContent content)
    {
        constexpr int Iterations = 200;
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < Iterations; ++i)
        {
            const auto snapshot = pads.getPadsSnapshot(content);
            EXPECT_EQ(snapshot.size(), 16u);
        }
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count() / Iterations;
    };

    const double fullMs = measure(SamplerInstrument::SnapshotContent::Patterns);
    const double stateMs = measure(SamplerInstrument::SnapshotContent::State);
    std::printf("\n  [PERF] pad snapshot patterns=%.3fms state=%.3fms speedup=%.1fx\n",
                fullMs, stateMs, fullMs / stateMs);

    EXPECT_LT(stateMs, fullMs * 0.5);
}
