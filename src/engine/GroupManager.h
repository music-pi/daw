#pragma once

#include <array>
#include <optional>
#include <vector>
#include <cstdint>
#include <juce_data_structures/juce_data_structures.h>

#include "../control/HardwareConstants.h"

class SamplerInstrument;
class AudioEngine;

class GroupManager
{
public:
    static constexpr int kGroupCount = 8;
    static constexpr int kColorCount = static_cast<int>(HardwareConstants::kIndexedColorPairs.size());

    static constexpr uint8_t colorAt(int index) noexcept
    {
        return index >= 0 && index < kColorCount
            ? HardwareConstants::kIndexedColorPairs[static_cast<std::size_t>(index)].bright
            : HardwareConstants::kIndexedColorPairs[0].bright;
    }

    static constexpr int colorIndex(uint8_t color) noexcept
    {
        return HardwareConstants::indexedColorPairIndex(color);
    }

    // Simplified pad snapshot for group storage
    struct PadSnapshot
    {
        int index { -1 };
        juce::String name;
        juce::String samplePath;
        std::vector<juce::String> sampleLayerPaths;
        std::vector<float> layerGainsDb;
        std::vector<float> layerWeights;
        std::vector<int> layerVelocityCurves;
        std::vector<float> layerVelocityMinimums;
        std::vector<float> layerVelocityMaximums;
        float gainDb { 0.0f };
        int chokeGroup { 0 };
        int triggerMode { 0 };
    };

    explicit GroupManager(AudioEngine& engine, SamplerInstrument& sampler);

    bool saveCurrentState(int groupIndex);
    bool recallGroup(int groupIndex);
    [[nodiscard]] bool isGroupActive(int groupIndex) const noexcept;
    [[nodiscard]] bool hasGroupData(int groupIndex) const noexcept;
    [[nodiscard]] uint8_t getGroupColor(int groupIndex) const noexcept;
    void createGroup(int groupIndex);
    [[nodiscard]] int getActiveGroupIndex() const noexcept { return activeGroupIndex; }
    void clearGroup(int groupIndex);
    void setGroupColor(int groupIndex, uint8_t color);
    void refreshStateBinding();

private:
    AudioEngine& audioEngine;
    SamplerInstrument& sampler_;

    struct Group
    {
        std::optional<std::vector<PadSnapshot>> padSnapshots;
        uint8_t color { 0 };
        bool isActive { false };
    };

    uint8_t generateRandomColor() const;
    static uint8_t normalizeColor(uint8_t color) noexcept;
    bool isValidGroupIndex(int groupIndex) const noexcept;

    juce::ValueTree groupsStateNode;
    juce::UndoManager* undoManager { nullptr };
    std::array<Group, kGroupCount> groups;
    int activeGroupIndex { -1 };

    void bindStateTree();
    void ensureGroupNodes();
    juce::ValueTree getGroupNode(int groupIndex) const;
    juce::ValueTree ensureGroupNode(int groupIndex, bool recordUndo = true);
    void updateGroupState(int groupIndex, bool recordUndo = true);
    void updateActiveGroupState(bool recordUndo = true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupManager)
};
