#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <juce_core/juce_core.h>

class ControllerHost;
class IController;

/**
 * Applies Maschine MK3 interaction semantics to raw controller input.
 *
 * Both the USB and TCP emulator backends use this class so modifier state,
 * Select-mode gestures, event normalisation, and momentary LED feedback stay
 * identical regardless of which controller produced the input.
 */
class ControllerGestureProcessor
{
public:
    ControllerGestureProcessor(ControllerHost& host, IController& output);

    void handleButton(const std::string& name, bool pressed);
    void handlePad(uint8_t physicalPadIndex, bool pressed, uint16_t pressure);
    void handleKnob(const std::string& name, int16_t delta, uint16_t absolute);
    void handleStepper(int8_t direction, uint8_t position);
    void handleTouchstrip(uint8_t finger, bool touching, uint16_t position);

    void setButtonActive(const std::string& buttonName, bool active);
    void setButtonBrightness(const std::string& buttonName, uint8_t brightness);
    void invalidateLedOutputCache();

private:
    struct ButtonState
    {
        bool active { false };
        bool pressed { false };
        uint8_t brightness { 0 };
    };

    void handleSelectMode(bool pressed);
    void updatePadLeds();
    void updateButtonPressState(const std::string& buttonName, bool pressed);
    ButtonState getButtonState(const std::string& buttonName) const;
    void applyButtonLed(const std::string& buttonName);

    ControllerHost& host;
    IController& output;
    std::atomic<bool> shiftHeld { false };
    std::atomic<bool> macroHeld { false };
    std::atomic<bool> selectionModeEnabled { false };
    std::atomic<bool> selectionModifiedDuringHold { false };
    std::array<std::atomic<bool>, 16> selectedPads {};
    mutable std::mutex buttonStateMutex;
    std::unordered_map<std::string, ButtonState> buttonStates;
    std::mutex ledOutputMutex;
    std::unordered_map<std::string, uint8_t> lastMonoLedValues;
    std::unordered_map<std::string, uint8_t> lastIndexedLedValues;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ControllerGestureProcessor)
};
