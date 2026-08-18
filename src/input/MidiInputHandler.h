#pragma once

#include <functional>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "InputHandler.h"

class MidiInputHandler : public InputHandler,
                         private juce::MidiInputCallback
{
public:
    using DeviceFilter = std::function<bool(const juce::MidiDeviceInfo&)>;

    MidiInputHandler(InputManager& inputMgr,
                     juce::AudioDeviceManager& deviceManager);

    void start() override;
    void stop() override;
    std::string_view name() const override { return "MidiInput"; }

    void setDeviceFilter(DeviceFilter filter);

private:
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    juce::AudioDeviceManager& deviceManager;
    DeviceFilter deviceFilter;
    bool running { false };
    std::vector<juce::MidiDeviceInfo> activeDevices;
};
