#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "InputHandler.h"

class KeyboardInputHandler : public InputHandler,
                             private juce::KeyListener,
                             private juce::FocusChangeListener
{
public:
    explicit KeyboardInputHandler(InputManager& inputMgr);
    ~KeyboardInputHandler() override;

    std::string_view name() const override { return "KeyboardInput"; }

    void start() override;
    void stop() override;

    bool isActive() const noexcept { return listening; }

    bool handleKeyPress(const juce::KeyPress& keyPress,
                        juce::Component* origin = nullptr);

private:
    bool keyPressed(const juce::KeyPress& keyPress,
                    juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown,
                         juce::Component* originatingComponent) override;
    void globalFocusChanged(juce::Component* newFocus) override;

    static juce::String buildComponentPath(juce::Component* component);
    static juce::String describeModifiers(const juce::ModifierKeys& modifiers);

    void attachToComponent(juce::Component* component);
    void detachFromFocusedComponent();

    juce::Component::SafePointer<juce::Component> focusedComponent;

    bool listening { false };
};
