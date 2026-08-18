#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/GroupManager.h"

#include "harness/EngineHarness.h"

// ============================================================================
// recallGroup Atomic Undo Tests
// ============================================================================

TEST(RecallGroupTests, RecallUsesAtomicUndoTransaction)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& engine = harness.audio();
    auto& pads = harness.pads();
    auto& groups = engine.getGroupManager();
    auto& undoManager = engine.getUndoManager();

    // Load samples and set choke groups on two pads
    auto sample1 = harness.createTemporarySampleFile("recall_s1", 4410);
    auto sample2 = harness.createTemporarySampleFile("recall_s2", 4410);
    pads.loadSample(0, sample1);
    pads.loadSample(1, sample2);
    pads.setChokeGroupDirect(0, 1);
    pads.setChokeGroupDirect(1, 2);

    // Save current state as group 0
    groups.saveCurrentState(0);

    // Clear undo history so we start fresh
    undoManager.clearUndoHistory();

    // Switch to group 1 (empty), save it
    for (int i = 0; i < pads.getPadCount(); ++i)
    {
        pads.clearSample(i);
        pads.setChokeGroupDirect(i, 0);
    }
    groups.saveCurrentState(1);
    undoManager.clearUndoHistory();

    // Now recall group 0 — should be a single undo transaction
    EXPECT_TRUE(groups.recallGroup(0));

    // Verify choke groups were restored
    EXPECT_EQ(pads.getChokeGroup(0), 1);
    EXPECT_EQ(pads.getChokeGroup(1), 2);

    // Verify the undo description identifies the recall (group number is
    // 1-based in the label — group index 0 → "Recall Group 1")
    EXPECT_TRUE(undoManager.canUndo());
    auto undoDesc = undoManager.getUndoDescription();
    EXPECT_EQ(undoDesc, juce::String("Recall Group 1"));
}

TEST(RecallGroupTests, RecallDoesNotCreateMultipleChokeGroupTransactions)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& engine = harness.audio();
    auto& pads = harness.pads();
    auto& groups = engine.getGroupManager();
    auto& undoManager = engine.getUndoManager();

    // Set up pads with different choke groups
    auto sample = harness.createTemporarySampleFile("recall_choke", 4410);
    for (int i = 0; i < 4; ++i)
    {
        pads.loadSample(i, sample);
        pads.setChokeGroupDirect(i, i + 1);
    }
    groups.saveCurrentState(0);

    // Clear and save as group 1
    for (int i = 0; i < pads.getPadCount(); ++i)
    {
        pads.clearSample(i);
        pads.setChokeGroupDirect(i, 0);
    }
    groups.saveCurrentState(1);
    undoManager.clearUndoHistory();

    // Recall group 0 — should NOT create 4 individual "Set Choke Group" transactions
    groups.recallGroup(0);

    // There should be exactly one undo transaction, not multiple
    EXPECT_TRUE(undoManager.canUndo());
    undoManager.undo();
    // After single undo, there should be nothing left to undo
    EXPECT_FALSE(undoManager.canUndo());
}
