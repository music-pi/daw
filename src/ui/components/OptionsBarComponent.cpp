#include "OptionsBarComponent.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

namespace
{
const juce::String kLeftIndicator  = juce::String(juce::CharPointer_UTF8("\u25C0"));
const juce::String kRightIndicator = juce::String(juce::CharPointer_UTF8("\u25B6"));
} // namespace

OptionsBarComponent::OptionsBarComponent()
{
    setInterceptsMouseClicks(true, false);
    slots.fill({});
}

void OptionsBarComponent::setOptions(const std::vector<Option>& options)
{
    // Short-circuit when the visible option set is byte-equal to the last one
    // we laid out. Callers (WindowManager::refreshBars) invoke this every
    // tick — without this gate we were hammering slot layout + repaint on
    // every frame even when nothing had changed. Note: onInvoke / onToggle
    // lambdas are intentionally NOT compared; we trust that identical
    // visible state means identical behaviour for this frame.
    if (previousOptions_.size() == options.size())
    {
        bool same = true;
        for (size_t i = 0; i < options.size(); ++i)
        {
            const auto& a = options[i];
            const auto& b = previousOptions_[i];
            if (a.label != b.label || a.state != b.state || a.span != b.span
                || a.isOperationNavigator != b.isOperationNavigator)
            {
                same = false;
                break;
            }
        }
        if (same)
            return;
    }

    ++rebuildCount_;
    slots.fill({});

    size_t slotIndex = 0;
    for (size_t optionIndex = 0; optionIndex < options.size() && slotIndex < slots.size(); ++optionIndex)
    {
        const auto& option = options[optionIndex];
        const int remainingSlots = static_cast<int>(slots.size() - slotIndex);
        const int span = juce::jlimit(1, remainingSlots, option.span);

        auto& startSlot = slots[slotIndex];
        startSlot.option = option;
        startSlot.option.span = span;
        startSlot.hasOption = true;
        startSlot.isStart = true;
        startSlot.span = span;
        startSlot.optionIndex = static_cast<int>(optionIndex);
        startSlot.startSlot = static_cast<int>(slotIndex);

        for (int offset = 1; offset < span; ++offset)
        {
            auto& follower = slots[slotIndex + static_cast<size_t>(offset)];
            follower.hasOption = true;
            follower.isStart = false;
            follower.span = 1;
            follower.optionIndex = static_cast<int>(optionIndex);
            follower.startSlot = static_cast<int>(slotIndex);
        }

        slotIndex += static_cast<size_t>(span);
    }

    // Store the lightweight snapshot for the next comparison.
    previousOptions_.clear();
    previousOptions_.reserve(options.size());
    for (const auto& o : options)
        previousOptions_.push_back({ o.label, o.state,
                                     juce::jmax(1, o.span),
                                     o.isOperationNavigator });

    repaint();
    requestUiRefresh(*this);
}

void OptionsBarComponent::setPressedSlot(int slotIndex)
{
    if (pressedSlot_ == slotIndex)
        return;
    pressedSlot_ = slotIndex;
    repaint();
    requestUiRefresh(*this);
}

int OptionsBarComponent::getOptionIndexForSlot(size_t slotIndex) const
{
    if (slotIndex >= slots.size())
        return -1;
    
    const auto& slot = slots[slotIndex];
    if (!slot.hasOption)
        return -1;
    
    return slot.optionIndex;
}

void OptionsBarComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // Background — matches Figma MK3 Display primitive (Option Bar sits on #111111)
    g.setColour(UiTheme::kKnobBarBackground);
    g.fillRect(bounds);

    const float tileWidth = static_cast<float>(bounds.getWidth()) / static_cast<float>(slots.size());

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& slot = slots[i];
        if (!slot.hasOption)
            continue;

        const auto& startSlot = slot.isStart ? slot : slots[static_cast<size_t>(slot.startSlot)];
        if (!slot.isStart)
            continue;
        
        // Skip rendering if option is an empty spacer
        if (startSlot.option.state == OptionState::Empty)
            continue;

        const float spanWidth = tileWidth * static_cast<float>(juce::jmax(1, startSlot.span));

        juce::Rectangle<float> tile(
            static_cast<float>(bounds.getX()) + static_cast<float>(i) * tileWidth,
            static_cast<float>(bounds.getY()),
            spanWidth,
            static_cast<float>(juce::jmax(bounds.getHeight(), UiTheme::kOptionHeight)));

        auto drawArea = tile.reduced(1.0f);

        const auto& option = startSlot.option;

        // Force Active appearance while the hardware button is physically held.
        // Multi-slot options (operation navigators spanning slots 0-1, for example)
        // can be pressed from any of their slots; we consider the whole option
        // pressed if EITHER this slot index matches pressedSlot_ OR the start
        // slot of this option's span matches it.
        const bool slotIsPressed = (pressedSlot_ >= 0)
            && (pressedSlot_ == static_cast<int>(i)
                || pressedSlot_ == startSlot.startSlot);
        OptionState effectiveState = slotIsPressed ? OptionState::Active : option.state;

        juce::Colour border, fill, text;

        switch (effectiveState)
        {
            case OptionState::Active:
                fill = UiTheme::kOptionFillActive;
                border = UiTheme::kOptionBorderActive;
                text = UiTheme::kOptionTextActive;
                break;
            case OptionState::Enabled:
                fill = UiTheme::kOptionFillEnabled;
                border = UiTheme::kOptionBorderEnabled;
                text = UiTheme::kOptionTextEnabled;
                break;
            case OptionState::Disabled:
                fill = UiTheme::kOptionFillDisabled;
                border = UiTheme::kOptionBorderDisabled;
                text = UiTheme::kOptionTextDisabled;
                break;
            case OptionState::Empty:
                fill = UiTheme::kOptionFillEmpty;
                border = UiTheme::kOptionBorderEmpty;
                text = juce::Colours::transparentBlack;
                break;
        }

        g.setColour(fill);
        g.fillRect(drawArea);

        g.setColour(border);
        g.drawRect(drawArea, 1.0f);

        g.setColour(text);
        g.setFont(juce::Font(juce::FontOptions("Inter", "Medium", UiTheme::Fonts::kOptionLabel)));

        if (option.span > 1)
        {
            constexpr int kIndicatorWidth = 20;
            const auto leftRect = juce::Rectangle<int>(
                static_cast<int>(drawArea.getX()) + 4,
                static_cast<int>(drawArea.getY()),
                kIndicatorWidth,
                static_cast<int>(drawArea.getHeight()));

            const auto rightRect = juce::Rectangle<int>(
                static_cast<int>(drawArea.getRight()) - 4 - kIndicatorWidth,
                static_cast<int>(drawArea.getY()),
                kIndicatorWidth,
                static_cast<int>(drawArea.getHeight()));

            auto labelRect = juce::Rectangle<int>(
                leftRect.getRight(),
                static_cast<int>(drawArea.getY()),
                rightRect.getX() - leftRect.getRight(),
                static_cast<int>(drawArea.getHeight()));

            // Draw left indicator
            g.drawText(kLeftIndicator,
                       leftRect,
                       juce::Justification::centred,
                       false);

            // Draw right indicator
            g.drawText(kRightIndicator,
                       rightRect,
                       juce::Justification::centred,
                       false);

            // Draw operation name in center
            g.drawFittedText(option.label,
                             labelRect.reduced(UiTheme::kSlotPadding, 0),
                             juce::Justification::centred,
                             1);
        }
        else if (option.label.isNotEmpty())
        {
            g.drawFittedText(option.label,
                             drawArea.toNearestInt().reduced(UiTheme::kSlotPadding),
                             juce::Justification::centred,
                             1);
        }
    }
}

void OptionsBarComponent::mouseUp(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;

    auto bounds = getLocalBounds();
    if (!bounds.contains(event.getPosition()))
        return;

    const float tileWidth = static_cast<float>(bounds.getWidth()) / static_cast<float>(slots.size());
    const int index = static_cast<int>((event.position.x - static_cast<float>(bounds.getX())) / tileWidth);

    if (!juce::isPositiveAndBelow(index, static_cast<int>(slots.size())))
        return;

    const auto& slot = slots[static_cast<size_t>(index)];
    if (!slot.hasOption)
        return;

    const auto& startSlot = slot.isStart ? slot : slots[static_cast<size_t>(slot.startSlot)];
    if (startSlot.option.state == OptionState::Disabled || startSlot.option.state == OptionState::Empty)
        return;

    // Check if this is an operation navigator with left/right clickable parts
    if (startSlot.option.isOperationNavigator && startSlot.span == 2 && onOperationNavigatorTriggered)
    {
        // Calculate the bounds of the 2-slot option
        const float spanWidth = tileWidth * 2.0f;
        const float optionStartX = static_cast<float>(startSlot.startSlot) * tileWidth;
        const float optionEndX = optionStartX + spanWidth;
        
        constexpr int kIndicatorWidth = 20;
        const float leftPartEnd = optionStartX + 4.0f + static_cast<float>(kIndicatorWidth);
        const float rightPartStart = optionEndX - 4.0f - static_cast<float>(kIndicatorWidth);
        
        const float clickX = event.position.x;
        
        if (clickX < leftPartEnd)
        {
            // Clicked left part
            onOperationNavigatorTriggered(static_cast<size_t>(startSlot.optionIndex), true);
        }
        else if (clickX > rightPartStart)
        {
            // Clicked right part
            onOperationNavigatorTriggered(static_cast<size_t>(startSlot.optionIndex), false);
        }
        // Center part is not clickable
        return;
    }

    // Normal option click
    if (onOptionTriggered != nullptr)
    {
        onOptionTriggered(static_cast<size_t>(startSlot.optionIndex));
    }
}
