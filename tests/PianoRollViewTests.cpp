#include <gtest/gtest.h>
#include "../src/ui/components/PianoRollView.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

namespace {
te::MidiClip* firstClipOfPad(testharness::EngineHarness& h, int pad)
{
    auto* track = h.pads().getTrack(pad);
    for (auto* c : track->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(c)) return m;
    return nullptr;
}
}

TEST(PianoRollViewTests, EmptyClipYieldsNoRenderedNotes)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);

    PianoRollView v;
    v.setMidiClip(mc);
    EXPECT_EQ(v.getRenderedNotes().size(), 0u);
}

TEST(PianoRollViewTests, NotesInClipAppearInRenderedOutput)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    mc->getSequence().addNote(60,
        tracktion::BeatPosition::fromBeats(0.0),
        tracktion::BeatDuration::fromBeats(0.5),
        100, 0, nullptr);
    mc->getSequence().addNote(64,
        tracktion::BeatPosition::fromBeats(1.0),
        tracktion::BeatDuration::fromBeats(0.25),
        80, 0, nullptr);

    PianoRollView v;
    v.setMidiClip(mc);
    auto notes = v.getRenderedNotes();
    ASSERT_EQ(notes.size(), 2u);

    // Order is not guaranteed — check by pitch.
    PianoRollView::RenderedNote n60{}, n64{};
    for (auto& n : notes)
    {
        if (n.pitch == 60) n60 = n;
        if (n.pitch == 64) n64 = n;
    }
    EXPECT_DOUBLE_EQ(n60.startBeats, 0.0);
    EXPECT_DOUBLE_EQ(n60.lengthBeats, 0.5);
    EXPECT_EQ(n60.velocity, 100);
    EXPECT_DOUBLE_EQ(n64.startBeats, 1.0);
    EXPECT_EQ(n64.velocity, 80);
}

TEST(PianoRollViewTests, CursorMoveClampsToPitchRange)
{
    PianoRollView v;
    v.setCursor({ 0.0, 60 });
    v.moveCursor(0, +200);  // push pitch above MIDI max
    EXPECT_LE(v.getCursor().pitch, 127);
    v.moveCursor(0, -400);
    EXPECT_GE(v.getCursor().pitch, 0);
}

namespace {
te::MidiNote* addNote(te::MidiClip& clip, int pitch, double beat, double len = 0.25, int vel = 96)
{
    auto& seq = clip.getSequence();
    seq.addNote(pitch,
        tracktion::BeatPosition::fromBeats(beat),
        tracktion::BeatDuration::fromBeats(len),
        vel, 0, nullptr);
    auto notes = seq.getNotes();
    return notes[notes.size() - 1];
}
}

TEST(PianoRollViewTests, QuantiseMovesNotesToGridAndIsUndoable)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    auto* n = addNote(*mc, 60, /*beat*/ 0.30);   // off-grid

    PianoRollView v;
    v.setMidiClip(mc);
    v.selectAll();
    v.quantise(1.0f);
    EXPECT_NEAR(n->getStartBeat().inBeats(), 0.25, 1e-9);

    mc->edit.getUndoManager().undo();
    EXPECT_NEAR(n->getStartBeat().inBeats(), 0.30, 1e-9);
}

TEST(PianoRollViewTests, AdjustableQuantiseStrengthPreservesSubStepTiming)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    auto* note = addNote(*mc, 60, 0.30);

    PianoRollView view;
    view.setMidiClip(mc);
    view.selectAll();
    view.quantise(0.5f, 0.125); // Halfway from 0.30 to the 1/32 grid at 0.25.

    EXPECT_NEAR(note->getStartBeat().inBeats(), 0.275, 1e-9);
}

TEST(PianoRollViewTests, QuantiseZeroStrengthIsNoopAndSkipsUndoTransaction)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    addNote(*mc, 60, 0.30);

    PianoRollView v;
    v.setMidiClip(mc);
    v.selectAll();
    auto& um = mc->edit.getUndoManager();
    const auto nameBefore = um.getCurrentTransactionName();
    v.quantise(0.0f);
    EXPECT_EQ(um.getCurrentTransactionName(), nameBefore)
        << "zero-strength quantise should not open a transaction";
}

TEST(PianoRollViewTests, ShiftSelectedPitchIsUndoable)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    auto* n = addNote(*mc, 60, 0.0);

    PianoRollView v;
    v.setMidiClip(mc);
    v.selectAll();
    v.shiftSelectedPitch(+1);
    EXPECT_EQ(n->getNoteNumber(), 61);

    mc->edit.getUndoManager().undo();
    EXPECT_EQ(n->getNoteNumber(), 60);
}

TEST(PianoRollViewTests, NudgeSelectedIsUndoable)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    auto* n = addNote(*mc, 60, 0.25);

    PianoRollView v;
    v.setMidiClip(mc);
    v.selectAll();
    v.nudgeSelected(+0.25);
    EXPECT_NEAR(n->getStartBeat().inBeats(), 0.50, 1e-9);

    mc->edit.getUndoManager().undo();
    EXPECT_NEAR(n->getStartBeat().inBeats(), 0.25, 1e-9);
}

TEST(PianoRollViewTests, NudgeClampsNoteInsidePatternBoundary)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    const double clipLength = mc->getLengthInBeats().inBeats();
    auto* note = addNote(*mc, 60, clipLength - 0.25, 0.25);

    PianoRollView view;
    view.setMidiClip(mc);
    view.selectAll();
    view.nudgeSelected(+1.0);

    EXPECT_NEAR(note->getStartBeat().inBeats(), clipLength - 0.25, 1e-9);
}

TEST(PianoRollViewTests, OpsNoOpWhenSelectionEmpty)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    auto* n = addNote(*mc, 60, 0.30);

    PianoRollView v;
    v.setMidiClip(mc);
    // Explicitly no selection — ops must be no-op to avoid touching every note.
    v.quantise(1.0f);
    v.shiftSelectedPitch(+2);
    v.nudgeSelected(+0.5);
    EXPECT_NEAR(n->getStartBeat().inBeats(), 0.30, 1e-9);
    EXPECT_EQ(n->getNoteNumber(), 60);
}

TEST(PianoRollViewTests, DeleteSelectedRemovesSelectedNotesAndIsUndoable)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    addNote(*mc, 60, 0.00);
    addNote(*mc, 64, 1.00);
    addNote(*mc, 67, 2.00);
    ASSERT_EQ(mc->getSequence().getNotes().size(), 3);

    PianoRollView v;
    v.setMidiClip(mc);
    // Navigate onto the middle note — navigateNotes sets a single-note selection.
    v.setCursor({ 1.00, 64 });
    v.navigateNotes(+1);  // walks to next note (67)
    v.navigateNotes(-1);  // back to 64 — selection = {64 @ 1.00}

    v.deleteSelected();

    auto remaining = mc->getSequence().getNotes();
    ASSERT_EQ(remaining.size(), 2);
    // Remaining should be the 60 and 67 notes.
    std::set<int> pitches;
    for (auto* nn : remaining) pitches.insert(nn->getNoteNumber());
    EXPECT_EQ(pitches.count(60), 1u);
    EXPECT_EQ(pitches.count(67), 1u);
    EXPECT_EQ(pitches.count(64), 0u);

    mc->edit.getUndoManager().undo();
    EXPECT_EQ(mc->getSequence().getNotes().size(), 3);
    std::set<int> restored;
    for (auto* nn : mc->getSequence().getNotes()) restored.insert(nn->getNoteNumber());
    EXPECT_EQ(restored.count(64), 1u);
}

TEST(PianoRollViewTests, DeleteSelectedClearsSelectionAfterRemoval)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    addNote(*mc, 60, 0.00);
    addNote(*mc, 64, 1.00);

    PianoRollView v;
    v.setMidiClip(mc);
    v.setCursor({ 0.00, 60 });
    v.navigateNotes(+1);   // walks to 64 — single-note selection
    v.navigateNotes(-1);   // back to 60

    v.deleteSelected();

    // One note left; selection was cleared, so allSelected() after a
    // fresh selectAll should be true (no phantom pre-existing selection).
    v.selectAll();
    EXPECT_TRUE(v.allSelected());
    EXPECT_EQ(v.getSelectionCount(), 1u);
}

// Regression: after quantise+undo, selectedNoteIds_ stored the post-quantise
// key while the note reverted to its pre-quantise position. applyTargets_
// filtered strictly by stored keys so subsequent nudge/quantise/etc. all
// silently no-op'd — shift+pad felt "stopped working" to the user.
TEST(PianoRollViewTests, OperationAfterUndoTargetsNoteAtCursor)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    addNote(*mc, 60, 0.30);

    PianoRollView v;
    v.setMidiClip(mc);
    v.setCursor({ 0.0, 60 });
    v.navigateNotes(+1);        // cursor → (0.30, 60), selection = {(0.30, 60)}
    v.quantise(1.0f);           // note moves 0.30 → 0.25, selection → {(0.25, 60)}

    mc->edit.getUndoManager().undo();   // note back at 0.30; selection stale

    v.nudgeSelected(+0.25);
    auto notes = mc->getSequence().getNotes();
    ASSERT_EQ(notes.size(), 1);
    EXPECT_NEAR(notes[0]->getStartBeat().inBeats(), 0.55, 1e-9)
        << "nudge after undo must still target the note at the cursor";
}

TEST(PianoRollViewTests, DeleteSelectedIsNoOpWhenSelectionEmpty)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto* mc = firstClipOfPad(h, 0);
    ASSERT_NE(mc, nullptr);
    addNote(*mc, 60, 0.30);

    PianoRollView v;
    v.setMidiClip(mc);
    v.clearSelection();
    auto& um = mc->edit.getUndoManager();
    const auto nameBefore = um.getCurrentTransactionName();

    v.deleteSelected();

    EXPECT_EQ(mc->getSequence().getNotes().size(), 1);
    EXPECT_EQ(um.getCurrentTransactionName(), nameBefore)
        << "empty-selection delete should not open a transaction";
}
