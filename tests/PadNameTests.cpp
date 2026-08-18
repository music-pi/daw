#include <gtest/gtest.h>

#include <juce_core/juce_core.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

TEST(PadNameTests, DefaultsToPadN)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    EXPECT_EQ(pads.getPadName(0), "Pad 1");
}

TEST(PadNameTests, SetAndGet)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.setPadName(0, "Kick");
    EXPECT_EQ(pads.getPadName(0), "Kick");
    pads.setPadName(0, "");
    EXPECT_EQ(pads.getPadName(0), "Pad 1");
}

TEST(PadNameTests, RoundTripsThroughSave)
{
    auto dir = juce::File::createTempFile("");
    dir.deleteFile();
    dir.createDirectory();
    auto proj = dir.getChildFile("names.mpi");
    {
        testharness::EngineHarness h;
        h.createEmptyEdit();
        h.pads().setPadName(0, "Kick");
        h.pads().setPadName(4, "Snare");
        ASSERT_TRUE(h.audio().saveProjectToFile(proj));
    }
    testharness::EngineHarness h2;
    ASSERT_TRUE(h2.audio().loadProjectFromFile(proj));
    EXPECT_EQ(h2.pads().getPadName(0), "Kick");
    EXPECT_EQ(h2.pads().getPadName(4), "Snare");
    dir.deleteRecursively();
}
