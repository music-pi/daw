#include "HardwareState.h"

#include <juce_core/juce_core.h>

HardwareState::HardwareState()
{
    auto registerAll = [this](const auto& ids, LedType type) {
        for (const auto& id : ids)
            registerResource(id, type);
    };

    registerAll(ResourceIds::PadLeds,       LedType::Indexed);
    registerAll(ResourceIds::GroupLeds,      LedType::Indexed);
    registerAll(ResourceIds::LeftOptions,    LedType::Mono);
    registerAll(ResourceIds::RightOptions,   LedType::Mono);
    registerAll(ResourceIds::LeftKnobs,      LedType::Mono);
    registerAll(ResourceIds::RightKnobs,     LedType::Mono);
    registerAll(ResourceIds::TransportLeds,  LedType::Mono);
    registerAll(ResourceIds::TouchStrips,    LedType::Indexed);
    registerAll(ResourceIds::NavLeds,        LedType::Indexed);
    registerAll(ResourceIds::ArrowLeds,      LedType::Mono);
    registerAll(ResourceIds::ModifierLeds,   LedType::Mono);
    registerAll(ResourceIds::ScreenButtons,  LedType::Button);
}

void HardwareState::registerResource(const std::string& id, LedType type)
{
    ResourceEntry entry;
    entry.ledType = type;
    resources_[id] = entry;
}

const HardwareState::ResourceEntry& HardwareState::getEntry(const std::string& id) const
{
    auto it = resources_.find(id);
    if (it == resources_.end())
        throw std::runtime_error("Unknown resource '" + id + "'");
    return it->second;
}

HardwareState::ResourceEntry& HardwareState::getEntry(const std::string& id)
{
    auto it = resources_.find(id);
    if (it == resources_.end())
        throw std::runtime_error("Unknown resource '" + id + "'");
    return it->second;
}

// --- Ownership ---

void HardwareState::claim(const std::string& resourceId, const std::string& ownerId)
{
    auto& entry = getEntry(resourceId);
    if (!entry.owner.empty() && entry.owner != ownerId)
    {
        throw std::runtime_error(
            "Resource '" + resourceId + "' already owned by '" + entry.owner
            + "', cannot claim for '" + ownerId + "'");
    }
    entry.owner = ownerId;
}

void HardwareState::release(const std::string& resourceId, const std::string& ownerId)
{
    auto& entry = getEntry(resourceId);
    if (entry.owner != ownerId)
    {
        throw std::runtime_error(
            "Resource '" + resourceId + "' is owned by '" + entry.owner
            + "', cannot release for '" + ownerId + "'");
    }
    entry.owner.clear();
    if (entry.value != 0)
        entry.dirty = true;  // flush will send the off-state
    entry.value = 0;
}

void HardwareState::releaseAll(const std::string& ownerId)
{
    for (auto& [id, entry] : resources_)
    {
        if (entry.owner == ownerId)
        {
            entry.owner.clear();
            if (entry.value != 0)
                entry.dirty = true;
            entry.value = 0;
        }
    }
}

void HardwareState::claimSet(ResourceSet set, const std::string& ownerId)
{
    for (const auto& id : getResourceIds(set))
        claim(id, ownerId);
}

void HardwareState::releaseSet(ResourceSet set, const std::string& ownerId)
{
    for (const auto& id : getResourceIds(set))
        release(id, ownerId);
}

void HardwareState::forceRelease(const std::string& resourceId, bool warn)
{
    auto& entry = getEntry(resourceId);
    if (warn && !entry.owner.empty())
    {
        juce::Logger::writeToLog(
            juce::String("[HardwareState] WARNING: force-releasing resource '")
            + resourceId + "' from owner '" + entry.owner + "'");
    }
    entry.owner.clear();
    if (entry.value != 0)
        entry.dirty = true;
    entry.value = 0;
}

// --- Queries ---

std::string HardwareState::getOwner(const std::string& resourceId) const
{
    return getEntry(resourceId).owner;
}

bool HardwareState::isClaimed(const std::string& resourceId) const
{
    return !getEntry(resourceId).owner.empty();
}

// --- LED values ---

void HardwareState::setLed(const std::string& resourceId, uint8_t value, const std::string& ownerId)
{
    auto& entry = getEntry(resourceId);
    if (entry.owner != ownerId)
        return;

    // Widgets publish their desired hardware state from the shared UI tick.
    // Treating an unchanged value as dirty turns that inexpensive state poll
    // into a synchronous USB write every 16 ms. Explicit retries and device
    // resynchronisation remain available through markSetDirty().
    if (entry.value == value)
        return;

    entry.value = value;
    entry.dirty = true;
}

uint8_t HardwareState::getLed(const std::string& resourceId) const
{
    return getEntry(resourceId).value;
}

// --- Dirty tracking ---

std::vector<std::pair<std::string, uint8_t>> HardwareState::getDirtyResources() const
{
    std::vector<std::pair<std::string, uint8_t>> result;
    for (const auto& [id, entry] : resources_)
    {
        if (entry.dirty)
            result.emplace_back(id, entry.value);
    }
    return result;
}

void HardwareState::clearDirty()
{
    for (auto& [id, entry] : resources_)
        entry.dirty = false;
}

void HardwareState::markSetDirty(ResourceSet set)
{
    for (const auto& id : getResourceIds(set))
    {
        auto it = resources_.find(id);
        if (it != resources_.end())
            it->second.dirty = true;
    }
}

// --- Flush ---

void HardwareState::flush(FlushTarget& target)
{
    bool batchStarted = false;
    for (auto& [id, entry] : resources_)
    {
        if (!entry.dirty)
            continue;

        if (!batchStarted)
        {
            target.beginLedBatch();
            batchStarted = true;
        }

        switch (entry.ledType)
        {
            case LedType::Mono:    target.setMonoLed(id, entry.value); break;
            case LedType::Indexed: target.setIndexedLed(id, entry.value); break;
            case LedType::Button:  target.setButtonBrightness(id, entry.value); break;
        }

        entry.dirty = false;
    }

    if (batchStarted)
        target.endLedBatch();
}

// --- Resource set lookup ---

std::vector<std::string> HardwareState::getResourceIds(ResourceSet set)
{
    auto toVec = [](const auto& arr) {
        return std::vector<std::string>(arr.begin(), arr.end());
    };

    switch (set)
    {
        case ResourceSet::PadLeds:       return toVec(ResourceIds::PadLeds);
        case ResourceSet::GroupLeds:     return toVec(ResourceIds::GroupLeds);
        case ResourceSet::LeftOptions:   return toVec(ResourceIds::LeftOptions);
        case ResourceSet::RightOptions:  return toVec(ResourceIds::RightOptions);
        case ResourceSet::LeftKnobs:     return toVec(ResourceIds::LeftKnobs);
        case ResourceSet::RightKnobs:    return toVec(ResourceIds::RightKnobs);
        case ResourceSet::TransportLeds: return toVec(ResourceIds::TransportLeds);
        case ResourceSet::TouchStrips:   return toVec(ResourceIds::TouchStrips);
        case ResourceSet::NavLeds:       return toVec(ResourceIds::NavLeds);
        case ResourceSet::ScreenButtons: return toVec(ResourceIds::ScreenButtons);
    }
    return {};
}
