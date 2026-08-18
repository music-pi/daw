#include "ControllerInputHandler.h"

#include "InputManager.h"
#include "../devices/Mk3Device.h"

ControllerInputHandler::ControllerInputHandler(InputManager& inputMgr)
    : InputHandler(inputMgr)
{
}

void ControllerInputHandler::attachDevice(Mk3Device* device)
{
    mk3Device = device;
}

void ControllerInputHandler::detachDevice()
{
    mk3Device = nullptr;
}

bool ControllerInputHandler::emitControl(const juce::String& controlId,
                                         const juce::NamedValueSet& payload,
                                         const juce::String& source)
{
    juce::NamedValueSet metadata { payload };
    if (mk3Device != nullptr)
        metadata.set("device", "mk3");

    return inputManager.dispatchController(controlId,
                                           source.isNotEmpty() ? source : juce::String { "controller" },
                                           std::move(metadata));
}
