#pragma once

#include "../screen/Screen.h"

class OptionDialogComponent : public ScreenView
{
public:
    struct Button
    {
        juce::String label;
        std::function<void()> onClick;
        bool enabled { true };
    };

    OptionDialogComponent();
    ~OptionDialogComponent() override = default;

    juce::String screenName() const override { return "Dialog"; }

    void setTitle(const juce::String& title);
    void setMessage(const juce::String& message);
    void setButtons(const std::vector<Button>& buttons);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String messageText;
    std::vector<Button> currentButtons;
};

