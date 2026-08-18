#include "GroupManager.h"

#include "SamplerInstrument.h"
#include "AudioEngine.h"
#include "commands/SetChokeGroupCommand.h"
#include "commands/SetGainDbCommand.h"

#include <juce_core/juce_core.h>
#include <random>

GroupManager::GroupManager(AudioEngine& engine, SamplerInstrument& sampler)
    : audioEngine(engine)
    , sampler_(sampler)
{
    // Initialize all groups with random colors
    for (int i = 0; i < kGroupCount; ++i)
        groups[i].color = generateRandomColor();

    bindStateTree();
    ensureGroupNodes();

    if (activeGroupIndex < 0 && kGroupCount > 0)
    {
        activeGroupIndex = 0;
        groups[0].isActive = true;
    }

    updateActiveGroupState(false);

    for (int i = 0; i < kGroupCount; ++i)
        updateGroupState(i, false);
}

bool GroupManager::saveCurrentState(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex))
        return false;

    // Capture current pad state
    std::vector<PadSnapshot> snapshots;
    const int padCount = sampler_.getPadCount();

    for (int i = 0; i < padCount; ++i)
    {
        if (const auto* pad = sampler_.getPad(i))
        {
            PadSnapshot snapshot;
            snapshot.index = i;
            snapshot.name = pad->name;
            snapshot.samplePath = pad->sampleFile.getFullPathName();
            for (const auto& layer : pad->sampleLayers)
            {
                snapshot.sampleLayerPaths.push_back(layer.file.getFullPathName());
                snapshot.layerGainsDb.push_back(layer.gainDb);
                snapshot.layerWeights.push_back(layer.randomWeight);
                snapshot.layerVelocityCurves.push_back(
                    static_cast<int>(layer.velocityCurve));
                snapshot.layerVelocityMinimums.push_back(
                    layer.velocityMinimum);
                snapshot.layerVelocityMaximums.push_back(
                    layer.velocityMaximum);
            }
            snapshot.gainDb = sampler_.getGainDb(i);
            snapshot.chokeGroup = pad->chokeGroup;
            snapshot.triggerMode = static_cast<int>(pad->triggerMode);
            snapshots.push_back(std::move(snapshot));
        }
    }

    groups[groupIndex].padSnapshots = std::move(snapshots);

    // Activate this group
    if (activeGroupIndex != groupIndex)
    {
        if (activeGroupIndex >= 0)
            groups[activeGroupIndex].isActive = false;
        activeGroupIndex = groupIndex;
        groups[groupIndex].isActive = true;
    }

    updateGroupState(groupIndex);
    updateActiveGroupState();
    return true;
}

bool GroupManager::recallGroup(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex))
        return false;

    if (!hasGroupData(groupIndex))
        return false;

    // Single undo transaction for the entire group recall: every
    // SamplerPlugin::add/removeSound inside clearSample/loadSample, every
    // SetGainDbCommand, and every SetChokeGroupCommand below coalesce into
    // one JUCE undo step. A single undo() reverts the whole recall.
    if (undoManager != nullptr)
        undoManager->beginNewTransaction("Recall Group " + juce::String(groupIndex + 1));

    // Save and deactivate current group if different
    if (activeGroupIndex >= 0 && activeGroupIndex != groupIndex)
    {
        saveCurrentState(activeGroupIndex);
        groups[activeGroupIndex].isActive = false;
    }

    // Clear all pads — TE records SamplerPlugin changes (add/removeSound) automatically.
    // Gain and choke group use UndoableActions to join the same transaction.
    for (int i = 0; i < sampler_.getPadCount(); ++i)
    {
        const float oldGain = sampler_.getGainDb(i);
        const int oldChoke = sampler_.getChokeGroup(i);

        sampler_.clearSample(i);

        if (undoManager != nullptr)
        {
            if (std::abs(oldGain) > 0.01f)
                undoManager->perform(new SetGainDbCommand(sampler_, i, oldGain, 0.0f));
            if (oldChoke != 0)
                undoManager->perform(new SetChokeGroupCommand(sampler_, i, oldChoke, 0));
        }
        else
        {
            sampler_.setGainDb(i, 0.0f);
            sampler_.setChokeGroupDirect(i, 0);
        }
    }

    // Restore pad state
    if (groups[groupIndex].padSnapshots.has_value())
    {
        for (const auto& snapshot : *groups[groupIndex].padSnapshots)
        {
            if (snapshot.index >= 0 && snapshot.index < sampler_.getPadCount())
            {
                juce::File sampleFile(snapshot.samplePath);
                if (sampleFile.existsAsFile())
                {
                    sampler_.loadSample(snapshot.index, sampleFile);
                    for (size_t layer = 1; layer < snapshot.sampleLayerPaths.size(); ++layer)
                    {
                        const juce::File layerFile(snapshot.sampleLayerPaths[layer]);
                        if (layerFile.existsAsFile())
                            sampler_.addSampleLayer(snapshot.index, layerFile);
                    }
                    for (size_t layer = 0; layer < snapshot.sampleLayerPaths.size(); ++layer)
                    {
                        if (layer < snapshot.layerGainsDb.size())
                            sampler_.setSampleLayerGainDb(
                                snapshot.index, static_cast<int>(layer),
                                snapshot.layerGainsDb[layer]);
                        if (layer < snapshot.layerWeights.size())
                            sampler_.setSampleLayerRandomWeight(
                                snapshot.index, static_cast<int>(layer),
                                snapshot.layerWeights[layer]);
                        if (layer < snapshot.layerVelocityCurves.size())
                            sampler_.setSampleLayerVelocityCurve(
                                snapshot.index, static_cast<int>(layer),
                                static_cast<SamplerInstrument::LayerVelocityCurve>(
                                    snapshot.layerVelocityCurves[layer]));
                        if (layer < snapshot.layerVelocityMinimums.size()
                            && layer < snapshot.layerVelocityMaximums.size())
                            sampler_.setSampleLayerVelocityRange(
                                snapshot.index, static_cast<int>(layer),
                                snapshot.layerVelocityMinimums[layer],
                                snapshot.layerVelocityMaximums[layer]);
                    }
                }
                sampler_.setTriggerModeDirect(
                    snapshot.index,
                    static_cast<SamplerInstrument::TriggerMode>(snapshot.triggerMode));

                if (undoManager != nullptr)
                {
                    const float curGain = sampler_.getGainDb(snapshot.index);
                    if (std::abs(curGain - snapshot.gainDb) > 0.01f)
                        undoManager->perform(new SetGainDbCommand(sampler_, snapshot.index, curGain, snapshot.gainDb));

                    const int curChoke = sampler_.getChokeGroup(snapshot.index);
                    if (curChoke != snapshot.chokeGroup)
                        undoManager->perform(new SetChokeGroupCommand(sampler_, snapshot.index, curChoke, snapshot.chokeGroup));
                }
                else
                {
                    sampler_.setGainDb(snapshot.index, snapshot.gainDb);
                    sampler_.setChokeGroupDirect(snapshot.index, snapshot.chokeGroup);
                }
            }
        }
    }

    // Activate this group
    activeGroupIndex = groupIndex;
    groups[groupIndex].isActive = true;

    updateGroupState(groupIndex);
    updateActiveGroupState();
    return true;
}

bool GroupManager::isGroupActive(int groupIndex) const noexcept
{
    if (!isValidGroupIndex(groupIndex))
        return false;
    return groups[groupIndex].isActive;
}

bool GroupManager::hasGroupData(int groupIndex) const noexcept
{
    if (!isValidGroupIndex(groupIndex))
        return false;
    return groups[groupIndex].padSnapshots.has_value();
}

uint8_t GroupManager::getGroupColor(int groupIndex) const noexcept
{
    if (!isValidGroupIndex(groupIndex))
        return 0;
    return groups[groupIndex].color;
}

void GroupManager::createGroup(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex))
        return;

    if (groups[groupIndex].color == 0)
        groups[groupIndex].color = generateRandomColor();

    groups[groupIndex].padSnapshots.reset();

    if (activeGroupIndex == groupIndex)
    {
        activeGroupIndex = -1;
        groups[groupIndex].isActive = false;
    }

    updateGroupState(groupIndex);
    updateActiveGroupState();
}

void GroupManager::clearGroup(int groupIndex)
{
    if (!isValidGroupIndex(groupIndex))
        return;

    groups[groupIndex].padSnapshots.reset();
    groups[groupIndex].color = 0;

    if (activeGroupIndex == groupIndex)
    {
        activeGroupIndex = -1;
        groups[groupIndex].isActive = false;
    }

    updateGroupState(groupIndex);
    updateActiveGroupState();
}

void GroupManager::setGroupColor(int groupIndex, uint8_t color)
{
    if (!isValidGroupIndex(groupIndex))
        return;

    groups[groupIndex].color = normalizeColor(color);
    updateGroupState(groupIndex);
}

uint8_t GroupManager::generateRandomColor() const
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, kColorCount - 1);
    return colorAt(dis(gen));
}

uint8_t GroupManager::normalizeColor(uint8_t color) noexcept
{
    const int existingIndex = colorIndex(color);
    if (existingIndex >= 0)
        return colorAt(existingIndex);

    // Migrate projects written by the former synthetic 4-67 hue wheel to a
    // deterministic real MK3 color rather than sending an undefined value.
    return colorAt(static_cast<int>(color) % kColorCount);
}

bool GroupManager::isValidGroupIndex(int groupIndex) const noexcept
{
    return groupIndex >= 0 && groupIndex < kGroupCount;
}

void GroupManager::bindStateTree()
{
    groupsStateNode = audioEngine.getGroupsState();
    if (audioEngine.hasEdit())
        undoManager = &audioEngine.getUndoManager();
}

void GroupManager::ensureGroupNodes()
{
    if (!groupsStateNode.isValid())
        return;

    const int storedActive = static_cast<int>(groupsStateNode.getProperty("activeGroupIndex", -1));
    bool foundActive = false;

    for (int i = 0; i < kGroupCount; ++i)
    {
        auto node = getGroupNode(i);
        if (!node.isValid())
        {
            node = juce::ValueTree("group");
            node.setProperty("index", i, nullptr);
            node.setProperty("color", groups[i].color, nullptr);
            node.setProperty("hasData", false, nullptr);
            node.setProperty("isActive", false, nullptr);
            groupsStateNode.addChild(node, -1, nullptr);
        }
        else
        {
            const int storedColor = static_cast<int>(node.getProperty("color", static_cast<int>(groups[i].color)));
            groups[i].color = normalizeColor(static_cast<uint8_t>(juce::jlimit(0, 255, storedColor)));
            groups[i].isActive = node.getProperty("isActive", false);

            // Restore pad snapshots from state
            if (node.getProperty("hasData", false))
            {
                if (auto padsNode = node.getChildWithName("pads"); padsNode.isValid())
                {
                    std::vector<PadSnapshot> snapshots;
                    for (int j = 0; j < padsNode.getNumChildren(); ++j)
                    {
                        auto padNode = padsNode.getChild(j);
                        PadSnapshot snapshot;
                        snapshot.index = static_cast<int>(padNode.getProperty("index", -1));
                        snapshot.name = padNode.getProperty("name", "").toString();
                        snapshot.samplePath = padNode.getProperty("samplePath", "").toString();
                        snapshot.gainDb = static_cast<float>(padNode.getProperty("gainDb", 0.0f));
                        snapshot.chokeGroup = static_cast<int>(padNode.getProperty("chokeGroup", 0));
                        snapshot.triggerMode = static_cast<int>(padNode.getProperty("triggerMode", 0));
                        for (int layer = 0; layer < padNode.getNumChildren(); ++layer)
                        {
                            const auto layerNode = padNode.getChild(layer);
                            if (layerNode.hasType("sampleLayer"))
                            {
                                snapshot.sampleLayerPaths.push_back(
                                    layerNode.getProperty("path", "").toString());
                                snapshot.layerGainsDb.push_back(
                                    static_cast<float>(layerNode.getProperty("gainDb", 0.0f)));
                                snapshot.layerWeights.push_back(
                                    static_cast<float>(layerNode.getProperty("weight", 1.0f)));
                                snapshot.layerVelocityCurves.push_back(
                                    static_cast<int>(layerNode.getProperty("velocityCurve", 0)));
                                snapshot.layerVelocityMinimums.push_back(
                                    static_cast<float>(layerNode.getProperty(
                                        "velocityMinimum", 0.85f)));
                                snapshot.layerVelocityMaximums.push_back(
                                    static_cast<float>(layerNode.getProperty(
                                        "velocityMaximum", 1.0f)));
                            }
                        }
                        if (snapshot.sampleLayerPaths.empty()
                            && snapshot.samplePath.isNotEmpty())
                            snapshot.sampleLayerPaths.push_back(snapshot.samplePath);
                        snapshots.push_back(std::move(snapshot));
                    }
                    if (!snapshots.empty())
                        groups[i].padSnapshots = std::move(snapshots);
                }
            }

            if (groups[i].isActive)
            {
                activeGroupIndex = i;
                foundActive = true;
            }
        }
    }

    if (!foundActive && storedActive >= 0 && storedActive < kGroupCount)
    {
        activeGroupIndex = storedActive;
        groups[activeGroupIndex].isActive = true;
    }
}

juce::ValueTree GroupManager::getGroupNode(int groupIndex) const
{
    if (!groupsStateNode.isValid() || !isValidGroupIndex(groupIndex))
        return {};

    for (int i = 0; i < groupsStateNode.getNumChildren(); ++i)
    {
        auto child = groupsStateNode.getChild(i);
        const int indexValue = static_cast<int>(child.getProperty("index", -1));
        if (indexValue == groupIndex)
            return child;
    }

    return {};
}

juce::ValueTree GroupManager::ensureGroupNode(int groupIndex, bool recordUndo)
{
    auto node = getGroupNode(groupIndex);
    if (node.isValid())
        return node;

    if (!groupsStateNode.isValid() || !isValidGroupIndex(groupIndex))
        return {};

    node = juce::ValueTree("group");
    node.setProperty("index", groupIndex, nullptr);
    node.setProperty("color", groups[groupIndex].color, nullptr);
    node.setProperty("hasData", false, nullptr);
    node.setProperty("isActive", false, nullptr);
    groupsStateNode.addChild(node, -1, recordUndo ? undoManager : nullptr);
    return node;
}

void GroupManager::updateGroupState(int groupIndex, bool recordUndo)
{
    if (!isValidGroupIndex(groupIndex))
        return;

    auto node = ensureGroupNode(groupIndex, recordUndo);
    if (!node.isValid())
        return;

    auto* manager = recordUndo ? undoManager : nullptr;
    const auto& group = groups[static_cast<size_t>(groupIndex)];

    node.setProperty("color", group.color, manager);
    node.setProperty("hasData", group.padSnapshots.has_value(), manager);
    node.setProperty("isActive", group.isActive, manager);

    auto padsNode = node.getChildWithName("pads");
    if (!padsNode.isValid())
    {
        padsNode = juce::ValueTree("pads");
        node.addChild(padsNode, -1, manager);
    }

    while (padsNode.getNumChildren() > 0)
        padsNode.removeChild(0, manager);

    if (group.padSnapshots.has_value())
    {
        for (const auto& snapshot : *group.padSnapshots)
        {
            juce::ValueTree padNode("pad");
            padNode.setProperty("index", snapshot.index, nullptr);
            padNode.setProperty("name", snapshot.name, nullptr);
            padNode.setProperty("samplePath", snapshot.samplePath, nullptr);
            padNode.setProperty("gainDb", snapshot.gainDb, nullptr);
            padNode.setProperty("chokeGroup", snapshot.chokeGroup, nullptr);
            padNode.setProperty("triggerMode", snapshot.triggerMode, nullptr);
            for (size_t layer = 0; layer < snapshot.sampleLayerPaths.size(); ++layer)
            {
                juce::ValueTree layerNode("sampleLayer");
                layerNode.setProperty("path", snapshot.sampleLayerPaths[layer], nullptr);
                if (layer < snapshot.layerGainsDb.size())
                    layerNode.setProperty("gainDb", snapshot.layerGainsDb[layer], nullptr);
                if (layer < snapshot.layerWeights.size())
                    layerNode.setProperty("weight", snapshot.layerWeights[layer], nullptr);
                if (layer < snapshot.layerVelocityCurves.size())
                    layerNode.setProperty("velocityCurve",
                                          snapshot.layerVelocityCurves[layer], nullptr);
                if (layer < snapshot.layerVelocityMinimums.size())
                    layerNode.setProperty("velocityMinimum",
                                          snapshot.layerVelocityMinimums[layer], nullptr);
                if (layer < snapshot.layerVelocityMaximums.size())
                    layerNode.setProperty("velocityMaximum",
                                          snapshot.layerVelocityMaximums[layer], nullptr);
                padNode.addChild(layerNode, -1, nullptr);
            }
            padsNode.addChild(padNode, -1, manager);
        }
    }
}

void GroupManager::updateActiveGroupState(bool recordUndo)
{
    if (!groupsStateNode.isValid())
        return;

    auto* manager = recordUndo ? undoManager : nullptr;
    groupsStateNode.setProperty("activeGroupIndex", activeGroupIndex, manager);
}

void GroupManager::refreshStateBinding()
{
    bindStateTree();
    ensureGroupNodes();
    updateActiveGroupState(false);
    for (int i = 0; i < kGroupCount; ++i)
        updateGroupState(i, false);
}
