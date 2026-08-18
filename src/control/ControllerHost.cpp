#include "ControllerHost.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <unordered_map>

#include "../engine/AudioEngine.h"
#include "Mk3Controller.h"
#include "EmulatorController.h"
#include "HardwareConstants.h"
#include "../input/ControllerInputHandler.h"
#include "../input/InputManager.h"
#include "../input/InputEvent.h"
#include "../ui/widget/WindowManager.h"

namespace {

const std::unordered_map<std::string, juce::String> kTransportButtonMap{
    {"play",        juce::String{"transport.play"}},
    {"stop",        juce::String{"transport.stop"}},
    {"recCountIn",  juce::String{"transport.record"}},
    {"restartLoop", juce::String{"transport.loop.toggle"}}
};

const std::unordered_map<std::string, juce::String> kNavButtonMap{
    {"navUp",    juce::String{"nav.up"}},
    {"navDown",  juce::String{"nav.down"}},
    {"navLeft",  juce::String{"nav.left"}},
    {"navRight", juce::String{"nav.right"}},
    {"navPush",  juce::String{"nav.select"}}
};

}

ControllerHost::ControllerHost(InputManager* inputManager, bool enableHardware)
    : inputManager(inputManager)
{
    if (enableHardware)
    {
        controllers_.push_back(std::make_unique<Mk3Controller>(*this));
        controllers_.push_back(std::make_unique<EmulatorController>(*this));
    }
    setButtonActive("select", true);
}

void ControllerHost::setInputManager(InputManager* inputManager)
{
    this->inputManager = inputManager;
}

void ControllerHost::setWindowManager(WindowManager* wm)
{
    windowManager_ = wm;
}

ControllerHost::~ControllerHost()
{
    shutdownControllers();
    // No resetAllLeds() — shutdownControllers has closed every controller,
    // so any LED write is a no-op on a closed device. Historically this call
    // was both ineffective and mid-shutdown blocking when the MK3 was slow.
    if (controllerInputHandler)
        controllerInputHandler->detachDevice();
    controllerInputHandler = nullptr;
}

void ControllerHost::shutdownControllers()
{
    for (auto& ctrl : controllers_)
    {
        if (ctrl)
            ctrl->shutdown();
    }
    windowManager_ = nullptr;
    inputManager = nullptr;
}

void ControllerHost::bindTransport(AudioEngine& engine)
{
    boundAudio = &engine;

    // Transport buttons — Global InputManager handlers
    setButtonActive("play", true);
    inputManager->addButtonHandler(InputManager::HandlerPriority::Global, "", "play",
        [this](InputEvent& event) {
            if (!event.metadata.contains("pressed") || !static_cast<bool>(event.metadata["pressed"])) return;
            if (boundAudio)
            {
                if (boundAudio->isPlaying())
                    boundAudio->stop();
                else
                    boundAudio->play();
            }
            event.consumed = true;
        });

    setButtonActive("stop", true);
    inputManager->addButtonHandler(InputManager::HandlerPriority::Global, "", "stop",
        [this](InputEvent& event) {
            if (!event.metadata.contains("pressed") || !static_cast<bool>(event.metadata["pressed"])) return;
            // The fullscreen PipeWire recorder uses the physical Stop button
            // to finalize its take. Let the event reach WindowManager while
            // that suite is active instead of consuming it as transport Stop.
            if (boundAudio && boundAudio->isAudioCaptureSuiteActive())
                return;
            if (boundAudio) boundAudio->stop();
            event.consumed = true;
        });

    setButtonActive("restartLoop", true);
    inputManager->addButtonHandler(InputManager::HandlerPriority::Global, "", "restartLoop",
        [this](InputEvent& event) {
            if (!event.metadata.contains("pressed") || !static_cast<bool>(event.metadata["pressed"])) return;
            if (boundAudio) boundAudio->toggleLoop();
            event.consumed = true;
        });

}

void ControllerHost::registerPadSelectionCallback(std::function<void(std::optional<int>)> callback)
{
    padSelectionCallback = std::move(callback);
}

ControllerHost::PadListenerId ControllerHost::addPadListener(PadCallback callback)
{
    PadListenerEntry entry;
    entry.id = nextPadListenerId++;
    entry.callback = std::move(callback);
    padListeners.push_back(std::move(entry));
    return padListeners.back().id;
}

void ControllerHost::removePadListener(PadListenerId id)
{
    padListeners.erase(std::remove_if(padListeners.begin(), padListeners.end(),
                                      [id](const PadListenerEntry& entry)
                                      {
                                          return entry.id == id;
                                      }),
                       padListeners.end());
}

void ControllerHost::handleButtonEvent(const ButtonEvent& event)
{
    if (event.name == "shift")
    {
        // relaxed — modifier flags observed via simple current-state reads;
        // no data-dependent publication ordering between flag and other state.
        const bool prev = shiftPressed.exchange(event.pressed, std::memory_order_relaxed);
        if (prev != event.pressed && windowManager_ != nullptr)
            windowManager_->handleShiftModifierChanged(event.pressed);
    }
    else if (event.name == "macroSet")
        macroPressed.store(event.pressed, std::memory_order_relaxed);
    else if (event.name == "select")
        selectPressed.store(event.pressed, std::memory_order_relaxed);

    // Route through InputManager
    if (inputManager != nullptr)
    {
        auto inputEvent = InputEvent::makeButton(juce::String(event.name), event.pressed, event.shift, "mk3");
        inputManager->dispatch(inputEvent);
        if (inputEvent.consumed)
            return;
    }

    // ControllerInputHandler (MIDI-style dispatch for external integrations)
    if (controllerInputHandler)
    {
        if (auto iter = kTransportButtonMap.find(event.name); iter != kTransportButtonMap.end())
        {
            if (event.pressed)
                emitControllerButtonEvent(iter->second, event);
            return;
        }
        if (auto navIter = kNavButtonMap.find(event.name); navIter != kNavButtonMap.end())
        {
            if (event.pressed)
                emitControllerButtonEvent(navIter->second, event);
            // Fall through to WindowManager so widgets receive nav events
        }
    }

    // Route to WindowManager (new widget system) if not consumed
    if (windowManager_ != nullptr)
    {
        windowManager_->handleButtonEvent(event);
        return;
    }

    if (event.name == "shift" || event.name == "select" || event.name == "macroSet")
        return;

    DBG("[ControllerHost] Unhandled button: " << event.name << " pressed=" << (event.pressed ? "true" : "false"));
}

void ControllerHost::handlePadEvent(const PadEvent& event)
{
    // Route through InputManager
    if (inputManager != nullptr)
    {
        juce::NamedValueSet metadata;
        metadata.set("macro", event.macro);
        auto inputEvent = InputEvent::makePad(event.pad, event.pressed, event.pressure, event.shift, "mk3", metadata);
        inputManager->dispatch(inputEvent);
        if (inputEvent.consumed)
            return;
    }

    // Route to WindowManager (new widget system) if not consumed
    if (windowManager_ != nullptr)
        windowManager_->handlePadEvent(event);

    // Pad listeners (used for non-exclusive monitoring, e.g. level meters).
    // Iterate in-place instead of copying the vector per event — dispatches
    // are async (MessageManager::callAsync), so any mid-dispatch mutation
    // from a callback runs on a later message-loop pump, not inside this
    // loop. The defensive min-with-current-size guards against synchronous
    // add/remove from non-callback paths racing through; any added listener
    // is simply skipped this round and picked up on the next event.
    const auto n = padListeners.size();
    for (size_t i = 0; i < n && i < padListeners.size(); ++i)
    {
        const auto& entry = padListeners[i];
        if (entry.callback)
            dispatchPadListenerAsync(entry.callback, event);
    }
}

void ControllerHost::handleKnobEvent(const KnobEvent& event)
{
    if (inputManager != nullptr)
    {
        auto inputEvent = InputEvent::makeKnob(juce::String(event.name), event.delta, event.absolute, event.shift, "mk3");
        inputManager->dispatch(inputEvent);
        if (inputEvent.consumed)
            return;
    }

    // Route to WindowManager (new widget system) if not consumed
    if (windowManager_ != nullptr)
        windowManager_->handleKnobEvent(event.name, event.delta, event.absolute, event.shift);
}

void ControllerHost::handleStepperEvent(const StepperEvent& event)
{
    // Route through InputManager as stepper event
    if (inputManager != nullptr)
    {
        auto inputEvent = InputEvent::makeStepper(event.direction, event.position, event.shift, "mk3");
        inputManager->dispatch(inputEvent);
        if (inputEvent.consumed)
            return;
    }

    if (event.direction == 0)
        return;

    // Synthesize navUp/navDown and route through the same paths as a real button press
    const std::string navName = event.direction > 0 ? "navDown" : "navUp";
    ButtonEvent synth { navName, true, event.shift };
    handleButtonEvent(synth);
}

void ControllerHost::handleTouchstripEvent(const TouchstripEvent& event)
{
    if (inputManager != nullptr)
    {
        auto inputEvent = InputEvent::makeTouchstrip(
            event.finger, event.touching, event.position, event.shift, "mk3");
        inputManager->dispatch(inputEvent);
        if (inputEvent.consumed)
            return;
    }

    if (windowManager_ != nullptr)
        windowManager_->handleTouchstripEvent(event);
}

void ControllerHost::updateModifierState(const ButtonEvent& event)
{
    if (event.name == "shift")
        shiftPressed.store(event.pressed, std::memory_order_relaxed);
    else if (event.name == "macroSet")
        macroPressed.store(event.pressed, std::memory_order_relaxed);
    else if (event.name == "select")
        selectPressed.store(event.pressed, std::memory_order_relaxed);
}

void ControllerHost::dispatchPadListenerAsync(const PadCallback& callback, const PadEvent& event)
{
    if (!callback)
        return;

    auto handlerCopy = callback;
    juce::MessageManager::callAsync([handlerCopy, event]() mutable
    {
        if (handlerCopy)
            handlerCopy(event);
    });
}

bool ControllerHost::hasController() const noexcept
{
    return anyControllerConnected();
}

void ControllerHost::pushDisplayFrame(int screenIndex, const juce::Image& image)
{
    forEachController([&](IController& c) { c.sendDisplayFrame(screenIndex, image); });
}

void ControllerHost::clearDisplay(int screenIndex, uint16_t color)
{
    forEachController([&](IController& c) { c.clearDisplay(screenIndex, color); });
}

void ControllerHost::clearAllDisplays()
{
    forEachController([](IController& c) { c.clearAllDisplays(); });
}

void ControllerHost::beginLedBatch()
{
    forEachController([](IController& controller) { controller.beginLedBatch(); });
}

void ControllerHost::endLedBatch()
{
    forEachController([](IController& controller) { controller.endLedBatch(); });
}

void ControllerHost::refreshGroupLeds()
{
    if (windowManager_ != nullptr)
        windowManager_->getHardwareState().markSetDirty(ResourceSet::GroupLeds);
}

void ControllerHost::refreshPadLeds()
{
    if (windowManager_ != nullptr)
        windowManager_->getHardwareState().markSetDirty(ResourceSet::PadLeds);
}

void ControllerHost::setMonoLed(const std::string& ledName, uint8_t brightness)
{
    forEachController([&](IController& c) { c.setMonoLed(ledName, brightness); });
}

void ControllerHost::setControllerInputHandler(ControllerInputHandler* handler)
{
    if (controllerInputHandler == handler)
        return;

    if (controllerInputHandler)
        controllerInputHandler->detachDevice();

    controllerInputHandler = handler;

    if (controllerInputHandler)
    {
        Mk3Device* devicePtr = nullptr;
        for (auto& ctrl : controllers_)
        {
            if (auto* mk3ctrl = dynamic_cast<Mk3Controller*>(ctrl.get()))
            {
                devicePtr = mk3ctrl->getDevice();
                break;
            }
        }
        controllerInputHandler->attachDevice(devicePtr);
    }
}

void ControllerHost::setButtonActive(const std::string& buttonName, bool active)
{
    buttonActiveStates[buttonName] = active;

    forEachController([&](IController& c) { c.setButtonActive(buttonName, active); });
}

void ControllerHost::setButtonBrightness(const std::string& buttonName, uint8_t brightness)
{
    // If button was registered as active (via setButtonActive), enforce minimum
    // dim brightness so Mk3Controller's state.active stays true.
    auto it = buttonActiveStates.find(buttonName);
    uint8_t effectiveBrightness = brightness;
    if (it != buttonActiveStates.end() && it->second && brightness == 0)
        effectiveBrightness = HardwareConstants::kLedDim;

    forEachController([&](IController& c) { c.setButtonBrightness(buttonName, effectiveBrightness); });
}

void ControllerHost::setNavigationLed(const std::string& ledName, uint8_t colorIndex)
{
    // Nav LEDs (navUp, navDown, navLeft, navRight) are indexed LEDs, not monochromatic
    setIndexedLed(ledName, colorIndex);
}

void ControllerHost::setIndexedLed(const std::string& ledName, uint8_t colorIndex)
{
    forEachController([&](IController& c) { c.setIndexedLed(ledName, colorIndex); });
}

void ControllerHost::resetAllLeds()
{
    for (auto& ctrl : controllers_)
    {
        if (ctrl)
            ctrl->resetAllLeds();
    }
}

void ControllerHost::notifyPadSelection(std::optional<int> padIndex)
{
    if (padSelectionCallback)
        padSelectionCallback(std::move(padIndex));
}

void ControllerHost::notifyGroupSelection(int groupIndex)
{
    if (groupDetailCallback)
        groupDetailCallback(groupIndex);
}

void ControllerHost::setGroupDetailCallback(std::function<void(int)> callback)
{
    groupDetailCallback = std::move(callback);
}

void ControllerHost::emitControllerButtonEvent(const juce::String& controlId, const ButtonEvent& event)
{
    if (!controllerInputHandler)
        return;

    juce::NamedValueSet metadata;
    metadata.set("pressed", event.pressed);
    metadata.set("shift", event.shift);
    controllerInputHandler->emitControl(controlId, metadata, "mk3");
}

bool ControllerHost::isSelectPressed() const noexcept
{
    return selectPressed.load(std::memory_order_relaxed);
}

void ControllerHost::forEachController(const std::function<void(IController&)>& fn)
{
    for (auto& ctrl : controllers_)
    {
        if (ctrl && ctrl->isConnected())
            fn(*ctrl);
    }
}

bool ControllerHost::anyControllerConnected() const
{
    for (const auto& ctrl : controllers_)
    {
        if (ctrl && ctrl->isConnected())
            return true;
    }
    return false;
}
