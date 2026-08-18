#include "InputManager.h"

#include <algorithm>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

InputManager::InputManager() = default;

void InputManager::setActiveContext(const ContextId& contextId, bool active)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (active)
    {
        activeContexts[contextId] = true;
    }
    else
    {
        activeContexts.erase(contextId);
    }
}

bool InputManager::isContextActive(const ContextId& contextId) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = activeContexts.find(contextId);
    return it != activeContexts.end() && it->second;
}

void InputManager::clearAllContexts()
{
    std::lock_guard<std::mutex> lock(mutex);
    activeContexts.clear();
}

InputManager::BindingId InputManager::addHandler(HandlerPriority priority,
                                                const ContextId& context,
                                                InputEvent::Type eventType,
                                                Action action)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = eventType;
    handler->action = std::move(action);

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addMidiHandler(HandlerPriority priority,
                                                      const ContextId& context,
                                                      Action action,
                                                      std::optional<int> channel,
                                                      std::optional<int> number,
                                                      std::optional<int> value)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = InputEvent::Type::Midi;
    handler->action = std::move(action);
    handler->midiChannel = channel;
    handler->midiNumber = number;
    handler->midiValue = value;

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addKeyHandler(HandlerPriority priority,
                                                     const ContextId& context,
                                                     const juce::KeyPress& keyPress,
                                                     Action action)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = InputEvent::Type::Key;
    handler->action = std::move(action);
    handler->keyPressPattern = keyPress;

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addControllerHandler(HandlerPriority priority,
                                                           const ContextId& context,
                                                           const juce::String& controlId,
                                                           Action action)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = InputEvent::Type::Controller;
    handler->action = std::move(action);
    handler->controllerIdPattern = controlId;

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addPadHandler(HandlerPriority priority,
                                                     const ContextId& context,
                                                     Action action,
                                                     std::optional<uint8_t> padIndex)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = InputEvent::Type::Pad;
    handler->action = std::move(action);
    if (padIndex.has_value())
    {
        handler->padPattern = juce::String(static_cast<int>(*padIndex));
    }

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addButtonHandler(HandlerPriority priority,
                                                        const ContextId& context,
                                                        const juce::String& buttonName,
                                                        Action action)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = InputEvent::Type::Button;
    handler->action = std::move(action);
    handler->buttonPattern = buttonName;

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addKnobHandler(HandlerPriority priority,
                                                      const ContextId& context,
                                                      const juce::String& knobName,
                                                      Action action)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = InputEvent::Type::Knob;
    handler->action = std::move(action);
    handler->knobPattern = knobName;

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addStepperHandler(HandlerPriority priority,
                                                          const ContextId& context,
                                                          Action action)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto handler = std::make_shared<Handler>();
    handler->id = nextId++;
    handler->priority = priority;
    handler->context = context;
    handler->eventType = InputEvent::Type::Stepper;
    handler->action = std::move(action);

    const auto id = handler->id;
    insertHandlerSorted(std::move(handler));
    return id;
}

InputManager::BindingId InputManager::addTouchstripHandler(HandlerPriority priority,
                                                           const ContextId& context,
                                                           Action action)
{
    return addHandler(priority, context, InputEvent::Type::Touchstrip, std::move(action));
}

void InputManager::removeHandler(BindingId id)
{
    std::lock_guard<std::mutex> lock(mutex);
    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                   [id](const std::shared_ptr<Handler>& h)
                                   { return h && h->id == id; }),
                   handlers.end());
}

bool InputManager::dispatch(InputEvent& event)
{
    // Snapshot handler pointers under the lock, then release it BEFORE
    // pattern-matching and action invocation. Pattern checks do juce::String
    // compares and hit the activeContexts map; keeping them outside the lock
    // lets add/remove on other threads proceed without contention. Handler
    // bodies stay alive via shared_ptr even if removed between snapshot and
    // invocation — the freshly copied ptrs keep them referenced.
    std::vector<std::shared_ptr<Handler>> snapshot;
    std::unordered_map<ContextId, bool> contextsSnapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot = handlers; // already sorted by priority
        contextsSnapshot = activeContexts;
    }

    bool anyInvoked = false;
    for (const auto& handlerPtr : snapshot)
    {
        if (!handlerPtr)
            continue;
        const auto& handler = *handlerPtr;

        if (!matchesPattern(handler, event))
            continue;

        if (!handler.context.empty())
        {
            auto it = contextsSnapshot.find(handler.context);
            if (it == contextsSnapshot.end() || !it->second)
                continue;
        }

        anyInvoked = true;
        if (handler.action)
        {
            handler.action(event);
            if (event.consumed)
                break;
        }
    }

    return anyInvoked;
}

bool InputManager::dispatchMidi(const juce::MidiMessage& message,
                                 const juce::String& source,
                                 juce::NamedValueSet metadata)
{
    auto event = InputEvent::makeMidi(message, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::dispatchKey(const juce::KeyPress& keyPress,
                                const juce::String& source,
                                juce::NamedValueSet metadata)
{
    auto event = InputEvent::makeKey(keyPress, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::dispatchController(const juce::String& controlId,
                                      const juce::String& source,
                                      juce::NamedValueSet metadata)
{
    auto event = InputEvent::makeController(controlId, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::dispatchPad(uint8_t pad,
                               bool pressed,
                               uint16_t pressure,
                               bool shift,
                               const juce::String& source,
                               juce::NamedValueSet metadata)
{
    auto event = InputEvent::makePad(pad, pressed, pressure, shift, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::dispatchButton(const juce::String& buttonName,
                                  bool pressed,
                                  bool shift,
                                  const juce::String& source,
                                  juce::NamedValueSet metadata)
{
    auto event = InputEvent::makeButton(buttonName, pressed, shift, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::dispatchKnob(const juce::String& knobName,
                                 int16_t delta,
                                 uint16_t absolute,
                                 bool shift,
                                 const juce::String& source,
                                 juce::NamedValueSet metadata)
{
    auto event = InputEvent::makeKnob(knobName, delta, absolute, shift, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::dispatchStepper(int8_t direction,
                                   uint8_t position,
                                   bool shift,
                                   const juce::String& source,
                                   juce::NamedValueSet metadata)
{
    auto event = InputEvent::makeStepper(direction, position, shift, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::dispatchTouchstrip(uint8_t finger,
                                      bool touching,
                                      uint16_t position,
                                      bool shift,
                                      const juce::String& source,
                                      juce::NamedValueSet metadata)
{
    auto event = InputEvent::makeTouchstrip(finger, touching, position, shift, source, std::move(metadata));
    return dispatch(event);
}

bool InputManager::matchesPattern(const Handler& handler, const InputEvent& event) const
{
    if (handler.eventType != event.type)
        return false;

    switch (event.type)
    {
        case InputEvent::Type::Midi:
            return matchesMidiPattern(handler, event.midiMessage);

        case InputEvent::Type::Key:
        {
            // If handler has a key pattern, match it exactly
            if (handler.keyPressPattern.getKeyCode() != 0)
            {
                if (handler.keyPressPattern.getKeyCode() != event.keyPress.getKeyCode())
                    return false;
                if (handler.keyPressPattern.getModifiers() != event.keyPress.getModifiers())
                    return false;
                const auto textA = handler.keyPressPattern.getTextCharacter();
                const auto textB = event.keyPress.getTextCharacter();
                if (textA != 0 && textB != 0 && textA != textB)
                    return false;
            }
            return true;
        }

        case InputEvent::Type::Controller:
            if (!handler.controllerIdPattern.isEmpty())
            {
                juce::String controlId;
                if (event.metadata.contains("controlId"))
                    controlId = event.metadata["controlId"].toString();
                return handler.controllerIdPattern == controlId;
            }
            return true;

        case InputEvent::Type::Pad:
            if (!handler.padPattern.isEmpty())
            {
                int padIndex = -1;
                if (event.metadata.contains("pad"))
                    padIndex = event.metadata["pad"];
                return handler.padPattern == juce::String(padIndex);
            }
            return true;

        case InputEvent::Type::Button:
            if (!handler.buttonPattern.isEmpty())
            {
                juce::String buttonName;
                if (event.metadata.contains("buttonName"))
                    buttonName = event.metadata["buttonName"].toString();
                return handler.buttonPattern == buttonName;
            }
            return true;

        case InputEvent::Type::Knob:
            if (!handler.knobPattern.isEmpty())
            {
                juce::String knobName;
                if (event.metadata.contains("knobName"))
                    knobName = event.metadata["knobName"].toString();
                return handler.knobPattern == knobName;
            }
            return true;

        case InputEvent::Type::Stepper:
            return true; // Stepper handlers match all stepper events

        case InputEvent::Type::Touchstrip:
            return true;

        default:
            return false;
    }
}

bool InputManager::matchesMidiPattern(const Handler& handler, const juce::MidiMessage& message) const
{
    if (handler.midiChannel.has_value() && message.getChannel() != *handler.midiChannel)
        return false;

    if (handler.midiNumber.has_value())
    {
        if (message.isNoteOnOrOff())
        {
            if (message.getNoteNumber() != *handler.midiNumber)
                return false;
        }
        else if (message.isController())
        {
            if (message.getControllerNumber() != *handler.midiNumber)
                return false;
        }
        else if (message.isProgramChange())
        {
            if (message.getProgramChangeNumber() != *handler.midiNumber)
                return false;
        }
        else
        {
            return false;
        }
    }

    if (handler.midiValue.has_value())
    {
        if (message.isNoteOnOrOff())
        {
            const auto velocity = static_cast<int>(message.getRawData()[2]);
            if (velocity != *handler.midiValue)
                return false;
        }
        else if (message.isController())
        {
            if (message.getControllerValue() != *handler.midiValue)
                return false;
        }
        else if (message.isPitchWheel())
        {
            if (message.getPitchWheelValue() != *handler.midiValue)
                return false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void InputManager::insertHandlerSorted(std::shared_ptr<Handler> handler)
{
    // Keeps `handlers` in dispatch-order (highest priority first). Ordering
    // rule mirrors the legacy sortHandlersByPriority: priority DESC, then
    // context-specific before global, then newer id (inserted later) first.
    // Mutex must be held by the caller.
    const auto order = [](const std::shared_ptr<Handler>& a,
                          const std::shared_ptr<Handler>& b)
    {
        if (static_cast<int>(a->priority) != static_cast<int>(b->priority))
            return static_cast<int>(a->priority) > static_cast<int>(b->priority);
        if (a->context.empty() != b->context.empty())
            return !a->context.empty();
        return a->id > b->id;
    };

    auto pos = std::upper_bound(handlers.begin(), handlers.end(), handler, order);
    handlers.insert(pos, std::move(handler));
}
