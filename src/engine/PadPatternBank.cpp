#include "PadPatternBank.h"

namespace
{
    // Placeholder time range on the slot clip — pattern length is carried by
    // the notes inside the sequence, not by the clip's edit-time range.
    te::MidiClip* ensureMidiClip (te::ClipSlot& slot)
    {
        if (auto* existing = dynamic_cast<te::MidiClip*>(slot.getClip()))
            return existing;

        const auto range = tracktion::core::TimeRange {
            tracktion::core::TimePosition{},
            tracktion::core::TimePosition::fromSeconds (1.0) };
        auto clip = te::insertMIDIClip (slot, range);
        return dynamic_cast<te::MidiClip*> (clip.get());
    }
}

void PadPatternBank::attach (te::AudioTrack& track) { track_ = &track; }
void PadPatternBank::detach() { track_ = nullptr; }

int PadPatternBank::getNumPatterns() const
{
    if (track_ == nullptr) return 0;
    return track_->getClipSlotList().getClipSlots().size();
}

int PadPatternBank::addPattern()
{
    if (track_ == nullptr) return -1;
    auto& list = track_->getClipSlotList();
    const int newIndex = list.getClipSlots().size();
    list.ensureNumberOfSlots (newIndex + 1);
    return newIndex;
}
void PadPatternBank::removePattern (int index)
{
    if (track_ == nullptr) return;
    auto slots = track_->getClipSlotList().getClipSlots();
    if (index < 0 || index >= slots.size()) return;
    track_->getClipSlotList().deleteSlot (*slots[index]);
}

void PadPatternBank::storePattern (int index, const te::MidiList& list)
{
    if (track_ == nullptr || index < 0) return;

    auto& slots = track_->getClipSlotList();
    slots.ensureNumberOfSlots (index + 1);

    auto* slot = slots.getClipSlots()[index];
    if (slot == nullptr) return;

    auto* clip = ensureMidiClip (*slot);
    if (clip == nullptr) return;

    auto& seq = clip->getSequence();
    while (seq.getNumNotes() > 0)
        seq.removeNote (*seq.getNotes()[0], nullptr);
    while (seq.getNumControllerEvents() > 0)
        seq.removeControllerEvent (*seq.getControllerEvents()[0], nullptr);

    for (auto* n : list.getNotes())
        seq.addNote (n->getNoteNumber(), n->getStartBeat(), n->getLengthBeats(),
                     n->getVelocity(), n->getColour(), nullptr);
}

void PadPatternBank::restorePattern (int index, te::MidiList& dest) const
{
    // Bounds-check BEFORE mutating dest: an earlier PadPatternStore regression
    // cleared dest first, silently erasing live patterns when a caller asked
    // for a slot that hadn't been created yet.
    if (track_ == nullptr) return;

    auto slots = track_->getClipSlotList().getClipSlots();
    if (index < 0 || index >= slots.size()) return;

    auto* clip = dynamic_cast<te::MidiClip*> (slots[index]->getClip());
    if (clip == nullptr) return;

    auto& src = clip->getSequence();

    while (dest.getNumNotes() > 0)
        dest.removeNote (*dest.getNotes()[0], nullptr);
    while (dest.getNumControllerEvents() > 0)
        dest.removeControllerEvent (*dest.getControllerEvents()[0], nullptr);

    for (auto* n : src.getNotes())
        dest.addNote (n->getNoteNumber(), n->getStartBeat(), n->getLengthBeats(),
                      n->getVelocity(), n->getColour(), nullptr);
}

void PadPatternBank::mutatePattern (int index, const std::function<void(te::MidiList&)>& fn)
{
    if (track_ == nullptr) return;
    auto slots = track_->getClipSlotList().getClipSlots();
    if (index < 0 || index >= slots.size()) return;

    auto* clip = ensureMidiClip (*slots[index]);
    if (clip == nullptr) return;

    fn (clip->getSequence());
}

void PadPatternBank::readPattern (int index, const std::function<void(const te::MidiList&)>& fn) const
{
    if (track_ == nullptr) return;
    auto slots = track_->getClipSlotList().getClipSlots();
    if (index < 0 || index >= slots.size()) return;

    // Don't create on read — a missing clip means an empty pattern, silently skip.
    auto* clip = dynamic_cast<te::MidiClip*> (slots[index]->getClip());
    if (clip == nullptr) return;

    fn (clip->getSequence());
}
