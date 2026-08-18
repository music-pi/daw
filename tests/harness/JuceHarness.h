#pragma once

#include <functional>
#include <memory>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace testharness
{

/**
 * Ensures JUCE is initialised for the lifetime of the harness.
 */
class JuceFrameworkContext
{
public:
    JuceFrameworkContext();
    ~JuceFrameworkContext();

    void flushMessageQueue(int milliseconds = 0);

private:
    juce::ScopedJuceInitialiser_GUI initialiser;
};

/**
 * Lightweight host that owns a root component without showing a real native window.
 */
class ComponentHost
{
public:
    ComponentHost();

    template <typename ComponentType, typename... Args>
    ComponentType* mount(Args&&... args)
    {
        auto instance = std::make_unique<ComponentType>(std::forward<Args>(args)...);
        instance->setBounds(bounds);
        instance->resized();
        auto* raw = instance.get();
        root = std::move(instance);
        return raw;
    }

    juce::Component* getRoot() const noexcept { return root.get(); }

    void setBounds(const juce::Rectangle<int>& newBounds);

    /**
     * Process pending JUCE messages for deterministic behaviour in tests.
     */
    void flushMessageQueue(int milliseconds = 0);

private:
    JuceFrameworkContext context;
    juce::Rectangle<int> bounds;
    std::unique_ptr<juce::Component> root;
};

} // namespace testharness

