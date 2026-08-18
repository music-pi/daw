#include "InsertBuiltInPluginCommand.h"

#include "../AudioEngine.h"

InsertBuiltInPluginCommand::InsertBuiltInPluginCommand(AudioEngine& engine,
                                                       te::PluginList& targetList,
                                                       juce::String pluginType,
                                                       int insertIndex)
    : engine_(engine),
      targetList_(targetList),
      pluginType_(std::move(pluginType)),
      insertIndex_(insertIndex)
{
}

bool InsertBuiltInPluginCommand::perform()
{
    auto* edit = engine_.getEdit();
    if (edit == nullptr)
        return false;

    auto& cache = edit->getPluginCache();
    plugin_ = savedPluginState_.isValid()
                ? cache.createNewPlugin(savedPluginState_)
                : cache.createNewPlugin(pluginType_, {});
    if (plugin_ == nullptr)
        return false;

    const int resolvedIndex = juce::jlimit(0, targetList_.getPlugins().size(), insertIndex_);
    targetList_.insertPlugin(plugin_, resolvedIndex, nullptr);
    if (plugin_->state.getParent() != targetList_.state)
        return false;
    plugin_->flushPluginStateToValueTree();
    savedPluginState_ = plugin_->state.createCopy();
    return true;
}

bool InsertBuiltInPluginCommand::undo()
{
    if (plugin_ == nullptr || plugin_->state.getParent() != targetList_.state)
        return false;

    plugin_->flushPluginStateToValueTree();
    savedPluginState_ = plugin_->state.createCopy();
    plugin_->deleteFromParent();
    plugin_ = nullptr;
    return true;
}
