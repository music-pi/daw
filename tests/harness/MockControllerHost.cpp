#include "MockControllerHost.h"

namespace testharness
{

MockControllerHost::MockControllerHost()
    : ControllerHost(nullptr, /*enableHardware*/ false)
{
}

void MockControllerHost::setButtonBrightness(const std::string& buttonName, uint8_t brightness)
{
    brightnessByButton[buttonName] = brightness;
    ControllerHost::setButtonBrightness(buttonName, brightness);
}

void MockControllerHost::pushDisplayFrame(int screenIndex, const juce::Image& /*image*/)
{
    ++pushDisplayFrameCount_;
    pushedDisplayScreens_.push_back(screenIndex);
}

void MockControllerHost::triggerButton(const std::string& buttonName, bool pressed, bool shift)
{
    ButtonEvent event;
    event.name = buttonName;
    event.pressed = pressed;
    event.shift = shift;

    // Update modifier state on the base class (shift, select, macro)
    updateModifierState(event);

    // Dispatch through InputManager path (same as real hardware)
    handleButtonEvent(event);
}

uint8_t MockControllerHost::getButtonBrightness(const std::string& buttonName) const
{
    if (auto it = brightnessByButton.find(buttonName); it != brightnessByButton.end())
        return it->second;
    return 0;
}

} // namespace testharness
