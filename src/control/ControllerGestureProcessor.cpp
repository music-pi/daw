#include "ControllerGestureProcessor.h"

#include <algorithm>
#include <optional>

#include <juce_events/juce_events.h>

#include "ControllerHost.h"
#include "HardwareConstants.h"
#include "IController.h"

using namespace HardwareConstants;

namespace
{
constexpr uint8_t kButtonActiveBrightness  = kLedDim;
constexpr uint8_t kButtonPressedBrightness = kLedBright;

bool isTransportButton(const std::string& name)
{
    return name == "play"
        || name == "stop"
        || name == "recCountIn"
        || name == "restartLoop";
}
}

ControllerGestureProcessor::ControllerGestureProcessor(ControllerHost& hostRef, IController& outputRef)
    : host(hostRef), output(outputRef)
{
    for (auto& selected : selectedPads)
        selected.store(false, std::memory_order_relaxed);
}

void ControllerGestureProcessor::handleButton(const std::string& name, bool pressed)
{
    updateButtonPressState(name, pressed);

    if (name == "shift")
    {
        shiftHeld.store(pressed, std::memory_order_relaxed);
    }
    else if (name == "macroSet")
    {
        macroHeld.store(pressed, std::memory_order_relaxed);
    }
    else if (name == "select")
    {
        handleSelectMode(pressed);
        return;
    }
    else if (selectionModeEnabled.load(std::memory_order_relaxed)
             && name.size() == 2
             && name[0] == 'g'
             && name[1] >= '1' && name[1] <= '8')
    {
        if (pressed)
        {
            const int groupIndex = name[1] - '1';
            juce::MessageManager::callAsync(
                [weak = juce::WeakReference<ControllerHost>(&host), groupIndex]()
                {
                    if (auto* controllerHost = weak.get())
                        controllerHost->notifyGroupSelection(groupIndex);
                });
            selectionModifiedDuringHold.store(true, std::memory_order_relaxed);
        }
        return;
    }
    ControllerHost::ButtonEvent event { name, pressed, shiftHeld.load(std::memory_order_relaxed) };
    juce::MessageManager::callAsync(
        [weak = juce::WeakReference<ControllerHost>(&host), event]()
        {
            if (auto* controllerHost = weak.get())
                controllerHost->handleButtonEvent(event);
        });

    // Transport LEDs are state indicators owned by TransportWidget. Raw
    // momentary feedback would race the semantic state and could dim an armed
    // Record LED on button release. Other buttons retain immediate physical
    // press feedback.
    if (!isTransportButton(name))
        applyButtonLed(name);
}

void ControllerGestureProcessor::handlePad(uint8_t physicalPadIndex, bool pressed, uint16_t pressure)
{
    if (physicalPadIndex >= selectedPads.size())
        return;

    if (selectionModeEnabled.load(std::memory_order_relaxed))
    {
        if (pressed)
        {
            const bool wasSelected = selectedPads[physicalPadIndex].load(std::memory_order_relaxed);
            for (auto& selected : selectedPads)
                selected.store(false, std::memory_order_relaxed);

            if (!wasSelected)
                selectedPads[physicalPadIndex].store(true, std::memory_order_relaxed);

            selectionModifiedDuringHold.store(true, std::memory_order_relaxed);
            const std::optional<int> selection = wasSelected
                ? std::nullopt
                : std::optional<int> { static_cast<int>(physicalPadIndex) };
            juce::MessageManager::callAsync(
                [weak = juce::WeakReference<ControllerHost>(&host), selection]()
                {
                    if (auto* controllerHost = weak.get())
                        controllerHost->notifyPadSelection(selection);
                });
        }

        juce::MessageManager::callAsync(
            [weak = juce::WeakReference<ControllerGestureProcessor>(this)]
            {
                if (auto* processor = weak.get())
                    processor->updatePadLeds();
            });
        return;
    }

    ControllerHost::PadEvent event {
        physicalPadIndex,
        pressed,
        pressure,
        shiftHeld.load(std::memory_order_relaxed),
        macroHeld.load(std::memory_order_relaxed)
    };
    juce::MessageManager::callAsync(
        [weak = juce::WeakReference<ControllerHost>(&host), event]()
        {
            if (auto* controllerHost = weak.get())
                controllerHost->handlePadEvent(event);
        });
}

void ControllerGestureProcessor::handleKnob(const std::string& name, int16_t delta, uint16_t absolute)
{
    ControllerHost::KnobEvent event { name, delta, absolute, shiftHeld.load(std::memory_order_relaxed) };
    juce::MessageManager::callAsync(
        [weak = juce::WeakReference<ControllerHost>(&host), event]()
        {
            if (auto* controllerHost = weak.get())
                controllerHost->handleKnobEvent(event);
        });
}

void ControllerGestureProcessor::handleStepper(int8_t direction, uint8_t position)
{
    ControllerHost::StepperEvent event { direction, position, shiftHeld.load(std::memory_order_relaxed) };
    juce::MessageManager::callAsync(
        [weak = juce::WeakReference<ControllerHost>(&host), event]()
        {
            if (auto* controllerHost = weak.get())
                controllerHost->handleStepperEvent(event);
        });
}

void ControllerGestureProcessor::handleTouchstrip(uint8_t finger, bool touching, uint16_t position)
{
    if (finger != 1)
        return;

    ControllerHost::TouchstripEvent event {
        finger, touching, position, shiftHeld.load(std::memory_order_relaxed)
    };
    juce::MessageManager::callAsync(
        [weak = juce::WeakReference<ControllerHost>(&host), event]()
        {
            if (auto* controllerHost = weak.get())
                controllerHost->handleTouchstripEvent(event);
        });
}

void ControllerGestureProcessor::handleSelectMode(bool pressed)
{
    if (pressed)
    {
        selectionModeEnabled.store(true, std::memory_order_relaxed);
        selectionModifiedDuringHold.store(false, std::memory_order_relaxed);
        juce::MessageManager::callAsync(
            [weak = juce::WeakReference<ControllerGestureProcessor>(this)]
            {
                if (auto* processor = weak.get())
                {
                    processor->applyButtonLed("select");
                    processor->updatePadLeds();
                }
            });
        juce::MessageManager::callAsync(
            [weak = juce::WeakReference<ControllerHost>(&host)]
            {
                if (auto* controllerHost = weak.get())
                    controllerHost->refreshGroupLeds();
            });
        return;
    }

    selectionModeEnabled.store(false, std::memory_order_relaxed);
    if (!selectionModifiedDuringHold.load(std::memory_order_relaxed))
    {
        const bool hadSelection = std::any_of(
            selectedPads.begin(), selectedPads.end(),
            [](const std::atomic<bool>& selected)
            {
                return selected.load(std::memory_order_relaxed);
            });
        if (hadSelection)
        {
            juce::MessageManager::callAsync(
                [weak = juce::WeakReference<ControllerHost>(&host)]
                {
                    if (auto* controllerHost = weak.get())
                        controllerHost->notifyPadSelection(std::nullopt);
                });
        }

        for (auto& selected : selectedPads)
            selected.store(false, std::memory_order_relaxed);
    }

    juce::MessageManager::callAsync(
        [weak = juce::WeakReference<ControllerGestureProcessor>(this)]
        {
            if (auto* processor = weak.get())
            {
                processor->applyButtonLed("select");
                processor->updatePadLeds();
            }
        });
    juce::MessageManager::callAsync(
        [weak = juce::WeakReference<ControllerHost>(&host)]
        {
            if (auto* controllerHost = weak.get())
                controllerHost->refreshGroupLeds();
        });
}

void ControllerGestureProcessor::updatePadLeds()
{
    if (selectionModeEnabled.load(std::memory_order_relaxed))
    {
        output.beginLedBatch();
        for (int i = 0; i < kPadCount; ++i)
        {
            const bool selected = selectedPads[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            output.setIndexedLed(kPadNames[static_cast<size_t>(i)], selected ? kColorWhite : kColorWhiteDim);
        }
        output.endLedBatch();
        return;
    }

    host.refreshPadLeds();
    for (int i = 0; i < kPadCount; ++i)
    {
        if (selectedPads[static_cast<size_t>(i)].load(std::memory_order_relaxed))
            output.setIndexedLed(kPadNames[static_cast<size_t>(i)], kColorWhite);
    }
}

void ControllerGestureProcessor::setButtonActive(const std::string& buttonName, bool active)
{
    {
        std::scoped_lock lock(buttonStateMutex);
        auto& state = buttonStates[buttonName];
        state.active = active;
        state.brightness = active ? kButtonActiveBrightness : kLedOff;
    }
    applyButtonLed(buttonName);
}

void ControllerGestureProcessor::setButtonBrightness(const std::string& buttonName, uint8_t brightness)
{
    {
        std::scoped_lock lock(buttonStateMutex);
        auto& state = buttonStates[buttonName];
        state.brightness = brightness;
        state.active = brightness >= kButtonActiveBrightness;
    }
    applyButtonLed(buttonName);
}

void ControllerGestureProcessor::invalidateLedOutputCache()
{
    std::scoped_lock lock(ledOutputMutex);
    lastMonoLedValues.clear();
    lastIndexedLedValues.clear();
}

void ControllerGestureProcessor::updateButtonPressState(const std::string& buttonName, bool pressed)
{
    std::scoped_lock lock(buttonStateMutex);
    buttonStates[buttonName].pressed = pressed;
}

ControllerGestureProcessor::ButtonState ControllerGestureProcessor::getButtonState(
    const std::string& buttonName) const
{
    std::scoped_lock lock(buttonStateMutex);
    const auto iter = buttonStates.find(buttonName);
    return iter != buttonStates.end() ? iter->second : ButtonState {};
}

void ControllerGestureProcessor::applyButtonLed(const std::string& buttonName)
{
    const auto state = getButtonState(buttonName);

    if (buttonName == "sampling")
    {
        const uint8_t colorIndex = (state.pressed || state.active) ? kColorWhite : kColorOff;
        std::scoped_lock lock(ledOutputMutex);
        const auto previous = lastIndexedLedValues.find(buttonName);
        if (previous == lastIndexedLedValues.end() || previous->second != colorIndex)
        {
            if (output.setIndexedLed(buttonName, colorIndex))
                lastIndexedLedValues[buttonName] = colorIndex;
        }
        return;
    }

    if (buttonName.size() == 2 && buttonName[0] == 'g'
        && buttonName[1] >= '1' && buttonName[1] <= '8')
        return;

    if (buttonName.starts_with("knobTouch") || buttonName.starts_with("nav"))
        return;

    const uint8_t brightness = state.pressed ? kButtonPressedBrightness : state.brightness;
    std::scoped_lock lock(ledOutputMutex);
    const auto previous = lastMonoLedValues.find(buttonName);
    if (previous == lastMonoLedValues.end() || previous->second != brightness)
    {
        if (output.setMonoLed(buttonName, brightness))
            lastMonoLedValues[buttonName] = brightness;
    }
}
