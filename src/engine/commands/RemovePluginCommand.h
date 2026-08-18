#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class AudioEngine;

class RemovePluginCommand : public juce::UndoableAction
{
public:
    RemovePluginCommand(AudioEngine& engine,
                       te::PluginList& targetList,
                       int pluginIndex);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Remove Plugin"; }

private:
    AudioEngine& engine_;
    te::PluginList& targetList_;
    const int pluginIndex_;
    juce::ValueTree pluginParentState_;
    int pluginTreeIndex_ { -1 };
    te::Plugin::Ptr removedPlugin_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemovePluginCommand)
};
