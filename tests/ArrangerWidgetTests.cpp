#include <algorithm>

#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/input/InputManager.h"
#include "../src/ui/widget/ArrangerWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

namespace te = tracktion::engine;

class ArrangerWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        // Wire an InputManager so Modal-priority button handlers (4D tilts,
        // duplicateDouble) registered in onActivated can actually dispatch.
        mockController.setInputManager(&inputManager);
        wm.setAudioEngine(&harness.audio());
        wm.setControllerHost(&mockController);
    }

    // Open a fresh ArrangerWidget on the left panel. Returns the raw ptr
    // owned by the WindowManager.
    ArrangerWidget* openWidget()
    {
        auto widget = std::make_unique<ArrangerWidget>();
        wm.open(std::move(widget), DisplaySide::Left);
        return static_cast<ArrangerWidget*>(wm.getWidget(DisplaySide::Left));
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    InputManager inputManager;
    testharness::MockControllerHost mockController;
    WindowManager wm;
};

// ── Descriptor / resources ──────────────────────────────────────────────

TEST_F(ArrangerWidgetTests, DescribesAsLeftOnlySinglePage)
{
    ArrangerWidget widget;
    auto desc = widget.describe();

    EXPECT_EQ(desc.id, "arranger");
    EXPECT_EQ(desc.pageCount, 1);
    EXPECT_FALSE(desc.forceOnTop);
    EXPECT_EQ(desc.display, DisplayConstraint::LeftOnly);
}

TEST_F(ArrangerWidgetTests, ResourceDeclarationLeftPanel)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto resources = raw->requiredResources(0);

    for (int i = 1; i <= 4; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)),
                  resources.end()) << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)),
                  resources.end()) << "Missing k" << i;
    }
    for (int i = 1; i <= 16; ++i)
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)),
                  resources.end()) << "Should not have p" << i;
}

// ── Grid edits ──────────────────────────────────────────────────────────

TEST_F(ArrangerWidgetTests, InsertBlockAtCursorLaneAndBar)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    raw->setCursorForTest(0, 3);
    raw->insertBlockAtCursor();

    const auto& lanes = harness.pads().getSongLanes();
    ASSERT_FALSE(lanes.empty());
    ASSERT_EQ(lanes[0].blocks.size(), 1u);
    EXPECT_EQ(lanes[0].blocks[0].startBar, 3);
    EXPECT_EQ(lanes[0].blocks[0].bars, 1);
    EXPECT_EQ(lanes[0].blocks[0].patternIndex, 0);
}

TEST_F(ArrangerWidgetTests, InsertOnOccupiedSlotWarns)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    raw->setCursorForTest(0, 0);
    raw->insertBlockAtCursor();
    // Second invocation at the same cell — overlap, should not create a block.
    raw->insertBlockAtCursor();

    const auto& blocks = harness.pads().getSongLanes()[0].blocks;
    EXPECT_EQ(blocks.size(), 1u);
}

TEST_F(ArrangerWidgetTests, DeleteRemovesBlockAtCursor)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    raw->setCursorForTest(0, 0);
    raw->insertBlockAtCursor();
    ASSERT_EQ(harness.pads().getSongLanes()[0].blocks.size(), 1u);

    raw->deleteBlockAtCursor();
    EXPECT_TRUE(harness.pads().getSongLanes()[0].blocks.empty());
}

TEST_F(ArrangerWidgetTests, DeleteOnEmptyCellIsNoOp)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    raw->setCursorForTest(0, 0);
    // No block under the cursor — delete should be a silent no-op.
    raw->deleteBlockAtCursor();
    EXPECT_TRUE(harness.pads().getSongLanes()[0].blocks.empty());
}

TEST_F(ArrangerWidgetTests, UniqueClonesBlockPattern)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto& sampler = harness.pads();
    // Seed lane 0 with: pattern 0 (bar 0), pattern 1 (bar 1), pattern 0 (bar 2).
    sampler.createPattern();            // creates pattern index 1
    ASSERT_GE(sampler.getNumPatterns(), 2);

    sampler.insertBlock(0, 0, 0, 1);
    sampler.insertBlock(0, 1, 1, 1);
    sampler.insertBlock(0, 2, 0, 1);

    const int patternsBefore = sampler.getNumPatterns();

    raw->setCursorForTest(0, 2); // third block references pattern 0
    raw->uniqueBlockAtCursor();

    EXPECT_EQ(sampler.getNumPatterns(), patternsBefore + 1);
    const auto& blocks = sampler.getSongLanes()[0].blocks;
    ASSERT_EQ(blocks.size(), 3u);
    // The block under the cursor now references the freshly cloned pattern.
    EXPECT_NE(blocks[2].patternIndex, blocks[0].patternIndex);
}

TEST_F(ArrangerWidgetTests, ModeOptionTogglesPlayMode)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Pattern);
    ASSERT_EQ(harness.audio().getPlayMode(), AudioEngine::PlayMode::Pattern);

    raw->togglePlayMode();
    EXPECT_EQ(harness.audio().getPlayMode(), AudioEngine::PlayMode::Track);

    raw->togglePlayMode();
    EXPECT_EQ(harness.audio().getPlayMode(), AudioEngine::PlayMode::Pattern);
}

// ── Shift layout ────────────────────────────────────────────────────────

TEST_F(ArrangerWidgetTests, ShiftSwapsOptionsToLaneManagement)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto shifted = raw->getOptionsForShiftState(true);
    ASSERT_EQ(shifted.size(), 4u);
    EXPECT_EQ(shifted[0].id, "arranger.rename");
    EXPECT_EQ(shifted[1].id, "arranger.addLane");
    EXPECT_EQ(shifted[2].id, "arranger.removeLane");
    EXPECT_EQ(shifted[3].id, "arranger.mode");

    auto unshifted = raw->getOptionsForShiftState(false);
    ASSERT_EQ(unshifted.size(), 4u);
    EXPECT_EQ(unshifted[0].id, "arranger.insert");
    EXPECT_EQ(unshifted[1].id, "arranger.delete");
    // d3 is now an explicit empty slot — Unique moved onto the hardware
    // duplicateDouble button (unshifted) and shift+duplicate = double.
    EXPECT_EQ(unshifted[2].id, "arranger.empty");
    EXPECT_EQ(unshifted[2].state, OptionState::Empty);
    EXPECT_EQ(unshifted[3].id, "arranger.mode");
}

TEST_F(ArrangerWidgetTests, AddLaneInsertsAfterCursorLane)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    const int before = harness.pads().getNumSongLanes();
    raw->addLaneAfterCursor();
    EXPECT_EQ(harness.pads().getNumSongLanes(), before + 1);
}

TEST_F(ArrangerWidgetTests, RemoveLaneRefusesWhenOnlyOne)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    // Drain down to the last lane to exercise the "refuse last" guard.
    while (harness.pads().getNumSongLanes() > 1)
        harness.pads().removeLane(harness.pads().getNumSongLanes() - 1);
    ASSERT_EQ(harness.pads().getNumSongLanes(), 1);
    raw->removeCursorLane();
    EXPECT_EQ(harness.pads().getNumSongLanes(), 1);
}

TEST_F(ArrangerWidgetTests, RemoveLaneWorksWhenMultiple)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    const int before = harness.pads().getNumSongLanes();
    ASSERT_GE(before, 2);

    raw->setCursorForTest(0, 0);
    raw->removeCursorLane();
    EXPECT_EQ(harness.pads().getNumSongLanes(), before - 1);
}

// ── Cursor navigation ───────────────────────────────────────────────────

TEST_F(ArrangerWidgetTests, CursorLaneNavigatesViaK4)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    // Fresh songs already have multiple default lanes — plenty of room.
    ASSERT_GE(harness.pads().getNumSongLanes(), 2);

    auto knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    auto* laneModel = std::get_if<Knob::NumericModel>(&knobs[3].model);
    ASSERT_NE(laneModel, nullptr);
    ASSERT_NE(laneModel->onChange, nullptr);

    // Drive the knob's onChange directly — the UI pipeline would snap values
    // after accumulating raw encoder deltas; we shortcut that here.
    laneModel->onChange(1.0);
    EXPECT_EQ(raw->getCursorLane(), 1);

    laneModel->onChange(0.0);
    EXPECT_EQ(raw->getCursorLane(), 0);
}

TEST_F(ArrangerWidgetTests, K3ShiftsBlockStartBar)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto& sampler = harness.pads();
    sampler.insertBlock(0, 0, 0, 1);

    raw->setCursorForTest(0, 0);
    auto knobs = raw->getKnobs(0);
    auto* startModel = std::get_if<Knob::NumericModel>(&knobs[2].model);
    ASSERT_NE(startModel, nullptr);
    ASSERT_NE(startModel->onChange, nullptr);

    startModel->onChange(5.0);
    EXPECT_EQ(sampler.getSongLanes()[0].blocks[0].startBar, 5);
}

// ── Playhead ────────────────────────────────────────────────────────────

TEST_F(ArrangerWidgetTests, PlayheadIsHiddenInPatternMode)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Pattern);
    raw->onUiHostTick();
    EXPECT_LT(raw->getPlayheadBar(), 0);
}

TEST_F(ArrangerWidgetTests, PlayheadUpdatesInTrackModeDuringPlay)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto& sampler = harness.pads();
    sampler.insertBlock(0, 0, 0, 4);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    harness.audio().play();

    // Advance the transport a few hundred ms — at a default BPM this should
    // cross into bar 0 proper, and playheadBar_ should clamp to >= 0.
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto& transport = edit->getTransport();
    transport.setPosition(tracktion::core::TimePosition::fromSeconds(0.5));

    raw->onUiHostTick();
    EXPECT_GE(raw->getPlayheadBar(), 0);

    harness.audio().stop();
}

// ── Lifecycle ───────────────────────────────────────────────────────────

// ── duplicateDouble button (unique / double) ───────────────────────────

TEST_F(ArrangerWidgetTests, DuplicateButtonUniquesBlock)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto& sampler = harness.pads();
    sampler.insertBlock(0, 0, 0, 1);
    const int patternsBefore = sampler.getNumPatterns();
    const int originalPattern = sampler.getSongLanes()[0].blocks[0].patternIndex;

    raw->setCursorForTest(0, 0);
    mockController.triggerButton("duplicateDouble", /*pressed*/ true, /*shift*/ false);

    EXPECT_EQ(sampler.getNumPatterns(), patternsBefore + 1);
    const auto& blocks = sampler.getSongLanes()[0].blocks;
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_NE(blocks[0].patternIndex, originalPattern);
}

TEST_F(ArrangerWidgetTests, ShiftDuplicateDoublesBlock)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto& sampler = harness.pads();
    sampler.insertBlock(0, 0, 0, 2);
    const int patternsBefore = sampler.getNumPatterns();

    raw->setCursorForTest(0, 0);
    mockController.triggerButton("duplicateDouble", /*pressed*/ true, /*shift*/ true);

    // Clone + insert a second block after the source.
    const auto& blocks = sampler.getSongLanes()[0].blocks;
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[1].startBar, 2);
    EXPECT_EQ(blocks[1].bars, 2);
    EXPECT_NE(blocks[1].patternIndex, blocks[0].patternIndex);
    EXPECT_EQ(sampler.getNumPatterns(), patternsBefore + 1);
}

TEST_F(ArrangerWidgetTests, DuplicateButtonOnEmptyCursorIsNoOp)
{
    auto* raw = openWidget();
    ASSERT_NE(raw, nullptr);

    auto& sampler = harness.pads();
    const int patternsBefore = sampler.getNumPatterns();

    // Cursor sits over an empty cell — button must not clone.
    raw->setCursorForTest(0, 0);
    mockController.triggerButton("duplicateDouble", /*pressed*/ true, /*shift*/ false);

    EXPECT_EQ(sampler.getNumPatterns(), patternsBefore);
    EXPECT_TRUE(sampler.getSongLanes()[0].blocks.empty());
}

TEST_F(ArrangerWidgetTests, LifecycleActivateDeactivate)
{
    openWidget();

    auto& hw = wm.getHardwareState();
    EXPECT_TRUE(hw.isClaimed("d1"));
    EXPECT_TRUE(hw.isClaimed("k1"));
    EXPECT_EQ(hw.getOwner("d1"), "arranger");
    EXPECT_FALSE(hw.isClaimed("p1"));

    wm.close("arranger");
    EXPECT_FALSE(hw.isClaimed("d1"));
    EXPECT_FALSE(hw.isClaimed("k1"));
}
