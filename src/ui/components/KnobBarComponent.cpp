#include "KnobBarComponent.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

namespace
{
bool hasSameAppearance(const KnobBarComponent::Slot& a,
                       const KnobBarComponent::Slot& b)
{
    return a.label == b.label
        && a.value == b.value
        && a.isEnabled == b.isEnabled
        && a.isActive == b.isActive
        && a.isInputActive == b.isInputActive
        && a.continuousMode == b.continuousMode;
}
} // namespace

KnobBarComponent::KnobBarComponent()
{
    slots.fill({});
    setInterceptsMouseClicks(true, true);
}

void KnobBarComponent::setSlots(const std::array<Slot, 4>& newSlots)
{
    bool appearanceChanged = false;
    for (size_t i = 0; i < slots.size(); ++i)
        appearanceChanged = appearanceChanged || !hasSameAppearance(slots[i], newSlots[i]);

    slots = newSlots;

    if (!appearanceChanged)
        return;

    ++visualUpdateCount_;
    repaint();
    requestUiRefresh(*this);
}

void KnobBarComponent::setSlot(size_t index, Slot slot)
{
    if (index >= slots.size())
        return;

    const bool appearanceChanged = !hasSameAppearance(slots[index], slot);
    slots[index] = std::move(slot);

    if (!appearanceChanged)
        return;

    ++visualUpdateCount_;
    repaint();
    requestUiRefresh(*this);
}

void KnobBarComponent::setTitle(const juce::String& title)
{
    if (titleText == title)
        return;
    titleText = title;
    repaint();
    requestUiRefresh(*this);
}

void KnobBarComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    // Background
    g.setColour(UiTheme::kKnobBarBackground);
    g.fillRect(bounds);

    // Top border
    g.setColour(UiTheme::kKnobBarBorder);
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.0f);

    auto contentBounds = bounds;

    // Title row
    if (titleText.isNotEmpty())
    {
        auto titleArea = contentBounds.removeFromTop(static_cast<float>(UiTheme::kKnobBarTitleAreaHeight));
        g.setColour(UiTheme::kKnobValueText);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kKnobBarTitle, juce::Font::bold)));
        g.drawText(titleText,
                   titleArea.toNearestInt().withTrimmedLeft(UiTheme::kSlotLeftPadding),
                   juce::Justification::centredLeft, true);
    }

    // Knob slots
    const float slotWidth = contentBounds.getWidth() / static_cast<float>(slots.size());

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& slot = slots[i];

        juce::Rectangle<float> slotBounds(
            contentBounds.getX() + static_cast<float>(i) * slotWidth,
            contentBounds.getY(),
            slotWidth,
            contentBounds.getHeight());

        // Left separator (skip first slot)
        if (i > 0)
        {
            g.setColour(slot.isEnabled
                ? UiTheme::kKnobSeparator
                : UiTheme::kKnobSeparator.withAlpha(UiTheme::kDisabledAlpha));
            g.fillRect(slotBounds.getX(), slotBounds.getY(), 1.0f, slotBounds.getHeight());
        }

        juce::Colour labelColour = UiTheme::kKnobLabelText;
        juce::Colour valueColour = UiTheme::kKnobValueText;

        if (slot.isInputActive && slot.isEnabled)
            labelColour = juce::Colours::white;

        if (!slot.isEnabled)
        {
            labelColour = labelColour.withAlpha(UiTheme::kDisabledAlpha);
            valueColour = valueColour.withAlpha(UiTheme::kDisabledAlpha);
        }

        auto textArea = slotBounds.toNearestInt();
        textArea.removeFromLeft(UiTheme::kSlotLeftPadding + 1);
        juce::Rectangle<int> valueArea = textArea;

        if (slot.label.isNotEmpty())
        {
            auto labelArea = textArea.removeFromTop(12);
            valueArea = textArea;

            g.setColour(labelColour);
            g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kKnobLabel, juce::Font::plain)));
            g.drawFittedText(slot.label, labelArea, juce::Justification::left, 1);
        }

        if (slot.value.isNotEmpty())
        {
            g.setColour(valueColour);
            float fontSize = UiTheme::Fonts::kKnobValue;
            if (slot.isInputActive && slot.isEnabled)
                fontSize = fontSize * 1.1f;
            g.setFont(juce::Font(juce::FontOptions(fontSize, juce::Font::bold)));
            g.drawFittedText(slot.value, valueArea, juce::Justification::left, 1);
        }
    }
}

int KnobBarComponent::getPreferredHeight() const
{
    return titleText.isNotEmpty() ? UiTheme::kKnobBarWithTitleHeight : UiTheme::kKnobBarHeight;
}

size_t KnobBarComponent::getSlotIndexFromPosition(int x) const
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return 0;
    
    const float slotWidth = static_cast<float>(bounds.getWidth()) / static_cast<float>(slots.size());
    const size_t index = static_cast<size_t>(x / slotWidth);
    return juce::jlimit(static_cast<size_t>(0), slots.size() - 1, index);
}

void KnobBarComponent::mouseDown(const juce::MouseEvent& event)
{
    if (knobDragCallback == nullptr)
        return;
    
    const size_t slotIndex = getSlotIndexFromPosition(event.x);
    if (slotIndex >= slots.size() || !slots[slotIndex].isEnabled)
        return;
    
    draggedSlotIndex = slotIndex;
    isDragging = true;
    lastDragY = event.y;
    shiftModifier = event.mods.isShiftDown();
}

void KnobBarComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!isDragging || knobDragCallback == nullptr)
        return;
    
    if (draggedSlotIndex >= slots.size() || !slots[draggedSlotIndex].isEnabled)
        return;
    
    const int deltaY = lastDragY - event.y; // Negative delta = drag up = increase value
    lastDragY = event.y;
    
    if (deltaY != 0)
    {
        knobDragCallback(draggedSlotIndex, deltaY, shiftModifier || event.mods.isShiftDown());
    }
}

void KnobBarComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    isDragging = false;
}

void KnobBarComponent::setInputActive(size_t index, bool active)
{
    if (index >= slots.size())
        return;
    
    auto& slot = slots[index];
    
    if (slot.isInputActive == active)
        return;
    
    bool wasActive = slot.isInputActive;
    slot.isInputActive = active;
    
    // If input became inactive and not in continuous mode, trigger resolution callback
    // The actual value will be read from the knob's model by ScreenView
    if (!active && wasActive && !slot.continuousMode)
    {
        if (knobInputResolvedCallback)
        {
            // Pass 0.0 as placeholder - ScreenView will read actual value from knob model
            knobInputResolvedCallback(index, 0.0);
        }
    }

    repaint();
    requestUiRefresh(*this);
}

void KnobBarComponent::handleKnobDelta(size_t index, int delta, bool shift)
{
    if (index >= slots.size() || delta == 0)
        return;
    
    auto& slot = slots[index];
    if (!slot.isEnabled)
        return;
    
    // Only process if in continuous mode or input is active
    if (!slot.continuousMode && !slot.isInputActive)
        return;
    
    // All knob logic is contained here - route to callback
    if (knobDragCallback)
    {
        knobDragCallback(index, delta, shift);
    }
}

void KnobBarComponent::handleKnobAbsolute(size_t index, uint16_t absoluteValue, uint16_t maxAbsolute)
{
    if (index >= slots.size() || maxAbsolute == 0)
        return;
    
    auto& slot = slots[index];
    if (!slot.isEnabled)
        return;
    
    // Only process if in continuous mode or input is active
    if (!slot.continuousMode && !slot.isInputActive)
        return;
    
    // All knob logic is contained here - route to absolute callback
    if (knobAbsoluteCallback)
    {
        knobAbsoluteCallback(index, absoluteValue, maxAbsolute);
    }
    // Fallback to delta callback if absolute callback not set
    else if (knobDragCallback)
    {
        // Calculate a delta based on absolute value change. Member (not
        // static-local) so two KnobBarComponent instances don't share the
        // previous reading — the left and right panel bars each keep their
        // own history.
        if (index < lastAbsoluteValues_.size())
        {
            int delta = static_cast<int>(absoluteValue) - static_cast<int>(lastAbsoluteValues_[index]);
            lastAbsoluteValues_[index] = absoluteValue;

            if (delta != 0)
            {
                knobDragCallback(index, delta, false);
            }
        }
    }
}
