#include <gtest/gtest.h>

#include "../src/engine/RoundRobinMidiPlugin.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

TEST(PadTriggerModeTests, ExistingPadsDefaultToHoldEnvelope)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    EXPECT_EQ(harness.pads().getTriggerMode(0),
              SamplerInstrument::TriggerMode::HoldEnvelope);
}

TEST(PadTriggerModeTests, LayerCountKeepsOnlyApplicableRoundRobinFamily)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    ASSERT_TRUE(pads.loadSample(
        0, harness.createTemporarySampleFile("mode_family_first", 4410)));

    pads.setTriggerMode(0, SamplerInstrument::TriggerMode::RoundRobinRandom);
    EXPECT_EQ(pads.getTriggerMode(0),
              SamplerInstrument::TriggerMode::VelocityRoundRobinRandom);

    ASSERT_TRUE(pads.addSampleLayer(
        0, harness.createTemporarySampleFile("mode_family_second", 4410)));
    EXPECT_EQ(pads.getTriggerMode(0),
              SamplerInstrument::TriggerMode::RoundRobinRandom);

    ASSERT_TRUE(pads.removeLastSampleLayer(0));
    EXPECT_EQ(pads.getTriggerMode(0),
              SamplerInstrument::TriggerMode::VelocityRoundRobinRandom);

    ASSERT_TRUE(pads.addSampleLayer(
        0, harness.createTemporarySampleFile("mode_family_third", 4410)));
    pads.setTriggerMode(
        0, SamplerInstrument::TriggerMode::VelocityRoundRobinOrdered);
    EXPECT_EQ(pads.getTriggerMode(0),
              SamplerInstrument::TriggerMode::RoundRobinOrdered);
}

TEST(PadTriggerModeTests, SingleLayerOrderedRoundRobinCyclesVelocityRange)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    ASSERT_TRUE(pads.loadSample(
        0, harness.createTemporarySampleFile("velocity_rr", 4410)));
    ASSERT_TRUE(pads.setSampleLayerVelocityRange(0, 0, 0.4f, 1.0f));
    pads.setTriggerMode(
        0, SamplerInstrument::TriggerMode::VelocityRoundRobinOrdered);

    const auto* pad = pads.getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->roundRobinMidi, nullptr);
    EXPECT_EQ(pad->roundRobinMidi->getLayerCount(), 1);
    EXPECT_EQ(pad->roundRobinMidi->getPolicy(),
              RoundRobinMidiPlugin::Policy::Ordered);

    te::MidiMessageArray midi;
    for (int i = 0; i < 5; ++i)
        midi.addMidiMessage(juce::MidiMessage::noteOn(
            1, pad->midiNote, 1.0f), {});
    pad->roundRobinMidi->remapMidiMessages(midi);

    ASSERT_EQ(midi.size(), 5);
    EXPECT_NEAR(midi[0].getFloatVelocity(), 0.4f, 0.01f);
    EXPECT_NEAR(midi[1].getFloatVelocity(), 0.6f, 0.01f);
    EXPECT_NEAR(midi[2].getFloatVelocity(), 0.8f, 0.01f);
    EXPECT_NEAR(midi[3].getFloatVelocity(), 1.0f, 0.01f);
    EXPECT_NEAR(midi[4].getFloatVelocity(), 0.4f, 0.01f);
    for (const auto& message : midi)
        EXPECT_EQ(message.getNoteNumber(), pad->midiNote);
}

TEST(PadTriggerModeTests, OneShotAndHoldConfigureSamplerReleaseBehaviour)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    const auto sample = harness.createTemporarySampleFile("trigger_mode", 4410);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));
    auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->sampler, nullptr);

    harness.pads().setTriggerMode(0, SamplerInstrument::TriggerMode::OneShot);
    EXPECT_TRUE(pad->sampler->isSoundOpenEnded(0));

    harness.pads().setTriggerMode(
        0, SamplerInstrument::TriggerMode::HoldEnvelope);
    EXPECT_FALSE(pad->sampler->isSoundOpenEnded(0));
}

TEST(PadTriggerModeTests, OrderedRoundRobinRemapsLiveAndSequencedMidi)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    const auto first = harness.createTemporarySampleFile("rr_first", 4410);
    const auto second = harness.createTemporarySampleFile("rr_second", 4410);
    const auto third = harness.createTemporarySampleFile("rr_third", 4410);
    ASSERT_TRUE(harness.pads().loadSample(0, first));
    ASSERT_TRUE(harness.pads().addSampleLayer(0, second));
    ASSERT_TRUE(harness.pads().addSampleLayer(0, third));
    harness.pads().setTriggerMode(
        0, SamplerInstrument::TriggerMode::RoundRobinOrdered);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->roundRobinMidi, nullptr);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_EQ(pad->sampler->getNumSounds(), 3);
    EXPECT_EQ(pad->sampler->getKeyNote(0), pad->midiNote);
    EXPECT_EQ(pad->sampler->getKeyNote(1),
              pad->midiNote + RoundRobinMidiPlugin::kLayerNoteStride);
    EXPECT_EQ(pad->sampler->getKeyNote(2),
              pad->midiNote + 2 * RoundRobinMidiPlugin::kLayerNoteStride);

    te::MidiMessageArray midi;
    for (int i = 0; i < 4; ++i)
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, pad->midiNote,
                                                      static_cast<juce::uint8>(100)), {});
    pad->roundRobinMidi->remapMidiMessages(midi);

    ASSERT_EQ(midi.size(), 4);
    EXPECT_EQ(midi[0].getNoteNumber(), pad->midiNote);
    EXPECT_EQ(midi[1].getNoteNumber(),
              pad->midiNote + RoundRobinMidiPlugin::kLayerNoteStride);
    EXPECT_EQ(midi[2].getNoteNumber(),
              pad->midiNote + 2 * RoundRobinMidiPlugin::kLayerNoteStride);
    EXPECT_EQ(midi[3].getNoteNumber(), pad->midiNote);
}

TEST(PadTriggerModeTests, AuditionChannelBypassesRoundRobinSelection)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    ASSERT_TRUE(harness.pads().loadSample(
        0, harness.createTemporarySampleFile("audition_first", 4410)));
    ASSERT_TRUE(harness.pads().addSampleLayer(
        0, harness.createTemporarySampleFile("audition_second", 4410)));
    harness.pads().setTriggerMode(
        0, SamplerInstrument::TriggerMode::RoundRobinOrdered);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->roundRobinMidi, nullptr);

    te::MidiMessageArray audition;
    audition.addMidiMessage(juce::MidiMessage::noteOn(
        RoundRobinMidiPlugin::kAuditionMidiChannel, pad->midiNote,
        static_cast<juce::uint8>(100)), {});
    pad->roundRobinMidi->remapMidiMessages(audition);
    ASSERT_EQ(audition.size(), 1);
    EXPECT_EQ(audition[0].getNoteNumber(), pad->midiNote);

    te::MidiMessageArray regular;
    regular.addMidiMessage(juce::MidiMessage::noteOn(
        1, pad->midiNote, static_cast<juce::uint8>(100)), {});
    regular.addMidiMessage(juce::MidiMessage::noteOn(
        1, pad->midiNote, static_cast<juce::uint8>(100)), {});
    pad->roundRobinMidi->remapMidiMessages(regular);
    ASSERT_EQ(regular.size(), 2);
    EXPECT_EQ(regular[0].getNoteNumber(), pad->midiNote);
    EXPECT_EQ(regular[1].getNoteNumber(),
              pad->midiNote + RoundRobinMidiPlugin::kLayerNoteStride);
}

TEST(PadTriggerModeTests, RandomRoundRobinAvoidsImmediateRepeats)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    ASSERT_TRUE(harness.pads().loadSample(
        0, harness.createTemporarySampleFile("random_first", 4410)));
    ASSERT_TRUE(harness.pads().addSampleLayer(
        0, harness.createTemporarySampleFile("random_second", 4410)));
    ASSERT_TRUE(harness.pads().addSampleLayer(
        0, harness.createTemporarySampleFile("random_third", 4410)));
    harness.pads().setTriggerMode(
        0, SamplerInstrument::TriggerMode::RoundRobinRandom);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->roundRobinMidi, nullptr);

    te::MidiMessageArray midi;
    for (int i = 0; i < 32; ++i)
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, pad->midiNote,
                                                      static_cast<juce::uint8>(100)), {});
    pad->roundRobinMidi->remapMidiMessages(midi);

    int previous = -1;
    for (const auto& message : midi)
    {
        const int layer = (message.getNoteNumber() - pad->midiNote)
                        / RoundRobinMidiPlugin::kLayerNoteStride;
        EXPECT_GE(layer, 0);
        EXPECT_LT(layer, 3);
        EXPECT_NE(layer, previous);
        previous = layer;
    }
}

TEST(PadTriggerModeTests, RandomRoundRobinNeverChoosesZeroWeightLayers)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    ASSERT_TRUE(harness.pads().loadSample(
        0, harness.createTemporarySampleFile("weighted_first", 4410)));
    ASSERT_TRUE(harness.pads().addSampleLayer(
        0, harness.createTemporarySampleFile("weighted_second", 4410)));
    ASSERT_TRUE(harness.pads().addSampleLayer(
        0, harness.createTemporarySampleFile("weighted_third", 4410)));
    ASSERT_TRUE(harness.pads().setSampleLayerRandomWeight(0, 0, 1.0f));
    ASSERT_TRUE(harness.pads().setSampleLayerRandomWeight(0, 1, 0.0f));
    ASSERT_TRUE(harness.pads().setSampleLayerRandomWeight(0, 2, 0.0f));
    harness.pads().setTriggerMode(
        0, SamplerInstrument::TriggerMode::RoundRobinRandom);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->roundRobinMidi, nullptr);

    te::MidiMessageArray midi;
    for (int i = 0; i < 16; ++i)
        midi.addMidiMessage(juce::MidiMessage::noteOn(
            1, pad->midiNote, static_cast<juce::uint8>(100)), {});
    pad->roundRobinMidi->remapMidiMessages(midi);

    for (const auto& message : midi)
        EXPECT_EQ(message.getNoteNumber(), pad->midiNote);
}

TEST(PadTriggerModeTests, LayerMixControlsGainWeightAndVelocityCurve)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    ASSERT_TRUE(harness.pads().loadSample(
        0, harness.createTemporarySampleFile("mix_first", 4410)));
    ASSERT_TRUE(harness.pads().addSampleLayer(
        0, harness.createTemporarySampleFile("mix_second", 4410)));
    ASSERT_TRUE(harness.pads().setSampleLayerGainDb(0, 0, -6.0f));
    ASSERT_TRUE(harness.pads().setSampleLayerRandomWeight(0, 0, 0.25f));
    ASSERT_TRUE(harness.pads().setSampleLayerVelocityCurve(
        0, 0, SamplerInstrument::LayerVelocityCurve::Fixed));
    harness.pads().setTriggerMode(
        0, SamplerInstrument::TriggerMode::RoundRobinOrdered);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_NE(pad->roundRobinMidi, nullptr);
    EXPECT_NEAR(pad->sampler->getSoundGainDb(0), -6.0f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[0].randomWeight, 0.25f, 0.001f);

    te::MidiMessageArray midi;
    midi.addMidiMessage(juce::MidiMessage::noteOn(1, pad->midiNote,
                                                  static_cast<juce::uint8>(20)), {});
    pad->roundRobinMidi->remapMidiMessages(midi);
    ASSERT_EQ(midi.size(), 1);
    EXPECT_EQ(midi[0].getVelocity(), 127);
}

TEST(PadTriggerModeTests, SelfAndAllEightMutualChokeGroupsAreRetained)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    harness.pads().setChokeGroupDirect(0, -1);
    harness.pads().setChokeGroupDirect(1, 8);

    EXPECT_EQ(harness.pads().getChokeGroup(0), -1);
    EXPECT_EQ(harness.pads().getChokeGroup(1), 8);
}
