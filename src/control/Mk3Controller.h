#pragma once

#include <memory>
#include <string>

#include <juce_core/juce_core.h>

#include "../devices/Mk3Device.h"
#include "ControllerGestureProcessor.h"
#include "IController.h"

class ControllerHost;

class Mk3Controller : public IController {
public:
    explicit Mk3Controller(ControllerHost& host);
    ~Mk3Controller() override;

    Mk3Controller(const Mk3Controller&) = delete;
    Mk3Controller& operator=(const Mk3Controller&) = delete;
    Mk3Controller(Mk3Controller&&) = delete;
    Mk3Controller& operator=(Mk3Controller&&) = delete;

    bool isConnected() const noexcept override;
    void shutdown() override;
    bool sendDisplayFrame(int screenIndex, const juce::Image& image) override;
    bool clearDisplay(int screenIndex, uint16_t color = 0) override;
    void clearAllDisplays() override;
    bool setMonoLed(const std::string& ledName, uint8_t brightness) override;
    bool setIndexedLed(const std::string& ledName, uint8_t colorIndex) override;
    void beginLedBatch() override;
    void endLedBatch() override;
    void setButtonActive(const std::string& buttonName, bool active) override;
    void setButtonBrightness(const std::string& buttonName, uint8_t brightness) override;
    void resetAllLeds() override;
    Mk3Device* getDevice() const noexcept { return device.get(); }

private:
    void handleButton(const Mk3Device::ButtonEvent& event);
    void handlePad(const Mk3Device::PadEvent& event);
    void handleKnob(const Mk3Device::KnobEvent& event);
    void handleStepper(const Mk3Device::StepperEvent& event);
    void handleTouchstrip(const Mk3Device::TouchstripEvent& event);

    ControllerHost& host;
    std::unique_ptr<Mk3Device> device;
    ControllerGestureProcessor gestureProcessor;

    JUCE_DECLARE_WEAK_REFERENCEABLE(Mk3Controller)
};
