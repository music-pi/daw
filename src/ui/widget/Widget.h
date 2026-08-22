#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../control/ControllerEvents.h"
#include "../components/Toast.h"
#include "../shared/ControlTypes.h"
#include "../theme/UiTheme.h"
#include "../widget/WidgetTypes.h"

class AudioEngine;
class ControllerHost;
class HardwareState;
class WindowManager;

/**
 * Widget — base class for all UI elements in the widget architecture.
 *
 * Replaces ScreenView as the fundamental building block. Every panel,
 * overlay, and modal in the application inherits from Widget.
 *
 * State access follows the JUCE/TE-direct pattern:
 *   engine().getAppState()    -- ValueTree hierarchy
 *   engine().getUndoManager() -- te::Edit UndoManager
 *
 * Lifecycle:
 *   Constructor -> setHardware/setAudioEngine -> onActivated -> onDeactivated -> destructor
 */
class Widget : public juce::Component
{
public:
    Widget() = default;
    ~Widget() override = default;

    // ── Identity ─────────────────────────────────────────────────────────

    /** Return a static descriptor for this widget (id, page count, constraints). */
    virtual WidgetDescriptor describe() const = 0;

    // ── Lifecycle ────────────────────────────────────────────────────────

    /** Called when the widget becomes the active view on a panel. */
    virtual void onActivated(int panelOffset) = 0;

    /** Called when the widget is deactivated (pushed down or removed). */
    virtual void onDeactivated() = 0;

    /** Called while the old Edit is still alive, immediately before replacement.
        Widgets must release Edit-owned pointers and listener registrations here. */
    virtual void onEditAboutToBeReplaced() {}

    /** Called after AudioEngine replaces its Edit and all Tracktion objects.
        Widgets may re-resolve stable IDs against the new Edit here. */
    virtual void onEditReplaced() {}
    virtual void onActiveSamplerAboutToChange() {}
    virtual void onActiveSamplerChanged() {}

    // ── Resource declaration ─────────────────────────────────────────────

    /** Return the hardware resource IDs this widget needs for the given page. */
    virtual std::vector<std::string> requiredResources(int page) = 0;

    // ── Options and knobs ────────────────────────────────────────────────

    /** Return option-bar buttons for the given page. */
    virtual std::vector<Option> getOptions(int page) = 0;

    /** Return knob-bar controls for the given page. */
    virtual std::vector<Knob> getKnobs(int page) = 0;

    /** Fast input-routing check for widgets spanning both panels. */
    virtual bool acceptsKnobInput(int /*page*/, int /*localIndex*/) const { return true; }

    // ── Optional bar titles ──────────────────────────────────────────────

    /** Title displayed in the widget titlebar. Empty = no titlebar (WindowManager reclaims that 25px for content). */
    virtual juce::String getTitle() const { return {}; }

    /** Title displayed above the knob-bar row. Empty = knob bar is 25px (no title); non-empty = 45px (with title). */
    virtual juce::String getKnobBarTitle() const { return {}; }

    /** Optional contextual info rendered in the titlebar's right-aligned subtitle slot. */
    virtual juce::String getTitleSubtitle() const { return {}; }

    /** Titlebar accent colour. Default = UiTheme::kTitlebarAccent (gold). Override to use a group colour, mode colour, etc. */
    virtual juce::Colour getAccentColour() const { return UiTheme::kTitlebarAccent; }

    // ── Rendering ────────────────────────────────────────────────────────

    /** Paint the widget content for the given page within the provided bounds. */
    virtual void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) = 0;

    /** Shadows `juce::Component::repaint()` so that calling `repaint()` inside a
        Widget (or any subclass) automatically flips the UiHost display-dirty
        flag. Widget authors no longer need to pair every `repaint()` with a
        separate `requestUiRefresh(*this)` call.

        Caveats — these are why the shadow can silently miss the dirty mark:

          1. `juce::Component::repaint` is non-virtual. A call that reaches a
             Widget via a `juce::Component*` pointer (e.g. a JUCE internal path
             or a lambda captured as `[c = static_cast<Component*>(w)]`) hits
             the base and skips the propagation. Prefer `Widget*` / concrete
             types in captures and APIs that cross back into user code.

          2. The shadow is **message-thread-only**: `requestUiRefresh` walks
             the parent chain and `juce::WeakReference` is not thread-safe.
             `juce::Component::repaint` itself is safe from any thread (with
             a `MessageManagerLock`), but calling `Widget::repaint` off the
             message thread is a bug. An assertion in the impl will catch it
             in debug builds.

        Non-Widget components (`ChannelStripsComponent`, `LevelMeterComponent`,
        `KnobBarComponent`, `WindowManager`, etc.) still pair their `repaint()`
        calls with `requestUiRefresh(*this)` explicitly — the shadow only kicks
        in for Widget-typed `this`. That split is intentional: the Widget is
        the coordination unit for a "dirty region on the hardware display";
        leaf components below it stay disciplined at their own level. */
    void repaint();
    void repaint(int x, int y, int width, int height);
    void repaint(juce::Rectangle<int> area);

    // ── Page lifecycle (optional overrides) ──────────────────────────────

    /** Called when a page becomes visible (e.g., user navigated to it). */
    virtual void onPageVisible(int /*page*/) {}

    /** Called when a page is hidden (e.g., user navigated away). */
    virtual void onPageHidden(int /*page*/) {}

    // ── Animation tick (optional override) ───────────────────────────────

    /** Called from UiHost's 60 Hz tick while this widget is active on any panel.
        Override to advance per-frame animation state (meter decay, flash fade,
        playhead advance). Default: no-op. Widgets that previously ran a private
        startTimerHz(60) should override this instead. */
    virtual void onUiHostTick() {}

    // ── Hardware input (optional overrides) ──────────────────────────────

    /** Handle a pad press/release event. */
    virtual void handlePad(const controller_events::PadEvent& /*e*/) {}

    /** Handle a button press/release event. */
    virtual void handleButton(const controller_events::ButtonEvent& /*e*/) {}

    /** Handle a knob turn event. localIndex is 0-3 (mapped from physical k1-k8). */
    virtual void handleKnob(int /*localIndex*/, int16_t /*delta*/, uint16_t /*absolute*/, bool /*shift*/) {}

    /** Handle the MK3 Smart Strip position. */
    virtual void handleTouchstrip(const controller_events::TouchstripEvent& /*e*/) {}

    /** Handle an option button press. localIndex is 0-3 (mapped from physical d1-d8). */
    virtual void handleOption(int /*localIndex*/) {}

    // ── Shift-overlay pad LEDs ────────────────────────────────────────────

    /** Return the pad indices (0..15) that have a shift+pad action at the
        current level, mapped to the LED colour to highlight them with while
        shift is held. Non-empty enables the overlay: WindowManager dims all
        pads and then paints the highlights from this map. Empty = no
        overlay; pads keep their normal state when shift is held. */
    virtual std::unordered_map<int, uint8_t> getShiftPadOverlay(int /*page*/) { return {}; }

    /** Re-apply the widget's default pad-LED layout. Called by WindowManager
        when a shift overlay is dismissed, so widgets that paint p1..p16 can
        restore their pre-overlay state. Default no-op — override in widgets
        that own pad LEDs. */
    virtual void refreshPadLeds() {}

    // ── Accessors ────────────────────────────────────────────────────────

    int currentPage() const { return currentPage_; }
    int panelOffset() const { return panelOffset_; }

    /** Access hardware state. Asserts non-null (must be set before use). */
    HardwareState& hw();
    const HardwareState& hw() const;

    /** True if the widget has been wired up with a HardwareState (by WindowManager). */
    bool hasHardware() const { return hardware_ != nullptr; }

    /** Access audio engine. Asserts non-null (must be set before use). */
    AudioEngine& engine();
    const AudioEngine& engine() const;

    /** True after WindowManager has injected an audio engine. */
    bool hasAudioEngine() const { return audioEngine_ != nullptr; }

    /** Access controller host. May be null if not set. */
    ControllerHost* controllerHost() const { return controllerHost_; }

    /** Access the window manager. May be null if the widget has not been opened. */
    WindowManager* windowManager() const { return windowManager_; }

    // ── Dependency injection (called by WindowManager) ───────────────────

    void setHardware(HardwareState* hardware) { hardware_ = hardware; }
    void setAudioEngine(AudioEngine* engine) { audioEngine_ = engine; }
    void setControllerHost(ControllerHost* host) { controllerHost_ = host; }
    void setWindowManager(WindowManager* wm) { windowManager_ = wm; }

protected:
    /** Show a typed toast. durationMs = 0 uses the kind's default. */
    void showToast(ToastKind kind, const juce::String& msg, int durationMs = 0);

    /** Show a loading toast (indeterminate). Returns an RAII handle. */
    [[nodiscard]] ToastHandle showLoadingToast(const juce::String& msg);

    int currentPage_ = 0;
    int panelOffset_ = 0;

private:
    HardwareState* hardware_ = nullptr;
    AudioEngine* audioEngine_ = nullptr;
    ControllerHost* controllerHost_ = nullptr;
    WindowManager* windowManager_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Widget)
};
