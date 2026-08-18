#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <juce_core/juce_core.h>

#include "IController.h"
#include "ControllerGestureProcessor.h"

class ControllerHost;

class EmulatorController : public IController
{
public:
    /** port=0 selects MK3_EMU_PORT or the default 9999; explicit ports ignore the environment. */
    explicit EmulatorController(ControllerHost& host, int port = 0);
    ~EmulatorController() override;

    EmulatorController(const EmulatorController&) = delete;
    EmulatorController& operator=(const EmulatorController&) = delete;

    bool isConnected() const noexcept override;
    void shutdown() override;

    bool sendDisplayFrame(int screenIndex, const juce::Image& image) override;
    bool clearDisplay(int screenIndex, uint16_t color = 0) override;
    void clearAllDisplays() override;

    bool setMonoLed(const std::string& ledName, uint8_t brightness) override;
    bool setIndexedLed(const std::string& ledName, uint8_t colorIndex) override;
    void setButtonActive(const std::string& buttonName, bool active) override;
    void setButtonBrightness(const std::string& buttonName, uint8_t brightness) override;
    void resetAllLeds() override;

private:
    static constexpr int kDisplayWidth  = 480;
    static constexpr int kDisplayHeight = 272;

    // Protocol message types (must match Python emulator)
    static constexpr uint8_t kMsgButton  = 0x01;
    static constexpr uint8_t kMsgPad     = 0x02;
    static constexpr uint8_t kMsgKnob    = 0x03;
    static constexpr uint8_t kMsgStepper = 0x04;
    static constexpr uint8_t kMsgTouchstrip = 0x05;
    static constexpr uint8_t kMsgDisplay    = 0x10;
    static constexpr uint8_t kMsgLedMono    = 0x11;
    static constexpr uint8_t kMsgLedIndexed = 0x12;

    bool tryConnect(int port);
    void pollLoop();
    void processMessage(uint8_t type, const uint8_t* payload, uint32_t len);
    bool sendMessage(uint8_t type, const void* payload, uint32_t len);
    bool sendAll(const void* data, size_t len);

    ControllerHost& host;
    ControllerGestureProcessor gestureProcessor;
    std::atomic<int> sockFd { -1 };
    std::atomic<bool> running { false };
    std::thread pollThread;
    std::recursive_mutex outputMutex;
    juce::HeapBlock<uint8_t> displayBuffer;

    // The USB driver emits edges, not raw repeated report state. Keep the
    // TCP backend at that same boundary even when automation sends duplicate
    // messages or pressure changes that do not change the pad's active state.
    std::unordered_map<std::string, bool> buttonPressedStates;
    std::unordered_map<std::string, uint16_t> knobPositions;
    std::array<bool, 16> padPressedStates {};
    std::array<uint16_t, 1> touchstripPositions {};
    std::array<bool, 1> touchstripPositionKnown {};
    uint8_t stepperPosition { 0 };
    bool stepperPositionKnown { false };
};
