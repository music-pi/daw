#include "GroupManager.h"

#include "AudioEngine.h"

#include <juce_core/juce_core.h>

#include <random>

GroupManager::GroupManager(AudioEngine& engine)
    : audioEngine(engine)
{
    groups[0].exists = true;
    groups[0].isActive = true;
    groups[0].color = generateRandomColor();
}

bool GroupManager::saveCurrentState(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex))
        return false;

    if (!groups[static_cast<size_t>(groupIndex)].exists)
        createGroup(groupIndex);

    return recallGroup(groupIndex);
}

bool GroupManager::recallGroup(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex)
        || !groups[static_cast<size_t>(groupIndex)].exists
        || audioEngine.getSamplerForGroup(groupIndex) == nullptr)
        return false;

    if (activeGroupIndex == groupIndex)
        return true;

    audioEngine.listeners_.call(
        [](AudioEngine::Listener& listener) { listener.activeSamplerAboutToChange(); });

    if (isValidGroupIndex(activeGroupIndex))
    {
        groups[static_cast<size_t>(activeGroupIndex)].isActive = false;
        updateGroupState(activeGroupIndex);
    }

    activeGroupIndex = groupIndex;
    groups[static_cast<size_t>(groupIndex)].isActive = true;
    updateGroupState(groupIndex);
    updateActiveGroupState();
    audioEngine.listeners_.call(
        [](AudioEngine::Listener& listener) { listener.activeSamplerChanged(); });
    return true;
}

bool GroupManager::isGroupActive(int groupIndex) const noexcept
{
    return isValidGroupIndex(groupIndex)
        && groups[static_cast<size_t>(groupIndex)].isActive;
}

bool GroupManager::hasGroupData(int groupIndex) const noexcept
{
    return isValidGroupIndex(groupIndex)
        && groups[static_cast<size_t>(groupIndex)].exists;
}

uint8_t GroupManager::getGroupColor(int groupIndex) const noexcept
{
    if (!isValidGroupIndex(groupIndex))
        return 0;
    return groups[static_cast<size_t>(groupIndex)].color;
}

void GroupManager::createGroup(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex))
        return;

    auto& group = groups[static_cast<size_t>(groupIndex)];
    if (!group.exists && !audioEngine.createSamplerGroup(groupIndex))
        return;

    group.exists = true;
    if (group.color == 0)
        group.color = generateRandomColor();
    updateGroupState(groupIndex);
    recallGroup(groupIndex);
}

void GroupManager::clearGroup(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex)
        || !groups[static_cast<size_t>(groupIndex)].exists)
        return;

    const int fallback = findFallbackGroup(groupIndex);
    if (fallback < 0)
        return; // The engine always keeps one editable pad bank.

    if (activeGroupIndex == groupIndex)
        recallGroup(fallback);

    audioEngine.removeSamplerGroup(groupIndex);
    auto& group = groups[static_cast<size_t>(groupIndex)];
    group.exists = false;
    group.isActive = false;
    group.color = 0;
    updateGroupState(groupIndex);
}

void GroupManager::setGroupColor(int groupIndex, uint8_t color)
{
    if (!isValidGroupIndex(groupIndex)
        || !groups[static_cast<size_t>(groupIndex)].exists)
        return;

    groups[static_cast<size_t>(groupIndex)].color = normalizeColor(color);
    updateGroupState(groupIndex);
}

uint8_t GroupManager::generateRandomColor() const
{
    static thread_local std::mt19937 rng { std::random_device{}() };
    std::uniform_int_distribution<int> dist(0, kColorCount - 1);
    return colorAt(dist(rng));
}

uint8_t GroupManager::normalizeColor(uint8_t color) noexcept
{
    const int index = colorIndex(color);
    return index >= 0 ? colorAt(index) : colorAt(0);
}

bool GroupManager::isValidGroupIndex(int groupIndex) const noexcept
{
    return groupIndex >= 0 && groupIndex < kGroupCount;
}

void GroupManager::refreshStateBinding()
{
    groupsStateNode = audioEngine.getGroupsState();

    for (auto& group : groups)
        group = {};

    int storedActive = 0;
    if (groupsStateNode.isValid())
        storedActive = static_cast<int>(
            groupsStateNode.getProperty("activeGroupIndex", 0));

    ensureGroupNodes();

    for (int i = 0; i < kGroupCount; ++i)
    {
        const auto node = getGroupNode(i);
        auto& group = groups[static_cast<size_t>(i)];
        group.exists = node.getProperty("hasData", i == 0);
        const int storedColor = static_cast<int>(
            node.getProperty("color", static_cast<int>(colorAt(i % kColorCount))));
        group.color = group.exists
            ? normalizeColor(static_cast<uint8_t>(juce::jlimit(0, 255, storedColor)))
            : 0;
    }

    if (!isValidGroupIndex(storedActive)
        || !groups[static_cast<size_t>(storedActive)].exists)
        storedActive = 0;

    groups[0].exists = true;
    if (groups[0].color == 0)
        groups[0].color = generateRandomColor();

    activeGroupIndex = storedActive;
    groups[static_cast<size_t>(activeGroupIndex)].isActive = true;

    for (int i = 0; i < kGroupCount; ++i)
    {
        if (groups[static_cast<size_t>(i)].exists)
            audioEngine.createSamplerGroup(i);
        updateGroupState(i);
    }
    updateActiveGroupState();
}

void GroupManager::ensureGroupNodes()
{
    if (!groupsStateNode.isValid())
        return;

    for (int i = 0; i < kGroupCount; ++i)
        ensureGroupNode(i);
}

juce::ValueTree GroupManager::getGroupNode(int groupIndex) const
{
    if (!groupsStateNode.isValid() || !isValidGroupIndex(groupIndex))
        return {};

    for (int i = 0; i < groupsStateNode.getNumChildren(); ++i)
    {
        auto child = groupsStateNode.getChild(i);
        if (child.hasType("group")
            && static_cast<int>(child.getProperty("index", -1)) == groupIndex)
            return child;
    }
    return {};
}

juce::ValueTree GroupManager::ensureGroupNode(int groupIndex)
{
    auto node = getGroupNode(groupIndex);
    if (node.isValid() || !groupsStateNode.isValid())
        return node;

    node = juce::ValueTree("group");
    node.setProperty("index", groupIndex, nullptr);
    node.setProperty("color", 0, nullptr);
    node.setProperty("hasData", groupIndex == 0, nullptr);
    node.setProperty("isActive", groupIndex == 0, nullptr);
    groupsStateNode.addChild(node, -1, nullptr);
    return node;
}

void GroupManager::updateGroupState(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex))
        return;

    auto node = ensureGroupNode(groupIndex);
    if (!node.isValid())
        return;

    const auto& group = groups[static_cast<size_t>(groupIndex)];
    node.setProperty("color", group.color, nullptr);
    node.setProperty("hasData", group.exists, nullptr);
    node.setProperty("isActive", group.isActive, nullptr);
}

void GroupManager::updateActiveGroupState()
{
    if (groupsStateNode.isValid())
        groupsStateNode.setProperty("activeGroupIndex", activeGroupIndex, nullptr);
}

int GroupManager::findFallbackGroup(int excluding) const noexcept
{
    for (int i = 0; i < kGroupCount; ++i)
        if (i != excluding && groups[static_cast<size_t>(i)].exists)
            return i;
    return -1;
}
