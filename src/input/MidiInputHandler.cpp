#include "MidiInputHandler.h"

#include "InputEvent.h"
#include "InputManager.h"

MidiInputHandler::MidiInputHandler(InputManager& inputMgr,
                                   juce::AudioDeviceManager& manager)
    : InputHandler(inputMgr), deviceManager(manager)
{
}

void MidiInputHandler::setDeviceFilter(DeviceFilter filter)
{
    deviceFilter = std::move(filter);
}

void MidiInputHandler::start()
{
    if (running)
        return;

    activeDevices.clear();
    auto devices = juce::MidiInput::getAvailableDevices();

    for (const auto& info : devices)
    {
        if (deviceFilter && !deviceFilter(info))
            continue;

        deviceManager.setMidiInputDeviceEnabled(info.identifier, true);
        deviceManager.addMidiInputDeviceCallback(info.identifier, this);
        activeDevices.push_back(info);
    }

    running = true;
}

void MidiInputHandler::stop()
{
    if (!running)
        return;

    for (const auto& info : activeDevices)
    {
        deviceManager.removeMidiInputDeviceCallback(info.identifier, this);
        deviceManager.setMidiInputDeviceEnabled(info.identifier, false);
    }

    activeDevices.clear();
    running = false;
}

void MidiInputHandler::handleIncomingMidiMessage(juce::MidiInput* source,
                                                 const juce::MidiMessage& message)
{
    juce::String identifier;
    juce::NamedValueSet metadata;

    if (source != nullptr)
    {
        const auto info = source->getDeviceInfo();
        identifier = info.identifier;
        metadata.set("deviceName", info.name);
    }

    inputManager.dispatchMidi(message, identifier, metadata);
}
