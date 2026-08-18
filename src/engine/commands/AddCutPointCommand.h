#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <vector>

/**
 * Undoable command to add/remove a cut point in a sorted cut point list.
 * Owns its own copy of the cut points — pushes state back via onChange callback.
 */
class AddCutPointCommand : public juce::UndoableAction
{
public:
    AddCutPointCommand(std::vector<double> cutPointsCopy,
                       double cutPosition,
                       double sampleRate,
                       double totalSeconds,
                       std::function<void(const std::vector<double>&)> onChange);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Add Cut Point"; }

private:
    std::vector<double> cutPoints;
    double cutPosition;
    // sampleRate/totalSeconds/onChange are set once in the ctor and never rewritten.
    const double sampleRate;
    const double totalSeconds;
    const std::function<void(const std::vector<double>&)> onChange;
    bool performed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AddCutPointCommand)
};
