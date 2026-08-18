#include "Screen.h"

#include "../assets/UiAssets.h"
#include "../theme/UiTheme.h"
#include "../components/TitleBarComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <limits>

namespace
{
constexpr int kKnobBarHeight = 56;
} // namespace
ScreenView::ScreenView()
{
    addChildComponent(optionBar);
    optionBar.setVisible(false);
    optionBar.onOptionTriggered = [this](size_t index)
    {
        triggerOption(index);
        refreshOptionDisplay();
    };
    optionBar.onOperationNavigatorTriggered = [this](size_t index, bool isLeftPart)
    {
        if (index >= options.size())
            return;
        
        auto& option = options[index];
        if (option.onOperationNavigatorPart)
        {
            option.onOperationNavigatorPart(isLeftPart);
        }
    };

    addChildComponent(knobBar);
    knobBar.setVisible(false);
    knobBar.setKnobDragCallback([this](size_t index, int delta, bool shift)
    {
        handleKnobDelta(index, delta, shift);
    });
    knobBar.setKnobAbsoluteCallback([this](size_t index, uint16_t absoluteValue, uint16_t maxAbsolute)
    {
        handleKnobAbsolute(index, absoluteValue, maxAbsolute);
    });
    knobBar.setKnobInputResolvedCallback([this](size_t index, double finalValue)
    {
        juce::ignoreUnused(finalValue); // We read the actual value from the knob model
        
        if (index >= knobs.size())
            return;
        
        auto& knob = knobs[index];
        
        // Read the current value from the knob's model
        double currentValue = 0.0;
        if (std::holds_alternative<Knob::NumericModel>(knob.model))
        {
            const auto& model = std::get<Knob::NumericModel>(knob.model);
            currentValue = model.value;
        }
        
        if (knob.onInputResolved)
        {
            knob.onInputResolved(currentValue);
        }
        else if (std::holds_alternative<Knob::NumericModel>(knob.model))
        {
            // Default behavior: value is already updated, just notify
            refreshKnobDisplay();
            notifyKnobsUpdated();
        }
    });

    addChildComponent(titleBar);
    titleBar.setVisible(false);
}

void ScreenView::paint(juce::Graphics& g)
{
    const auto background = UiAssets::getDefaultScreenBackground();

    if (background.isValid())
    {
        g.drawImage(background,
                    0,
                    0,
                    getWidth(),
                    getHeight(),
                    0,
                    0,
                    background.getWidth(),
                    background.getHeight());
    }
    else
    {
        g.fillAll(UiTheme::kBackgroundDark);
    }
}

void ScreenView::resized()
{
    juce::Component::resized();
    updateLayout();
}

void ScreenView::setOptions(std::vector<Option> newOptions)
{
    options = std::move(newOptions);
    refreshOptionDisplay();
    updateLayout();
    notifyOptionsUpdated();
}

void ScreenView::triggerOption(size_t index)
{
    if (index >= options.size())
        return;

    auto& option = options[index];
    if (option.state == OptionState::Disabled || option.state == OptionState::Empty)
        return;

    if (option.onToggle)
    {
        bool nowActive = (option.state != OptionState::Active);
        option.state = nowActive ? OptionState::Active : OptionState::Enabled;
        option.onToggle(nowActive);
    }

    if (option.onInvoke)
        option.onInvoke();

    refreshOptionDisplay();
    notifyOptionsUpdated();
}

void ScreenView::setOptionEnabled(size_t index, bool enabled)
{
    if (index >= options.size())
        return;

    OptionState newState = enabled ? OptionState::Enabled : OptionState::Disabled;
    if (options[index].state == newState)
        return;

    options[index].state = newState;
    refreshOptionDisplay();
    notifyOptionsUpdated();
}

void ScreenView::setOptionActive(size_t index, bool active)
{
    if (index >= options.size())
        return;

    OptionState newState = active ? OptionState::Active : OptionState::Enabled;
    if (options[index].state == newState)
        return;

    options[index].state = newState;
    refreshOptionDisplay();
    notifyOptionsUpdated();
}

void ScreenView::setOptionLabel(size_t index, const juce::String& label)
{
    if (index >= options.size())
        return;

    if (options[index].label == label)
        return;

    options[index].label = label;
    refreshOptionDisplay();
    notifyOptionsUpdated();
}

void ScreenView::setOptionUpdateCallback(std::function<void()> callback)
{
    optionsChangedCallback = std::move(callback);
}

int ScreenView::getOptionIndexForSlot(size_t slotIndex) const
{
    return optionBar.getOptionIndexForSlot(slotIndex);
}

void ScreenView::notifyOptionsUpdated()
{
    if (optionsChangedCallback)
        optionsChangedCallback();
}

bool ScreenView::shouldShowOptionBar() const
{
    return !options.empty();
}

void ScreenView::refreshOptionDisplay()
{
    std::vector<OptionsBarComponent::Option> slots;
    slots.reserve(options.size());

    for (const auto& option : options)
    {
        OptionsBarComponent::Option slot;
        slot.label = option.label;
        slot.state = option.state;
        slot.span = juce::jmax(1, option.span);
        slot.isOperationNavigator = option.isOperationNavigator;
        slots.push_back(std::move(slot));
    }

    optionBar.setOptions(slots);
}

void ScreenView::setKnobs(std::vector<Knob> newKnobs)
{
    constexpr size_t kMaxKnobs = 4;

    if (newKnobs.size() > kMaxKnobs)
        newKnobs.resize(kMaxKnobs);

    // Preserve isInputActive state from existing knobs
    // This is critical because configureKnobs() may be called frequently (e.g., during UI updates),
    // and we don't want to lose the input active state that was set by WindowManager via knobTouch events
    for (size_t i = 0; i < newKnobs.size() && i < knobs.size(); ++i)
    {
        newKnobs[i].isInputActive = knobs[i].isInputActive;
    }

    knobs = std::move(newKnobs);
    const size_t oldSize = knobLastAbsoluteValues.size();
    knobLastAbsoluteValues.resize(knobs.size(), std::numeric_limits<double>::quiet_NaN());
    knobAbsoluteBaselineNormalized.resize(knobs.size(), std::numeric_limits<double>::quiet_NaN());
    knobAbsoluteStartNormalized.resize(knobs.size(), std::numeric_limits<double>::quiet_NaN());
    for (size_t i = 0; i < juce::jmin(oldSize, knobLastAbsoluteValues.size()); ++i)
    {
        if (!std::isfinite(knobLastAbsoluteValues[i]) && i < knobs.size())
        {
            if (std::holds_alternative<Knob::NumericModel>(knobs[i].model))
            {
                knobLastAbsoluteValues[i] = std::get<Knob::NumericModel>(knobs[i].model).value;
            }
        }
    }
    refreshKnobDisplay();
    updateLayout();
    notifyKnobsUpdated();
}

void ScreenView::handleKnobDelta(size_t index, int delta, bool shift)
{
    if (index >= knobs.size() || delta == 0)
        return;

    auto& knob = knobs[index];
    if (!knob.isEnabled)
        return;

    // Only process if in continuous mode or input is active
    if (!knob.continuousMode && !knob.isInputActive)
        return;

    const double sensitivity = shift ? knob.shiftSensitivity : knob.sensitivity;
    if (std::abs(sensitivity) < 1.0e-9)
        return;

    if (knob.onAdjust)
    {
        knob.onAdjust(delta, shift);
        refreshKnobDisplay();
        notifyKnobsUpdated();
        return;
    }

    if (std::holds_alternative<Knob::NumericModel>(knob.model))
    {
        auto& model = std::get<Knob::NumericModel>(knob.model);

        if (model.step == 0.0)
            return;

        const double scaledDelta = static_cast<double>(delta) * sensitivity;
        double newValue = model.value + scaledDelta * model.step;
        newValue = juce::jlimit(model.minimum, model.maximum, newValue);

        if (std::abs(newValue - model.value) < 1e-9)
            return;

        model.value = newValue;
        if (index < knobLastAbsoluteValues.size())
            knobLastAbsoluteValues[index] = newValue;

        if (model.onChange)
            model.onChange(model.value);
        
        refreshKnobDisplay();
        notifyKnobsUpdated();
    }
}

void ScreenView::handleKnobAbsolute(size_t index, uint16_t absoluteValue, uint16_t maxAbsolute)
{
    if (index >= knobs.size() || maxAbsolute == 0)
        return;

    auto& knob = knobs[index];
    if (!knob.isEnabled)
        return;

    // Only process if in continuous mode or input is active
    if (!knob.continuousMode && !knob.isInputActive)
        return;

    // Use absolute value for smooth, accurate updates (like mk3_test does)
    if (std::holds_alternative<Knob::NumericModel>(knob.model))
    {
        auto& model = std::get<Knob::NumericModel>(knob.model);

        // Map absolute value (0-maxAbsolute) to parameter range (minimum-maximum)
        const double normalized = static_cast<double>(absoluteValue) / static_cast<double>(maxAbsolute);
        const double span = model.maximum - model.minimum;
        if (span <= 0.0)
            return;

        if (index >= knobAbsoluteBaselineNormalized.size() || index >= knobAbsoluteStartNormalized.size())
            return;

        if (!std::isfinite(knobAbsoluteBaselineNormalized[index]))
        {
            knobAbsoluteBaselineNormalized[index] = normalized;
            knobAbsoluteStartNormalized[index] = juce::jlimit(0.0, 1.0, (model.value - model.minimum) / span);
        }

        const double deltaNormalized = normalized - knobAbsoluteBaselineNormalized[index];
        double targetNormalized = knobAbsoluteStartNormalized[index] + deltaNormalized;
        targetNormalized = juce::jlimit(0.0, 1.0, targetNormalized);

        double newValue = model.minimum + (targetNormalized * span);

        newValue = juce::jlimit(model.minimum, model.maximum, newValue);

        // Only update if value actually changed (avoid unnecessary updates)
        if (std::abs(newValue - model.value) < 1e-9)
            return;

        model.value = newValue;
        if (index < knobLastAbsoluteValues.size())
            knobLastAbsoluteValues[index] = newValue;

        if (model.onChange)
            model.onChange(model.value);

        refreshKnobDisplay();
        notifyKnobsUpdated();
    }
    else if (std::holds_alternative<Knob::ListModel>(knob.model))
    {
        auto& model = std::get<Knob::ListModel>(knob.model);
        if (model.entries.empty())
            return;

        // Map absolute value (0-maxAbsolute) to list index (0 to entries.size()-1)
        const double normalized = static_cast<double>(absoluteValue) / static_cast<double>(maxAbsolute);
        if (index >= knobAbsoluteBaselineNormalized.size() || index >= knobAbsoluteStartNormalized.size())
            return;

        if (!std::isfinite(knobAbsoluteBaselineNormalized[index]))
        {
            knobAbsoluteBaselineNormalized[index] = normalized;
            const int denom = juce::jmax(1, static_cast<int>(model.entries.size()) - 1);
            knobAbsoluteStartNormalized[index] = static_cast<double>(model.selectedIndex) / static_cast<double>(denom);
        }

        const double deltaNormalized = normalized - knobAbsoluteBaselineNormalized[index];
        double targetNormalized = knobAbsoluteStartNormalized[index] + deltaNormalized;
        targetNormalized = juce::jlimit(0.0, 1.0, targetNormalized);

        int newIndex = static_cast<int>(std::round(targetNormalized * (static_cast<int>(model.entries.size()) - 1)));
        const int lastIndex = static_cast<int>(model.entries.size()) - 1;

        newIndex = juce::jlimit(0, lastIndex, newIndex);

        if (newIndex == model.selectedIndex)
            return;

        model.selectedIndex = newIndex;

        if (model.onChange)
            model.onChange(model.selectedIndex);
    }
    else
    {
        return;
    }

    refreshKnobDisplay();
    notifyKnobsUpdated();
}

void ScreenView::setKnobEnabled(size_t index, bool enabled)
{
    if (index >= knobs.size())
        return;

    auto& knob = knobs[index];
    if (knob.isEnabled == enabled)
        return;

    knob.isEnabled = enabled;
    refreshKnobDisplay();
    notifyKnobsUpdated();
}

void ScreenView::setKnobLabel(size_t index, const juce::String& label)
{
    if (index >= knobs.size())
        return;

    auto& knob = knobs[index];
    if (knob.label == label)
        return;

    knob.label = label;
    refreshKnobDisplay();
    notifyKnobsUpdated();
}

void ScreenView::setKnobActive(size_t index, bool active)
{
    if (index >= knobs.size())
    {
        juce::Logger::writeToLog("[ScreenView] setKnobActive: index=" + juce::String(index) + " out of range (size=" + juce::String(knobs.size()) + ")");
        return;
    }

    auto& knob = knobs[index];
    if (knob.isActive == active)
    {
        juce::Logger::writeToLog("[ScreenView] setKnobActive: index=" + juce::String(index) + " already " + (active ? "active" : "inactive") + ", skipping");
        return;
    }

    bool oldState = knob.isActive;
    knob.isActive = active;
    
    juce::Logger::writeToLog("[ScreenView] setKnobActive: index=" + juce::String(index) + 
                             " oldState=" + (oldState ? "ACTIVE" : "INACTIVE") + 
                             " newState=" + (active ? "ACTIVE" : "INACTIVE") +
                             " knobId=" + knob.id);
    
    refreshKnobDisplay();
}

void ScreenView::setKnobInputActive(size_t index, bool active)
{
    if (index >= knobs.size())
        return;

    auto& knob = knobs[index];
    if (knob.isInputActive == active)
        return;

    knob.isInputActive = active;

    if (index < knobLastAbsoluteValues.size())
    {
        if (active && std::holds_alternative<Knob::NumericModel>(knob.model))
        {
            knobLastAbsoluteValues[index] = std::get<Knob::NumericModel>(knob.model).value;
        }
        else if (active)
        {
            knobLastAbsoluteValues[index] = std::numeric_limits<double>::quiet_NaN();
        }
    }
    if (index < knobAbsoluteBaselineNormalized.size())
        knobAbsoluteBaselineNormalized[index] = std::numeric_limits<double>::quiet_NaN();
    if (index < knobAbsoluteStartNormalized.size())
        knobAbsoluteStartNormalized[index] = std::numeric_limits<double>::quiet_NaN();

    // Also update the KnobBarComponent directly
    knobBar.setInputActive(index, active);
    
    refreshKnobDisplay();
}

void ScreenView::setKnobNumericValue(size_t index, double value, bool notify)
{
    if (index >= knobs.size())
        return;

    auto& knob = knobs[index];
    if (!std::holds_alternative<Knob::NumericModel>(knob.model))
        return;

    auto& model = std::get<Knob::NumericModel>(knob.model);
    double clamped = juce::jlimit(model.minimum, model.maximum, value);

    if (std::abs(clamped - model.value) < 1e-9)
        return;

    model.value = clamped;
    if (index < knobLastAbsoluteValues.size())
        knobLastAbsoluteValues[index] = clamped;
    refreshKnobDisplay();

    if (notify && model.onChange)
        model.onChange(model.value);

    notifyKnobsUpdated();
}

void ScreenView::setKnobListSelection(size_t index, int selectionIndex, bool notify)
{
    if (index >= knobs.size())
        return;

    auto& knob = knobs[index];
    if (!std::holds_alternative<Knob::ListModel>(knob.model))
        return;

    auto& model = std::get<Knob::ListModel>(knob.model);
    if (model.entries.empty())
        return;

    const int maxIndex = static_cast<int>(model.entries.size()) - 1;
    int clamped = juce::jlimit(0, maxIndex, selectionIndex);

    if (model.selectedIndex == clamped)
        return;

    model.selectedIndex = clamped;
    refreshKnobDisplay();

    if (notify && model.onChange)
        model.onChange(model.selectedIndex);

    notifyKnobsUpdated();
}

void ScreenView::setKnobUpdateCallback(std::function<void()> callback)
{
    knobsChangedCallback = std::move(callback);
}

void ScreenView::notifyKnobsUpdated()
{
    if (knobsChangedCallback)
        knobsChangedCallback();
}

void ScreenView::updateLayout()
{
    auto bounds = getLocalBounds();

    if (shouldShowOptionBar())
    {
        auto optionArea = bounds.removeFromTop(UiTheme::kOptionHeight);
        optionBar.setBounds(optionArea);
        optionBar.setVisible(true);
        optionBar.toFront(false);
    }
    else
    {
        optionBar.setVisible(false);
    }

    if (titleBar.isVisible())
    {
        auto titleArea = bounds.removeFromTop(UiTheme::kTitlebarHeight);
        titleBar.setBounds(titleArea);
        titleBar.toFront(false);
    }

    if (!shouldShowKnobBar())
    {
        knobBar.setVisible(false);
    }
    else
    {
        const int preferredHeight = knobBar.getPreferredHeight();
        auto knobArea = bounds.removeFromBottom(preferredHeight);
        knobBar.setBounds(knobArea);
        knobBar.setVisible(true);
        knobBar.toFront(false);
    }

    contentBounds = bounds;
}

juce::Rectangle<int> ScreenView::knobBarArea() const
{
    return getLocalBounds().removeFromBottom(kKnobBarHeight);
}

void ScreenView::refreshKnobDisplay()
{
    std::array<KnobBarComponent::Slot, 4> slots;
    slots.fill({});

    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (i >= knobs.size())
            continue;

        const auto& knob = knobs[i];
        auto& slot = slots[i];
        slot.label = knob.label;
        slot.isEnabled = knob.isEnabled;
        slot.isActive = knob.isActive;
        slot.isInputActive = knob.isInputActive;
        slot.continuousMode = knob.continuousMode;
        slot.value = formatKnobValue(knob);
    }

    knobBar.setSlots(slots);
}

juce::String ScreenView::formatKnobValue(const Knob& knob) const
{
    if (std::holds_alternative<Knob::NumericModel>(knob.model))
    {
        const auto& model = std::get<Knob::NumericModel>(knob.model);
        if (model.formatter)
            return model.formatter(model.value);

        const int asInt = static_cast<int>(std::round(model.value));
        if (std::abs(model.value - static_cast<double>(asInt)) < 1.0e-3)
            return juce::String(asInt);

        return juce::String(model.value, 2);
    }

    if (std::holds_alternative<Knob::ListModel>(knob.model))
    {
        const auto& model = std::get<Knob::ListModel>(knob.model);

        if (juce::isPositiveAndBelow(model.selectedIndex, static_cast<int>(model.entries.size())))
            return model.entries[static_cast<size_t>(model.selectedIndex)];

        return juce::String();
    }

    return juce::String();
}

bool ScreenView::shouldShowKnobBar() const
{
    if (knobs.empty())
        return false;

    for (const auto& knob : knobs)
    {
        if (knob.label.isNotEmpty())
            return true;

        if (!std::holds_alternative<std::monostate>(knob.model))
            return true;
    }

    return false;
}

bool ScreenView::knobLabelsAreEmpty() const
{
    for (const auto& knob : knobs)
        if (knob.label.isNotEmpty())
            return false;
    return true;
}

void ScreenView::setPadLedUpdateCallback(std::function<void()> callback)
{
    padLedCallback_ = std::move(callback);
}

void ScreenView::updatePadLeds(const std::array<uint8_t, 16>& colors)
{
    padLedState_ = colors;
    if (padLedCallback_)
        padLedCallback_();
}

void ScreenView::updatePadLed(int index, uint8_t color)
{
    if (index < 0 || index >= 16)
        return;
    padLedState_[static_cast<size_t>(index)] = color;
    if (padLedCallback_)
        padLedCallback_();
}
