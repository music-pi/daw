#include "KeyboardInputHandler.h"

#include <typeinfo>

#include <juce_core/juce_core.h>

#include "InputManager.h"

namespace
{
constexpr const char* defaultSourceName = "keyboard";
}

KeyboardInputHandler::KeyboardInputHandler(InputManager& inputMgr)
    : InputHandler(inputMgr)
{
}

KeyboardInputHandler::~KeyboardInputHandler()
{
    stop();
}

void KeyboardInputHandler::start()
{
    if (listening)
        return;

    if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        messageManager == nullptr || !messageManager->isThisTheMessageThread())
    {
        jassertfalse; // KeyboardInputHandler::start must be called on the message thread
        return;
    }

    juce::Desktop::getInstance().addFocusChangeListener(this);
#if JUCE_DEBUG
    juce::Logger::writeToLog("[KeyboardInput] start() – focus listener attached");
#endif
    listening = true;
    attachToComponent(juce::Component::getCurrentlyFocusedComponent());
}

void KeyboardInputHandler::stop()
{
    if (!listening)
        return;

    if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        messageManager == nullptr || !messageManager->isThisTheMessageThread())
    {
        // During JUCE shutdown the message manager may already be gone. In that case
        // best effort: detach without touching Desktop internals.
        detachFromFocusedComponent();
        listening = false;
        return;
    }

    juce::Desktop::getInstance().removeFocusChangeListener(this);
#if JUCE_DEBUG
    juce::Logger::writeToLog("[KeyboardInput] stop() – focus listener removed");
#endif
    detachFromFocusedComponent();
    listening = false;
}

bool KeyboardInputHandler::keyPressed(const juce::KeyPress& keyPress,
                                      juce::Component* originatingComponent)
{
    return handleKeyPress(keyPress, originatingComponent);
}

bool KeyboardInputHandler::keyStateChanged(bool /*isKeyDown*/,
                                           juce::Component* /*originatingComponent*/)
{
    return false;
}

void KeyboardInputHandler::globalFocusChanged(juce::Component* newFocus)
{
    if (!listening)
        return;

#if JUCE_DEBUG
    juce::String msg = "[KeyboardInput] globalFocusChanged -> ";
    msg += (newFocus != nullptr) ? buildComponentPath(newFocus) : juce::String{"<null>"};
    juce::Logger::writeToLog(msg);
#endif

    attachToComponent(newFocus);
}

bool KeyboardInputHandler::handleKeyPress(const juce::KeyPress& keyPress,
                                          juce::Component* origin)
{
    juce::String source { defaultSourceName };
    juce::NamedValueSet metadata;

    const auto modifiers = keyPress.getModifiers();

    metadata.set("device", defaultSourceName);
    metadata.set("keyCode", keyPress.getKeyCode());
    metadata.set("keyDescription", keyPress.getTextDescription());
    metadata.set("textCharacter", juce::String::charToString(keyPress.getTextCharacter()));
    metadata.set("modifierFlags", modifiers.getRawFlags());
    metadata.set("modifierDescription", describeModifiers(modifiers));
    metadata.set("modifierShift", modifiers.isShiftDown());
    metadata.set("modifierCtrl", modifiers.isCtrlDown());
    metadata.set("modifierAlt", modifiers.isAltDown());
    metadata.set("modifierCommand", modifiers.isCommandDown());

    juce::String componentPath;
    juce::String componentName;
    juce::String componentId;

    if (origin != nullptr)
    {
        componentName = origin->getName();
        componentId   = origin->getComponentID();
        componentPath = buildComponentPath(origin);
        const auto hasFocus      = origin->hasKeyboardFocus(false);
        const auto typeName      = juce::String(typeid(*origin).name());

        if (componentId.isNotEmpty())
            metadata.set("componentId", componentId);
        if (componentName.isNotEmpty())
            metadata.set("componentName", componentName);
        if (componentPath.isNotEmpty())
            metadata.set("componentPath", componentPath);

        metadata.set("componentType", typeName);
        metadata.set("hasKeyboardFocus", hasFocus);

        if (componentPath.isNotEmpty())
            source = componentPath;
        else if (componentName.isNotEmpty())
            source = componentName;
    }

#if JUCE_DEBUG
    const auto modifierDescription = metadata.getWithDefault("modifierDescription", {}).toString();
    juce::String logLine;
    logLine << "[KeyboardInput] keyPress keyCode=" << keyPress.getKeyCode()
            << " desc=" << keyPress.getTextDescription()
            << " modifiers=" << (modifierDescription.isNotEmpty() ? modifierDescription : juce::String{"<none>"})
            << " source=" << source;

    if (componentId.isNotEmpty())
        logLine << " componentId=" << componentId;
    if (componentName.isNotEmpty())
        logLine << " componentName=" << componentName;
    if (componentPath.isNotEmpty())
        logLine << " componentPath=" << componentPath;

    juce::Logger::writeToLog(logLine);
#endif

    return inputManager.dispatchKey(keyPress, source, metadata);
}

juce::String KeyboardInputHandler::buildComponentPath(juce::Component* component)
{
    if (component == nullptr)
        return {};

    juce::StringArray segments;

    for (auto* current = component; current != nullptr; current = current->getParentComponent())
    {
        juce::String label;

        if (current->getComponentID().isNotEmpty())
            label = current->getComponentID();
        else if (current->getName().isNotEmpty())
            label = current->getName();
        else
            label = juce::String(typeid(*current).name());

        segments.insert(0, label);
    }

    return segments.joinIntoString("/");
}

juce::String KeyboardInputHandler::describeModifiers(const juce::ModifierKeys& modifiers)
{
    juce::StringArray parts;

    if (modifiers.isShiftDown())
        parts.add("Shift");

    if (modifiers.isAltDown())
        parts.add("Alt");

#if JUCE_MAC
    if (modifiers.isCommandDown())
        parts.add("Cmd");
    if (modifiers.isCtrlDown())
        parts.add("Ctrl");
#else
    if (modifiers.isCtrlDown())
        parts.add("Ctrl");
    if (modifiers.isCommandDown() && !modifiers.isCtrlDown())
        parts.add("Cmd");
#endif

    if (parts.isEmpty())
        return {};

    return parts.joinIntoString("+");
}

void KeyboardInputHandler::attachToComponent(juce::Component* component)
{
    if (focusedComponent == component)
        return;

    detachFromFocusedComponent();

    if (component != nullptr)
    {
        component->addKeyListener(this);
#if JUCE_DEBUG
        juce::Logger::writeToLog("[KeyboardInput] attached to " + buildComponentPath(component));
#endif
        focusedComponent = component;
    }
}

void KeyboardInputHandler::detachFromFocusedComponent()
{
    if (auto* current = focusedComponent.getComponent())
    {
#if JUCE_DEBUG
        juce::Logger::writeToLog("[KeyboardInput] detached from " + buildComponentPath(current));
#endif
        current->removeKeyListener(this);
    }

    focusedComponent = nullptr;
}
