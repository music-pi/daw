#pragma once

#include <set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/** Renders a te::MidiClip's MidiList as a beats-vs-pitch grid (960×272).
    Selection + cursor + playhead + operations (quantize, pitch shift)
    live here; PianoRollWidget forwards hardware input (shift+pads, 4D
    encoder) to these methods. */
class PianoRollView : public juce::Component
{
public:
    struct Cursor { double beat; int pitch; };
    struct RenderedNote {
        int pitch;
        double startBeats;
        double lengthBeats;
        int velocity;
    };

    PianoRollView();
    ~PianoRollView() override = default;

    void setMidiClip(te::MidiClip* clip);
    te::MidiClip* getMidiClip() const { return clip_; }

    // Cursor + navigation
    void setCursor(Cursor c);
    Cursor getCursor() const { return cursor_; }
    void moveCursor(double deltaBeats, int deltaPitch);

    /** Move the cursor to the next / previous note (by start beat). If a
        note is currently highlighted under the cursor the walk starts
        from its position; otherwise from cursor_.beat. */
    void navigateNotes(int direction);

    // Playhead (absolute beats within the clip; negative = no playhead).
    void setPlayheadBeat(double beatInClip);

    // Selection
    void selectAll();
    void clearSelection();
    bool hasSelection() const { return !selectedNoteIds_.empty(); }
    std::size_t getSelectionCount() const { return selectedNoteIds_.size(); }

    // Operations — apply to the current selection. No-op when nothing is
    // selected so stray shift+pad presses don't touch every note by
    // accident. Wrapped in the Edit's UndoManager so undo/redo round-trips.
    void quantise(float strength, double gridBeats = 0.25);
                                      // 1.0f = snap, 0.5f = half-way pull
    void shiftSelectedPitch(int semitones);
    void nudgeSelected(double deltaBeats);
    void deleteSelected();

    /** True when every note in the clip is currently selected. */
    bool allSelected() const;

    // View window (pitch range + beats span). Defaults: C3..C5, 4 beats.
    void setVisiblePitchRange(int lowPitch, int highPitch);
    void setVisibleBeats(double beats);

    // For tests and paint.
    std::vector<RenderedNote> getRenderedNotes() const;

    void paint(juce::Graphics&) override;

private:
    // Notes don't expose stable identifiers across edits; we identify a
    // note by its start-beat + pitch, which is enough for selection and
    // nav since overlapping same-pitch notes at the same time are
    // effectively one note.
    struct NoteKey { double startBeats; int pitch; bool operator<(const NoteKey& o) const {
        return startBeats < o.startBeats ? true
             : (startBeats > o.startBeats ? false : pitch < o.pitch); } };

    std::vector<te::MidiNote*> applyTargets_();
    NoteKey keyOf(const te::MidiNote& n) const;

    te::MidiClip* clip_ { nullptr };
    Cursor cursor_ { 0.0, 60 };
    int lowPitch_ { 48 };
    int highPitch_ { 72 };
    double viewBeats_ { 4.0 };
    double playheadBeat_ { -1.0 };
    std::set<NoteKey> selectedNoteIds_;
};
