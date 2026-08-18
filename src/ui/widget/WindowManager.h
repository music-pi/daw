#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../components/KnobBarComponent.h"
#include "../components/OptionsBarComponent.h"
#include "../components/TitleBarComponent.h"
#include "../components/Toast.h"
#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"
#include "Widget.h"
#include "WidgetTypes.h"

class AudioEngine;
class ControllerHost;

/**
 * WidgetSlot — holds a widget placed on one physical panel.
 *
 * For multi-page widgets, viewportOffset tracks which page
 * is currently displayed at this panel position.
 */
struct WidgetSlot
{
    std::unique_ptr<Widget> widget;
    int viewportOffset = 0;
};

/**
 * WindowManager — central layout engine for the dual-display UI.
 *
 * Manages widget placement across two 480x272 physical panels,
 * viewport scrolling for multi-page widgets, focus tracking,
 * dialog stacking, snackbar overlays, and wallpaper rendering.
 *
 * This is the top-level UI orchestrator for the current Widget system.
 */
class WindowManager : public juce::Component
{
public:
    static constexpr int kDialogMarginStep = 12;

    WindowManager();
    ~WindowManager() override;

    // -- Widget placement --

    /** Place a widget on the given display side. Closes any existing widget on that side first. */
    void open(std::unique_ptr<Widget> widget, DisplaySide side);

    /** Close a widget by its descriptor ID. No-op if not found. */
    void close(const juce::String& widgetId);

    /** Close all widgets in both slots. */
    void closeAll();

    /** Remove a widget from a slot without destroying it. Deactivates the widget
        and releases its resources, then returns ownership of the unique_ptr to
        the caller. Pass it back into `open()` later to restore. Returns nullptr
        if the slot is empty. */
    std::unique_ptr<Widget> takeWidget(DisplaySide side);

    /** Get the widget on the given side, or nullptr if empty. */
    Widget* getWidget(DisplaySide side);

    /** Find which side a widget is placed on. Asserts if the widget is not in any slot. */
    DisplaySide getSide(const Widget* widget) const;

    /** Get the widget on the currently focused side, or nullptr. */
    Widget* getFocusedWidget();

    // -- Focus --

    /** Set which display side has focus for input/navigation. */
    void setFocus(DisplaySide side);

    /** Get the currently focused side. */
    DisplaySide getFocus() const;

    // -- Page navigation (scroll) --

    /** Scroll the focused multi-page widget one page left (decrement viewport offset). */
    void scrollLeft();

    /** Scroll the focused multi-page widget one page right (increment viewport offset). */
    void scrollRight();

    // -- Dialog stack --

    /** Push a dialog onto the stack. Renders on top with margin nesting. */
    void showDialog(std::unique_ptr<Widget> dialog);

    /** Pop and dismiss the topmost dialog. */
    void dismissDialog();

    /** Dismiss all dialogs. */
    void dismissAllDialogs();

    /** Check if any dialog is on the stack. */
    bool hasDialog() const;

    // -- Toast --

    /** Show a typed toast on the given display side. durationMs = 0 uses the kind's default. */
    void showToast(DisplaySide side, ToastKind kind, const juce::String& msg, int durationMs = 0);

    /** Show a loading toast (indeterminate) and return an RAII handle. */
    [[nodiscard]] ToastHandle showLoadingToast(DisplaySide side, const juce::String& msg);

    /** Access a toast component for test observability. */
    const ToastComponent& getToast(DisplaySide side) const;

    // -- Wallpaper --

    /** Set the wallpaper image rendered on empty panels. */
    void setWallpaper(const juce::Image& img);

    // -- Dependencies --

    /** Access the hardware resource state. */
    HardwareState& getHardwareState();

    /** Set the audio engine pointer (injected into widgets on open). */
    void setAudioEngine(AudioEngine* ae);

    /** Access the injected audio engine pointer. May be null. */
    AudioEngine* getAudioEngine() const { return audioEngine_; }

    /** Set the controller host pointer (injected into widgets on open). */
    void setControllerHost(ControllerHost* ch);

    // -- Input routing --

    /** Route a knob event to the appropriate panel widget. k1-k4 = left, k5-k8 = right. */
    void handleKnobEvent(const std::string& knobName, int16_t delta, uint16_t absolute, bool shift);

    /** Route a touchstrip contact to the topmost dialog or focused widget. */
    void handleTouchstripEvent(const controller_events::TouchstripEvent& e);

    /** Route an option button to the appropriate panel widget. d1-d4 = left, d5-d8 = right. */
    void handleOptionButton(const std::string& buttonName);

    /** Route a pad event to topmost dialog or focused widget. */
    void handlePadEvent(const controller_events::PadEvent& e);

    /** Route a button event. arrowLeft/arrowRight scroll pages; others go to dialog or focused widget. */
    void handleButtonEvent(const controller_events::ButtonEvent& e);
    bool isTempoHeld() const noexcept { return tempoHeld_; }

    /** Called by ControllerHost when the global shift modifier transitions.
        If the focused widget has a getShiftPadOverlay() map, pads are dimmed
        and the highlighted ones are lit while shift is held; on release, every
        slot widget's refreshPadLeds() is called to restore the previous state. */
    void handleShiftModifierChanged(bool pressed);

    /** Get the widget that should receive input for a given panel side (dialog stack first, then slot). */
    Widget* widgetForPanel(DisplaySide side);

    /** Get the current viewport page index for a given panel side. */
    int pageForPanel(DisplaySide side) const;

    /** Reclaim the visible widget's declared hardware resources after a mode
        transition changes which panel should own shared controls such as pads. */
    void reclaimResources(DisplaySide side);

    // -- System widgets --

    /** Add an invisible system widget (e.g., transport, groups). */
    void addSystemWidget(std::unique_ptr<Widget> widget);

    /** Create and add the built-in system widgets (transport, groups). */
    void initSystemWidgets();

    /** Refresh option/knob bars from the current widget state. */
    void refreshBars();

    /** Deliver a UiHost tick to each active slot widget (deduplicated). */
    void tickActiveWidgets();

    /** Notify every manager-owned widget before/after AudioEngine replaces its Edit. */
    void notifyEditAboutToBeReplaced();
    void notifyEditReplaced();

    /** Flush any dirty LED state to the connected controller. */
    void flushHardwareState();

    /** Suppress bars on a panel (e.g. when a UiHost overlay covers it). */
    void setSuppressBars(DisplaySide side, bool suppress);

    // -- juce::Component --

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

private:
    int slotIndex(DisplaySide side) const;
    void closeSlot(int index);
    void claimResourcesForWidget(Widget& widget, int slotIdx);
    void releaseResourcesForWidget(Widget& widget);

    HardwareState hardwareState_;
    AudioEngine* audioEngine_ = nullptr;
    ControllerHost* controllerHost_ = nullptr;

    std::array<WidgetSlot, 2> slots_;
    std::vector<std::unique_ptr<Widget>> dialogStack_;
    std::array<ToastComponent, 2> toasts_;
    std::array<OptionsBarComponent, 2> optionBars_;
    std::array<KnobBarComponent, 2> knobBars_;
    std::array<TitleBarComponent, 2> titleBars_;
    juce::Image wallpaper_;
    DisplaySide focus_ = DisplaySide::Left;
    std::array<bool, 2> suppressBars_ { false, false };
    std::array<bool, 8> dButtonPressed_ { false, false, false, false, false, false, false, false };
    std::array<bool, 8> knobInputActive_ { false, false, false, false,
                                          false, false, false, false };
    bool tempoHeld_ { false };
    bool swingHeld_ { false };

    // Per-slot formatter memo — refreshBars() is driven by the 60 Hz UiHost
    // heartbeat and calls model.formatter(value) for every knob on every tick.
    // Cache the (value, model-identity, formatted string) triple so we only
    // re-run the formatter when a slot's numeric value actually moves.
    struct KnobFormatCache
    {
        bool hasValue { false };
        double lastValue { 0.0 };
        int64_t lastIdentityHash { 0 };
        juce::String lastFormatted;
    };
    // [panel][knob] — 2 panels × 4 knobs
    std::array<std::array<KnobFormatCache, 4>, 2> knobFormatCache_ {};
    bool shiftOverlayActive_ { false };
    std::vector<std::string> shiftSavedOwners_;    // per-pad owners before shift press
    std::vector<std::unique_ptr<Widget>> systemWidgets_;

    /** Attempt to consume a button as a global tempo/swing adjust. Returns true if consumed. */
    bool handleGlobalTempoSwing(const controller_events::ButtonEvent& e);

    /** Sync d1-d8 LED brightness from current option state + press state. */
    void syncOptionLeds();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindowManager)
};
