#include "JuceHarness.h"

namespace testharness
{

JuceFrameworkContext::JuceFrameworkContext() = default;
JuceFrameworkContext::~JuceFrameworkContext() = default;

void JuceFrameworkContext::flushMessageQueue(int milliseconds)
{
    juce::ignoreUnused(milliseconds);
}

ComponentHost::ComponentHost()
    : bounds(juce::Rectangle<int>(0, 0, 960, 272))
{
}

void ComponentHost::setBounds(const juce::Rectangle<int>& newBounds)
{
    bounds = newBounds;
    if (root != nullptr)
    {
        root->setBounds(bounds);
        root->resized();
    }
}

void ComponentHost::flushMessageQueue(int milliseconds)
{
    context.flushMessageQueue(milliseconds);
}

} // namespace testharness

