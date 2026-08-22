#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/GroupManager.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

TEST(GroupBankTests, NewGroupOwnsIndependentPadAndPatternState)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& engine = harness.audio();
    auto& groups = engine.getGroupManager();
    auto sampleA = harness.createTemporarySampleFile("group_a", 4410);
    auto sampleB = harness.createTemporarySampleFile("group_b", 4410);

    auto* groupA = engine.getSamplerForGroup(0);
    ASSERT_NE(groupA, nullptr);
    ASSERT_TRUE(groupA->loadSample(0, sampleA));
    groupA->setGainDb(0, -6.0f);
    ASSERT_TRUE(groupA->setStep(0, 0, 0, true));

    groups.createGroup(1);
    auto* groupB = engine.getSamplerForGroup(1);
    ASSERT_NE(groupB, nullptr);
    EXPECT_EQ(&engine.getSampler(), groupB);
    EXPECT_FALSE(groupB->getPad(0)->hasSample);
    ASSERT_TRUE(groupB->loadSample(0, sampleB));
    groupB->setGainDb(0, -3.0f);
    ASSERT_TRUE(groupB->setStep(0, 0, 4, true));

    ASSERT_TRUE(groups.recallGroup(0));
    EXPECT_EQ(&engine.getSampler(), groupA);
    EXPECT_EQ(groupA->getPad(0)->sampleFile, sampleA);
    EXPECT_NEAR(groupA->getGainDb(0), -6.0f, 0.01f);

    const auto groupAPatterns = groupA->getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    const auto groupBPatterns = groupB->getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    ASSERT_FALSE(groupAPatterns.empty());
    ASSERT_FALSE(groupBPatterns.empty());
    EXPECT_TRUE(groupAPatterns[0].patterns[0].steps[0]);
    EXPECT_FALSE(groupAPatterns[0].patterns[0].steps[4]);
    EXPECT_FALSE(groupBPatterns[0].patterns[0].steps[0]);
    EXPECT_TRUE(groupBPatterns[0].patterns[0].steps[4]);
}

TEST(GroupBankTests, SelectingGroupDoesNotRewritePadStateOrUndoHistory)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& engine = harness.audio();
    auto& groups = engine.getGroupManager();
    auto* groupA = engine.getSamplerForGroup(0);
    ASSERT_NE(groupA, nullptr);
    groupA->setChokeGroupDirect(0, 3);

    groups.createGroup(1);
    auto* groupB = engine.getSamplerForGroup(1);
    ASSERT_NE(groupB, nullptr);
    groupB->setChokeGroupDirect(0, 5);

    auto& undo = engine.getUndoManager();
    undo.clearUndoHistory();

    ASSERT_TRUE(groups.recallGroup(0));
    EXPECT_EQ(groupA->getChokeGroup(0), 3);
    EXPECT_EQ(groupB->getChokeGroup(0), 5);
    EXPECT_FALSE(undo.canUndo());
}
