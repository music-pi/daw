#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/KeyboardInstrumentBank.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

namespace
{
// Fabricate an "Instruments" folder + one AudioTrack so KeyboardInstrumentBank
// rediscovery can populate a slot without a real VST. Mirrors the layout the
// bank writes itself so round-trip through attachToEdit exercises the real
// code path.
te::AudioTrack::Ptr buildSingleSlot(te::Edit& edit)
{
    auto folder = edit.insertNewFolderTrack(
        te::TrackInsertPoint{ nullptr, nullptr }, nullptr, false);
    if (folder != nullptr) folder->setName("Instruments");

    auto track = edit.insertNewAudioTrack(
        te::TrackInsertPoint{ folder.get(), nullptr }, nullptr);
    if (track == nullptr) return nullptr;

    track->state.setProperty("instrumentSlot", 0, nullptr);
    track->state.setProperty("slotName", "Inst 1", nullptr);
    track->setName("Inst 1");
    return track;
}

int countNotesAtPitch(const te::MidiList& list, int pitch)
{
    int n = 0;
    for (auto* note : list.getNotes())
        if (note->getNoteNumber() == pitch) ++n;
    return n;
}
}

TEST(KeyboardInstrumentBankPatternTests, SlotContentSurvivesPatternSwap)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);

    // Record a C4 on pattern 0.
    live->getSequence().addNote(60, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);
    EXPECT_EQ(countNotesAtPitch(live->getSequence(), 60), 1);

    // Switch to pattern 1, record a different note.
    bank.setActivePatternIndex(1);
    EXPECT_EQ(live->getSequence().getNumNotes(), 0) << "live clip should be empty on fresh pattern";

    live->getSequence().addNote(72, tracktion::BeatPosition::fromBeats(0.5),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);
    EXPECT_EQ(countNotesAtPitch(live->getSequence(), 72), 1);

    // Switch back to pattern 0 — the original C4 should be restored and the
    // C5 from pattern 1 should be gone from the live clip.
    bank.setActivePatternIndex(0);
    EXPECT_EQ(countNotesAtPitch(live->getSequence(), 60), 1);
    EXPECT_EQ(countNotesAtPitch(live->getSequence(), 72), 0);
}

namespace
{
// Count MidiClips on the slot's track. Used to check song materialisation
// added new clips alongside the live one.
int countMidiClipsOnTrack(te::AudioTrack& track)
{
    int n = 0;
    for (auto* c : track.getClips())
        if (dynamic_cast<te::MidiClip*>(c) != nullptr) ++n;
    return n;
}

// Find the MidiClip whose start bar matches startBar (using the edit's tempo
// sequence). Returns nullptr if none match.
te::MidiClip* findClipAtBar(te::AudioTrack& track, te::Edit& edit, int startBar)
{
    const auto wantStart = edit.tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ startBar, {} });
    for (auto* c : track.getClips())
    {
        if (auto* mc = dynamic_cast<te::MidiClip*>(c))
        {
            const auto s = mc->getPosition().getStart();
            if (std::abs((s - wantStart).inSeconds()) < 1.0e-6)
                return mc;
        }
    }
    return nullptr;
}
}

TEST(KeyboardInstrumentBankPatternTests, MaterializeSongPlacesSlotBlocks)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    // Pattern 0: record a C4. Switch to pattern 1, record a G5. Back to 0.
    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);
    live->getSequence().addNote(60, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);
    bank.setActivePatternIndex(1);
    live->getSequence().addNote(79, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);
    bank.setActivePatternIndex(0);
    ASSERT_EQ(countNotesAtPitch(live->getSequence(), 60), 1);

    // Build a song: one lane, one block referring to pattern 1 at bars [2,4).
    std::vector<SamplerInstrument::SongLane> lanes;
    lanes.push_back({});
    lanes[0].blocks.push_back(SamplerInstrument::SongBlock{
        /*patternIndex*/ 1, /*startBar*/ 2, /*bars*/ 2 });

    auto* track = bank.getTrack();
    ASSERT_NE(track, nullptr);
    const int clipsBefore = countMidiClipsOnTrack(*track);

    bank.materializeSong(lanes);

    // One extra MidiClip should now sit at bar 2..4 on the slot's track.
    EXPECT_EQ(countMidiClipsOnTrack(*track), clipsBefore + 1);

    auto* placed = findClipAtBar(*track, *edit, 2);
    ASSERT_NE(placed, nullptr);

    const auto endExpected = edit->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ 4, {} });
    EXPECT_NEAR(placed->getPosition().getEnd().inSeconds(),
                endExpected.inSeconds(), 1.0e-6);

    // Placed clip carries pattern 1's content (G5 at beat 0).
    EXPECT_EQ(countNotesAtPitch(placed->getSequence(), 79), 1);
    EXPECT_EQ(countNotesAtPitch(placed->getSequence(), 60), 0);
}

TEST(KeyboardInstrumentBankPatternTests, DematerializeRestoresLiveClipPosition)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);
    live->getSequence().addNote(60, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);

    std::vector<SamplerInstrument::SongLane> lanes;
    lanes.push_back({});
    lanes[0].blocks.push_back(SamplerInstrument::SongBlock{ 0, 0, 1 });

    bank.materializeSong(lanes);
    bank.dematerializeSong();

    // Live clip snaps back to bar 0..1 and still carries pattern 0's content.
    const auto livePos = live->getPosition();
    EXPECT_NEAR(livePos.getStart().inSeconds(), 0.0, 1.0e-6);

    const auto oneBar = edit->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ 1, {} });
    EXPECT_NEAR(livePos.getEnd().inSeconds(), oneBar.inSeconds(), 1.0e-6);

    EXPECT_EQ(countNotesAtPitch(live->getSequence(), 60), 1);

    // No stray materialised clips remain on the slot's track.
    auto* track = bank.getTrack();
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(countMidiClipsOnTrack(*track), 1);
}

TEST(KeyboardInstrumentBankPatternTests, MaterializeDoesNotStompOngoingRecording)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    // Record on the active pattern but never swap — the bank hasn't seen the
    // note yet. materializeSong must archive live→bank first.
    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);
    live->getSequence().addNote(60, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);

    std::vector<SamplerInstrument::SongLane> lanes;
    lanes.push_back({});
    lanes[0].blocks.push_back(SamplerInstrument::SongBlock{ 0, 0, 1 });

    bank.materializeSong(lanes);

    // Placed clip at bar 0 should carry the freshly archived C4.
    auto* track = bank.getTrack();
    ASSERT_NE(track, nullptr);
    auto* placed = findClipAtBar(*track, *edit, 0);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(countNotesAtPitch(placed->getSequence(), 60), 1);

    // And after teardown, the live clip still holds the recording.
    bank.dematerializeSong();
    EXPECT_EQ(countNotesAtPitch(live->getSequence(), 60), 1);
}

TEST(KeyboardInstrumentBankPatternTests, SlotLiveClipFollowsPatternLength)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);

    // Route through the sampler so the coupling path is exercised end-to-end.
    ASSERT_TRUE(h.audio().getSampler().setPatternLength(0, /*bars*/ 2, /*stepsPerBar*/ 16));

    // 2 bars @ 4/4 = 8 beats.
    EXPECT_NEAR(live->getLengthInBeats().inBeats(), 8.0, 1.0e-3);
}

TEST(KeyboardInstrumentBankPatternTests, MaterializedClipsUseSamplerBarsForLoop)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    // Set the sampler pattern length to 2 bars before materialising.
    ASSERT_TRUE(h.audio().getSampler().setPatternLength(0, /*bars*/ 2, /*stepsPerBar*/ 16));

    std::vector<SamplerInstrument::SongLane> lanes;
    lanes.push_back({});
    lanes[0].blocks.push_back(SamplerInstrument::SongBlock{
        /*patternIndex*/ 0, /*startBar*/ 0, /*bars*/ 4 });

    bank.materializeSong(lanes);

    auto* track = bank.getTrack();
    ASSERT_NE(track, nullptr);
    auto* placed = findClipAtBar(*track, *edit, 0);
    ASSERT_NE(placed, nullptr);

    // 2 bars @ 4/4 = 8 beats loop length — not the legacy 1-bar loop.
    const auto loopLen = placed->getLoopLengthBeats();
    EXPECT_NEAR(loopLen.inBeats(), 8.0, 1.0e-3);
}

TEST(KeyboardInstrumentBankPatternTests, SlotLiveClipAppliesSwing)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);

    // Route through the sampler so the coupling path mirrors production use.
    h.audio().getSampler().applySwing(0.7f);

    EXPECT_NEAR(live->getGrooveStrength(), 0.7f, 1.0e-3f);
    EXPECT_EQ(live->getGrooveTemplate(), juce::String("Basic 16th Swing"));
}

TEST(KeyboardInstrumentBankPatternTests, InsertPatternSyncsKeyboardSlotList)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    // Record distinctive content on pattern 0, archive it to bank[0] via a
    // swap round-trip, then record something else on pattern 1. After
    // insertPattern(1) (the splice point), bank[0] must keep its original
    // note and bank[2] (was bank[1] pre-shift) must hold pattern 1's note —
    // i.e. insertPatternSlot performed a real insert-and-shift, not a
    // tail-append. The new slot at index 1 must be empty.
    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);

    auto& pads = h.audio().getSampler();

    // Pattern 0 → note 40 (archived in bank[0] after swap-away).
    live->getSequence().addNote(40, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);
    // Swap to pattern 1 (creates scene-list slack), record note 50, swap back.
    bank.setActivePatternIndex(1);
    live = bank.getMidiClip();
    live->getSequence().addNote(50, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);
    bank.setActivePatternIndex(0);

    // Splice a fresh empty pattern at index 1. bank[0] is untouched; bank[1]
    // is new-empty; bank[2] carries the old pattern-1 content.
    pads.insertPattern(1);

    const auto* slot = bank.getSlot(0);
    ASSERT_NE(slot, nullptr);

    int bank0Count40 = -1;
    slot->patternBank.readPattern(0, [&](const te::MidiList& list)
    {
        bank0Count40 = countNotesAtPitch(list, 40);
    });
    EXPECT_EQ(bank0Count40, 1) << "bank[0] content survives the splice";

    int bank2Count50 = -1;
    slot->patternBank.readPattern(2, [&](const te::MidiList& list)
    {
        bank2Count50 = countNotesAtPitch(list, 50);
    });
    EXPECT_EQ(bank2Count50, 1) << "old pattern-1 content shifted to bank[2]";

    int bank1Count50 = 0;
    slot->patternBank.readPattern(1, [&](const te::MidiList& list)
    {
        bank1Count50 = countNotesAtPitch(list, 50);
    });
    EXPECT_EQ(bank1Count50, 0) << "freshly inserted bank[1] must be empty";
}

TEST(KeyboardInstrumentBankPatternTests, UniqueSongBlockClonesKeyboardContent)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    // Record a distinctive note on pattern 0 (the active pattern) in the
    // keyboard slot. clonePattern must route live→snapshot→bank[new].
    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);
    live->getSequence().addNote(67, tracktion::BeatPosition::fromBeats(0.25),
                                tracktion::BeatDuration::fromBeats(0.5),
                                105, 0, nullptr);

    auto& pads = h.audio().getSampler();

    // Set up a minimal song: one lane, one block → pattern 0.
    ASSERT_EQ(pads.insertBlock(0, 0, 0, 1), 0);

    // Unique the block — clones pattern 0 into pattern 1, repoints the block.
    const int newIdx = pads.uniqueSongBlock(0, 0);
    ASSERT_EQ(newIdx, 1);
    EXPECT_EQ(pads.getSongLanes()[0].blocks[0].patternIndex, 1);

    // Keyboard bank[newIdx] must carry the cloned note.
    const auto* slot = bank.getSlot(0);
    ASSERT_NE(slot, nullptr);

    int clonedCount = -1;
    slot->patternBank.readPattern(newIdx, [&clonedCount](const te::MidiList& list)
    {
        clonedCount = countNotesAtPitch(list, 67);
    });
    EXPECT_EQ(clonedCount, 1)
        << "uniqueSongBlock must duplicate keyboard content into the new pattern";
}

TEST(KeyboardInstrumentBankPatternTests, ClonePatternDuplicatesNonActiveKeyboardContent)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    // Put one note on pattern 0, swap to pattern 1 so pattern 0 is archived
    // in the bank (not live). Then clonePattern(0) must read from bank[0].
    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);
    live->getSequence().addNote(53, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.25),
                                100, 0, nullptr);
    bank.setActivePatternIndex(1);

    auto& pads = h.audio().getSampler();
    const int newIdx = pads.clonePattern(0);
    ASSERT_EQ(newIdx, 1);

    const auto* slot = bank.getSlot(0);
    ASSERT_NE(slot, nullptr);

    int clonedCount = -1;
    slot->patternBank.readPattern(newIdx, [&clonedCount](const te::MidiList& list)
    {
        clonedCount = countNotesAtPitch(list, 53);
    });
    EXPECT_EQ(clonedCount, 1)
        << "clonePattern must duplicate bank-archived keyboard content";
}

TEST(KeyboardInstrumentBankPatternTests, PatternSwapArchivesCurrentClip)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* edit = h.audio().getEdit();
    ASSERT_NE(edit, nullptr);

    buildSingleSlot(*edit);

    auto& bank = h.audio().getKeyboardBank();
    bank.attachToEdit(*edit);
    ASSERT_EQ(bank.getNumSlots(), 1);
    bank.setActiveSlot(0);

    // Put a note on the live clip while pattern 0 is active.
    auto* live = bank.getMidiClip();
    ASSERT_NE(live, nullptr);
    live->getSequence().addNote(48, tracktion::BeatPosition::fromBeats(0.0),
                                tracktion::BeatDuration::fromBeats(0.5),
                                110, 0, nullptr);

    // Swap away — the bank slot for index 0 must now hold the archived note.
    bank.setActivePatternIndex(1);

    const auto* slot = bank.getSlot(0);
    ASSERT_NE(slot, nullptr);

    int archivedCount = -1;
    slot->patternBank.readPattern(0, [&archivedCount](const te::MidiList& list)
    {
        archivedCount = countNotesAtPitch(list, 48);
    });
    EXPECT_EQ(archivedCount, 1);
}
