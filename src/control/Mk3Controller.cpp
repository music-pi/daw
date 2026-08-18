#include "Mk3Controller.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "ControllerHost.h"

Mk3Controller::Mk3Controller(ControllerHost& hostRef)
  : host(hostRef),
    device(std::make_unique<Mk3Device>()),
    gestureProcessor(hostRef, *this)
{
    if (!device->isConnected())
    {
        juce::Logger::writeToLog("[Mk3Controller] No hardware detected.");
        return;
    }

    device->setButtonHandler([this](const Mk3Device::ButtonEvent& event) { handleButton(event); });
    device->setPadHandler([this](const Mk3Device::PadEvent& event) { handlePad(event); });
    device->setKnobHandler([this](const Mk3Device::KnobEvent& event) { handleKnob(event); });
    device->setStepperHandler([this](const Mk3Device::StepperEvent& event) { handleStepper(event); });
    device->setTouchstripHandler([this](const Mk3Device::TouchstripEvent& event) { handleTouchstrip(event); });

    juce::Logger::writeToLog("[Mk3Controller] Input callbacks registered.");
}

Mk3Controller::~Mk3Controller()
{
    shutdown();
}

bool Mk3Controller::isConnected() const noexcept
{
    return device && device->isConnected();
}

void Mk3Controller::shutdown()
{
    // Callers clear visible hardware state explicitly before shutdown. Keep
    // this method limited to stopping threads and closing the shared handle.
    if (device)
        device->shutdown();
}

void Mk3Controller::resetAllLeds()
{
    if (device)
    {
        device->resetAllLeds();
        gestureProcessor.invalidateLedOutputCache();
    }
}

bool Mk3Controller::sendDisplayFrame(int screenIndex, const juce::Image& image)
{
    if (!device || !device->isConnected())
        return false;

    return device->sendDisplayFrame(screenIndex, image);
}

bool Mk3Controller::clearDisplay(int screenIndex, uint16_t color)
{
    if (!device || !device->isConnected())
        return false;

    return device->clearDisplay(screenIndex, color);
}

void Mk3Controller::clearAllDisplays()
{
    if (device)
    {
        device->clearAllDisplays();
    }
}

bool Mk3Controller::setMonoLed(const std::string& ledName, uint8_t brightness)
{
    if (!device || !device->isConnected())
        return false;

    return device->setMonoLed(ledName, brightness);
}

bool Mk3Controller::setIndexedLed(const std::string& ledName, uint8_t colorIndex)
{
    if (!device || !device->isConnected())
        return false;

    return device->setIndexedLed(ledName, colorIndex);
}

void Mk3Controller::beginLedBatch()
{
    if (device)
        device->beginLedBatch();
}

void Mk3Controller::endLedBatch()
{
    if (device)
        device->endLedBatch();
}

void Mk3Controller::handleButton(const Mk3Device::ButtonEvent& event)
{
    gestureProcessor.handleButton(event.name, event.pressed);
}

void Mk3Controller::handlePad(const Mk3Device::PadEvent& event)
{
    if (event.pad == 0)
        return;

    gestureProcessor.handlePad(static_cast<uint8_t>(event.pad - 1), event.pressed, event.pressure);
}

void Mk3Controller::handleKnob(const Mk3Device::KnobEvent& event)
{
    gestureProcessor.handleKnob(event.name, event.delta, event.absolute);
}

void Mk3Controller::handleStepper(const Mk3Device::StepperEvent& event)
{
    gestureProcessor.handleStepper(event.direction, event.position);
}

void Mk3Controller::handleTouchstrip(const Mk3Device::TouchstripEvent& event)
{
    gestureProcessor.handleTouchstrip(event.finger, event.touching, event.position);
}

void Mk3Controller::setButtonActive(const std::string& buttonName, bool active)
{
    gestureProcessor.setButtonActive(buttonName, active);
}

void Mk3Controller::setButtonBrightness(const std::string& buttonName, uint8_t brightness)
{
    gestureProcessor.setButtonBrightness(buttonName, brightness);
}
