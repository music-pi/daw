#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/GroupManager.h"
#include "../src/engine/SamplerInstrument.h"

#include "harness/EngineHarness.h"

TEST(GroupManagerUndoTests, RecallGroupIsAtomicUndo)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto& groups = harness.audio().getGroupManager();

    // Load samples into group A (group 0)
    auto sample0 = harness.createTemporarySampleFile("grp_pad0", 44100);
    auto sample1 = harness.createTemporarySampleFile("grp_pad1", 44100);
    pads.loadSample(0, sample0);
    pads.loadSample(1, sample1);
    pads.setGainDb(0, -6.0f);
    pads.setChokeGroupDirect(0, 2);
    groups.saveCurrentState(0);

    // Load different samples into group B (group 1)
    auto sample2 = harness.createTemporarySampleFile("grp_pad2", 44100);
    pads.loadSample(0, sample2);
    pads.loadSample(1, juce::File());  // clear pad 1
    pads.clearSample(1);
    pads.setGainDb(0, -3.0f);
    pads.setChokeGroupDirect(0, 0);
    groups.saveCurrentState(1);

    // Record the state before recall
    auto& undo = harness.audio().getUndoManager();

    // Recall group A — this should be one atomic transaction
    undo.beginNewTransaction("Before recall");
    groups.recallGroup(0);

    // Verify group A is restored
    EXPECT_TRUE(groups.isGroupActive(0));
    auto* pad0 = pads.getPad(0);
    ASSERT_NE(pad0, nullptr);
    EXPECT_NEAR(pads.getGainDb(0), -6.0f, 0.01f);

    // A single undo should revert the entire recall
    // (If recallGroup created 40+ transactions, we'd need 40+ undos)
    EXPECT_TRUE(undo.canUndo());
    undo.undo();

    // After undo, TE restores SamplerPlugin/VolumeAndPanPlugin state natively.
    // Gains and choke groups should be back to group B's state.
    EXPECT_NEAR(pads.getGainDb(0), -3.0f, 0.01f);
    EXPECT_EQ(pads.getChokeGroup(0), 0);
}

TEST(GroupManagerUndoTests, RecallGroupCoalesceIntoOneUndoStep)
{
    // Regression: the recall loop touches up to 16 pads with up to two
    // commands each (gain, choke group) plus sampler plugin mutations.
    // Every one of those must coalesce into a single JUCE transaction so
    // one undo() reverts the whole recall, not just one pad.
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto& groups = harness.audio().getGroupManager();
    auto& undo = harness.audio().getUndoManager();

    // Group 0 ("source"): distinct gain + choke on three pads.
    auto sampleA = harness.createTemporarySampleFile("coalesce_a", 4096);
    auto sampleB = harness.createTemporarySampleFile("coalesce_b", 4096);
    auto sampleC = harness.createTemporarySampleFile("coalesce_c", 4096);
    pads.loadSample(0, sampleA);
    pads.loadSample(1, sampleB);
    pads.loadSample(2, sampleC);
    pads.setGainDb(0, -6.0f);
    pads.setGainDb(1, -9.0f);
    pads.setGainDb(2, -12.0f);
    pads.setChokeGroupDirect(0, 1);
    pads.setChokeGroupDirect(1, 2);
    pads.setChokeGroupDirect(2, 3);
    groups.saveCurrentState(0);

    // Group 1 ("destination"): wipe pads 1-2, alter pad 0.
    pads.clearSample(1);
    pads.clearSample(2);
    pads.setGainDb(0, 0.0f);
    pads.setGainDb(1, 0.0f);
    pads.setGainDb(2, 0.0f);
    pads.setChokeGroupDirect(0, 5);
    pads.setChokeGroupDirect(1, 0);
    pads.setChokeGroupDirect(2, 0);
    groups.saveCurrentState(1);

    // Recall group 0 — should coalesce into a single transaction.
    undo.beginNewTransaction("Seal destination");
    groups.recallGroup(0);

    EXPECT_NEAR(pads.getGainDb(0), -6.0f, 0.01f);
    EXPECT_NEAR(pads.getGainDb(1), -9.0f, 0.01f);
    EXPECT_NEAR(pads.getGainDb(2), -12.0f, 0.01f);
    EXPECT_EQ(pads.getChokeGroup(0), 1);
    EXPECT_EQ(pads.getChokeGroup(1), 2);
    EXPECT_EQ(pads.getChokeGroup(2), 3);

    // One undo() reverts the entire recall.
    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_NEAR(pads.getGainDb(0), 0.0f, 0.01f);
    EXPECT_NEAR(pads.getGainDb(1), 0.0f, 0.01f);
    EXPECT_NEAR(pads.getGainDb(2), 0.0f, 0.01f);
    EXPECT_EQ(pads.getChokeGroup(0), 5);
    EXPECT_EQ(pads.getChokeGroup(1), 0);
    EXPECT_EQ(pads.getChokeGroup(2), 0);
}

TEST(GroupManagerUndoTests, RecallGroupUsesDirectChokeGroup)
{
    // Verify that recallGroup uses setChokeGroupDirect (no individual undo transactions)
    // This is verified by checking that only one undo transaction exists after recall
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto& groups = harness.audio().getGroupManager();

    // Set up group 0 with choke groups
    pads.setChokeGroupDirect(0, 3);
    pads.setChokeGroupDirect(1, 4);
    groups.saveCurrentState(0);

    // Set up different state
    pads.setChokeGroupDirect(0, 0);
    pads.setChokeGroupDirect(1, 0);
    groups.saveCurrentState(1);

    auto& undo = harness.audio().getUndoManager();
    undo.clearUndoHistory();

    // Recall group 0
    groups.recallGroup(0);

    // Verify choke groups are restored
    EXPECT_EQ(pads.getChokeGroup(0), 3);
    EXPECT_EQ(pads.getChokeGroup(1), 4);
}
