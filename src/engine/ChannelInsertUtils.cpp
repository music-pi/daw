#include "ChannelInsertUtils.h"

namespace ChannelInsertUtils
{
bool isInfrastructurePlugin(const te::Plugin* plugin)
{
    if (plugin == nullptr)
        return true;

    return dynamic_cast<const te::VolumeAndPanPlugin*>(plugin) != nullptr
        || dynamic_cast<const te::LevelMeterPlugin*>(plugin) != nullptr
        || dynamic_cast<const te::AuxSendPlugin*>(plugin) != nullptr;
}

int insertionIndex(const te::PluginList& list)
{
    const auto plugins = list.getPlugins();
    for (int i = 0; i < plugins.size(); ++i)
    {
        if (isInfrastructurePlugin(plugins[i].get()))
            return i;
    }

    return plugins.size();
}

te::EqualiserPlugin* findEqualiser(const te::PluginList& list)
{
    for (auto* plugin : list.getPlugins())
    {
        if (auto* equaliser = dynamic_cast<te::EqualiserPlugin*>(plugin))
            return equaliser;
    }

    return nullptr;
}
}
