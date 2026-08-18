#pragma once

#include <juce_core/juce_core.h>

/**
 * Display constraint — controls which panel a widget may be placed on.
 */
enum class DisplayConstraint
{
    Any,       ///< Widget can appear on either side
    LeftOnly,  ///< Widget must appear on the left panel
    RightOnly, ///< Widget must appear on the right panel
    Both       ///< Widget spans both physical panels simultaneously
};

/**
 * Display side — identifies which panel is active.
 */
enum class DisplaySide
{
    Left,
    Right
};

/**
 * Static descriptor returned by Widget::describe().
 * Lightweight value type — no heap allocations for typical use.
 */
struct WidgetDescriptor
{
    juce::String id;
    int pageCount = 1;
    bool forceOnTop = false;
    DisplayConstraint display = DisplayConstraint::Any;
};
