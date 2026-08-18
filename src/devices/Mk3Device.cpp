#include "Mk3Device.h"
#include "DisplayFrameConversion.h"

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <atomic>
#include <chrono>

extern "C" {
#include "mk3_output.h"
#include "mk3_output_map.h"
}

namespace {
constexpr int kDisplayWidth  = 480;
constexpr int kDisplayHeight = 272;

bool inputLoggingEnabled()
{
    static const bool enabled = juce::SystemStats::getEnvironmentVariable("MK3_LOG_INPUTS", "0").trim().equalsIgnoreCase("1");
    return enabled;
}

void logInfo(const juce::String& message)
{
    juce::Logger::writeToLog("[Mk3Device] " + message);
}
}

// Global flag for input tracing (set via command-line or environment variable)
static std::atomic<bool> g_inputTracingEnabled{false};

// Functions for input tracing (accessible from other files)
bool inputTracingEnabled()
{
    // Check environment variable first (for build.sh --trace-input)
    static const bool envEnabled = juce::SystemStats::getEnvironmentVariable("MASCHINEPI_TRACE_INPUT", "0").trim().equalsIgnoreCase("1");
    // Also check the global flag (for --trace-input command-line argument)
    return envEnabled || g_inputTracingEnabled.load(std::memory_order_relaxed);
}

void setInputTracingEnabled(bool enabled)
{
    g_inputTracingEnabled.store(enabled, std::memory_order_relaxed);
}

Mk3Device::Mk3Device()
{
    const auto pixelCount = static_cast<size_t>(kDisplayWidth)
                          * static_cast<size_t>(kDisplayHeight);
    for (auto& slot : displaySlots)
        slot.pixels.resize(pixelCount);

    auto* openedDevice = mk3_open();
    device.store(openedDevice, std::memory_order_release);
    if (openedDevice == nullptr)
    {
        logInfo("Unable to open device – running in controller-offline mode.");
        return;
    }

    // libmk3 diffs against the last successful frame and transfers only the
    // changed bounding box. Full 480x272 USB frames take ~37 ms per panel on
    // the MK3, which limits two-panel refresh to ~13 fps.
    mk3_display_disable_partial_rendering(openedDevice, false);

    mk3_input_set_button_callback(openedDevice, &Mk3Device::buttonThunk, this);
    mk3_input_set_pad_callback(openedDevice, &Mk3Device::padThunk, this);
    mk3_input_set_knob_callback(openedDevice, &Mk3Device::knobThunk, this);
    mk3_input_set_stepper_callback(openedDevice, &Mk3Device::stepperThunk, this);
    mk3_input_set_touchstrip_callback(openedDevice, &Mk3Device::touchstripThunk, this);

    {
        std::scoped_lock lock(displayMutex);
        displayThreadRunning = true;
    }
    displayThread = std::thread([this]() { displayLoop(); });

    running.store(true, std::memory_order_relaxed);
    pollThread = std::thread([this]() { pollLoop(); });

    logInfo("Maschine MK3 controller ready.");
}

Mk3Device::~Mk3Device()
{
    shutdown();
}

void Mk3Device::shutdown()
{
    // relaxed — pure stop flag for poll loop; thread.join() provides the
    // happens-before edge for any data the thread touched.
    running.store(false, std::memory_order_relaxed);
    if (pollThread.joinable())
    {
        pollThread.join();
    }

    // Stop display transfers before closing the shared libusb handle. Pending
    // animation frames are intentionally dropped; UiHost explicitly clears
    // the displays before controller shutdown.
    stopDisplayThread();

    std::scoped_lock lock(outputMutex);
    if (auto* deviceToClose = device.exchange(nullptr, std::memory_order_acq_rel))
    {
        mk3_close(deviceToClose);
        logInfo("Controller closed.");
    }
}

void Mk3Device::setButtonHandler(ButtonHandler handler)
{
    std::scoped_lock lock(callbackMutex);
    buttonHandler = std::move(handler);
}

void Mk3Device::setPadHandler(PadHandler handler)
{
    std::scoped_lock lock(callbackMutex);
    padHandler = std::move(handler);
}

void Mk3Device::setKnobHandler(KnobHandler handler)
{
    std::scoped_lock lock(callbackMutex);
    knobHandler = std::move(handler);
}

void Mk3Device::setStepperHandler(StepperHandler handler)
{
    std::scoped_lock lock(callbackMutex);
    stepperHandler = std::move(handler);
}

void Mk3Device::setTouchstripHandler(TouchstripHandler handler)
{
    std::scoped_lock lock(callbackMutex);
    touchstripHandler = std::move(handler);
}

bool Mk3Device::sendDisplayFrame(int screenIndex, const juce::Image& sourceImage)
{
    if (!isConnected())
        return false;

    if (screenIndex < 0 || screenIndex > 1)
        return false;

    if (sourceImage.isNull())
        return false;

    juce::Image image = sourceImage;
    if (image.getWidth() != kDisplayWidth || image.getHeight() != kDisplayHeight)
        image = sourceImage.rescaled(kDisplayWidth, kDisplayHeight);

    const auto pixelCount = static_cast<size_t>(kDisplayWidth)
                          * static_cast<size_t>(kDisplayHeight);
    std::scoped_lock lock(displayMutex);
    if (!displayThreadRunning)
        return false;

    auto& slot = displaySlots[static_cast<size_t>(screenIndex)];
    if (!DisplayFrameConversion::toRgb565(image, { slot.pixels.data(), pixelCount }))
        return false;

    // A frame not yet picked up by the worker is stale as soon as a newer one
    // arrives. Overwriting it bounds latency and memory even when USB is busy.
    slot.pending = true;
    displayCondition.notify_one();
    return true;
}

bool Mk3Device::flushDisplayFrames(int timeoutMs)
{
    std::unique_lock lock(displayMutex);
    const auto idle = [this]
    {
        return displayTransfersInFlight == 0
            && !displaySlots[0].pending
            && !displaySlots[1].pending;
    };
    if (!displayCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs), idle))
        return false;

    const bool succeeded = !displayTransferFailedSinceFlush;
    displayTransferFailedSinceFlush = false;
    return succeeded;
}

void Mk3Device::displayLoop()
{
    const auto pixelCount = static_cast<size_t>(kDisplayWidth)
                          * static_cast<size_t>(kDisplayHeight);
    std::array<std::vector<uint16_t>, 2> transferBuffers;
    for (auto& buffer : transferBuffers)
        buffer.resize(pixelCount);

    int nextScreen = 0;
    for (;;)
    {
        int screenIndex = 0;
        {
            std::unique_lock lock(displayMutex);
            displayCondition.wait(lock, [this]
            {
                return !displayThreadRunning
                    || displaySlots[0].pending
                    || displaySlots[1].pending;
            });

            if (!displayThreadRunning)
                break;

            if (displaySlots[static_cast<size_t>(nextScreen)].pending)
                screenIndex = nextScreen;
            else
                screenIndex = 1 - nextScreen;
            nextScreen = 1 - screenIndex;

            auto& slot = displaySlots[static_cast<size_t>(screenIndex)];
            slot.pixels.swap(transferBuffers[static_cast<size_t>(screenIndex)]);
            slot.pending = false;
            ++displayTransfersInFlight;
        }

        auto* currentDevice = device.load(std::memory_order_acquire);
        const int result = currentDevice != nullptr
            ? mk3_display_draw(
                currentDevice, screenIndex,
                transferBuffers[static_cast<size_t>(screenIndex)].data())
            : -1;

        {
            std::scoped_lock lock(displayMutex);
            --displayTransfersInFlight;
            if (result != 0)
                displayTransferFailedSinceFlush = true;
        }
        displayCondition.notify_all();
    }

    displayCondition.notify_all();
}

void Mk3Device::stopDisplayThread()
{
    {
        std::scoped_lock lock(displayMutex);
        displayThreadRunning = false;
        for (auto& slot : displaySlots)
            slot.pending = false;
    }
    displayCondition.notify_all();
    if (displayThread.joinable())
        displayThread.join();
}

bool Mk3Device::queueSolidDisplayFrame(int screenIndex, uint16_t color)
{
    if (!isConnected() || screenIndex < 0 || screenIndex > 1)
        return false;

    std::scoped_lock lock(displayMutex);
    if (!displayThreadRunning)
        return false;

    auto& slot = displaySlots[static_cast<size_t>(screenIndex)];
    std::fill(slot.pixels.begin(), slot.pixels.end(), color);
    slot.pending = true;
    displayCondition.notify_one();
    return true;
}

bool Mk3Device::clearDisplay(int screenIndex, uint16_t color)
{
    return queueSolidDisplayFrame(screenIndex, color) && flushDisplayFrames();
}

void Mk3Device::clearAllDisplays()
{
    if (!isConnected())
        return;

    queueSolidDisplayFrame(0, 0);
    queueSolidDisplayFrame(1, 0);
    flushDisplayFrames();
}

bool Mk3Device::setMonoLed(const std::string& ledName, uint8_t brightness)
{
    std::scoped_lock lock(outputMutex);
    auto* currentDevice = device.load(std::memory_order_acquire);
    if (currentDevice == nullptr)
        return false;
    uint8_t updatedReport = 0;
    const auto result = ledBatchDepth > 0
        ? mk3_led_set_brightness_deferred(
              currentDevice, ledName.c_str(), brightness, &updatedReport)
        : mk3_led_set_brightness(currentDevice, ledName.c_str(), brightness);
    if (result == 0 && ledBatchDepth > 0)
    {
        ledReport80Dirty |= updatedReport == 0x80;
        ledReport81Dirty |= updatedReport == 0x81;
    }
    return result == 0;
}

bool Mk3Device::setIndexedLed(const std::string& ledName, uint8_t colorIndex)
{
    std::scoped_lock lock(outputMutex);
    auto* currentDevice = device.load(std::memory_order_acquire);
    if (currentDevice == nullptr)
        return false;
    uint8_t updatedReport = 0;
    const auto result = ledBatchDepth > 0
        ? mk3_led_set_indexed_color_deferred(
              currentDevice, ledName.c_str(), colorIndex, &updatedReport)
        : mk3_led_set_indexed_color(currentDevice, ledName.c_str(), colorIndex);
    if (result == 0 && ledBatchDepth > 0)
    {
        ledReport80Dirty |= updatedReport == 0x80;
        ledReport81Dirty |= updatedReport == 0x81;
    }
    return result == 0;
}

void Mk3Device::beginLedBatch()
{
    std::scoped_lock lock(outputMutex);
    ++ledBatchDepth;
}

void Mk3Device::endLedBatch()
{
    std::scoped_lock lock(outputMutex);
    if (ledBatchDepth <= 0)
        return;

    if (--ledBatchDepth > 0)
        return;

    auto* currentDevice = device.load(std::memory_order_acquire);
    if (currentDevice != nullptr)
    {
        if (ledReport80Dirty
            && mk3_output_flush_report(currentDevice, 0x80) == 0)
            ledReport80Dirty = false;
        if (ledReport81Dirty
            && mk3_output_flush_report(currentDevice, 0x81) == 0)
            ledReport81Dirty = false;
        return;
    }

    ledReport80Dirty = false;
    ledReport81Dirty = false;
}

void Mk3Device::resetAllLeds()
{
    if (!isConnected())
        return;

    // Keep display traffic on its worker; direct display transfers here would
    // race an in-flight async frame.
    clearAllDisplays();

    // The hardware output map is the single source of truth for both the USB
    // controller and emulator reset paths. Build both reports in memory and
    // transmit each once; sending a full report for every LED made startup
    // perform more than 100 redundant synchronous USB transfers.
    std::scoped_lock lock(outputMutex);
    auto* currentDevice = device.load(std::memory_order_acquire);
    if (currentDevice == nullptr)
        return;
    bool report80Dirty = false;
    bool report81Dirty = false;
    for (int i = 0; i < mk3_leds_count; ++i)
    {
        const auto& definition = mk3_leds[i];
        uint8_t updatedReport = 0;
        int result = -1;
        if (definition.type == MK3_LED_TYPE_MONO)
            result = mk3_led_set_brightness_deferred(
                currentDevice, definition.name, 0, &updatedReport);
        else
            result = mk3_led_set_indexed_color_deferred(
                currentDevice, definition.name, 0, &updatedReport);

        if (result == 0)
        {
            report80Dirty |= updatedReport == 0x80;
            report81Dirty |= updatedReport == 0x81;
        }
    }
    if (report80Dirty && mk3_output_flush_report(currentDevice, 0x80) != 0)
        ledReport80Dirty = true;
    if (report81Dirty && mk3_output_flush_report(currentDevice, 0x81) != 0)
        ledReport81Dirty = true;
}

void Mk3Device::pollLoop()
{
    while (running.load(std::memory_order_relaxed))
    {
        auto* currentDevice = device.load(std::memory_order_acquire);
        if (currentDevice == nullptr)
            break;

        const int result = mk3_input_poll_ex(currentDevice);
        if (result < 0)
        {
            logInfo("Polling aborted — device disconnected.");
            stopDisplayThread();
            {
                std::scoped_lock lock(outputMutex);
                if (auto* disconnectedDevice = device.exchange(
                        nullptr, std::memory_order_acq_rel))
                    mk3_close(disconnectedDevice);
            }
            break;
        }

        if (!running.load(std::memory_order_relaxed))
            break;

        // mk3_input_poll_ex already blocks in libusb for up to 100 ms when
        // idle, so immediately re-arm the interrupt transfer. Sleeping here
        // created a 10 ms blind window after every timeout and added avoidable
        // tail latency to the next hardware event without reducing CPU use.
    }

    running.store(false, std::memory_order_relaxed);
}

void Mk3Device::handleButton(const char* name, bool pressed)
{
    if (inputTracingEnabled() && name != nullptr && juce::String(name).startsWith("knobTouch"))
    {
        juce::String msg;
        msg << "[Mk3Device] Button event (knobTouch): name=" << name
            << " pressed=" << (pressed ? "YES" : "NO");
        juce::Logger::writeToLog(msg);
    }
    
    ButtonHandler handlerCopy;
    {
        std::scoped_lock lock(callbackMutex);
        handlerCopy = buttonHandler;
    }

    if (inputLoggingEnabled())
    {
        juce::String msg;
        msg << "button name=" << (name != nullptr ? name : "")
            << " pressed=" << (pressed ? "true" : "false");
        logInfo(msg);
    }

    if (handlerCopy)
    {
        handlerCopy(ButtonEvent{std::string{name ? name : ""}, pressed});
    }
    else if (inputTracingEnabled() && name != nullptr && juce::String(name).startsWith("knobTouch"))
    {
        juce::Logger::writeToLog("[Mk3Device] WARNING: No button handler registered for knobTouch");
    }
}

void Mk3Device::handlePad(uint8_t pad, bool pressed, uint16_t pressure)
{
    PadHandler handlerCopy;
    {
        std::scoped_lock lock(callbackMutex);
        handlerCopy = padHandler;
    }

    if (inputLoggingEnabled())
    {
        juce::String msg;
        msg << "pad " << static_cast<int>(pad)
            << (pressed ? " down" : " up")
            << " pressure=" << static_cast<int>(pressure);
        logInfo(msg);
    }

    if (handlerCopy)
    {
        handlerCopy(PadEvent{pad, pressed, pressure});
    }
}

void Mk3Device::handleKnob(const char* name, int16_t delta, uint16_t absolute)
{
    KnobHandler handlerCopy;
    {
        std::scoped_lock lock(callbackMutex);
        handlerCopy = knobHandler;
    }

    if (inputTracingEnabled())
    {
        juce::String msg;
        msg << "[Mk3Device] Knob event: name=" << (name != nullptr ? name : "")
            << " delta=" << delta
            << " absolute=" << absolute;
        juce::Logger::writeToLog(msg);
    }

    if (inputLoggingEnabled())
    {
        juce::String msg;
        msg << "knob " << (name != nullptr ? name : "")
            << " delta=" << delta
            << " absolute=" << absolute;
        logInfo(msg);
    }

    if (handlerCopy)
    {
        handlerCopy(KnobEvent{std::string{name ? name : ""}, delta, absolute});
    }
    else if (inputTracingEnabled())
    {
        juce::Logger::writeToLog("[Mk3Device] WARNING: No knob handler registered");
    }
}

void Mk3Device::handleStepper(int8_t direction, uint8_t position)
{
    StepperHandler handlerCopy;
    {
        std::scoped_lock lock(callbackMutex);
        handlerCopy = stepperHandler;
    }

    if (inputLoggingEnabled())
    {
        juce::String msg;
        msg << "stepper direction=" << static_cast<int>(direction)
            << " position=" << static_cast<int>(position);
        logInfo(msg);
    }

    if (handlerCopy)
    {
        handlerCopy(StepperEvent{direction, position});
    }
}

void Mk3Device::handleTouchstrip(uint8_t finger, bool touching, uint16_t position)
{
    TouchstripHandler handlerCopy;
    {
        std::scoped_lock lock(callbackMutex);
        handlerCopy = touchstripHandler;
    }

    if (inputLoggingEnabled())
    {
        juce::String msg;
        msg << "touchstrip finger=" << static_cast<int>(finger)
            << " touching=" << (touching ? "true" : "false")
            << " position=" << static_cast<int>(position);
        logInfo(msg);
    }

    if (handlerCopy)
        handlerCopy(TouchstripEvent { finger, touching, position });
}

void Mk3Device::buttonThunk(const char* name, bool pressed, void* user)
{
    if (auto* self = static_cast<Mk3Device*>(user))
        self->handleButton(name, pressed);
}

void Mk3Device::padThunk(uint8_t pad, bool pressed, uint16_t pressure, void* user)
{
    if (auto* self = static_cast<Mk3Device*>(user))
        self->handlePad(pad, pressed, pressure);
}

void Mk3Device::knobThunk(const char* name, int16_t delta, uint16_t absolute, void* user)
{
    if (auto* self = static_cast<Mk3Device*>(user))
        self->handleKnob(name, delta, absolute);
}

void Mk3Device::stepperThunk(int8_t direction, uint8_t position, void* user)
{
    if (auto* self = static_cast<Mk3Device*>(user))
        self->handleStepper(direction, position);
}

void Mk3Device::touchstripThunk(uint8_t finger, bool touching, uint16_t position, void* user)
{
    if (auto* self = static_cast<Mk3Device*>(user))
        self->handleTouchstrip(finger, touching, position);
}
