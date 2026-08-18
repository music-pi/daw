#include <gtest/gtest.h>

#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/T9Dictionary.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "harness/EngineHarness.h"

TEST(AudioEngineTests, CreateEmptyEditInitialisesPads)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    EXPECT_EQ(pads.getPadCount(), 16);
}

TEST(AudioEngineUndoTests, UndoPadChokeGroupChange)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto& engine = harness.audio();

    const int padId = pads.getPadCount() > 0 ? pads.getPadsSnapshot().front().id : -1;
    ASSERT_GE(padId, 0);

    const int originalChoke = pads.getChokeGroup(padId);
    const int newChoke = (originalChoke + 2) % 7;

    pads.setChokeGroup(padId, newChoke);
    EXPECT_EQ(pads.getChokeGroup(padId), newChoke);

    EXPECT_TRUE(engine.undo());
    EXPECT_EQ(pads.getChokeGroup(padId), originalChoke);
}

// TODO: Rewrite audio trigger tests using TE infrastructure

TEST(AudioEngineTests, T9DictionaryIsConstructedAndExposed)
{
    testharness::EngineHarness harness;
    auto& dict = harness.audio().getT9Dictionary();
    // Record + retrieve to prove it's live
    dict.record("test-scope", "hello");
    auto hits = dict.completionsFor("test-scope", "he", 5);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], "hello");
}

