#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

struct ChannelDescriptor
{
    enum class Kind { Folder, Track, Master };

    juce::String id;
    juce::String name;
    juce::String subLabel;
    Kind kind { Kind::Track };

    te::Track* track { nullptr };
    te::FolderTrack* folder { nullptr };
    te::VolumeAndPanPlugin* volume { nullptr };
    te::LevelMeterPlugin* meter { nullptr };

    bool canDrillDown { false };

    bool operator==(const ChannelDescriptor& other) const { return id == other.id; }
};
