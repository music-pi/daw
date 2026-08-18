#include <gtest/gtest.h>
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

// ──────────────────────────────────────────────────────────────────────────
// Multi-lane song: base invariants + block mutators.
// ──────────────────────────────────────────────────────────────────────────

TEST(SongTests, FreshSongHasFiveLanes)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    EXPECT_EQ(pads.getNumSongLanes(), 5);
    ASSERT_EQ(pads.getSongLanes().size(), 5u);
    for (const auto& lane : pads.getSongLanes())
        EXPECT_TRUE(lane.blocks.empty());
    EXPECT_EQ(pads.getSongTotalBars(), 0);
}

TEST(SongTests, InsertBlockAppendsToLane)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    EXPECT_EQ(pads.insertBlock(0, 0, 0, 1), 0);
    EXPECT_EQ(pads.insertBlock(0, 1, 0, 2), 1);
    EXPECT_EQ(pads.insertBlock(0, 3, 0, 1), 2);
    EXPECT_EQ(pads.getSongLanes()[0].blocks.size(), 3u);
}

TEST(SongTests, InsertBlockSortsByStartBar)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.createPattern();

    pads.insertBlock(0, 8, 1, 2);
    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(0, 4, 2, 2);

    const auto& blocks = pads.getSongLanes()[0].blocks;
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].startBar, 0);
    EXPECT_EQ(blocks[1].startBar, 4);
    EXPECT_EQ(blocks[2].startBar, 8);
}

TEST(SongTests, InsertBlockRejectsOverlap)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    EXPECT_EQ(pads.insertBlock(0, 0, 0, 2), 0);
    // [1..4) overlaps [0..2), must reject.
    EXPECT_EQ(pads.insertBlock(0, 1, 0, 3), -1);
    // Touching at the edge is fine — [2..4) sits right after [0..2).
    EXPECT_GE(pads.insertBlock(0, 2, 0, 2), 0);
    EXPECT_EQ(pads.getSongLanes()[0].blocks.size(), 2u);
}

TEST(SongTests, InsertBlockRejectsInvalidLane)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    EXPECT_EQ(harness.pads().insertBlock(5, 0, 0, 1), -1);
    EXPECT_EQ(harness.pads().insertBlock(-1, 0, 0, 1), -1);
}

TEST(SongTests, RemoveBlockRemovesAndReturnsFalseOnInvalid)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.insertBlock(0, 0, 0, 1);
    pads.insertBlock(0, 1, 0, 1);

    EXPECT_TRUE(pads.removeBlock(0, 0));
    EXPECT_EQ(pads.getSongLanes()[0].blocks.size(), 1u);
    EXPECT_FALSE(pads.removeBlock(0, 5));
    EXPECT_FALSE(pads.removeBlock(5, 0));
}

TEST(SongTests, SetBlockPatternUpdatesReference)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.createPattern();

    pads.insertBlock(0, 0, 0, 1);

    EXPECT_TRUE(pads.setBlockPattern(0, 0, 2));
    EXPECT_EQ(pads.getSongLanes()[0].blocks[0].patternIndex, 2);
    EXPECT_FALSE(pads.setBlockPattern(0, 0, 99));
    EXPECT_FALSE(pads.setBlockPattern(0, 5, 0));
}

TEST(SongTests, SetBlockBarsAcceptsFit)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(0, 4, 0, 2);

    // Resize [0..2) → [0..4); still fits because next block starts at 4.
    EXPECT_TRUE(pads.setBlockBars(0, 0, 4));
    EXPECT_EQ(pads.getSongLanes()[0].blocks[0].bars, 4);
}

TEST(SongTests, SetBlockBarsRejectsOverlap)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(0, 3, 0, 2);

    // Growing [0..2) to bars=5 would reach bar 5, overlapping [3..5).
    EXPECT_FALSE(pads.setBlockBars(0, 0, 5));
    // Block 0 unchanged.
    EXPECT_EQ(pads.getSongLanes()[0].blocks[0].bars, 2);
}

TEST(SongTests, SetBlockStartMovesAndReSorts)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(0, 4, 0, 2);

    // Move block 0 from [0..2) to [6..8). Must re-sort so block 0 ends up
    // at lane index 1.
    EXPECT_TRUE(pads.setBlockStart(0, 0, 6));
    const auto& blocks = pads.getSongLanes()[0].blocks;
    EXPECT_EQ(blocks[0].startBar, 4);
    EXPECT_EQ(blocks[1].startBar, 6);
}

TEST(SongTests, SetBlockStartRejectsOverlap)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(0, 4, 0, 2);

    // Moving [0..2) to startBar=3 makes [3..5) overlap [4..6).
    EXPECT_FALSE(pads.setBlockStart(0, 0, 3));
    EXPECT_EQ(pads.getSongLanes()[0].blocks[0].startBar, 0);
}

TEST(SongTests, MoveBlockToLaneSucceedsIfDestFree)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    // Fresh songs already have 5 default lanes — no need to add more here.
    ASSERT_GE(pads.getNumSongLanes(), 2);

    pads.insertBlock(0, 0, 0, 2);
    EXPECT_TRUE(pads.moveBlockToLane(0, 0, 1, 4));

    EXPECT_TRUE(pads.getSongLanes()[0].blocks.empty());
    ASSERT_EQ(pads.getSongLanes()[1].blocks.size(), 1u);
    EXPECT_EQ(pads.getSongLanes()[1].blocks[0].startBar, 4);
}

TEST(SongTests, MoveBlockToLaneRejectsOnOverlap)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.insertLane(1);

    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(1, 0, 0, 2);

    // Destination already occupied at bar 0 for 2 bars.
    EXPECT_FALSE(pads.moveBlockToLane(0, 0, 1, 0));
    EXPECT_EQ(pads.getSongLanes()[0].blocks.size(), 1u);
    EXPECT_EQ(pads.getSongLanes()[1].blocks.size(), 1u);
}

// ──────────────────────────────────────────────────────────────────────────
// Lane mutators.
// ──────────────────────────────────────────────────────────────────────────

TEST(SongTests, InsertLaneCreatesEmpty)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    const int priorCount = pads.getNumSongLanes();
    const int idx = pads.insertLane(1);
    EXPECT_EQ(idx, 1);
    EXPECT_EQ(pads.getNumSongLanes(), priorCount + 1);
    EXPECT_TRUE(pads.getSongLanes()[1].blocks.empty());
}

TEST(SongTests, CantRemoveLastLane)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    // Drain down to the single-lane case to exercise the "refuse last" guard.
    while (pads.getNumSongLanes() > 1)
        ASSERT_TRUE(pads.removeLane(pads.getNumSongLanes() - 1));
    ASSERT_EQ(pads.getNumSongLanes(), 1);
    EXPECT_FALSE(pads.removeLane(0));
    EXPECT_EQ(pads.getNumSongLanes(), 1);
}

TEST(SongTests, InsertRemoveLanePreservesBlocks)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    const int priorCount = pads.getNumSongLanes();
    pads.insertLane(1);
    pads.insertLane(2);
    ASSERT_EQ(pads.getNumSongLanes(), priorCount + 2);

    pads.insertBlock(0, 0, 0, 1);
    pads.insertBlock(2, 0, 0, 4);

    // Remove the empty middle lane.
    EXPECT_TRUE(pads.removeLane(1));
    EXPECT_EQ(pads.getNumSongLanes(), priorCount + 1);
    // Block on original lane 0 still there.
    EXPECT_EQ(pads.getSongLanes()[0].blocks.size(), 1u);
    // Block that was on lane 2 is now on lane 1 (content preserved).
    ASSERT_EQ(pads.getSongLanes()[1].blocks.size(), 1u);
    EXPECT_EQ(pads.getSongLanes()[1].blocks[0].bars, 4);
}

// ──────────────────────────────────────────────────────────────────────────
// Song span + pattern shifting.
// ──────────────────────────────────────────────────────────────────────────

TEST(SongTests, GetSongTotalBarsIsMaxAcrossLanes)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.insertLane(1);

    pads.insertBlock(0, 0, 0, 4);  // lane 0 ends at bar 4
    pads.insertBlock(1, 0, 0, 10); // lane 1 ends at bar 10

    EXPECT_EQ(pads.getSongTotalBars(), 10);
}

TEST(SongTests, InsertPatternShiftsAllLaneBlocks)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();  // pattern 1
    pads.createPattern();  // pattern 2
    pads.insertLane(1);

    // Both lanes reference pattern 2.
    pads.insertBlock(0, 0, 2, 1);
    pads.insertBlock(1, 0, 2, 2);

    pads.insertPattern(1);

    // Both references must have shifted from 2 → 3.
    EXPECT_EQ(pads.getSongLanes()[0].blocks[0].patternIndex, 3);
    EXPECT_EQ(pads.getSongLanes()[1].blocks[0].patternIndex, 3);
}

TEST(SongTests, UniqueSongBlockClonesAndRepoints)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();  // pattern 1
    pads.createPattern();  // pattern 2
    ASSERT_EQ(pads.getNumPatterns(), 3);

    pads.insertBlock(0, 0, 0, 1);
    pads.insertBlock(0, 1, 1, 1);
    pads.insertBlock(0, 2, 2, 1);

    const int newIdx = pads.uniqueSongBlock(0, 1);  // clone pattern 1
    EXPECT_EQ(newIdx, 2);
    EXPECT_EQ(pads.getNumPatterns(), 4);

    const auto& blocks = pads.getSongLanes()[0].blocks;
    EXPECT_EQ(blocks[0].patternIndex, 0);
    EXPECT_EQ(blocks[1].patternIndex, 2);
    // The third block was pointing at pattern 2; insertPattern(1+1)=(2)
    // shifted its ref up to 3.
    EXPECT_EQ(blocks[2].patternIndex, 3);
}

TEST(SongTests, DoubleSongBlockInsertsAfterSource)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.insertBlock(0, 0, 0, 2);
    const int patternsBefore = pads.getNumPatterns();

    const int newIdx = pads.doubleSongBlock(0, 0);
    ASSERT_GE(newIdx, 0);

    const auto& blocks = pads.getSongLanes()[0].blocks;
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].startBar, 0);
    EXPECT_EQ(blocks[0].bars, 2);
    EXPECT_EQ(blocks[1].startBar, 2);
    EXPECT_EQ(blocks[1].bars, 2);
    EXPECT_NE(blocks[1].patternIndex, blocks[0].patternIndex);
    EXPECT_EQ(pads.getNumPatterns(), patternsBefore + 1);
}

TEST(SongTests, DoubleSongBlockRejectsOverlap)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    // Two adjacent blocks — doubling the first would want to insert at bar 2,
    // which is already occupied by the second block.
    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(0, 2, 0, 2);
    const int patternsBefore = pads.getNumPatterns();

    EXPECT_EQ(pads.doubleSongBlock(0, 0), -1);
    // Lane still has exactly 2 blocks (no placement).
    EXPECT_EQ(pads.getSongLanes()[0].blocks.size(), 2u);
    // The clone still happened — pattern count grew even though block didn't
    // land. This matches the engine's "keep the clone" policy on overlap.
    EXPECT_EQ(pads.getNumPatterns(), patternsBefore + 1);
}

TEST(SongTests, TrackModeLoopRangeMatchesSongLength)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.insertBlock(0, 0, 0, 4);
    pads.insertBlock(0, 4, 1, 2);
    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto loopRange = edit->getTransport().getLoopRange();
    auto expectedEnd = edit->tempoSequence.toTime({ 6, {} });
    EXPECT_NEAR(loopRange.getEnd().inSeconds(), expectedEnd.inSeconds(), 0.01);
}

// ──────────────────────────────────────────────────────────────────────────
// Materialization.
// ──────────────────────────────────────────────────────────────────────────

TEST(SongTests, MaterializeSongTimelinePlacesClipsAtBarRanges)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.createPattern();
    pads.insertBlock(0, 0, 0, 1);
    pads.insertBlock(0, 1, 1, 2);
    pads.insertBlock(0, 3, 2, 4);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    harness.audio().play();

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto* track = pads.getTrack(0);
    ASSERT_NE(track, nullptr);

    const auto songEndSec = edit->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ 7, {} }).inSeconds();

    std::vector<te::Clip*> materialized;
    for (auto* c : track->getClips())
        if (c->getPosition().getStart().inSeconds() <= songEndSec + 0.01
            && c->getPosition().getEnd().inSeconds() <= songEndSec + 0.01)
            materialized.push_back(c);

    ASSERT_EQ(materialized.size(), 3u);

    const auto bar = [&](int n)
    {
        return edit->tempoSequence.toTime(
            tracktion::core::tempo::BarsAndBeats{ n, {} }).inSeconds();
    };

    EXPECT_NEAR(materialized[0]->getPosition().getStart().inSeconds(), bar(0), 0.01);
    EXPECT_NEAR(materialized[0]->getPosition().getEnd().inSeconds(),   bar(1), 0.01);
    EXPECT_NEAR(materialized[1]->getPosition().getStart().inSeconds(), bar(1), 0.01);
    EXPECT_NEAR(materialized[1]->getPosition().getEnd().inSeconds(),   bar(3), 0.01);
    EXPECT_NEAR(materialized[2]->getPosition().getStart().inSeconds(), bar(3), 0.01);
    EXPECT_NEAR(materialized[2]->getPosition().getEnd().inSeconds(),   bar(7), 0.01);

    harness.audio().stop();
}

TEST(SongTests, MaterializeTwoLanesPlacesAllBlocks)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.insertLane(1);

    pads.insertBlock(0, 0, 0, 1);  // lane 0, bar 0..1
    pads.insertBlock(1, 2, 1, 2);  // lane 1, bar 2..4

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    harness.audio().play();

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto* track = pads.getTrack(0);
    ASSERT_NE(track, nullptr);

    const auto songEndSec = edit->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ 4, {} }).inSeconds();

    int materializedCount = 0;
    for (auto* c : track->getClips())
        if (c->getPosition().getStart().inSeconds() <= songEndSec + 0.01
            && c->getPosition().getEnd().inSeconds() <= songEndSec + 0.01)
            ++materializedCount;

    EXPECT_EQ(materializedCount, 2);
    harness.audio().stop();
}

TEST(SongTests, OverlappingLaneBlocksCreateOverlappingClips)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.insertLane(1);

    // Pattern 0 on lane 0 at bars 0..2, pattern 1 on lane 1 at bars 0..2.
    // Both lanes overlap in time; pad 0's track should see two clips at the
    // same range.
    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(1, 0, 1, 2);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    harness.audio().play();

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto* track = pads.getTrack(0);
    ASSERT_NE(track, nullptr);

    const auto bar2Sec = edit->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ 2, {} }).inSeconds();

    int clipsInRange = 0;
    for (auto* c : track->getClips())
    {
        const auto start = c->getPosition().getStart().inSeconds();
        const auto end = c->getPosition().getEnd().inSeconds();
        if (start <= 0.01 && std::abs(end - bar2Sec) < 0.02)
            ++clipsInRange;
    }
    EXPECT_EQ(clipsInRange, 2);

    harness.audio().stop();
}

TEST(SongTests, DematerializeRestoresPatternClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.insertBlock(0, 0, 0, 2);
    pads.insertBlock(0, 2, 1, 3);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    harness.audio().play();
    harness.audio().stop();

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto* track = pads.getTrack(0);
    ASSERT_NE(track, nullptr);

    auto clips = track->getClips();
    ASSERT_EQ(clips.size(), 1);

    const auto restoredEnd = edit->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ 1, {} }).inSeconds();
    EXPECT_NEAR(clips[0]->getPosition().getStart().inSeconds(), 0.0, 0.01);
    EXPECT_NEAR(clips[0]->getPosition().getEnd().inSeconds(), restoredEnd, 0.01);
}

TEST(SongTests, MaterializedClipsCarryCorrectPatternContent)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.createPattern();

    ASSERT_TRUE(pads.setStep(2, 0, 0, true));
    pads.insertBlock(0, 0, 2, 1);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    harness.audio().play();

    auto* track = pads.getTrack(0);
    ASSERT_NE(track, nullptr);

    te::MidiClip* slotClip = nullptr;
    for (auto* c : track->getClips())
    {
        if (c->getPosition().getStart().inSeconds() < 0.01)
        {
            slotClip = dynamic_cast<te::MidiClip*>(c);
            break;
        }
    }
    ASSERT_NE(slotClip, nullptr);

    bool foundPad0Note = false;
    for (auto* n : slotClip->getSequence().getNotes())
        if (n->getNoteNumber() == 36)
            foundPad0Note = true;
    EXPECT_TRUE(foundPad0Note);

    harness.audio().stop();
}

TEST(SongTests, PatternEditWhilePlayingUpdatesMaterializedClips)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.insertBlock(0, 0, 1, 1);
    pads.insertBlock(0, 1, 1, 1);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    harness.audio().play();

    ASSERT_TRUE(pads.setStep(1, 0, 0, true));

    auto* track = pads.getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* edit = harness.audio().getEdit();
    const auto bar = [&](int n)
    {
        return edit->tempoSequence.toTime(
            tracktion::core::tempo::BarsAndBeats{ n, {} }).inSeconds();
    };

    int blocksFoundWithNote = 0;
    for (auto* c : track->getClips())
    {
        const auto start = c->getPosition().getStart().inSeconds();
        if (start > bar(2) + 0.01)
            continue;

        auto* mc = dynamic_cast<te::MidiClip*>(c);
        if (mc == nullptr)
            continue;

        for (auto* n : mc->getSequence().getNotes())
            if (n->getNoteNumber() == 36) { ++blocksFoundWithNote; break; }
    }
    EXPECT_EQ(blocksFoundWithNote, 2);
    harness.audio().stop();
}

// ──────────────────────────────────────────────────────────────────────────
// Serialisation + loop range refresh.
// ──────────────────────────────────────────────────────────────────────────

TEST(SongTests, SongLanesRoundTrip)
{
    auto tempDir = juce::File::createTempFile("");
    tempDir.deleteFile();
    tempDir.createDirectory();
    auto tempProject = tempDir.getChildFile("song.mpi");

    {
        testharness::EngineHarness harness;
        harness.createEmptyEdit();
        auto& pads = harness.pads();
        pads.createPattern();
        pads.createPattern();
        // Fresh song already has the default 5 lanes.
        const int savedLaneCount = pads.getNumSongLanes();
        ASSERT_GE(savedLaneCount, 2);

        pads.insertBlock(0, 0, 0, 4);
        pads.insertBlock(0, 4, 1, 2);
        pads.insertBlock(1, 0, 2, 8);

        ASSERT_TRUE(harness.audio().saveProjectToFile(tempProject));
    }

    testharness::EngineHarness harness2;
    ASSERT_TRUE(harness2.audio().loadProjectFromFile(tempProject));
    auto& pads2 = harness2.pads();

    // Round-trip preserves the exact lane count the song was saved with.
    ASSERT_EQ(pads2.getNumSongLanes(), 5);
    const auto& lane0 = pads2.getSongLanes()[0];
    const auto& lane1 = pads2.getSongLanes()[1];
    ASSERT_EQ(lane0.blocks.size(), 2u);
    EXPECT_EQ(lane0.blocks[0].patternIndex, 0);
    EXPECT_EQ(lane0.blocks[0].startBar, 0);
    EXPECT_EQ(lane0.blocks[0].bars, 4);
    EXPECT_EQ(lane0.blocks[1].patternIndex, 1);
    EXPECT_EQ(lane0.blocks[1].startBar, 4);
    EXPECT_EQ(lane0.blocks[1].bars, 2);
    ASSERT_EQ(lane1.blocks.size(), 1u);
    EXPECT_EQ(lane1.blocks[0].patternIndex, 2);
    EXPECT_EQ(lane1.blocks[0].startBar, 0);
    EXPECT_EQ(lane1.blocks[0].bars, 8);

    tempDir.deleteRecursively();
}

TEST(SongTests, LegacySlotSongMigratesToLane0)
{
    // Build a synthetic pads state tree using the OLD <song>/<slot> schema
    // and hand it straight to restorePadsFromState. No file IO — we're
    // exercising the migration path directly.
    juce::ValueTree padsState("pads");
    padsState.setProperty("numPatterns", 3, nullptr);
    padsState.setProperty("activePatternIndex", 0, nullptr);
    padsState.setProperty("stepsPerBar", 16, nullptr);
    padsState.setProperty("bars", 1, nullptr);

    // Three legacy slots: pattern 0 x 4 bars, pattern 2 x 2 bars,
    // pattern 1 x 3 bars → cumulative startBars 0, 4, 6.
    juce::ValueTree songNode("song");
    const auto addSlot = [&](int pat, int bars)
    {
        juce::ValueTree slot("slot");
        slot.setProperty("patternIndex", pat, nullptr);
        slot.setProperty("bars", bars, nullptr);
        songNode.addChild(slot, -1, nullptr);
    };
    addSlot(0, 4);
    addSlot(2, 2);
    addSlot(1, 3);
    padsState.addChild(songNode, -1, nullptr);

    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    harness.pads().restorePadsFromState(padsState);

    // One lane with three blocks, laid out in legacy order with cumulative
    // startBars.
    ASSERT_EQ(harness.pads().getNumSongLanes(), 1);
    const auto& blocks = harness.pads().getSongLanes()[0].blocks;
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].patternIndex, 0);
    EXPECT_EQ(blocks[0].startBar, 0);
    EXPECT_EQ(blocks[0].bars, 4);
    EXPECT_EQ(blocks[1].patternIndex, 2);
    EXPECT_EQ(blocks[1].startBar, 4);
    EXPECT_EQ(blocks[1].bars, 2);
    EXPECT_EQ(blocks[2].patternIndex, 1);
    EXPECT_EQ(blocks[2].startBar, 6);
    EXPECT_EQ(blocks[2].bars, 3);
}

TEST(SongTests, FreshProjectRoundTripsDefaultLaneCount)
{
    // A just-created project has 5 default empty lanes. Save/load must
    // preserve that exact shape — no silent re-defaulting on either side.
    auto tempDir = juce::File::createTempFile("");
    tempDir.deleteFile();
    tempDir.createDirectory();
    auto tempProject = tempDir.getChildFile("fresh.mpi");

    {
        testharness::EngineHarness harness;
        harness.createEmptyEdit();
        ASSERT_TRUE(harness.audio().saveProjectToFile(tempProject));
    }

    testharness::EngineHarness harness2;
    ASSERT_TRUE(harness2.audio().loadProjectFromFile(tempProject));
    EXPECT_EQ(harness2.pads().getNumSongLanes(), 5);
    for (const auto& lane : harness2.pads().getSongLanes())
        EXPECT_TRUE(lane.blocks.empty());

    tempDir.deleteRecursively();
}

TEST(SongTests, LegacySingleLaneSaveLoadsAsOneLane)
{
    // A legacy save with exactly one lane must NOT be force-upgraded to the
    // new 5-lane default on load. Build the pads tree by hand to simulate an
    // older file that stored a single empty lane, then hand it straight to
    // restorePadsFromState.
    juce::ValueTree padsState("pads");
    padsState.setProperty("numPatterns", 1, nullptr);
    padsState.setProperty("activePatternIndex", 0, nullptr);
    padsState.setProperty("stepsPerBar", 16, nullptr);
    padsState.setProperty("bars", 1, nullptr);

    juce::ValueTree lanesNode("songLanes");
    lanesNode.addChild(juce::ValueTree("lane"), -1, nullptr);
    padsState.addChild(lanesNode, -1, nullptr);

    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    harness.pads().restorePadsFromState(padsState);

    EXPECT_EQ(harness.pads().getNumSongLanes(), 1);
    EXPECT_TRUE(harness.pads().getSongLanes()[0].blocks.empty());
}

TEST(SongTests, EditingSongInTrackModeRefreshesLoopRange)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    pads.insertBlock(0, 0, 0, 4);

    auto* edit = harness.audio().getEdit();
    auto loop = edit->getTransport().getLoopRange();
    const auto expectedEnd4Bars = edit->tempoSequence.toTime({ 4, {} }).inSeconds();
    EXPECT_NEAR(loop.getEnd().inSeconds(), expectedEnd4Bars, 0.01);

    pads.insertBlock(0, 4, 1, 2);  // song grows to 6 bars
    loop = edit->getTransport().getLoopRange();
    const auto expectedEnd6Bars = edit->tempoSequence.toTime({ 6, {} }).inSeconds();
    EXPECT_NEAR(loop.getEnd().inSeconds(), expectedEnd6Bars, 0.01);

    pads.setBlockBars(0, 0, 8);  // first block grows; next block starts at 4...
    // setBlockBars refuses overlap, so this should be rejected. Verify the
    // loop end is unchanged.
    loop = edit->getTransport().getLoopRange();
    EXPECT_NEAR(loop.getEnd().inSeconds(), expectedEnd6Bars, 0.01);
}

// ──────────────────────────────────────────────────────────────────────────
// Unchanged pattern-name behaviour (kept from pre-migration tests).
// ──────────────────────────────────────────────────────────────────────────

TEST(PatternNameTests, DefaultsToPatternN)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    EXPECT_EQ(pads.getPatternName(0), "Pattern 1");
}

TEST(PatternNameTests, SetAndGet)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.setPatternName(0, "Intro");
    EXPECT_EQ(pads.getPatternName(0), "Intro");
    pads.setPatternName(0, "");
    EXPECT_EQ(pads.getPatternName(0), "Pattern 1");
}

TEST(PatternNameTests, InsertPatternAlignsNames)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.setPatternName(0, "A");
    pads.setPatternName(1, "B");
    pads.insertPattern(1);
    EXPECT_EQ(pads.getPatternName(0), "A");
    EXPECT_EQ(pads.getPatternName(1), "Pattern 2");
    EXPECT_EQ(pads.getPatternName(2), "B");
}

TEST(PatternNameTests, ClonePatternGetsDefaultName)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.setPatternName(0, "Intro");
    const int newIdx = pads.clonePattern(0);
    ASSERT_EQ(newIdx, 1);
    EXPECT_EQ(pads.getPatternName(0), "Intro");
    EXPECT_EQ(pads.getPatternName(1), "Pattern 2");
}

TEST(PatternNameTests, DeletePatternDropsName)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();
    pads.createPattern();
    pads.createPattern();
    pads.setPatternName(0, "A");
    pads.setPatternName(1, "B");
    pads.setPatternName(2, "C");
    pads.deletePattern(1);
    EXPECT_EQ(pads.getPatternName(0), "A");
    EXPECT_EQ(pads.getPatternName(1), "C");
}

TEST(PatternNameTests, RoundTripsThroughSave)
{
    auto dir = juce::File::createTempFile("");
    dir.deleteFile();
    dir.createDirectory();
    auto proj = dir.getChildFile("names.mpi");
    {
        testharness::EngineHarness h;
        h.createEmptyEdit();
        h.pads().createPattern();
        h.pads().setPatternName(0, "Intro");
        h.pads().setPatternName(1, "Verse");
        ASSERT_TRUE(h.audio().saveProjectToFile(proj));
    }
    testharness::EngineHarness h2;
    ASSERT_TRUE(h2.audio().loadProjectFromFile(proj));
    EXPECT_EQ(h2.pads().getPatternName(0), "Intro");
    EXPECT_EQ(h2.pads().getPatternName(1), "Verse");
    dir.deleteRecursively();
}
