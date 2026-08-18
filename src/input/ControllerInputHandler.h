#pragma once

#include <juce_core/juce_core.h>

#include "InputHandler.h"

class Mk3Device;

class ControllerInputHandler : public InputHandler
{
public:
    explicit ControllerInputHandler(InputManager& inputMgr);

    std::string_view name() const override { return "ControllerInput"; }

    void attachDevice(Mk3Device* device);
    void detachDevice();

    bool emitControl(const juce::String& controlId,
                     const juce::NamedValueSet& payload = {},
                     const juce::String& source = {});

private:
    Mk3Device* mk3Device { nullptr };
};
