#include "MidiStepView.h"

#include <cmath>

MidiStepView::MidiStepView (te::MidiList& list, int rootPitch, int stepsPerBar, int bars)
    : readList_(&list), writeList_(&list), rootPitch_(rootPitch), stepsPerBar_(stepsPerBar), bars_(bars)
{
    jassert (stepsPerBar_ > 0 && bars_ > 0);
}

MidiStepView::MidiStepView (const te::MidiList& list, int rootPitch, int stepsPerBar, int bars)
    : readList_(&list), writeList_(nullptr), rootPitch_(rootPitch), stepsPerBar_(stepsPerBar), bars_(bars)
{
    jassert (stepsPerBar_ > 0 && bars_ > 0);
}

tracktion::BeatDuration MidiStepView::stepDuration() const
{
    // 1 bar = 4 beats
    return tracktion::BeatDuration::fromBeats (4.0 / stepsPerBar_);
}

tracktion::BeatPosition MidiStepView::stepBeatPosition (int stepIndex) const
{
    return tracktion::BeatPosition::fromBeats (stepIndex * stepDuration().inBeats());
}

te::MidiNote* MidiStepView::findRootNoteAtStep (int stepIndex) const
{
    const auto target = stepBeatPosition (stepIndex).inBeats();
    const double tolerance = stepDuration().inBeats() * 0.5;
    for (auto* note : readList_->getNotes())
    {
        if (note->getNoteNumber() != rootPitch_)
            continue;
        if (std::abs (note->getStartBeat().inBeats() - target) < tolerance)
            return note;
    }
    return nullptr;
}

bool MidiStepView::isStepOn (int stepIndex) const
{
    return findRootNoteAtStep (stepIndex) != nullptr;
}

int MidiStepView::getStepVelocity (int stepIndex) const
{
    if (auto* note = findRootNoteAtStep (stepIndex))
        return note->getVelocity();
    return 0;
}

void MidiStepView::setStep (int stepIndex, bool on, int velocity)
{
    jassert (writeList_ != nullptr && "setStep called on a read-only MidiStepView");
    if (writeList_ == nullptr)
        return;

    if (stepIndex < 0 || stepIndex >= getNumSteps())
        return;

    auto* existing = findRootNoteAtStep (stepIndex);

    if (! on)
    {
        if (existing)
            writeList_->removeNote (*existing, nullptr);
        return;
    }

    if (existing)
    {
        existing->setVelocity (juce::jlimit (0, 127, velocity), nullptr);
        return;
    }

    writeList_->addNote (rootPitch_,
                         stepBeatPosition (stepIndex),
                         tracktion::BeatDuration::fromBeats (stepDuration().inBeats() * 0.5),
                         juce::jlimit (0, 127, velocity),
                         /*colour*/ 0,
                         nullptr);
}
