#include "WindowManager.h"

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/commands/SetSwingCommand.h"
#include "../../engine/commands/SetTempoCommand.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"
#include "GroupDetailsDialog.h"
#include "GroupWidget.h"
#include "TransportWidget.h"

#include <string_view>

namespace
{
constexpr const char* kShiftOverlayOwner = "_shift_overlay";
}

WindowManager::WindowManager()
{
    setSize(UiTheme::kTotalWidth, UiTheme::kPanelHeight);

    // Add toast overlays as children (one per panel)
    for (auto& t : toasts_)
        addChildComponent(t);

    // Add option/knob/title bars as children (one per panel)
    for (auto& ob : optionBars_)
        addChildComponent(ob);
    for (auto& kb : knobBars_)
        addChildComponent(kb);
    for (auto& tb : titleBars_)
        addChildComponent(tb);
}

WindowManager::~WindowManager()
{
    if (controllerHost_ != nullptr)
        controllerHost_->setGroupDetailCallback({});

    dismissAllDialogs();
    closeAll();

    // Deactivate and release system widgets
    for (auto& sw : systemWidgets_)
    {
        if (sw != nullptr)
        {
            sw->onDeactivated();
            auto desc = sw->describe();
            hardwareState_.releaseAll(desc.id.toStdString());
        }
    }
    systemWidgets_.clear();
}

// -- Widget placement --

void WindowManager::open(std::unique_ptr<Widget> widget, DisplaySide side)
{
    // A shift overlay temporarily owns every pad LED.  View changes can be
    // triggered while Shift is still held (for example Shift + Keyboard), so
    // return those resources before the incoming widget tries to claim them.
    // The physical Shift release is harmless after this early dismissal.
    if (shiftOverlayActive_)
        handleShiftModifierChanged(false);

    const auto descriptor = widget->describe();
    const bool spansBoth = descriptor.display == DisplayConstraint::Both;

    // A spanning widget owns the complete display surface. Conversely,
    // opening any regular panel widget must first vacate an existing span.
    if (slots_[0].widget != nullptr
        && slots_[0].widget->describe().display == DisplayConstraint::Both)
        closeSlot(0);

    if (spansBoth)
    {
        closeSlot(0);
        closeSlot(1);
    }

    int idx = spansBoth ? 0 : slotIndex(side);

    // Close existing widget on this side
    if (slots_[idx].widget != nullptr)
        closeSlot(idx);

    auto& slot = slots_[idx];
    slot.widget = std::move(widget);
    slot.viewportOffset = 0;

    auto& w = *slot.widget;

    // Inject dependencies
    w.setHardware(&hardwareState_);
    w.setAudioEngine(audioEngine_);
    w.setControllerHost(controllerHost_);
    w.setWindowManager(this);

    // Widget routes showToast/showLoadingToast directly through windowManager_,
    // using its own panelOffset_ to pick the side. No callback plumbing needed.

    // Claim hardware resources for visible page(s)
    claimResourcesForWidget(w, idx);

    // Activate the widget
    int panelOffset = spansBoth ? 0 : ((side == DisplaySide::Left) ? 0 : 1);
    w.onActivated(panelOffset);

    // Add as child component and make visible
    addAndMakeVisible(w);
    resized();

    // Focus the newly opened side
    setFocus(side);
}

void WindowManager::close(const juce::String& widgetId)
{
    for (int i = 0; i < 2; ++i)
    {
        if (slots_[i].widget != nullptr &&
            slots_[i].widget->describe().id == widgetId)
        {
            closeSlot(i);
            return;
        }
    }
}

void WindowManager::closeAll()
{
    for (int i = 0; i < 2; ++i)
    {
        if (slots_[i].widget != nullptr)
            closeSlot(i);
    }
}

Widget* WindowManager::getWidget(DisplaySide side)
{
    auto* direct = slots_[slotIndex(side)].widget.get();
    if (direct != nullptr)
        return direct;

    if (side == DisplaySide::Right && slots_[0].widget != nullptr
        && slots_[0].widget->describe().display == DisplayConstraint::Both)
        return slots_[0].widget.get();

    return nullptr;
}

DisplaySide WindowManager::getSide(const Widget* widget) const
{
    if (widget == nullptr)
    {
        jassertfalse;
        return DisplaySide::Left;
    }
    for (int i = 0; i < 2; ++i)
    {
        if (slots_[i].widget.get() == widget)
            return (i == 0) ? DisplaySide::Left : DisplaySide::Right;
    }
    // Not in slots — caller is asking about a widget this manager doesn't own.
    jassertfalse;
    return DisplaySide::Left;
}

Widget* WindowManager::getFocusedWidget()
{
    return getWidget(focus_);
}

// -- Focus --

void WindowManager::setFocus(DisplaySide side)
{
    focus_ = side;
}

DisplaySide WindowManager::getFocus() const
{
    return focus_;
}

// -- Page navigation --

namespace
{
// Shared scroll primitive: release old-page resources, move the viewport,
// claim new-page resources. Direction = -1 (prev) or +1 (next); bounds are
// checked before mutating so callers can be no-ops at page edges.
void scrollFocusedSlot(std::array<WidgetSlot, 2>& slots,
                        int focusIdx,
                        HardwareState& hw,
                        int direction)
{
    auto& slot = slots[focusIdx];
    if (slot.widget == nullptr) return;

    auto& w = *slot.widget;
    const auto desc = w.describe();
    if (desc.display == DisplayConstraint::Both) return;
    const int next = slot.viewportOffset + direction;
    if (next < 0 || next > desc.pageCount - 1) return;

    w.onPageHidden(slot.viewportOffset);

    const auto owner = desc.id.toStdString();
    for (const auto& rid : w.requiredResources(slot.viewportOffset))
        hw.release(rid, owner);

    slot.viewportOffset = next;

    for (const auto& rid : w.requiredResources(slot.viewportOffset))
        hw.claim(rid, owner);

    w.onPageVisible(slot.viewportOffset);
}
}

void WindowManager::scrollLeft()
{
    scrollFocusedSlot(slots_, slotIndex(focus_), hardwareState_, -1);
}

void WindowManager::scrollRight()
{
    scrollFocusedSlot(slots_, slotIndex(focus_), hardwareState_, +1);
}

// -- Dialog stack --

void WindowManager::showDialog(std::unique_ptr<Widget> dialog)
{
    auto& d = *dialog;

    // Inject dependencies
    d.setHardware(&hardwareState_);
    d.setAudioEngine(audioEngine_);
    d.setControllerHost(controllerHost_);
    d.setWindowManager(this);

    // Calculate margin based on stack depth
    int margin = static_cast<int>(dialogStack_.size()) * kDialogMarginStep;

    // Determine bounds based on display constraint
    auto desc = d.describe();
    juce::Rectangle<int> bounds;

    switch (desc.display)
    {
        case DisplayConstraint::LeftOnly:
            bounds = juce::Rectangle<int>(0, 0, UiTheme::kPanelWidth, UiTheme::kPanelHeight);
            break;
        case DisplayConstraint::RightOnly:
            bounds = juce::Rectangle<int>(UiTheme::kPanelWidth, 0, UiTheme::kPanelWidth, UiTheme::kPanelHeight);
            break;
        case DisplayConstraint::Any:
        default:
            bounds = juce::Rectangle<int>(0, 0, UiTheme::kTotalWidth, UiTheme::kPanelHeight);
            break;
    }

    // Apply margin inset
    bounds = bounds.reduced(margin);

    // Activate
    d.onActivated(0);

    // Add as child, set bounds, bring to front
    addAndMakeVisible(d);
    d.setBounds(bounds);
    d.toFront(false);

    dialogStack_.push_back(std::move(dialog));
    refreshBars();
}

void WindowManager::dismissDialog()
{
    if (dialogStack_.empty())
        return;

    auto& d = *dialogStack_.back();
    d.onDeactivated();

    auto desc = d.describe();
    hardwareState_.releaseAll(desc.id.toStdString());

    removeChildComponent(&d);
    dialogStack_.pop_back();
}

void WindowManager::dismissAllDialogs()
{
    while (!dialogStack_.empty())
        dismissDialog();
}

bool WindowManager::hasDialog() const
{
    return !dialogStack_.empty();
}

// -- Toast --

void WindowManager::showToast(DisplaySide side, ToastKind kind, const juce::String& msg, int durationMs)
{
    int idx = slotIndex(side);
    toasts_[idx].show(kind, msg, durationMs);
    toasts_[idx].toFront(false);
}

ToastHandle WindowManager::showLoadingToast(DisplaySide side, const juce::String& msg)
{
    int idx = slotIndex(side);
    uint64_t id = toasts_[idx].show(ToastKind::Loading, msg, 0);
    toasts_[idx].toFront(false);
    if (id == 0)
        return ToastHandle{}; // dropped by priority
    return ToastHandle(juce::WeakReference<ToastComponent>(&toasts_[idx]), id);
}

const ToastComponent& WindowManager::getToast(DisplaySide side) const
{
    return toasts_[static_cast<size_t>(slotIndex(side))];
}

// -- Wallpaper --

void WindowManager::setWallpaper(const juce::Image& img)
{
    wallpaper_ = img;
    repaint();
    requestUiRefresh(*this);
}

// -- Dependencies --

HardwareState& WindowManager::getHardwareState()
{
    return hardwareState_;
}

void WindowManager::setAudioEngine(AudioEngine* ae)
{
    audioEngine_ = ae;
}

void WindowManager::setControllerHost(ControllerHost* ch)
{
    if (controllerHost_ != nullptr)
        controllerHost_->setGroupDetailCallback({});

    controllerHost_ = ch;

    if (controllerHost_ != nullptr)
    {
        controllerHost_->setGroupDetailCallback(
            [this](int groupIndex) { showGroupDetails(groupIndex); });
    }
}

void WindowManager::syncOptionLeds()
{
    if (controllerHost_ == nullptr)
        return;

    // Full brightness when pressed or toggled-on, dim when available, off otherwise.
    constexpr uint8_t kFullBrightness = HardwareConstants::kLedBright;
    constexpr uint8_t kDimBrightness = HardwareConstants::kLedDim;
    constexpr uint8_t kOffBrightness = HardwareConstants::kLedOff;

    for (int panel = 0; panel < 2; ++panel)
    {
        auto side = (panel == 0) ? DisplaySide::Left : DisplaySide::Right;
        Widget* w = widgetForPanel(side);
        int page = pageForPanel(side);

        std::vector<Option> opts = (w != nullptr) ? w->getOptions(page) : std::vector<Option>{};

        for (int slot = 0; slot < 4; ++slot)
        {
            const int buttonIdx = panel * 4 + slot;
            const std::string name = "d" + std::to_string(buttonIdx + 1);

            int optionIndex = optionBars_[panel].getOptionIndexForSlot(static_cast<size_t>(slot));
            OptionState state = (optionIndex >= 0 && optionIndex < static_cast<int>(opts.size()))
                ? opts[static_cast<size_t>(optionIndex)].state
                : OptionState::Empty;

            uint8_t brightness;
            if (dButtonPressed_[buttonIdx])
                brightness = kFullBrightness;
            else if (state == OptionState::Active)
                brightness = kFullBrightness;
            else if (state == OptionState::Enabled)
                brightness = kDimBrightness;
            else
                brightness = kOffBrightness;

            controllerHost_->setButtonBrightness(name, brightness);
        }
    }
}

// -- Input routing --

void WindowManager::handleKnobEvent(const std::string& knobName, int16_t delta, uint16_t absolute, bool shift)
{
    // Parse knob name: "k1"-"k8"
    if (knobName.size() < 2 || knobName[0] != 'k')
        return;

    int knobNum = std::atoi(knobName.c_str() + 1); // 1-8
    if (knobNum < 1 || knobNum > 8)
        return;

    // Dialogs span both panels — pass raw knob index 0-7 so they can
    // distinguish k1 (left) from k5 (right)
    if (!dialogStack_.empty())
    {
        dialogStack_.back()->handleKnob(knobNum - 1, delta, absolute, shift);
        return;
    }

    DisplaySide side = (knobNum <= 4) ? DisplaySide::Left : DisplaySide::Right;
    int localIndex = (knobNum - 1) % 4; // 0-3

    Widget* target = widgetForPanel(side);
    if (target != nullptr)
    {
        if (target->describe().display == DisplayConstraint::Both)
        {
            if (!target->acceptsKnobInput(pageForPanel(side), localIndex))
                return;
        }
        target->handleKnob(localIndex, delta, absolute, shift);
        refreshBars();
    }
}

void WindowManager::handleTouchstripEvent(const controller_events::TouchstripEvent& e)
{
    if (!dialogStack_.empty())
    {
        dialogStack_.back()->handleTouchstrip(e);
        return;
    }

    if (auto* target = getFocusedWidget())
        target->handleTouchstrip(e);
}

void WindowManager::handleOptionButton(const std::string& buttonName)
{
    // Parse button name: "d1"-"d8"
    if (buttonName.size() < 2 || buttonName[0] != 'd')
        return;

    int buttonNum = std::atoi(buttonName.c_str() + 1); // 1-8
    if (buttonNum < 1 || buttonNum > 8)
        return;

    DisplaySide side = (buttonNum <= 4) ? DisplaySide::Left : DisplaySide::Right;
    int slotIndex = (buttonNum - 1) % 4; // 0-3
    int panelIdx = (side == DisplaySide::Left) ? 0 : 1;

    Widget* target = widgetForPanel(side);
    if (target == nullptr)
        return;

    // Use the option bar's slot-to-option mapping to handle spans correctly
    int optionIndex = optionBars_[panelIdx].getOptionIndexForSlot(static_cast<size_t>(slotIndex));
    if (optionIndex < 0)
        return;

    int page = pageForPanel(side);
    auto opts = target->getOptions(page);
    if (optionIndex >= static_cast<int>(opts.size()))
        return;

    auto& opt = opts[static_cast<size_t>(optionIndex)];
    if (opt.state == OptionState::Disabled || opt.state == OptionState::Empty)
        return;

    // For operation navigators with span > 1, determine left/right based on slot position
    if (opt.isOperationNavigator && opt.span > 1 && opt.onOperationNavigatorPart)
    {
        // Find the start slot for this option to determine if this is left or right part
        // First slot in the span = left/prev, later slots = right/next
        bool isFirstSlot = (optionBars_[panelIdx].getOptionIndexForSlot(
            static_cast<size_t>(slotIndex > 0 ? slotIndex - 1 : 0)) != optionIndex) || (slotIndex == 0);
        opt.onOperationNavigatorPart(isFirstSlot);
        refreshBars();
        return;
    }

    if (opt.onToggle)
    {
        opt.onToggle(opt.state != OptionState::Active);
        refreshBars();
        return;
    }

    if (opt.onInvoke)
    {
        opt.onInvoke();
        refreshBars();
    }
}

void WindowManager::handlePadEvent(const controller_events::PadEvent& e)
{
    // Topmost dialog gets pads, otherwise focused widget
    if (!dialogStack_.empty())
    {
        dialogStack_.back()->handlePad(e);
        return;
    }

    Widget* target = getWidget(focus_);
    if (target != nullptr)
        target->handlePad(e);
}

void WindowManager::handleShiftModifierChanged(bool pressed)
{
    if (pressed)
    {
        if (shiftOverlayActive_) return;
        auto* focused = getFocusedWidget();
        if (focused == nullptr) return;
        const auto overlay = focused->getShiftPadOverlay(focused->currentPage());
        if (overlay.empty()) return;

        // Stash each pad's pre-shift owner so we can hand ownership back
        // after release — otherwise widgets' refreshPadLeds hits the
        // "owner mismatch" path in setLed and the pads stay dark.
        shiftSavedOwners_.clear();
        shiftSavedOwners_.reserve(16);
        for (int i = 0; i < HardwareConstants::kPadCount; ++i)
        {
            const auto id = "p" + std::to_string(i + 1);
            shiftSavedOwners_.push_back(hardwareState_.getOwner(id));
            hardwareState_.forceRelease(id, /*warn=*/false);
            hardwareState_.claim(id, kShiftOverlayOwner);
            hardwareState_.setLed(id, HardwareConstants::kColorOff, kShiftOverlayOwner);
        }
        for (const auto& [padIdx, color] : overlay)
        {
            if (padIdx < 0 || padIdx >= 16) continue;
            const auto id = "p" + std::to_string(padIdx + 1);
            hardwareState_.setLed(id, color, kShiftOverlayOwner);
        }
        shiftOverlayActive_ = true;
        return;
    }

    if (! shiftOverlayActive_) return;
    for (int i = 0; i < HardwareConstants::kPadCount; ++i)
    {
        const auto id = "p" + std::to_string(i + 1);
        hardwareState_.forceRelease(id, /*warn=*/false);
        const auto& prev = shiftSavedOwners_[static_cast<size_t>(i)];
        if (! prev.empty())
            hardwareState_.claim(id, prev);
    }
    shiftSavedOwners_.clear();
    shiftOverlayActive_ = false;

    for (auto& slot : slots_)
        if (slot.widget) slot.widget->refreshPadLeds();
    for (auto& dlg : dialogStack_)
        if (dlg) dlg->refreshPadLeds();
    for (auto& sys : systemWidgets_)
        if (sys) sys->refreshPadLeds();
}

void WindowManager::handleButtonEvent(const controller_events::ButtonEvent& e)
{
    constexpr std::string_view KnobTouchPrefix { "knobTouch" };
    if (e.name.starts_with(KnobTouchPrefix))
    {
        const int knobNum = std::atoi(e.name.c_str() + KnobTouchPrefix.size());
        if (knobNum < 1 || knobNum > 8)
            return;

        const int globalIndex = knobNum - 1;
        const int panel = globalIndex / 4;
        const int localIndex = globalIndex % 4;
        const bool wasActive = knobInputActive_[static_cast<size_t>(globalIndex)];
        knobInputActive_[static_cast<size_t>(globalIndex)] = e.pressed;
        knobBars_[static_cast<size_t>(panel)].setInputActive(
            static_cast<size_t>(localIndex), e.pressed);

        const auto side = panel == 0 ? DisplaySide::Left : DisplaySide::Right;
        if (auto* target = widgetForPanel(side))
        {
            auto knobs = target->getKnobs(pageForPanel(side));
            if (juce::isPositiveAndBelow(localIndex, static_cast<int>(knobs.size())))
            {
                auto& knob = knobs[static_cast<size_t>(localIndex)];
                if (!e.pressed && wasActive && knob.onInputResolved)
                {
                    double finalValue = 0.0;
                    if (const auto* model = std::get_if<Knob::NumericModel>(&knob.model))
                        finalValue = model->value;
                    else if (const auto* model = std::get_if<Knob::ListModel>(&knob.model))
                        finalValue = static_cast<double>(model->selectedIndex);
                    knob.onInputResolved(finalValue);
                }
            }
        }

        refreshBars();
        return;
    }

    // Option buttons: track press/release for visual feedback + LED brightness
    if (e.name.size() == 2 && e.name[0] == 'd'
        && e.name[1] >= '1' && e.name[1] <= '8')
    {
        const int buttonIdx = (e.name[1] - '1'); // 0-7
        dButtonPressed_[buttonIdx] = e.pressed;

        const int panel = buttonIdx / 4;
        const int slot = buttonIdx % 4;
        optionBars_[panel].setPressedSlot(e.pressed ? slot : -1);

        if (e.pressed)
            handleOptionButton(e.name);

        // Update LED brightness immediately (don't wait for next refreshBars).
        syncOptionLeds();
        return;
    }

    // Global tempo/swing hold + nav. Intercepted here so the feature works
    // regardless of which widget has focus.
    if (handleGlobalTempoSwing(e))
        return;

    // Route to system widgets first (transport, groups, etc.)
    for (auto& sw : systemWidgets_)
    {
        if (sw != nullptr)
        {
            auto resources = sw->requiredResources(0);
            for (const auto& rid : resources)
            {
                if (rid == e.name)
                {
                    sw->handleButton(e);
                    return;
                }
            }
        }
    }

    // Dialog stack has priority
    if (!dialogStack_.empty())
    {
        dialogStack_.back()->handleButton(e);
        return;
    }

    // If focused widget claims this button as a resource, route it there instead
    // of applying default behaviour (e.g., the mixer claims arrowLeft/arrowRight
    // for channel scrolling).
    Widget* target = getWidget(focus_);
    if (target != nullptr)
    {
        auto resources = target->requiredResources(target->currentPage());
        for (const auto& rid : resources)
        {
            if (rid == e.name)
            {
                target->handleButton(e);
                return;
            }
        }
    }

    // Default arrow behaviour: scroll focused widget's pages
    if (e.pressed)
    {
        if (e.name == "arrowLeft")
        {
            scrollLeft();
            return;
        }
        if (e.name == "arrowRight")
        {
            scrollRight();
            return;
        }
    }

    // Fallback: forward other buttons to focused widget
    if (target != nullptr)
        target->handleButton(e);
}

Widget* WindowManager::widgetForPanel(DisplaySide side)
{
    // Topmost dialog takes priority, but only on the panel(s) its
    // DisplayConstraint targets — so a LeftOnly dialog doesn't steal
    // d5-d8 or k5-k8 on the right panel.
    if (!dialogStack_.empty())
    {
        auto& dialog = *dialogStack_.back();
        const auto constraint = dialog.describe().display;
        const bool matches = (constraint == DisplayConstraint::Any)
            || (constraint == DisplayConstraint::Both)
            || (constraint == DisplayConstraint::LeftOnly  && side == DisplaySide::Left)
            || (constraint == DisplayConstraint::RightOnly && side == DisplaySide::Right);
        if (matches)
            return &dialog;
        // Dialog is up but this side is the "background" panel — fall
        // through so the underlying slot widget keeps rendering behind
        // the dim overlay.
    }

    return getWidget(side);
}

int WindowManager::pageForPanel(DisplaySide side) const
{
    if (slots_[0].widget != nullptr
        && slots_[0].widget->describe().display == DisplayConstraint::Both)
        return side == DisplaySide::Left ? 0 : 1;

    int idx = slotIndex(side);
    return slots_[idx].viewportOffset;
}

void WindowManager::reclaimResources(DisplaySide side)
{
    int idx = slotIndex(side);
    if (side == DisplaySide::Right && slots_[0].widget != nullptr
        && slots_[0].widget->describe().display == DisplayConstraint::Both)
        idx = 0;
    auto& slot = slots_[idx];
    if (slot.widget == nullptr)
        return;

    claimResourcesForWidget(*slot.widget, idx);
    slot.widget->refreshPadLeds();
}

// -- System widgets --

void WindowManager::addSystemWidget(std::unique_ptr<Widget> widget)
{
    widget->setHardware(&hardwareState_);
    widget->setAudioEngine(audioEngine_);
    widget->setControllerHost(controllerHost_);
    widget->setWindowManager(this);

    // Claim resources for the system widget
    auto desc = widget->describe();
    auto resources = widget->requiredResources(0);
    for (const auto& rid : resources)
        hardwareState_.claim(rid, desc.id.toStdString());

    // Activate the system widget
    widget->onActivated(0);

    systemWidgets_.push_back(std::move(widget));
}

void WindowManager::initSystemWidgets()
{
    addSystemWidget(std::make_unique<TransportWidget>());

    auto groupWidget = std::make_unique<GroupWidget>();
    groupWidget->setGroupDetailsCallback(
        [this](int groupIndex) { showGroupDetails(groupIndex); });
    addSystemWidget(std::move(groupWidget));
}

void WindowManager::showGroupDetails(int groupIndex)
{
    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(groupIndex);
    dialog->setDismissCallback([this]() { dismissDialog(); });
    showDialog(std::move(dialog));
}

void WindowManager::tickActiveWidgets()
{
    Widget* left = slots_[0].widget.get();

    if (left != nullptr)
        left->onUiHostTick();

    // A widget tick may synchronously change the other panel. For example,
    // AudioEditorWidget can open or dismiss SliceDetailsWidget while it
    // refreshes its state. Re-read the right slot after ticking the left one
    // so we never dispatch through a widget that was just removed.
    Widget* right = slots_[1].widget.get();
    if (right != nullptr && right != left)
        right->onUiHostTick();

    // System widgets (TransportWidget, GroupWidget) live outside the panel
    // slots but still need the tick — their LED animations (play/record
    // blink, group-lifecycle effects) advance here.
    for (auto& sw : systemWidgets_)
        if (sw != nullptr)
            sw->onUiHostTick();

    // Advance toast animation/expiry for each panel
    const auto now = static_cast<int64_t>(juce::Time::getMillisecondCounter());
    for (auto& t : toasts_)
        t.tick(now);
}

void WindowManager::notifyEditAboutToBeReplaced()
{
    for (auto& slot : slots_)
        if (slot.widget != nullptr)
            slot.widget->onEditAboutToBeReplaced();

    for (auto& dialog : dialogStack_)
        if (dialog != nullptr)
            dialog->onEditAboutToBeReplaced();

    for (auto& systemWidget : systemWidgets_)
        if (systemWidget != nullptr)
            systemWidget->onEditAboutToBeReplaced();
}

void WindowManager::notifyEditReplaced()
{
    for (auto& slot : slots_)
        if (slot.widget != nullptr)
            slot.widget->onEditReplaced();

    for (auto& dialog : dialogStack_)
        if (dialog != nullptr)
            dialog->onEditReplaced();

    for (auto& systemWidget : systemWidgets_)
        if (systemWidget != nullptr)
            systemWidget->onEditReplaced();

    refreshBars();
}

void WindowManager::notifyActiveSamplerAboutToChange()
{
    for (auto& slot : slots_)
        if (slot.widget != nullptr)
            slot.widget->onActiveSamplerAboutToChange();

    for (auto& dialog : dialogStack_)
        if (dialog != nullptr)
            dialog->onActiveSamplerAboutToChange();
}

void WindowManager::notifyActiveSamplerChanged()
{
    for (auto& slot : slots_)
        if (slot.widget != nullptr)
            slot.widget->onActiveSamplerChanged();

    for (auto& dialog : dialogStack_)
        if (dialog != nullptr)
            dialog->onActiveSamplerChanged();
}

// -- juce::Component --

void WindowManager::paint(juce::Graphics& g)
{
    // Draw wallpaper on empty panels
    for (int i = 0; i < 2; ++i)
    {
        const auto side = i == 0 ? DisplaySide::Left : DisplaySide::Right;
        if (getWidget(side) == nullptr)
        {
            int x = i * UiTheme::kPanelWidth;
            auto panelBounds = juce::Rectangle<int>(x, 0, UiTheme::kPanelWidth, UiTheme::kPanelHeight);

            if (wallpaper_.isValid())
            {
                g.drawImage(wallpaper_, panelBounds.toFloat(),
                            juce::RectanglePlacement::centred);
            }
            else
            {
                g.setColour(juce::Colours::black);
                g.fillRect(panelBounds);
            }
        }
    }
}

void WindowManager::paintOverChildren(juce::Graphics& g)
{
    // When a single-panel dialog is up, dim the opposite panel so the
    // user's focus lands on the dialog.
    if (dialogStack_.empty())
        return;
    const auto constraint = dialogStack_.back()->describe().display;
    if (constraint == DisplayConstraint::Any || constraint == DisplayConstraint::Both)
        return;

    const int dimPanel = (constraint == DisplayConstraint::LeftOnly) ? 1 : 0;
    const int x = dimPanel * UiTheme::kPanelWidth;
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(juce::Rectangle<int>(x, 0, UiTheme::kPanelWidth, UiTheme::kPanelHeight));
}

void WindowManager::resized()
{
    refreshBars();

    // Position toast overlays — bottom-center per panel, 280×50, 16px above bottom
    constexpr int kToastW = 280;
    constexpr int kToastH = 50;
    constexpr int kToastMargin = 16;
    for (int i = 0; i < 2; ++i)
    {
        int sideOffset = i * UiTheme::kPanelWidth;
        int x = sideOffset + (UiTheme::kPanelWidth - kToastW) / 2;
        int y = UiTheme::kPanelHeight - kToastH - kToastMargin;
        toasts_[i].setBounds(x, y, kToastW, kToastH);
    }
}

void WindowManager::setSuppressBars(DisplaySide side, bool suppress)
{
    suppressBars_[slotIndex(side)] = suppress;
}

void WindowManager::refreshBars()
{
    for (int i = 0; i < 2; ++i)
    {
        // When an overlay covers this panel, hide bars and give widget full height
        if (suppressBars_[i])
        {
            optionBars_[i].setVisible(false);
            knobBars_[i].setVisible(false);
            if (slots_[i].widget != nullptr)
            {
                int x = i * UiTheme::kPanelWidth;
                slots_[i].widget->setBounds(x, 0, UiTheme::kPanelWidth, UiTheme::kPanelHeight);
            }
            continue;
        }

        auto side = (i == 0) ? DisplaySide::Left : DisplaySide::Right;
        Widget* w = widgetForPanel(side);
        int page = pageForPanel(side);
        int x = i * UiTheme::kPanelWidth;

        // Option bar
        if (w != nullptr)
        {
            auto opts = w->getOptions(page);
            if (!opts.empty())
            {
                std::vector<OptionsBarComponent::Option> barOpts;
                for (const auto& o : opts)
                {
                    barOpts.push_back({
                        o.label, o.state,
                        juce::jmax(1, o.span), o.isOperationNavigator
                    });
                }
                optionBars_[i].setOptions(barOpts);
                optionBars_[i].setBounds(x, 0, UiTheme::kPanelWidth, UiTheme::kOptionHeight);
                optionBars_[i].setVisible(true);
                optionBars_[i].toFront(false);
            }
            else
            {
                optionBars_[i].setVisible(false);
            }
        }
        else
        {
            optionBars_[i].setVisible(false);
        }

        // Knob bar
        if (w != nullptr)
        {
            auto knobs = w->getKnobs(page);
            bool hasKnobs = false;
            std::array<KnobBarComponent::Slot, 4> slots{};
            for (size_t k = 0; k < knobs.size() && k < 4; ++k)
            {
                auto& knob = knobs[k];
                auto& slot = slots[k];
                slot.label = knob.label;
                slot.isEnabled = knob.isEnabled;
                slot.isActive = knob.isActive;
                slot.isInputActive = knobInputActive_[
                    static_cast<size_t>(i * 4) + k];
                slot.continuousMode = knob.continuousMode;

                // Format the value — memoized per slot. refreshBars() runs at
                // 60 Hz so calling formatter() on every tick for knobs whose
                // value hasn't changed is pure overhead (string allocations,
                // juce::String concatenation). Skip it when the slot's model
                // identity + numeric value match the last cached pair.
                if (std::holds_alternative<Knob::NumericModel>(knob.model))
                {
                    const auto& model = std::get<Knob::NumericModel>(knob.model);
                    auto& cache = knobFormatCache_[static_cast<size_t>(i)][k];
                    // Knob.id is freshly constructed every getKnobs() call so
                    // pointer identity is unreliable — use hashCode64 instead.
                    // If the id changes, the logical knob changed and we must
                    // recompute regardless of value equality.
                    const int64_t identity = knob.id.hashCode64();
                    const bool cacheHit = cache.hasValue
                        && cache.lastIdentityHash == identity
                        && std::abs(cache.lastValue - model.value) < 1e-9;

                    if (cacheHit)
                    {
                        slot.value = cache.lastFormatted;
                    }
                    else
                    {
                        if (model.formatter)
                            slot.value = model.formatter(model.value);
                        else
                            slot.value = juce::String(model.value, 2);
                        cache.hasValue = true;
                        cache.lastValue = model.value;
                        cache.lastIdentityHash = identity;
                        cache.lastFormatted = slot.value;
                    }
                }
                else if (std::holds_alternative<Knob::ListModel>(knob.model))
                {
                    const auto& model = std::get<Knob::ListModel>(knob.model);
                    if (juce::isPositiveAndBelow(model.selectedIndex, static_cast<int>(model.entries.size())))
                        slot.value = model.entries[static_cast<size_t>(model.selectedIndex)];
                }

                if (knob.label.isNotEmpty() || !std::holds_alternative<std::monostate>(knob.model))
                    hasKnobs = true;
            }

            if (hasKnobs)
            {
                // Propagate the widget's knob-bar title so getPreferredHeight picks the right size
                knobBars_[i].setTitle(w->getKnobBarTitle());
                knobBars_[i].setSlots(slots);
                int knobH = knobBars_[i].getPreferredHeight();
                knobBars_[i].setBounds(x, UiTheme::kPanelHeight - knobH, UiTheme::kPanelWidth, knobH);
                knobBars_[i].setVisible(true);
                knobBars_[i].toFront(false);
            }
            else
            {
                knobBars_[i].setVisible(false);
            }
        }
        else
        {
            knobBars_[i].setVisible(false);
        }

        // Position the widget content area — bars that are hidden give their
        // space back to the content. A widget with no options, no titlebar,
        // and no knobs receives the full 272px panel height.
        if (slots_[i].widget != nullptr
            && slots_[i].widget->describe().display != DisplayConstraint::Both)
        {
            int topInset = optionBars_[i].isVisible() ? UiTheme::kOptionHeight : 0;

            // Titlebar lives just under the option bar. If the widget has no
            // title, reclaim the 25px for content. Cache getTitle/getTitleSubtitle
            // results so we call them once per tick (subtitle in particular may
            // allocate a fresh juce::String per call).
            const juce::String title = (w != nullptr) ? w->getTitle() : juce::String();
            const bool hasTitle = title.isNotEmpty();
            int titlebarInset = hasTitle ? UiTheme::kTitlebarHeight : 0;

            if (hasTitle)
            {
                const juce::String subtitle = w->getTitleSubtitle();
                titleBars_[i].setTitle(title);
                titleBars_[i].setAccentColour(w->getAccentColour());
                if (subtitle.isNotEmpty())
                    titleBars_[i].setSubtitle(subtitle);
                else
                    titleBars_[i].clearSubtitle();
                titleBars_[i].setBounds(x, topInset, UiTheme::kPanelWidth, UiTheme::kTitlebarHeight);
                titleBars_[i].setVisible(true);
                titleBars_[i].toFront(false);
            }
            else
            {
                titleBars_[i].setVisible(false);
            }

            int bottomInset = knobBars_[i].isVisible() ? knobBars_[i].getHeight() : 0;

            slots_[i].widget->setBounds(x,
                                        topInset + titlebarInset,
                                        UiTheme::kPanelWidth,
                                        UiTheme::kPanelHeight - topInset - titlebarInset - bottomInset);
        }
        else
        {
            titleBars_[i].setVisible(false);
        }
    }

    if (slots_[0].widget != nullptr
        && slots_[0].widget->describe().display == DisplayConstraint::Both)
        slots_[0].widget->setBounds(0, 0, UiTheme::kTotalWidth, UiTheme::kPanelHeight);

    syncOptionLeds();

    // Option/knob bars intentionally remain in front of modal content: they
    // are the dialog's hardware controls and must keep their labels visible.
}

void WindowManager::flushHardwareState()
{
    if (controllerHost_ != nullptr)
        hardwareState_.flush(*controllerHost_);
}

// -- Private --

int WindowManager::slotIndex(DisplaySide side) const
{
    return (side == DisplaySide::Left) ? 0 : 1;
}

void WindowManager::closeSlot(int index)
{
    auto& slot = slots_[index];
    if (slot.widget == nullptr)
        return;

    slot.widget->onDeactivated();

    auto desc = slot.widget->describe();
    hardwareState_.releaseAll(desc.id.toStdString());

    removeChildComponent(slot.widget.get());
    slot.widget.reset();
    slot.viewportOffset = 0;
}

std::unique_ptr<Widget> WindowManager::takeWidget(DisplaySide side)
{
    int idx = slotIndex(side);
    if (side == DisplaySide::Right && slots_[0].widget != nullptr
        && slots_[0].widget->describe().display == DisplayConstraint::Both)
        idx = 0;
    auto& slot = slots_[idx];
    if (slot.widget == nullptr)
        return nullptr;

    slot.widget->onDeactivated();

    auto desc = slot.widget->describe();
    hardwareState_.releaseAll(desc.id.toStdString());

    removeChildComponent(slot.widget.get());
    auto taken = std::move(slot.widget);
    slot.viewportOffset = 0;
    return taken;
}

void WindowManager::claimResourcesForWidget(Widget& widget, int slotIdx)
{
    auto desc = widget.describe();

    const int visiblePages = desc.display == DisplayConstraint::Both
        ? juce::jmin(2, desc.pageCount) : 1;
    for (int page = 0; page < visiblePages; ++page)
    {
        auto resources = widget.requiredResources(page);
        for (const auto& rid : resources)
        {
            // If a widget in the other slot owns this resource, release it first
            for (int i = 0; i < 2; ++i)
            {
                if (i == slotIdx || slots_[i].widget == nullptr)
                    continue;

                auto otherOwner = slots_[i].widget->describe().id.toStdString();
                if (hardwareState_.getOwner(rid) == otherOwner)
                    hardwareState_.release(rid, otherOwner);
            }

            hardwareState_.claim(rid, desc.id.toStdString());
        }
    }
}

void WindowManager::releaseResourcesForWidget(Widget& widget)
{
    auto desc = widget.describe();
    hardwareState_.releaseAll(desc.id.toStdString());
}

bool WindowManager::handleGlobalTempoSwing(const controller_events::ButtonEvent& e)
{
    if (e.name == "tempo")
    {
        tempoHeld_ = e.pressed;
        return true;
    }

    if (e.name == "swing")
    {
        swingHeld_ = e.pressed;
        return true;
    }

    if (!(tempoHeld_ || swingHeld_))
        return false;

    if (!e.pressed || (e.name != "navUp" && e.name != "navDown"))
        return false;

    if (audioEngine_ == nullptr)
        return true;

    auto patternVisible = [this]()
    {
        for (auto& slot : slots_)
            if (slot.widget != nullptr && slot.widget->describe().id == "pattern")
                return true;
        return false;
    };

    if (tempoHeld_)
    {
        auto* edit = audioEngine_->getEdit();
        if (edit == nullptr)
            return true;

        const double current = audioEngine_->getTransportSnapshot().tempoBpm;
        const double step = (e.name == "navUp") ? -1.0 : 1.0;
        const double next = juce::jlimit(20.0, 300.0, current + step);
        if (next != current)
        {
            auto& um = edit->getUndoManager();
            um.beginNewTransaction("Set Tempo");
            um.perform(new SetTempoCommand(*edit, current, next));

            if (!patternVisible())
                showToast(focus_, ToastKind::Info, juce::String(next, 1) + " BPM", 1000);
        }
        return true;
    }

    if (swingHeld_)
    {
        const double current = audioEngine_->getSwingPercent();
        const double step = (e.name == "navUp") ? -1.0 : 1.0;
        const double next = juce::jlimit(50.0, 75.0, current + step);
        if (next != current)
        {
            auto& um = audioEngine_->getUndoManager();
            um.beginNewTransaction("Set Swing");
            um.perform(new SetSwingCommand(*audioEngine_, current, next));

            if (!patternVisible())
                showToast(focus_, ToastKind::Info, "SW " + juce::String(juce::roundToInt(next)) + "%", 1000);
        }
        return true;
    }

    return false;
}
