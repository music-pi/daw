#include "InsertEffectPluginCommand.h"
#include "../AudioEngine.h"

InsertEffectPluginCommand::InsertEffectPluginCommand(AudioEngine& e,
                                                    te::PluginList& l,
                                                    juce::PluginDescription d,
                                                    int idx)
    : engine_(e), targetList_(l), desc_(std::move(d)), insertIndex_(idx) {}

bool InsertEffectPluginCommand::perform()
{
    if (insertedPlugin_ != nullptr)
    {
        if (! pluginParentState_.isValid() || pluginTreeIndex_ < 0)
            return false;
        pluginParentState_.addChild(
            insertedPlugin_->state, pluginTreeIndex_, nullptr);
        return true;
    }

    auto& cache = engine_.getEdit()->getPluginCache();

    auto plugin = cache.createNewPlugin(te::ExternalPlugin::xmlTypeName, desc_);

    if (plugin == nullptr)
        return false;

    const auto existingPlugins = targetList_.getPlugins();
    if (! existingPlugins.isEmpty())
        pluginParentState_ = existingPlugins.getFirst()->state.getParent();
    else if (auto* track = targetList_.getOwnerTrack())
        pluginParentState_ = track->state;
    else if (auto* clip = targetList_.getOwnerClip())
        pluginParentState_ = clip->state;
    else
        pluginParentState_ = engine_.getEdit()->state.getChildWithName(
            te::IDs::MASTERPLUGINS);

    if (! pluginParentState_.isValid())
        return false;

    if (juce::isPositiveAndBelow(insertIndex_, existingPlugins.size()))
        pluginTreeIndex_ = pluginParentState_.indexOf(
            existingPlugins[insertIndex_]->state);
    else if (! existingPlugins.isEmpty())
        pluginTreeIndex_ = pluginParentState_.indexOf(
            existingPlugins.getLast()->state) + 1;
    else
        pluginTreeIndex_ = -1;

    pluginParentState_.addChild(plugin->state, pluginTreeIndex_, nullptr);
    insertedPlugin_ = plugin;
    pluginTreeIndex_ = pluginParentState_.indexOf(plugin->state);
    return true;
}

bool InsertEffectPluginCommand::undo()
{
    auto plugins = targetList_.getPlugins();
    if (insertIndex_ < 0 || insertIndex_ >= plugins.size())
        return false;

    auto plugin = plugins[insertIndex_];
    if (plugin == nullptr)
        return false;
    pluginParentState_.removeChild(plugin->state, nullptr);
    return true;
}
