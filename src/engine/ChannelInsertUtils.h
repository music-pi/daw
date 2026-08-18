#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace ChannelInsertUtils
{
/** Returns true for plugins owned by the channel strip rather than the user
    insert chain. */
bool isInfrastructurePlugin(const te::Plugin* plugin);

/** Returns the slot immediately before the strip's volume/meter/send
    infrastructure. Existing user effects remain in their current order. */
int insertionIndex(const te::PluginList& list);

/** Finds the first native Tracktion equaliser in a processing list. */
te::EqualiserPlugin* findEqualiser(const te::PluginList& list);
}
