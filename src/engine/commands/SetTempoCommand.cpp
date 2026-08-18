#include "SetTempoCommand.h"

SetTempoCommand::SetTempoCommand(te::Edit& edit, double oldTempo, double newTempo)
    : edit(edit)
    , oldTempo(oldTempo)
    , newTempo(newTempo)
{
}

bool SetTempoCommand::perform()
{
    auto& transport = edit.getTransport();
    const auto position = transport.getPosition();
    edit.tempoSequence.getTempoAt(position).setBpm(newTempo);
    return true;
}

bool SetTempoCommand::undo()
{
    auto& transport = edit.getTransport();
    const auto position = transport.getPosition();
    edit.tempoSequence.getTempoAt(position).setBpm(oldTempo);
    return true;
}

juce::UndoableAction* SetTempoCommand::createCoalescedAction(UndoableAction* nextAction)
{
    if (auto* next = dynamic_cast<SetTempoCommand*>(nextAction))
    {
        if (&next->edit == &edit)
        {
            // Merge: keep our oldTempo, take next's newTempo
            return new SetTempoCommand(edit, oldTempo, next->newTempo);
        }
    }
    return nullptr;
}
