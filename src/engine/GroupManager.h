#pragma once

#include <array>
#include <cstdint>

#include <juce_data_structures/juce_data_structures.h>

#include "../control/HardwareConstants.h"

class AudioEngine;

/**
 * Owns the eight sampler-group slots.
 *
 * A group is an independent SamplerInstrument (16 pads, pad settings,
 * patterns, and song data) attached to the same Tracktion Edit. Selecting a
 * group only changes which bank the controller/UI addresses; it does not stop
 * or replace any other group's clips, so all groups share the global transport.
 */
class GroupManager
{
public:
    static constexpr int kGroupCount = 8;
    static constexpr int kColorCount = static_cast<int>(
        HardwareConstants::kIndexedColorPairs.size());

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

    explicit GroupManager(AudioEngine& engine);

    // Compatibility names retained for existing callers: groups no longer
    // snapshot/restore another bank. Both operations select the group's own
    // persistent sampler bank.
    bool saveCurrentState(int groupIndex);
    bool recallGroup(int groupIndex);

    [[nodiscard]] bool isGroupActive(int groupIndex) const noexcept;
    [[nodiscard]] bool hasGroupData(int groupIndex) const noexcept;
    [[nodiscard]] uint8_t getGroupColor(int groupIndex) const noexcept;
    [[nodiscard]] int getActiveGroupIndex() const noexcept { return activeGroupIndex; }

    void createGroup(int groupIndex);
    void clearGroup(int groupIndex);
    void setGroupColor(int groupIndex, uint8_t color);
    void refreshStateBinding();

private:
    struct Group
    {
        uint8_t color { 0 };
        bool exists { false };
        bool isActive { false };
    };

    AudioEngine& audioEngine;
    juce::ValueTree groupsStateNode;
    std::array<Group, kGroupCount> groups;
    int activeGroupIndex { 0 };

    uint8_t generateRandomColor() const;
    static uint8_t normalizeColor(uint8_t color) noexcept;
    bool isValidGroupIndex(int groupIndex) const noexcept;
    void ensureGroupNodes();
    juce::ValueTree getGroupNode(int groupIndex) const;
    juce::ValueTree ensureGroupNode(int groupIndex);
    void updateGroupState(int groupIndex);
    void updateActiveGroupState();
    int findFallbackGroup(int excluding) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupManager)
};
