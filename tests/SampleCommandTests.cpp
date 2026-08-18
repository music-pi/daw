#include <gtest/gtest.h>

#include "../src/engine/commands/NormalizeSampleCommand.h"
#include "../src/engine/commands/LoadSampleCommand.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/RoundRobinMidiPlugin.h"
#include "../src/engine/SamplerInstrument.h"

#include "harness/EngineHarness.h"

// ============================================================================
// NormalizeSampleCommand Tests
// ============================================================================

TEST(NormalizeSampleCommandTests, PerformNormalizesSample)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("norm_cmd", 44100);
    harness.pads().loadSample(0, sample);

    NormalizeSampleCommand cmd(harness.audio(), 0, -3.0);
    EXPECT_TRUE(cmd.perform());

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    EXPECT_TRUE(pad->sampleFile.existsAsFile());
    EXPECT_EQ(pad->sampleFile, sample);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_GT(pad->sampler->getNumSounds(), 0);
    EXPECT_NEAR(pad->sampler->getSoundGainDb(0), -3.0f, 0.3f);
}

TEST(NormalizeSampleCommandTests, UndoRestoresOriginal)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("norm_undo", 44100);
    harness.pads().loadSample(0, sample);

    NormalizeSampleCommand cmd(harness.audio(), 0, -3.0);
    EXPECT_TRUE(cmd.perform());
    EXPECT_TRUE(cmd.undo());

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    EXPECT_TRUE(pad->sampleFile.existsAsFile());
    EXPECT_EQ(pad->sampleFile, sample);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_GT(pad->sampler->getNumSounds(), 0);
    EXPECT_NEAR(pad->sampler->getSoundGainDb(0), 0.0f, 0.1f);
}

TEST(NormalizeSampleCommandTests, RedoAfterUndoSucceeds)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("norm_redo", 44100);
    harness.pads().loadSample(0, sample);

    NormalizeSampleCommand cmd(harness.audio(), 0, -3.0);
    EXPECT_TRUE(cmd.perform());
    EXPECT_TRUE(cmd.undo());

    // Redo — same bug scenario as TruncateSampleCommand
    EXPECT_TRUE(cmd.perform());

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    EXPECT_TRUE(pad->sampleFile.existsAsFile());
    EXPECT_EQ(pad->sampleFile, sample);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_GT(pad->sampler->getNumSounds(), 0);
    EXPECT_NEAR(pad->sampler->getSoundGainDb(0), -3.0f, 0.3f);
}

TEST(NormalizeSampleCommandTests, MultipleUndoRedoCycles)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("norm_multi", 44100);
    harness.pads().loadSample(0, sample);

    NormalizeSampleCommand cmd(harness.audio(), 0, -3.0);
    EXPECT_TRUE(cmd.perform());

    for (int i = 0; i < 3; ++i)
    {
        EXPECT_TRUE(cmd.undo()) << "Undo failed on cycle " << i;
        const auto* pad = harness.pads().getPad(0);
        ASSERT_NE(pad, nullptr);
        EXPECT_TRUE(pad->sampleFile.existsAsFile()) << "Sample missing after undo cycle " << i;
        EXPECT_EQ(pad->sampleFile, sample);
        ASSERT_NE(pad->sampler, nullptr);
        ASSERT_GT(pad->sampler->getNumSounds(), 0);
        EXPECT_NEAR(pad->sampler->getSoundGainDb(0), 0.0f, 0.1f);

        EXPECT_TRUE(cmd.perform()) << "Redo failed on cycle " << i;
        pad = harness.pads().getPad(0);
        ASSERT_NE(pad, nullptr);
        EXPECT_TRUE(pad->sampleFile.existsAsFile()) << "Sample missing after redo cycle " << i;
        EXPECT_EQ(pad->sampleFile, sample);
        ASSERT_NE(pad->sampler, nullptr);
        ASSERT_GT(pad->sampler->getNumSounds(), 0);
        EXPECT_NEAR(pad->sampler->getSoundGainDb(0), -3.0f, 0.3f);
    }
}

TEST(SampleCommandTests, UndoLoadRestoresRoundRobinLayerStateInPlugin)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    constexpr int padIndex = 0;
    auto fileA = harness.createTemporarySampleFile("load_undo_a", 44100);
    auto fileB = harness.createTemporarySampleFile("load_undo_b", 44100);
    auto fileC = harness.createTemporarySampleFile("load_undo_c", 44100);
    auto& sampler = harness.pads();
    ASSERT_TRUE(sampler.loadSample(padIndex, fileA));
    ASSERT_TRUE(sampler.addSampleLayer(padIndex, fileB));
    ASSERT_TRUE(sampler.setSampleLayerGainDb(padIndex, 1, -6.0f));
    ASSERT_TRUE(sampler.setSampleLayerRandomWeight(padIndex, 1, 0.25f));
    ASSERT_TRUE(sampler.setSampleLayerVelocityCurve(
        padIndex, 1, SamplerInstrument::LayerVelocityCurve::Fixed));

    auto& undoManager = harness.audio().getUndoManager();
    undoManager.clearUndoHistory();
    undoManager.beginNewTransaction("Load sample");
    ASSERT_TRUE(undoManager.perform(
        new LoadSampleCommand(harness.audio(), padIndex, fileC)));
    ASSERT_TRUE(undoManager.undo());

    const auto* pad = sampler.getPad(padIndex);
    ASSERT_NE(pad, nullptr);
    ASSERT_EQ(pad->sampleLayers.size(), 2u);
    EXPECT_FLOAT_EQ(pad->sampleLayers[1].gainDb, -6.0f);
    EXPECT_FLOAT_EQ(pad->sampleLayers[1].randomWeight, 0.25f);
    EXPECT_EQ(pad->sampleLayers[1].velocityCurve,
              SamplerInstrument::LayerVelocityCurve::Fixed);
    ASSERT_NE(pad->sampler, nullptr);
    auto sound = pad->sampler->state.getChild(1);
    ASSERT_TRUE(sound.hasType(tracktion::engine::IDs::SOUND));
    EXPECT_NEAR(static_cast<float>(sound.getProperty(tracktion::engine::IDs::gainDb)),
                pad->normalizationGainDb - 6.0f,
                0.01f);
    ASSERT_NE(pad->roundRobinMidi, nullptr);
    EXPECT_NEAR(static_cast<float>(
                    pad->roundRobinMidi->state.getProperty("weight2")),
                0.25f,
                0.001f);
    EXPECT_EQ(static_cast<int>(
                  pad->roundRobinMidi->state.getProperty("velocityCurve2")),
              static_cast<int>(SamplerInstrument::LayerVelocityCurve::Fixed));
}

TEST(SampleCommandTests, RawNormalizationPreservesLayerZeroGain)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    constexpr int padIndex = 0;
    auto sample = harness.createTemporarySampleFile("raw_norm_gain", 44100);
    auto& sampler = harness.pads();
    ASSERT_TRUE(sampler.loadSample(padIndex, sample));
    ASSERT_TRUE(sampler.setSampleLayerGainDb(padIndex, 0, -3.0f));
    ASSERT_TRUE(sampler.setPadSampleNormalizationGainDbRaw(padIndex, 2.0f));

    const auto* pad = sampler.getPad(padIndex);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->sampler, nullptr);
    auto sound = pad->sampler->state.getChild(0);
    ASSERT_TRUE(sound.hasType(tracktion::engine::IDs::SOUND));
    EXPECT_NEAR(static_cast<float>(sound.getProperty(tracktion::engine::IDs::gainDb)),
                -1.0f,
                0.01f);
}
