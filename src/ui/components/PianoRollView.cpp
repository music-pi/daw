#include "PianoRollView.h"

#include "../../engine/MidiConstants.h"
#include "../theme/UiTheme.h"

PianoRollView::PianoRollView() = default;

void PianoRollView::setMidiClip(te::MidiClip* clip)
{
    clip_ = clip;
    selectedNoteIds_.clear();
    repaint();
}

void PianoRollView::setCursor(Cursor c)
{
    // Clamp to the clip length, not the visible window — otherwise navigate-
    // to-note snaps to the right edge for notes past the viewport and the
    // selection silently desyncs from where the cursor actually sits.
    const double maxBeat = clip_ != nullptr
        ? clip_->getLengthInBeats().inBeats()
        : viewBeats_;
    cursor_.beat = juce::jlimit(0.0, juce::jmax(viewBeats_, maxBeat), c.beat);
    cursor_.pitch = juce::jlimit(midi::kNoteMin, midi::kNoteMax, c.pitch);
    repaint();
}

void PianoRollView::moveCursor(double deltaBeats, int deltaPitch)
{
    setCursor({ cursor_.beat + deltaBeats, cursor_.pitch + deltaPitch });
}

void PianoRollView::setVisiblePitchRange(int lowPitch, int highPitch)
{
    lowPitch_  = juce::jlimit(midi::kNoteMin, midi::kNoteMax, lowPitch);
    highPitch_ = juce::jlimit(lowPitch_ + 1, midi::kNoteMax, highPitch);
    repaint();
}

void PianoRollView::setVisibleBeats(double beats)
{
    viewBeats_ = juce::jmax(0.25, beats);
    repaint();
}

void PianoRollView::setPlayheadBeat(double beatInClip)
{
    if (beatInClip == playheadBeat_) return;
    playheadBeat_ = beatInClip;
    repaint();
}

PianoRollView::NoteKey PianoRollView::keyOf(const te::MidiNote& n) const
{
    return NoteKey { n.getStartBeat().inBeats(), n.getNoteNumber() };
}

void PianoRollView::selectAll()
{
    selectedNoteIds_.clear();
    if (clip_ == nullptr) return;
    for (auto* n : clip_->getSequence().getNotes())
        selectedNoteIds_.insert(keyOf(*n));
    repaint();
}

void PianoRollView::clearSelection()
{
    selectedNoteIds_.clear();
    repaint();
}

std::vector<te::MidiNote*> PianoRollView::applyTargets_()
{
    std::vector<te::MidiNote*> targets;
    if (clip_ == nullptr) return targets;

    if (! selectedNoteIds_.empty())
    {
        for (auto* n : clip_->getSequence().getNotes())
            if (selectedNoteIds_.count(keyOf(*n)))
                targets.push_back(n);
        if (! targets.empty()) return targets;

        // Stored selection references notes that no longer exist — typically
        // after undo of a prior mutation flipped every note's position back
        // to pre-op state. Drop the stale keys and fall through to the
        // cursor-based fallback so the next shift+pad still has a target.
        selectedNoteIds_.clear();
    }

    // Fallback: re-select the note at the cursor so navigate → op → undo → op
    // keeps working without a second navigate pass.
    for (auto* n : clip_->getSequence().getNotes())
    {
        if (n->getNoteNumber() != cursor_.pitch) continue;
        if (std::abs(n->getStartBeat().inBeats() - cursor_.beat) > 1e-6) continue;
        selectedNoteIds_.insert(keyOf(*n));
        targets.push_back(n);
        break;
    }
    return targets;
}

bool PianoRollView::allSelected() const
{
    if (clip_ == nullptr) return false;
    const auto noteCount = static_cast<std::size_t>(clip_->getSequence().getNotes().size());
    return noteCount > 0 && selectedNoteIds_.size() == noteCount;
}

void PianoRollView::quantise(float strength, double gridBeats)
{
    const float s = juce::jlimit(0.0f, 1.0f, strength);
    const double stepBeats = juce::jmax(1.0 / 256.0, gridBeats);
    if (s <= 0.0f) return;    // strength=0 means "do nothing" — skip the transaction entirely

    auto targets = applyTargets_();
    if (targets.empty()) return;
    auto& um = clip_->edit.getUndoManager();
    um.beginNewTransaction("Quantise notes");

    std::set<NoteKey> newSelection;
    for (auto* n : targets)
    {
        const double start = n->getStartBeat().inBeats();
        const double snapped = std::round(start / stepBeats) * stepBeats;
        const double moved = start + (snapped - start) * s;
        if (std::abs(moved - start) > 1e-6)
            n->setStartAndLength(
                tracktion::BeatPosition::fromBeats(moved),
                n->getLengthBeats(),
                &um);
        newSelection.insert(NoteKey { moved, n->getNoteNumber() });
    }

    selectedNoteIds_ = std::move(newSelection);
    repaint();
}

void PianoRollView::shiftSelectedPitch(int semitones)
{
    if (semitones == 0) return;
    auto targets = applyTargets_();
    if (targets.empty()) return;
    auto& um = clip_->edit.getUndoManager();
    um.beginNewTransaction(std::abs(semitones) == 12
                               ? juce::String("Shift pitch octave")
                               : juce::String("Shift pitch semitone"));

    std::set<NoteKey> newSelection;
    for (auto* n : targets)
    {
        const int newPitch = juce::jlimit(midi::kNoteMin, midi::kNoteMax, n->getNoteNumber() + semitones);
        n->setNoteNumber(newPitch, &um);
        newSelection.insert(NoteKey { n->getStartBeat().inBeats(), newPitch });
    }

    selectedNoteIds_ = std::move(newSelection);
    repaint();
}

void PianoRollView::nudgeSelected(double deltaBeats)
{
    if (std::abs(deltaBeats) < 1e-9) return;
    auto targets = applyTargets_();
    if (targets.empty()) return;
    auto& um = clip_->edit.getUndoManager();
    um.beginNewTransaction("Nudge notes");

    std::set<NoteKey> newSelection;
    for (auto* n : targets)
    {
        const double clipLength = juce::jmax(
            0.0, clip_->getLengthInBeats().inBeats());
        const double noteLength = n->getLengthBeats().inBeats();
        const double maxStart = juce::jmax(
            0.0, clipLength - juce::jmin(noteLength, clipLength));
        const double moved = juce::jlimit(
            0.0, maxStart, n->getStartBeat().inBeats() + deltaBeats);
        n->setStartAndLength(
            tracktion::BeatPosition::fromBeats(moved),
            n->getLengthBeats(),
            &um);
        newSelection.insert(NoteKey { moved, n->getNoteNumber() });
    }

    selectedNoteIds_ = std::move(newSelection);
    repaint();
}

void PianoRollView::deleteSelected()
{
    auto targets = applyTargets_();
    if (targets.empty()) return;
    auto& um = clip_->edit.getUndoManager();
    um.beginNewTransaction("Delete notes");

    // Remove via MidiList::removeNote — the list may reorder after each call,
    // so iterate over the captured target pointers rather than the live view.
    auto& seq = clip_->getSequence();
    for (auto* n : targets)
        seq.removeNote(*n, &um);

    // Selection keys referenced notes that no longer exist; clear rather than
    // leave a phantom set that would ghost-select a future re-added note.
    selectedNoteIds_.clear();
    repaint();
}

void PianoRollView::navigateNotes(int direction)
{
    if (clip_ == nullptr || direction == 0) return;
    auto notes = clip_->getSequence().getNotes();
    if (notes.size() == 0) return;

    // Sort note keys by start-beat then pitch so nav is deterministic.
    std::vector<NoteKey> keys;
    keys.reserve(static_cast<size_t>(notes.size()));
    for (auto* n : notes) keys.push_back(keyOf(*n));
    std::sort(keys.begin(), keys.end());

    // Find the cursor's current position among the notes.
    NoteKey cursorKey { cursor_.beat, cursor_.pitch };
    auto it = std::lower_bound(keys.begin(), keys.end(), cursorKey);
    int idx = static_cast<int>(std::distance(keys.begin(), it));

    if (direction > 0)
        idx = (idx < (int) keys.size() - 1) ? idx + 1 : 0;
    else
        idx = (idx > 0) ? idx - 1 : (int) keys.size() - 1;

    idx = juce::jlimit(0, (int) keys.size() - 1, idx);
    const auto& target = keys[(size_t) idx];
    setCursor({ target.startBeats, target.pitch });

    // Single-note selection lets pitch-shift / quantise act on just this one.
    selectedNoteIds_.clear();
    selectedNoteIds_.insert(target);
}

std::vector<PianoRollView::RenderedNote> PianoRollView::getRenderedNotes() const
{
    std::vector<RenderedNote> out;
    if (clip_ == nullptr) return out;
    for (auto* n : clip_->getSequence().getNotes())
    {
        out.push_back({
            n->getNoteNumber(),
            n->getStartBeat().inBeats(),
            n->getLengthBeats().inBeats(),
            n->getVelocity()
        });
    }
    return out;
}

void PianoRollView::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    auto bounds = getLocalBounds();

    const int numPitches = juce::jmax(1, highPitch_ - lowPitch_);
    const float rowH = bounds.getHeight() / (float) numPitches;
    const float beatW = bounds.getWidth() / (float) juce::jmax(0.001, viewBeats_);

    // Black-key row shading.
    for (int p = lowPitch_; p < highPitch_; ++p)
    {
        const int pc = ((p % 12) + 12) % 12;
        const bool isBlackKey = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (! isBlackKey) continue;
        const float y = bounds.getBottom() - (p - lowPitch_ + 1) * rowH;
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRect((float) bounds.getX(), y, (float) bounds.getWidth(), rowH);
    }

    // Horizontal pitch lines.
    for (int p = lowPitch_; p <= highPitch_; ++p)
    {
        const int pc = ((p % 12) + 12) % 12;
        const bool isC = pc == 0;
        g.setColour(juce::Colours::white.withAlpha(isC ? 0.35f : 0.12f));
        const float y = bounds.getBottom() - (p - lowPitch_) * rowH;
        g.drawHorizontalLine(juce::roundToInt(y),
                             (float) bounds.getX(), (float) bounds.getRight());
    }

    // Vertical beat + sub-beat lines.
    const int beatCount = juce::jmax(1, static_cast<int>(std::ceil(viewBeats_)));
    for (int b = 0; b <= beatCount; ++b)
    {
        const float x = bounds.getX() + b * beatW;
        g.setColour(juce::Colours::white.withAlpha((b % 4 == 0) ? 0.4f : 0.2f));
        g.drawVerticalLine(juce::roundToInt(x),
                           (float) bounds.getY(), (float) bounds.getBottom());
    }
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (int q = 0; q < beatCount * 4; ++q)
    {
        if (q % 4 == 0) continue;
        const float x = bounds.getX() + (q * 0.25f) * beatW;
        g.drawVerticalLine(juce::roundToInt(x),
                           (float) bounds.getY(), (float) bounds.getBottom());
    }

    // Notes.
    if (clip_ != nullptr)
    {
        for (auto* n : clip_->getSequence().getNotes())
        {
            const int pitch = n->getNoteNumber();
            if (pitch < lowPitch_ || pitch >= highPitch_) continue;
            const float x = bounds.getX() + n->getStartBeat().inBeats() * beatW;
            const float w = juce::jmax(2.0f, (float) n->getLengthBeats().inBeats() * beatW);
            const float y = bounds.getBottom() - (pitch - lowPitch_ + 1) * rowH + 1.0f;
            const juce::Rectangle<float> r { x, y, w, rowH - 2.0f };

            const bool isSelected = selectedNoteIds_.count(keyOf(*n)) > 0;
            g.setColour(isSelected
                ? UiTheme::kTitlebarAccent.brighter(0.5f)
                : UiTheme::kTitlebarAccent);
            g.fillRoundedRectangle(r, 2.0f);
            g.setColour(isSelected
                ? juce::Colours::white
                : UiTheme::kTitlebarAccent.brighter(0.3f));
            g.drawRoundedRectangle(r, 2.0f, isSelected ? 2.0f : 1.0f);
        }
    }

    // Playhead.
    if (playheadBeat_ >= 0.0 && viewBeats_ > 0.0)
    {
        const double wrapped = std::fmod(playheadBeat_, viewBeats_);
        const float px = bounds.getX() + static_cast<float>(wrapped) * beatW;
        g.setColour(juce::Colours::red.withAlpha(0.8f));
        g.drawVerticalLine(juce::roundToInt(px),
                           (float) bounds.getY(), (float) bounds.getBottom());
    }

    // Cursor indicator.
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    const float cx = bounds.getX() + cursor_.beat * beatW;
    const float cy = bounds.getBottom() - (cursor_.pitch - lowPitch_ + 1) * rowH;
    g.drawRect(cx, cy, juce::jmax(4.0f, 0.25f * beatW), rowH - 1, 1.5f);
}
