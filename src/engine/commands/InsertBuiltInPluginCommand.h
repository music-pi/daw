#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class AudioEngine;

/** Undoable insertion of one of Tracktion Engine's native plugins.

    External effects use InsertEffectPluginCommand because they are created
    from a JUCE PluginDescription. Native effects are identified by their TE
    XML type and restored directly from their ValueTree on redo. */
class InsertBuiltInPluginCommand : public juce::UndoableAction
{
public:
    InsertBuiltInPluginCommand(AudioEngine& engine,
                               te::PluginList& targetList,
                               juce::String pluginType,
                               int insertIndex);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Insert Built-in Effect"; }

private:
    AudioEngine& engine_;
    te::PluginList& targetList_;
    const juce::String pluginType_;
    const int insertIndex_;
    te::Plugin::Ptr plugin_;
    juce::ValueTree savedPluginState_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertBuiltInPluginCommand)
};
