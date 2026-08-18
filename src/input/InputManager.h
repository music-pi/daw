#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "InputEvent.h"

class InputManager
{
public:
    using Action = std::function<void(InputEvent&)>;
    using BindingId = std::uint32_t;
    using ContextId = std::string;

    enum class HandlerPriority
    {
        Modal = 200,     // Foreground modal widgets (piano roll, dialogs)
        View = 100,      // Active views (e.g., AudioEditor)
        Component = 50, // UI components
        Global = 0      // Default handlers (e.g., UiHost)
    };

    struct Handler
    {
        BindingId id;
        HandlerPriority priority;
        ContextId context; // Empty string means global handler
        InputEvent::Type eventType;
        Action action;

        // Pattern matching data (for MIDI, Key, Controller types)
        std::optional<int> midiChannel;
        std::optional<int> midiNumber;
        std::optional<int> midiValue;
        juce::KeyPress keyPressPattern; // For key matching
        juce::String controllerIdPattern;
        juce::String padPattern;      // For pad index matching
        juce::String buttonPattern;   // For button name matching
        juce::String knobPattern;    // For knob name matching
    };

    InputManager();
    ~InputManager() = default;

    // Context management
    void setActiveContext(const ContextId& contextId, bool active);
    bool isContextActive(const ContextId& contextId) const;
    void clearAllContexts();

    // Handler registration
    BindingId addHandler(HandlerPriority priority,
                         const ContextId& context,
                         InputEvent::Type eventType,
                         Action action);

    // Convenience methods for specific event types
    BindingId addMidiHandler(HandlerPriority priority,
                             const ContextId& context,
                             Action action,
                             std::optional<int> channel = {},
                             std::optional<int> number = {},
                             std::optional<int> value = {});

    BindingId addKeyHandler(HandlerPriority priority,
                            const ContextId& context,
                            const juce::KeyPress& keyPress,
                            Action action);

    BindingId addControllerHandler(HandlerPriority priority,
                                    const ContextId& context,
                                    const juce::String& controlId,
                                    Action action);

    BindingId addPadHandler(HandlerPriority priority,
                            const ContextId& context,
                            Action action,
                            std::optional<uint8_t> padIndex = {});

    BindingId addButtonHandler(HandlerPriority priority,
                                const ContextId& context,
                                const juce::String& buttonName,
                                Action action);

    BindingId addKnobHandler(HandlerPriority priority,
                              const ContextId& context,
                              const juce::String& knobName,
                              Action action);

    BindingId addStepperHandler(HandlerPriority priority,
                                 const ContextId& context,
                                 Action action);

    BindingId addTouchstripHandler(HandlerPriority priority,
                                    const ContextId& context,
                                    Action action);

    // Handler removal
    void removeHandler(BindingId id);

    // Event dispatch
    bool dispatch(InputEvent& event);

    // Convenience dispatch methods
    bool dispatchMidi(const juce::MidiMessage& message,
                      const juce::String& source,
                      juce::NamedValueSet metadata = {});

    bool dispatchKey(const juce::KeyPress& keyPress,
                     const juce::String& source,
                     juce::NamedValueSet metadata = {});

    bool dispatchController(const juce::String& controlId,
                            const juce::String& source,
                            juce::NamedValueSet metadata = {});

    bool dispatchPad(uint8_t pad,
                     bool pressed,
                     uint16_t pressure,
                     bool shift,
                     const juce::String& source,
                     juce::NamedValueSet metadata = {});

    bool dispatchButton(const juce::String& buttonName,
                       bool pressed,
                       bool shift,
                       const juce::String& source,
                       juce::NamedValueSet metadata = {});

    bool dispatchKnob(const juce::String& knobName,
                      int16_t delta,
                      uint16_t absolute,
                      bool shift,
                      const juce::String& source,
                      juce::NamedValueSet metadata = {});

    bool dispatchStepper(int8_t direction,
                         uint8_t position,
                         bool shift,
                         const juce::String& source,
                         juce::NamedValueSet metadata = {});

    bool dispatchTouchstrip(uint8_t finger,
                            bool touching,
                            uint16_t position,
                            bool shift,
                            const juce::String& source,
                            juce::NamedValueSet metadata = {});

private:
    bool matchesPattern(const Handler& handler, const InputEvent& event) const;
    bool matchesMidiPattern(const Handler& handler, const juce::MidiMessage& message) const;
    // caller must hold `mutex`
    void insertHandlerSorted(std::shared_ptr<Handler> handler);

    BindingId nextId { 1 };
    // shared_ptr values so dispatch() can snapshot pointers under the lock
    // and invoke actions without holding it — the Handler bodies stay alive
    // even if another thread removes them mid-dispatch.
    std::vector<std::shared_ptr<Handler>> handlers;
    std::unordered_map<ContextId, bool> activeContexts;
    mutable std::mutex mutex;
};
