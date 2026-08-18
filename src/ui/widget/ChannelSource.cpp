#include "ChannelSource.h"

#include <algorithm>

namespace
{
    te::LevelMeterPlugin* findLevelMeterPlugin(te::Track& track)
    {
        for (auto* p : track.pluginList.getPlugins())
            if (auto* meter = dynamic_cast<te::LevelMeterPlugin*>(p))
                return meter;
        return nullptr;
    }

    ChannelDescriptor describeFolder(te::FolderTrack& folder)
    {
        ChannelDescriptor d;
        d.id = folder.itemID.toString();
        d.name = folder.getName();
        d.kind = ChannelDescriptor::Kind::Folder;
        d.track = &folder;
        d.folder = &folder;
        d.volume = folder.getVolumePlugin();
        d.meter = findLevelMeterPlugin(folder);  // nullable — engine owns provisioning
        d.canDrillDown = true;
        return d;
    }

    ChannelDescriptor describeAudioTrack(te::AudioTrack& track)
    {
        ChannelDescriptor d;
        d.id = track.itemID.toString();
        d.name = track.getName();
        d.kind = ChannelDescriptor::Kind::Track;
        d.track = &track;
        d.volume = track.getVolumePlugin();
        d.meter = findLevelMeterPlugin(track);
        return d;
    }

    ChannelDescriptor describeMaster(te::Edit& edit)
    {
        ChannelDescriptor d;
        d.id = "master";
        d.name = "Master";
        d.kind = ChannelDescriptor::Kind::Master;
        d.volume = edit.getMasterVolumePlugin().get();

        // The master audio bus runs through edit.getMasterPluginList(), not
        // MasterTrack::pluginList — see AudioEngine::installEdit comment.
        for (auto* p : edit.getMasterPluginList().getPlugins())
        {
            if (auto* meter = dynamic_cast<te::LevelMeterPlugin*>(p))
            {
                d.meter = meter;
                break;
            }
        }
        return d;
    }
}

GlobalChannelSource::GlobalChannelSource(te::Edit& edit)
    : edit_(edit)
{}

std::vector<ChannelDescriptor> GlobalChannelSource::enumerate() const
{
    std::vector<ChannelDescriptor> out;

    // Master first (leftmost).
    out.push_back(describeMaster(edit_));

    for (auto* track : te::getTopLevelTracks(edit_))
    {
        if (auto* folder = dynamic_cast<te::FolderTrack*>(track))
            out.push_back(describeFolder(*folder));
    }

    for (auto* track : te::getTopLevelTracks(edit_))
    {
        if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track))
            out.push_back(describeAudioTrack(*audioTrack));
    }

    return out;
}

GroupChannelSource::GroupChannelSource(te::Edit& edit, te::EditItemID folderId)
    : edit_(edit), folderId_(folderId)
{}

std::vector<ChannelDescriptor> GroupChannelSource::enumerate() const
{
    std::vector<ChannelDescriptor> out;

    auto* track = te::findTrackForID(edit_, folderId_);
    auto* folder = dynamic_cast<te::FolderTrack*>(track);
    if (folder == nullptr)
        return out;

    struct ChildEntry
    {
        te::AudioTrack* track;
        int padIndex;
        int instrumentSlot;
    };
    std::vector<ChildEntry> children;

    for (auto* child : folder->getInputTracks())
    {
        if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(child))
        {
            if (audioTrack->getName().startsWith("Sequencer"))
                continue;
            const int padIndex = static_cast<int>(audioTrack->state.getProperty("padIndex", -1));
            const int instrumentSlot = static_cast<int>(
                audioTrack->state.getProperty("instrumentSlot", -1));
            children.push_back({ audioTrack, padIndex, instrumentSlot });
        }
    }

    // Keep both sampler pads and keyboard-instrument slots in their stable,
    // user-facing order. Untagged tracks remain last in edit order.
    std::stable_sort(children.begin(), children.end(),
        [](const ChildEntry& a, const ChildEntry& b) {
            const int aIndex = a.padIndex >= 0 ? a.padIndex : a.instrumentSlot;
            const int bIndex = b.padIndex >= 0 ? b.padIndex : b.instrumentSlot;
            if (aIndex < 0 && bIndex < 0) return false;
            if (aIndex < 0) return false;
            if (bIndex < 0) return true;
            return aIndex < bIndex;
        });

    for (const auto& entry : children)
        out.push_back(describeAudioTrack(*entry.track));

    return out;
}
