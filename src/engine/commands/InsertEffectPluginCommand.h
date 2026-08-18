#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class AudioEngine;

class InsertEffectPluginCommand : public juce::UndoableAction
{
public:
    InsertEffectPluginCommand(AudioEngine& engine,
                              te::PluginList& targetList,
                              juce::PluginDescription desc,
                              int insertIndex);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Insert Effect Plugin"; }

private:
    AudioEngine& engine_;
    te::PluginList& targetList_;
    const juce::PluginDescription desc_;
    const int insertIndex_;
    te::Plugin::Ptr insertedPlugin_;
    juce::ValueTree pluginParentState_;
    int pluginTreeIndex_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertEffectPluginCommand)
};
