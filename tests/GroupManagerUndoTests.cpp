#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/GroupManager.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

TEST(GroupTransportTests, EveryGroupPatternUsesTheSameGlobalTimeline)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& engine = harness.audio();
    auto& groups = engine.getGroupManager();
    auto* groupA = engine.getSamplerForGroup(0);
    ASSERT_NE(groupA, nullptr);
    ASSERT_TRUE(groupA->setStep(0, 0, 0, true));

    groups.createGroup(1);
    auto* groupB = engine.getSamplerForGroup(1);
    ASSERT_NE(groupB, nullptr);
    ASSERT_TRUE(groupB->setStep(0, 0, 8, true));

    const auto* padA = groupA->getPad(0);
    const auto* padB = groupB->getPad(0);
    ASSERT_NE(padA, nullptr);
    ASSERT_NE(padB, nullptr);
    ASSERT_NE(padA->patternClip, nullptr);
    ASSERT_NE(padB->patternClip, nullptr);

    // Both clips are live in the same Edit at the same transport origin.
    EXPECT_EQ(padA->track->edit.getProjectItemID(),
              padB->track->edit.getProjectItemID());
    EXPECT_EQ(padA->patternClip->getPosition().getStart(),
              padB->patternClip->getPosition().getStart());
    EXPECT_EQ(padA->patternClip->getPosition().getEnd(),
              padB->patternClip->getPosition().getEnd());

    engine.play();
    ASSERT_TRUE(engine.isPlaying());
    ASSERT_TRUE(groups.recallGroup(0));
    EXPECT_TRUE(engine.isPlaying());
    engine.stop();
}

TEST(GroupTransportTests, DeletingOneGroupLeavesOtherBankAndTransportIntact)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& engine = harness.audio();
    auto& groups = engine.getGroupManager();
    auto sample = harness.createTemporarySampleFile("surviving_group", 4410);
    ASSERT_TRUE(engine.getSamplerForGroup(0)->loadSample(0, sample));

    groups.createGroup(1);
    ASSERT_NE(engine.getSamplerForGroup(1), nullptr);
    groups.clearGroup(1);

    EXPECT_EQ(engine.getSamplerForGroup(1), nullptr);
    ASSERT_NE(engine.getSamplerForGroup(0), nullptr);
    EXPECT_EQ(engine.getSamplerForGroup(0)->getPad(0)->sampleFile, sample);
    EXPECT_TRUE(groups.isGroupActive(0));
}
