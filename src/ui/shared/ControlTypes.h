#pragma once

#include <functional>
#include <variant>
#include <vector>

#include <juce_core/juce_core.h>

/**
 * OptionState — the four visual states an option button can have.
 *
 * Matches the Figma component's state model:
 *   Active   — toggled-on (filled highlight)
 *   Enabled  — normal clickable
 *   Disabled — visible but non-interactive
 *   Empty    — invisible placeholder / spacer
 */
enum class OptionState { Active, Enabled, Disabled, Empty };

/**
 * Option — describes a single option-bar button.
 *
 * Used by the Widget system and reusable ScreenView-based components.
 */
struct Option
{
    juce::String id {};
    juce::String label {};
    OptionState state { OptionState::Enabled };
    int span { 1 };
    bool isOperationNavigator { false }; // If true, left/right parts are separately clickable
    std::function<void()> onInvoke {};
    std::function<void(bool)> onToggle {};
    std::function<void(bool isLeftPart)> onOperationNavigatorPart {}; // Called when left/right part of navigator is clicked
};

/**
 * Knob — describes a single knob-bar control.
 *
 * Used by the Widget system and reusable ScreenView-based components.
 */
struct Knob
{
    struct NumericModel
    {
        double value { 0.0 };
        double minimum { 0.0 };
        double maximum { 1.0 };
        double step { 0.01 };
        std::function<void(double)> onChange {};
        std::function<juce::String(double)> formatter {};
    };

    struct ListModel
    {
        std::vector<juce::String> entries;
        int selectedIndex { 0 };
        std::function<void(int)> onChange {};
    };

    juce::String id;
    juce::String label;
    bool isEnabled { false };
    bool isActive { false }; // True when knob is being touched (knobTouchX pressed)
    bool isInputActive { false }; // True when input is actively being tracked
    bool continuousMode { false }; // If true, update on every change; if false, only on input resolution
    double sensitivity { 1.0 }; // Multiplier applied to delta adjustments
    double shiftSensitivity { 1.0 }; // Multiplier applied when shift/fine mode is active
    std::variant<std::monostate, NumericModel, ListModel> model;
    std::function<void(int delta, bool shift)> onAdjust;
    // Called when knob touch becomes inactive. Non-continuous controls use
    // this to commit; continuous controls may use it to finalize viewport or
    // persistence state after their live adjustments.
    std::function<void(double finalValue)> onInputResolved;
};
