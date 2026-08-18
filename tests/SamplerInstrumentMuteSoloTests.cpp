#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

TEST(SamplerInstrumentMuteSoloTests, MuteRoundTrip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    auto sample = harness.createTemporarySampleFile("mute.wav", 2048, 44100.0);
    ASSERT_TRUE(sample.existsAsFile());
    ASSERT_TRUE(pads.loadSample(0, sample));

    EXPECT_FALSE(pads.isMuted(0));
    pads.setMute(0, true);
    EXPECT_TRUE(pads.isMuted(0));
    pads.setMute(0, false);
    EXPECT_FALSE(pads.isMuted(0));
}

TEST(SamplerInstrumentMuteSoloTests, SoloRoundTrip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    auto sample = harness.createTemporarySampleFile("solo.wav", 2048, 44100.0);
    ASSERT_TRUE(sample.existsAsFile());
    ASSERT_TRUE(pads.loadSample(0, sample));

    EXPECT_FALSE(pads.isSoloed(0));
    pads.setSolo(0, true);
    EXPECT_TRUE(pads.isSoloed(0));
    pads.setSolo(0, false);
    EXPECT_FALSE(pads.isSoloed(0));
}

TEST(SamplerInstrumentMuteSoloTests, InvalidPadIndexIsNoOp)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.setMute(-1, true);
    pads.setMute(99, true);
    pads.setSolo(-1, true);
    pads.setSolo(99, true);

    EXPECT_FALSE(pads.isMuted(-1));
    EXPECT_FALSE(pads.isMuted(99));
    EXPECT_FALSE(pads.isSoloed(-1));
    EXPECT_FALSE(pads.isSoloed(99));
}
