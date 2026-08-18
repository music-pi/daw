# UI system

The current UI is built on `Widget` and `WindowManager`. It targets two
physical 480x272 MK3 displays, represented as a single 960x272 JUCE component.
The old ScreenManager/ScreenFactory architecture has been removed.

## Main types

| Type | Location | Role |
|---|---|---|
| `Widget` | `src/ui/widget/Widget.h` | Base for panel views, dialogs, and invisible system views |
| `WindowManager` | `src/ui/widget/WindowManager.h` | Placement, focus, paging, dialogs, bars, input routing |
| `WidgetDescriptor` | `src/ui/widget/WidgetTypes.h` | Stable ID, page count, modal flag, display constraint |
| `HardwareState` | `src/ui/hw/HardwareState.h` | Resource ownership and dirty LED state |
| `Option`, `Knob` | `src/ui/shared/ControlTypes.h` | Declarative bar models and callbacks |
| `UiTheme` | `src/ui/theme/UiTheme.h` | Dimensions, fonts, and colours |

`UiHost` owns the `WindowManager`, connects it to the engine/controller, opens
default Widgets, and renders the combined surface to MK3 display frames.

## Display placement

`DisplayConstraint` describes placement:

- `Any` — may open in either slot.
- `LeftOnly` / `RightOnly` — constrained to one panel.
- `Both` — one Widget spans both panels and exposes page 0 on the left and
  page 1 on the right simultaneously.

Normal multi-page Widgets use the arrow buttons to change the focused slot's
viewport page. A `Both` Widget does not scroll between its two visible pages.

```cpp
auto widget = std::make_unique<PadOverviewWidget>();
windowManager.open(std::move(widget), DisplaySide::Right);
```

Opening a Widget replaces the occupant of that slot. `takeWidget()` removes
and deactivates a Widget without destroying it so temporary full-screen flows
can later restore the previous workspace.

## Widget contract

Every Widget implements:

```cpp
WidgetDescriptor describe() const override;
void onActivated(int panelOffset) override;
void onDeactivated() override;
std::vector<std::string> requiredResources(int page) override;
std::vector<Option> getOptions(int page) override;
std::vector<Knob> getKnobs(int page) override;
void paintPage(juce::Graphics&, int page,
               juce::Rectangle<int> bounds) override;
```

Optional input hooks include `handlePad`, `handleButton`, `handleKnob`, and
`handleTouchstrip`. `onUiHostTick()` receives the shared 60 Hz heartbeat.

Lifecycle order:

1. Construct without reading engine/hardware state or starting timers.
2. `WindowManager` injects dependencies.
3. `requiredResources(page)` is claimed through `HardwareState`.
4. `onActivated(panelOffset)` registers handlers and starts work.
5. Input, painting, and shared ticks run while visible.
6. `onDeactivated()` cancels work and unregisters handlers.
7. All resources owned by the descriptor ID are released.

Calls to `Widget::repaint()` also mark the hardware display dirty. Leaf JUCE
components that repaint independently must call `requestUiRefresh()`.

## Bars and panel routing

Each panel has four option slots and four knob slots:

```text
left:  d1-d4, k1-k4
right: d5-d8, k5-k8
```

Return bar definitions from `getOptions(page)` and `getKnobs(page)`; do not
configure physical controls elsewhere. `WindowManager` maps physical controls
to local indices 0-3, tracks option press brightness, manages knob-touch
highlighting, and refreshes the bars after actions.

Knobs use either `NumericModel` or `ListModel`. `continuousMode=false` defers
the committed action to `onInputResolved` on touch release. Inspect
`ControlTypes.h` for the complete model fields.

Bars are omitted when empty, returning their height to content. Widget title
bars behave the same way when `getTitle()` is empty.

## Hardware resources

Widgets declare resource IDs such as `d1`, `k3`, `p16`, `arrowLeft`, or
`play`. The declaration is the source of truth for ownership:

```cpp
std::vector<std::string> MyWidget::requiredResources(int page)
{
    if (page == 0)
        return { "d1", "d2", "k1", "k2", "p1" };
    return {};
}
```

Only the current owner may write a resource's LED state. `HardwareState`
releases resources during page changes, dialog transitions, and deactivation.
Shift pad overlays temporarily take pad ownership and restore it through each
Widget's `refreshPadLeds()`.

## Dialogs, toasts, and system Widgets

Dialogs are Widgets pushed with `showDialog()` and popped with
`dismissDialog()`. Set `forceOnTop=true` in the descriptor. Nested dialogs are
inset by `WindowManager::kDialogMarginStep` and receive input before panel
Widgets.

Use `Widget::showToast()` or `showLoadingToast()` for transient feedback.
Loading toasts return an RAII handle.

`TransportWidget` and `GroupWidget` are invisible system Widgets. They claim
hardware resources and handle global events without consuming display slots.

## Principal Widgets

Current top-level views live in `src/ui/widget/`, including:

- `PatternWidget`, `ArrangerWidget`, `MixerWidget`
- `PadOverviewWidget`, `PadDetailsWidget`, `ChannelDetailsWidget`
- `AudioEditorWidget`, `EqualiserWidget`
- `PluginBrowserWidget`, `PluginEditorWidget`, `PluginParamsWidget`
- `FileBrowserWidget`, `FilePreviewWidget`, `SettingsDialog`
- `PianoRollWidget`, `KeyboardWidget`, `T9Widget`

Reusable controls live in `src/ui/components/`. The remaining
`OptionDialogComponent` inherits the legacy `ScreenView`; it is compatibility
code and should eventually be migrated, not copied.

## Adding a Widget

1. Add `MyWidget.h/.cpp` under `src/ui/widget/`.
2. Return a unique descriptor ID and correct display constraint.
3. Declare page resources, options, and knobs.
4. Paint only inside the supplied bounds.
5. Override the minimal input hooks required.
6. Put registration/cancellation in activation/deactivation.
7. Open it through `WindowManager`.
8. Add headless tests using the harnesses under `tests/harness/`.

See [Agent UI notes](agents/ui.md) and [test harnesses](../tests/harness/README.md).
