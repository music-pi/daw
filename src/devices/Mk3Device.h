#pragma once

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

extern "C" {
#include "mk3.h"
#include "mk3_display.h"
}

namespace juce { class Image; }

class Mk3Device {
public:
    struct ButtonEvent {
        std::string name;
        bool pressed{};
    };

    struct PadEvent {
        uint8_t pad{};
        bool pressed{};
        uint16_t pressure{};
    };

    struct KnobEvent {
        std::string name;
        int16_t delta{};
        uint16_t absolute{};
    };

    struct StepperEvent {
        int8_t direction{};
        uint8_t position{};
    };

    struct TouchstripEvent {
        uint8_t finger{};
        bool touching{};
        uint16_t position{};
    };

    using ButtonHandler  = std::function<void(const ButtonEvent&)>;
    using PadHandler     = std::function<void(const PadEvent&)>;
    using KnobHandler    = std::function<void(const KnobEvent&)>;
    using StepperHandler = std::function<void(const StepperEvent&)>;
    using TouchstripHandler = std::function<void(const TouchstripEvent&)>;

    Mk3Device();
    ~Mk3Device();

    Mk3Device(const Mk3Device&) = delete;
    Mk3Device& operator=(const Mk3Device&) = delete;
    Mk3Device(Mk3Device&&) = delete;
    Mk3Device& operator=(Mk3Device&&) = delete;

    bool isConnected() const noexcept { return device.load(std::memory_order_acquire) != nullptr; }

    void setButtonHandler(ButtonHandler handler);
    void setPadHandler(PadHandler handler);
    void setKnobHandler(KnobHandler handler);
    void setStepperHandler(StepperHandler handler);
    void setTouchstripHandler(TouchstripHandler handler);

    bool sendDisplayFrame(int screenIndex, const juce::Image& image);
    /** Wait until all currently queued display frames have reached libmk3.
        Intended for shutdown and performance tests; normal UI rendering is
        asynchronous and coalesces newer frames over stale queued ones. */
    bool flushDisplayFrames(int timeoutMs = 2000);
    bool clearDisplay(int screenIndex, uint16_t color = 0);
    bool setMonoLed(const std::string& ledName, uint8_t brightness);
    bool setIndexedLed(const std::string& ledName, uint8_t colorIndex);
    void beginLedBatch();
    void endLedBatch();
    void resetAllLeds();
    void clearAllDisplays();

    void shutdown();

private:
    void pollLoop();
    void displayLoop();
    void stopDisplayThread();
    bool queueSolidDisplayFrame(int screenIndex, uint16_t color);

    void handleButton(const char* name, bool pressed);
    void handlePad(uint8_t pad, bool pressed, uint16_t pressure);
    void handleKnob(const char* name, int16_t delta, uint16_t absolute);
    void handleStepper(int8_t direction, uint8_t position);
    void handleTouchstrip(uint8_t finger, bool touching, uint16_t position);

    static void buttonThunk(const char* name, bool pressed, void* user);
    static void padThunk(uint8_t pad, bool pressed, uint16_t pressure, void* user);
    static void knobThunk(const char* name, int16_t delta, uint16_t absolute, void* user);
    static void stepperThunk(int8_t direction, uint8_t position, void* user);
    static void touchstripThunk(uint8_t finger, bool touching, uint16_t position, void* user);

    std::atomic<mk3_t*> device { nullptr };
    std::atomic<bool> running{false};
    std::thread pollThread;

    std::mutex callbackMutex;
    ButtonHandler buttonHandler;
    PadHandler padHandler;
    KnobHandler knobHandler;
    StepperHandler stepperHandler;
    TouchstripHandler touchstripHandler;

    std::mutex outputMutex;
    int ledBatchDepth { 0 };
    bool ledReport80Dirty { false };
    bool ledReport81Dirty { false };

    struct DisplaySlot
    {
        std::vector<uint16_t> pixels;
        bool pending { false };
    };

    std::array<DisplaySlot, 2> displaySlots;
    std::mutex displayMutex;
    std::condition_variable displayCondition;
    std::thread displayThread;
    bool displayThreadRunning { false };
    int displayTransfersInFlight { 0 };
    bool displayTransferFailedSinceFlush { false };
};
