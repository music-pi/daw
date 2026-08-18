#pragma once

#include <atomic>
#include <juce_core/juce_core.h>

#include "ChannelDescriptor.h"

struct MixerChannelState
{
    std::atomic<float> inputPeakDbfs { -100.0f };
    std::atomic<float> inputRmsDbfs { -100.0f };
    std::atomic<float> peakHoldDbfs { -100.0f };
    std::atomic<float> faderGainDb { 0.0f };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };
    std::atomic<int> activePlugins { 0 };

    juce::String name;
    juce::String subLabel;
    ChannelDescriptor::Kind kind { ChannelDescriptor::Kind::Track };
    bool canDrillDown { false };
};
