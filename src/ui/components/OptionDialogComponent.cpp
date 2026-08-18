#include "OptionDialogComponent.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

namespace
{
constexpr float kTextPadding = 16.0f;
}

OptionDialogComponent::OptionDialogComponent()
{
    getTitleBar().setTitle("Dialog");
    getTitleBar().setVisible(true);
    getKnobBar().setVisible(false);
}

void OptionDialogComponent::setTitle(const juce::String& title)
{
    getTitleBar().setTitle(title);
}

void OptionDialogComponent::setMessage(const juce::String& message)
{
    if (messageText == message)
        return;
    messageText = message;
    repaint();
    requestUiRefresh(*this);
}

void OptionDialogComponent::setButtons(const std::vector<Button>& buttons)
{
    currentButtons = buttons;

    std::vector<ScreenView::Option> options;
    options.reserve(4);

    for (size_t i = 0; i < 4; ++i)
    {
        if (i < currentButtons.size())
        {
            const auto& button = currentButtons[i];
            options.push_back(ScreenView::Option{
                .id = "dialog_button_" + juce::String(i),
                .label = button.label,
                .state = button.enabled ? OptionState::Enabled : OptionState::Disabled,
                .onInvoke = [this, i]()
                {
                    if (i < currentButtons.size() && currentButtons[i].onClick)
                        currentButtons[i].onClick();
                },
                .onToggle = nullptr,
                .onOperationNavigatorPart = nullptr
            });
        }
        else
        {
            options.push_back(ScreenView::Option{ .state = OptionState::Empty });
        }
    }

    setOptions(std::move(options));
}

void OptionDialogComponent::paint(juce::Graphics& g)
{
    ScreenView::paint(g);

    auto bounds = getContentBounds().toFloat();
    g.setColour(UiTheme::kBackgroundDark);
    g.fillRoundedRectangle(bounds.reduced(8.0f), 6.0f);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(UiTheme::Fonts::kBody);
    g.drawFittedText(messageText,
                     bounds.toNearestInt().reduced(static_cast<int>(kTextPadding)),
                     juce::Justification::centred,
                     4);
}

void OptionDialogComponent::resized()
{
    ScreenView::resized();
}

