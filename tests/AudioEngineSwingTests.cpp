#include <gtest/gtest.h>
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace
{
te::MidiClip* findFirstPadMidiClip (SamplerInstrument& pads, int padIndex)
{
    auto* track = pads.getTrack (padIndex);
    if (track == nullptr)
        return nullptr;
    for (auto* c : track->getClips())
        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
            return mc;
    return nullptr;
}
}

TEST(AudioEngineSwingTests, BuiltinSwingTemplateIsActive)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto* gt = harness.audio().getEngine().getGrooveTemplateManager()
                   .getTemplateByName("Basic 16th Swing");
    EXPECT_NE(gt, nullptr);
}

TEST(AudioEngineSwingTests, DefaultSwingPercentIs50)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto snapshot = harness.audio().getTransportSnapshot();
    EXPECT_DOUBLE_EQ(snapshot.swingPercent, 50.0);
}

TEST(AudioEngineSwingTests, SetSwingPercentUpdatesSnapshot)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    harness.audio().setSwingPercent(60.0);
    auto snap = harness.audio().getTransportSnapshot();
    EXPECT_DOUBLE_EQ(snap.swingPercent, 60.0);

    EXPECT_DOUBLE_EQ(harness.audio().getSwingPercent(), 60.0);
}

TEST(AudioEngineSwingTests, SetSwingPercentClampsRange)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    harness.audio().setSwingPercent(100.0);
    EXPECT_DOUBLE_EQ(harness.audio().getSwingPercent(), 75.0);

    harness.audio().setSwingPercent(0.0);
    EXPECT_DOUBLE_EQ(harness.audio().getSwingPercent(), 50.0);
}

// Post-MidiClip-unification: groove template lives on each pad's MidiClip,
// not on shared StepClip channels.
TEST(AudioEngineSwingTests, PadMidiClipsGetBuiltinSwingTemplate)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.audio().getSampler();
    for (int i = 0; i < pads.getPadCount(); ++i)
    {
        auto* mc = findFirstPadMidiClip(pads, i);
        ASSERT_NE(mc, nullptr) << "pad " << i << " has no MidiClip";
        EXPECT_EQ(mc->getGrooveTemplate().toStdString(),
                  std::string("Basic 16th Swing"))
            << "pad " << i;
    }
}

TEST(AudioEngineSwingTests, SetSwingPercentUpdatesClipGrooveStrength)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.audio().getSampler();
    auto* mc0 = findFirstPadMidiClip(pads, 0);
    ASSERT_NE(mc0, nullptr);

    harness.audio().setSwingPercent(75.0);
    EXPECT_FLOAT_EQ(mc0->getGrooveStrength(), 1.0f);

    harness.audio().setSwingPercent(50.0);
    EXPECT_FLOAT_EQ(mc0->getGrooveStrength(), 0.0f);

    harness.audio().setSwingPercent(62.5);
    EXPECT_FLOAT_EQ(mc0->getGrooveStrength(), 0.5f);
}

TEST(AudioEngineSwingTests, SwingPersistsAcrossProjectReload)
{
    const auto tmp = juce::File::createTempFile(".mpi");
    tmp.deleteFile();

    {
        testharness::EngineHarness harness;
        harness.createEmptyEdit();
        harness.audio().setSwingPercent(62.0);
        ASSERT_TRUE(harness.audio().saveProjectToFile(tmp));
    }

    {
        testharness::EngineHarness harness;
        ASSERT_TRUE(harness.audio().loadProjectFromFile(tmp));
        EXPECT_DOUBLE_EQ(harness.audio().getSwingPercent(), 62.0);

        auto& pads = harness.audio().getSampler();
        auto* mc0 = findFirstPadMidiClip(pads, 0);
        ASSERT_NE(mc0, nullptr);
        EXPECT_NEAR(mc0->getGrooveStrength(),
                    static_cast<float>((62.0 - 50.0) / 25.0), 1e-5);
    }

    tmp.deleteFile();
}
