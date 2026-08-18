#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ControllerEvents.h"

#include <juce_core/juce_core.h>

#include "../ui/hw/HardwareState.h"

class AudioEngine;
#include "IController.h"
class Mk3Controller;
class EmulatorController;
class ControllerInputHandler;
class InputManager;
class WindowManager;
namespace juce { class Image; }

class ControllerHost : public HardwareState::FlushTarget {
public:
    explicit ControllerHost(InputManager* inputManager = nullptr, bool enableHardware = true);
    virtual ~ControllerHost();

    void setInputManager(InputManager* inputManager);
    void setWindowManager(WindowManager* wm);

    /** Access the attached InputManager. May be null if none was injected. */
    InputManager* getInputManager() const noexcept { return inputManager; }

    void bindTransport(AudioEngine& engine);

    // Event structs live in ControllerEvents.h so Widget.h and other light
    // consumers can see the layout without pulling in all of ControllerHost's
    // transitive dependencies. Re-exported here for callers that spell them
    // `ControllerHost::PadEvent`.
    using ButtonEvent  = controller_events::ButtonEvent;
    using PadEvent     = controller_events::PadEvent;
    using KnobEvent    = controller_events::KnobEvent;
    using StepperEvent = controller_events::StepperEvent;
    using TouchstripEvent = controller_events::TouchstripEvent;

    using ButtonCallback  = std::function<void(const ButtonEvent&)>;
    using PadCallback     = std::function<void(const PadEvent&)>;
    using KnobCallback    = std::function<void(const KnobEvent&)>;
    using StepperCallback = std::function<void(const StepperEvent&)>;

    using PadListenerId = std::uint32_t;

    virtual void registerPadSelectionCallback(std::function<void(std::optional<int>)> callback);

    PadListenerId addPadListener(PadCallback callback);
    void removePadListener(PadListenerId id);

    void handleButtonEvent(const ButtonEvent& event);
    void handlePadEvent(const PadEvent& event);
    void handleKnobEvent(const KnobEvent& event);
    void handleStepperEvent(const StepperEvent& event);
    void handleTouchstripEvent(const TouchstripEvent& event);

    bool hasController() const noexcept;
    virtual void pushDisplayFrame(int screenIndex, const juce::Image& image);
    void clearDisplay(int screenIndex, uint16_t color = 0);
    void clearAllDisplays();
    void refreshGroupLeds();
    void refreshPadLeds();
    void setControllerInputHandler(ControllerInputHandler* handler);
    virtual void setButtonActive(const std::string& buttonName, bool active);
    void setButtonBrightness(const std::string& buttonName, uint8_t brightness) override;
    void setNavigationLed(const std::string& ledName, uint8_t brightness);
    void setIndexedLed(const std::string& ledName, uint8_t colorIndex) override;
    void setMonoLed(const std::string& ledName, uint8_t brightness) override;
    void beginLedBatch() override;
    void endLedBatch() override;
    void resetAllLeds();
    /** Stop all controller background threads. Call before destroying objects they reference. */
    void shutdownControllers();
    void notifyPadSelection(std::optional<int> padIndex);
    void notifyGroupSelection(int groupIndex);
    void setGroupDetailCallback(std::function<void(int)> callback);
    bool isSelectPressed() const noexcept;
    bool isShiftPressed() const noexcept { return shiftPressed.load(std::memory_order_relaxed); }

protected:
    void updateModifierState(const ButtonEvent& event);

private:
    void dispatchPadListenerAsync(const PadCallback& callback, const PadEvent& event);
    void emitControllerButtonEvent(const juce::String& controlId, const ButtonEvent& event);
    void forEachController(const std::function<void(IController&)>& fn);
    bool anyControllerConnected() const;

    std::unordered_map<std::string, bool> buttonActiveStates;
    struct PadListenerEntry
    {
        PadListenerId id { 0 };
        PadCallback callback;
    };
    std::vector<PadListenerEntry> padListeners;
    PadListenerId nextPadListenerId { 1 };
    std::function<void(std::optional<int>)> padSelectionCallback;
    std::function<void(int)> groupDetailCallback;

    std::atomic<bool> shiftPressed{false};
    std::atomic<bool> macroPressed{false};
    std::atomic<bool> selectPressed{false};
    std::vector<std::unique_ptr<IController>> controllers_;
    AudioEngine* boundAudio{nullptr};
    ControllerInputHandler* controllerInputHandler{nullptr};
    InputManager* inputManager{nullptr};
    WindowManager* windowManager_{nullptr};

    JUCE_DECLARE_WEAK_REFERENCEABLE(ControllerHost)
};
