#pragma once

#include <vector>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "../components/mixer/ChannelDescriptor.h"

namespace te = tracktion::engine;

class ChannelSource
{
public:
    virtual ~ChannelSource() = default;
    virtual std::vector<ChannelDescriptor> enumerate() const = 0;
};

class GlobalChannelSource : public ChannelSource
{
public:
    explicit GlobalChannelSource(te::Edit& edit);
    std::vector<ChannelDescriptor> enumerate() const override;

private:
    te::Edit& edit_;
};

class GroupChannelSource : public ChannelSource
{
public:
    GroupChannelSource(te::Edit& edit, te::EditItemID folderId);
    std::vector<ChannelDescriptor> enumerate() const override;

private:
    te::Edit& edit_;
    te::EditItemID folderId_;
};
