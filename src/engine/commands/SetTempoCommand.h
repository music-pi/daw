#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class SetTempoCommand : public juce::UndoableAction
{
public:
    SetTempoCommand(te::Edit& edit, double oldTempo, double newTempo);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Set Tempo"; }

    // For coalescence of consecutive tempo changes
    UndoableAction* createCoalescedAction(UndoableAction* nextAction) override;

private:
    te::Edit& edit;
    const double oldTempo;
    const double newTempo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetTempoCommand)
};
