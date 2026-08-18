#include <gtest/gtest.h>
#include "../src/engine/PadPatternBank.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

TEST(PadPatternBankTests, EmptyBankHasNoPatterns)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint({}), nullptr);
    ASSERT_NE(track.get(), nullptr);

    PadPatternBank bank;
    bank.attach(*track);
    EXPECT_EQ(bank.getNumPatterns(), 0);
}

TEST(PadPatternBankTests, AddPatternGrowsSlotList)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint({}), nullptr);
    ASSERT_NE(track.get(), nullptr);

    PadPatternBank bank;
    bank.attach(*track);

    EXPECT_EQ(bank.addPattern(), 0);
    EXPECT_EQ(bank.addPattern(), 1);
    EXPECT_EQ(bank.getNumPatterns(), 2);
    EXPECT_EQ(track->getClipSlotList().getClipSlots().size(), 2);
}

TEST(PadPatternBankTests, StoreAndRestorePreservesMidiList)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint({}), nullptr);
    ASSERT_NE(track.get(), nullptr);

    PadPatternBank bank;
    bank.attach(*track);

    const int idx = bank.addPattern();

    te::MidiList source;
    source.addNote(60, tracktion::BeatPosition::fromBeats(0.5),
                   tracktion::BeatDuration::fromBeats(0.25), 100, 0, nullptr);

    bank.storePattern(idx, source);

    te::MidiList dest;
    bank.restorePattern(idx, dest);

    ASSERT_EQ(dest.getNumNotes(), 1);
    EXPECT_EQ(dest.getNotes()[0]->getNoteNumber(), 60);
    EXPECT_DOUBLE_EQ(dest.getNotes()[0]->getStartBeat().inBeats(), 0.5);
}

TEST(PadPatternBankTests, RestoreNonexistentIndexLeavesDestUntouched)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint({}), nullptr);
    ASSERT_NE(track.get(), nullptr);

    PadPatternBank bank;
    bank.attach(*track);

    te::MidiList dest;
    dest.addNote(72, tracktion::BeatPosition::fromBeats(0),
                 tracktion::BeatDuration::fromBeats(0.25), 100, 0, nullptr);

    bank.restorePattern(9, dest);
    ASSERT_EQ(dest.getNumNotes(), 1);
    EXPECT_EQ(dest.getNotes()[0]->getNoteNumber(), 72);
}

TEST(PadPatternBankTests, MutatePatternUpdatesStoredNotes)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint({}), nullptr);
    ASSERT_NE(track.get(), nullptr);

    PadPatternBank bank;
    bank.attach(*track);

    const int idx = bank.addPattern();
    bank.mutatePattern(idx, [](te::MidiList& list) {
        list.addNote(48, tracktion::BeatPosition::fromBeats(0),
                     tracktion::BeatDuration::fromBeats(1.0), 90, 0, nullptr);
    });

    int count = -1;
    bank.readPattern(idx, [&count](const te::MidiList& list) {
        count = list.getNumNotes();
    });
    EXPECT_EQ(count, 1);
}

TEST(PadPatternBankTests, RemovePatternDeletesSlot)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint({}), nullptr);
    ASSERT_NE(track.get(), nullptr);

    PadPatternBank bank;
    bank.attach(*track);

    bank.addPattern();
    bank.addPattern();
    ASSERT_EQ(bank.getNumPatterns(), 2);

    bank.removePattern(0);
    EXPECT_EQ(bank.getNumPatterns(), 1);
}
