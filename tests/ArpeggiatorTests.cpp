#include <gtest/gtest.h>

#include "../src/engine/Arpeggiator.h"

// All tests here exercise the pure stepPitch resolver so we don't need a
// running AudioEngine or a track. The timer/injection path is covered by
// integration tests elsewhere.

using Mode = Arpeggiator::Mode;

TEST(ArpeggiatorTests, UpModeWalksSortedPitchesThenOctaves)
{
    const std::vector<int> held = { 60, 67, 64 };  // C4, G4, E4 — unsorted on purpose

    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 0, 1), 60);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 1, 1), 64);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 2, 1), 67);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 3, 1), 60);   // wraps

    // Two octaves: after the first 3 steps, climbs an octave.
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 3, 2), 60 + 12);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 5, 2), 67 + 12);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 6, 2), 60);    // wraps both
}

TEST(ArpeggiatorTests, DownModeWalksDescending)
{
    const std::vector<int> held = { 60, 64, 67 };
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Down, held, 0, 1), 67);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Down, held, 1, 1), 64);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Down, held, 2, 1), 60);
}

TEST(ArpeggiatorTests, UpDownSkipsExtremesFollowingSequentialConvention)
{
    // 3 notes: cycle should be C E G E (back down without re-repeating G),
    // length 2N-2 = 4 steps.
    const std::vector<int> held = { 60, 64, 67 };
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::UpDown, held, 0, 1), 60);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::UpDown, held, 1, 1), 64);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::UpDown, held, 2, 1), 67);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::UpDown, held, 3, 1), 64);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::UpDown, held, 4, 1), 60);  // cycle restart
}

TEST(ArpeggiatorTests, UpDownSingleNoteJustRepeats)
{
    const std::vector<int> held = { 60 };
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::UpDown, held, 0, 1), 60);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::UpDown, held, 7, 1), 60);
}

TEST(ArpeggiatorTests, AsPlayedUsesPressOrder)
{
    // Press order was E, C, G — As-Played walks that order, not sorted.
    const std::vector<int> held = { 64, 60, 67 };
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::AsPlayed, held, 0, 1), 64);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::AsPlayed, held, 1, 1), 60);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::AsPlayed, held, 2, 1), 67);
}

TEST(ArpeggiatorTests, RandomModeReturnsHeldPitchOrOctaveTransposition)
{
    const std::vector<int> held = { 60, 64, 67 };
    for (int step = 0; step < 20; ++step)
    {
        const int p = Arpeggiator::stepPitch(Mode::Random, held, step, 2);
        const int base = ((p - 60) % 12 + 12) % 12;
        // Must be a semitone that appears in the held set modulo 12.
        const bool valid = (base == 0) || (base == 4) || (base == 7);
        EXPECT_TRUE(valid) << "step " << step << " -> " << p;
        EXPECT_GE(p, 60);
        EXPECT_LT(p, 60 + 24);   // within 2 octaves
    }
}

TEST(ArpeggiatorTests, EmptyHeldSetReturnsMinusOne)
{
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, {}, 0, 1), -1);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Random, {}, 5, 4), -1);
}

TEST(ArpeggiatorTests, OffModeReturnsMinusOneEvenWithHeldNotes)
{
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Off, { 60, 64 }, 0, 1), -1);
}

TEST(ArpeggiatorTests, OctaveClampsToOneWhenNegativeOrZero)
{
    const std::vector<int> held = { 60 };
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 0, 0),  60);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Up, held, 0, -3), 60);
}

TEST(ArpeggiatorTests, ChordModeReturnsFirstHeldPitchAsRepresentative)
{
    const std::vector<int> held = { 60, 64, 67 };
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Chord, held, 0, 1), 60);
    EXPECT_EQ(Arpeggiator::stepPitch(Mode::Chord, held, 5, 1), 60);
}

// ── Stateful behaviour (noteOn → advance) ──────────────────────────────────
// These tests exercise the held-note bookkeeping without needing a track.

#include "harness/EngineHarness.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"

TEST(ArpeggiatorTests, AdvanceForTestWalksUpSequence)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& arp = h.audio().getKeyboardArp();

    Arpeggiator::Settings s;
    s.mode = Mode::Up;
    s.octaves = 1;
    arp.setSettings(s);

    arp.noteOn(64, 100);  // E first
    arp.noteOn(60, 100);  // then C
    arp.noteOn(67, 100);  // then G — held set sorted = 60, 64, 67

    EXPECT_EQ(arp.advanceForTest(), 60);
    EXPECT_EQ(arp.advanceForTest(), 64);
    EXPECT_EQ(arp.advanceForTest(), 67);
    EXPECT_EQ(arp.advanceForTest(), 60);   // wraps
}

TEST(ArpeggiatorTests, NoteOffRemovesFromHeldSetUnlessLatched)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& arp = h.audio().getKeyboardArp();
    arp.setSettings({ Mode::Up, 0.25, 1, 0.5f, /*latch*/ false, 1 });

    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    EXPECT_EQ(arp.getHeldCount(), 2);
    arp.noteOff(60);
    EXPECT_EQ(arp.getHeldCount(), 1);

    // Switch to latch: noteOff now no-op.
    arp.setSettings({ Mode::Up, 0.25, 1, 0.5f, /*latch*/ true, 1 });
    arp.noteOn(60, 100);
    EXPECT_EQ(arp.getHeldCount(), 2);
    arp.noteOff(60);
    EXPECT_EQ(arp.getHeldCount(), 2);     // latch kept it
}

TEST(ArpeggiatorTests, AllNotesOffClearsHeldSet)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& arp = h.audio().getKeyboardArp();
    arp.setSettings({ Mode::Up, 0.25, 1, 0.5f, true, 1 });

    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);
    EXPECT_EQ(arp.getHeldCount(), 3);

    arp.allNotesOff();
    EXPECT_EQ(arp.getHeldCount(), 0);
}

TEST(ArpeggiatorTests, DuplicateNoteOnRefreshesVelocityWithoutDuplicating)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& arp = h.audio().getKeyboardArp();
    arp.setSettings({ Mode::Up, 0.25, 1, 0.5f, false, 1 });

    arp.noteOn(60, 50);
    arp.noteOn(60, 100);   // same pitch — should refresh velocity, not add a row
    EXPECT_EQ(arp.getHeldCount(), 1);
}

TEST(ArpeggiatorTests, DetachingTrackStopsFurtherTimerEmissions)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto& arp = h.audio().getKeyboardArp();
    auto* track = h.pads().getTrack(0);
    ASSERT_NE(track, nullptr);

    int emissionCount = 0;
    arp.setNoteEmittedCallback(
        [&emissionCount](int, int, double) { ++emissionCount; });
    arp.setTrack(track);
    arp.setSettings({ Mode::Up, 1.0 / 32.0, 1, 0.5f, false, 1 });
    arp.noteOn(60, 100);

    for (int tick = 0; tick < 8; ++tick)
    {
        juce::Thread::sleep(6);
        juce::Timer::callPendingTimersSynchronously();
    }
    ASSERT_GT(emissionCount, 0);

    arp.setTrack(nullptr);
    const int countAfterDetach = emissionCount;
    for (int tick = 0; tick < 8; ++tick)
    {
        juce::Thread::sleep(6);
        juce::Timer::callPendingTimersSynchronously();
    }

    EXPECT_EQ(emissionCount, countAfterDetach);
    arp.allNotesOff();
}
