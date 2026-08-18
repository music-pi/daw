#pragma once

#include <cstdint>
#include <string>

namespace juce { class Image; }

// Abstract interface for hardware controllers.
// Mk3Controller implements this for the Maschine Mk3 hardware.
// MockController or emulator-backed controllers can also implement this.
class IController
{
public:
    virtual ~IController() = default;

    virtual bool isConnected() const noexcept = 0;
    virtual void shutdown() = 0;

    // Display
    virtual bool sendDisplayFrame(int screenIndex, const juce::Image& image) = 0;
    virtual bool clearDisplay(int screenIndex, uint16_t color = 0) = 0;
    virtual void clearAllDisplays() = 0;

    // LEDs
    virtual bool setMonoLed(const std::string& ledName, uint8_t brightness) = 0;
    virtual bool setIndexedLed(const std::string& ledName, uint8_t colorIndex) = 0;
    virtual void beginLedBatch() {}
    virtual void endLedBatch() {}
    virtual void setButtonActive(const std::string& buttonName, bool active) = 0;
    virtual void setButtonBrightness(const std::string& buttonName, uint8_t brightness) = 0;
    virtual void resetAllLeds() = 0;
};
