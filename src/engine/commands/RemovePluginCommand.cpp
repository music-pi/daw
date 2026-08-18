#include "RemovePluginCommand.h"
#include "../AudioEngine.h"

RemovePluginCommand::RemovePluginCommand(AudioEngine& e, te::PluginList& l, int idx)
    : engine_(e), targetList_(l), pluginIndex_(idx) {}

bool RemovePluginCommand::perform()
{
    auto plugins = targetList_.getPlugins();
    if (pluginIndex_ < 0 || pluginIndex_ >= plugins.size())
        return false;

    auto p = plugins[pluginIndex_];
    if (p == nullptr)
        return false;
    removedPlugin_ = p;
    pluginParentState_ = p->state.getParent();
    pluginTreeIndex_ = pluginParentState_.indexOf(p->state);
    if (! pluginParentState_.isValid() || pluginTreeIndex_ < 0)
        return false;
    pluginParentState_.removeChild(p->state, nullptr);
    return true;
}

bool RemovePluginCommand::undo()
{
    if (removedPlugin_ == nullptr || ! pluginParentState_.isValid()
        || pluginTreeIndex_ < 0)
        return false;

    pluginParentState_.addChild(
        removedPlugin_->state, pluginTreeIndex_, nullptr);
    return true;
}
