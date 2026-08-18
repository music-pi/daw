#include "ConfirmDialog.h"

#include "WindowManager.h"
#include "../theme/UiTheme.h"

ConfirmDialog::ConfirmDialog(juce::String title,
                             juce::String message,
                             juce::String confirmLabel,
                             std::function<void()> onConfirm,
                             juce::Colour confirmColour)
    : title_(std::move(title))
    , message_(std::move(message))
    , confirmLabel_(std::move(confirmLabel))
    , onConfirm_(std::move(onConfirm))
    , confirmColour_(confirmColour)
{
}

WidgetDescriptor ConfirmDialog::describe() const
{
    const auto constraint = (preferredSide_ == DisplaySide::Right)
                                ? DisplayConstraint::RightOnly
                                : DisplayConstraint::LeftOnly;
    return { "confirm_dialog", 1, true, constraint };
}

void ConfirmDialog::onActivated(int offset)
{
    panelOffset_ = offset;
}

void ConfirmDialog::onDeactivated()
{
}

std::vector<std::string> ConfirmDialog::requiredResources(int /*page*/)
{
    if (preferredSide_ == DisplaySide::Right)
        return { "d5", "d6", "d7", "d8" };
    return { "d1", "d2", "d3", "d4" };
}

std::vector<Option> ConfirmDialog::getOptions(int /*page*/)
{
    std::vector<Option> opts(4);

    opts[0] = Option{
        .id = "cancel",
        .label = "Cancel",
        .state = OptionState::Enabled,
        .onInvoke = [this]() { dismiss(); }
    };
    opts[1] = Option{ .id = "empty2", .label = "", .state = OptionState::Empty };
    opts[2] = Option{ .id = "empty3", .label = "", .state = OptionState::Empty };
    opts[3] = Option{
        .id = "confirm",
        .label = confirmLabel_,
        .state = OptionState::Enabled,
        .onInvoke = [this]() {
            auto action = std::move(onConfirm_);
            dismiss(); // pops the dialog and destroys `this`
            if (action)
                action();
        }
    };

    return opts;
}

void ConfirmDialog::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void ConfirmDialog::paintPage(juce::Graphics& g, int /*page*/, juce::Rectangle<int> bounds)
{
    // Titlebar (red-ish accent to signal destructive action)
    auto titleArea = bounds.removeFromTop(UiTheme::kTitlebarHeight);
    g.setColour(UiTheme::kTitlebarBackground);
    g.fillRect(titleArea);

    auto accentRect = titleArea.removeFromLeft(UiTheme::kAccentWidth);
    g.setColour(confirmColour_);
    g.fillRect(accentRect);

    auto inner = titleArea.reduced(6, 0);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarTitle, juce::Font::bold)));
    g.drawText(title_, inner, juce::Justification::centredLeft, false);

    // Message body
    auto body = bounds.reduced(16);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarTitle)));
    g.drawFittedText(message_, body, juce::Justification::centred, 3);
}

void ConfirmDialog::dismiss()
{
    if (auto* wm = windowManager())
        wm->dismissDialog();
}
