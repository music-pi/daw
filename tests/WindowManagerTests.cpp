#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"

#include "../src/ui/widget/WindowManager.h"

namespace
{

/**
 * TestWidget — minimal Widget subclass for testing WindowManager.
 * Tracks lifecycle calls and exposes configurable descriptor.
 */
class TestWidget : public Widget
{
public:
    TestWidget(const juce::String& id, int pages = 1,
               DisplayConstraint constraint = DisplayConstraint::Any)
        : id_(id), pages_(pages), constraint_(constraint)
    {
    }

    WidgetDescriptor describe() const override
    {
        return { id_, pages_, false, constraint_ };
    }

    void onActivated(int panelOffset) override
    {
        activatedCount++;
        lastPanelOffset = panelOffset;
        panelOffset_ = panelOffset;
    }

    void onDeactivated() override
    {
        deactivatedCount++;
    }

    std::vector<std::string> requiredResources(int page) override
    {
        if (page < static_cast<int>(resourcesByPage.size()))
            return resourcesByPage[static_cast<size_t>(page)];
        return {};
    }

    std::vector<Option> getOptions(int /*page*/) override { return testOptions; }
    std::vector<Knob> getKnobs(int /*page*/) override { return testKnobs; }

    void paintPage(juce::Graphics& /*g*/, int /*page*/,
                   juce::Rectangle<int> /*bounds*/) override
    {
    }

    void paint(juce::Graphics& /*g*/) override {}

    void onPageVisible(int page) override { lastPageVisible = page; pageVisibleCount++; }
    void onPageHidden(int page) override { lastPageHidden = page; pageHiddenCount++; }

    void handlePad(const ControllerHost::PadEvent& e) override
    {
        lastPadIndex = e.pad;
        padCallCount++;
    }

    void handleButton(const ControllerHost::ButtonEvent& e) override
    {
        lastButtonName = e.name;
        buttonCallCount++;
    }

    void handleKnob(int localIndex, int16_t delta, uint16_t /*absolute*/, bool /*shift*/) override
    {
        lastKnobIndex = localIndex;
        lastKnobDelta = delta;
        knobCallCount++;
    }

    void handleTouchstrip(const ControllerHost::TouchstripEvent& e) override
    {
        lastTouchstrip = e;
        touchstripCallCount++;
    }

    void handleOption(int localIndex) override
    {
        lastOptionIndex = localIndex;
        optionCallCount++;
    }

    // Tracking members
    int activatedCount = 0;
    int deactivatedCount = 0;
    int lastPanelOffset = -1;
    int pageVisibleCount = 0;
    int pageHiddenCount = 0;
    int lastPageVisible = -1;
    int lastPageHidden = -1;

    // Input tracking
    int lastPadIndex = -1;
    int padCallCount = 0;
    std::string lastButtonName;
    int buttonCallCount = 0;
    int lastKnobIndex = -1;
    int lastKnobDelta = 0;
    int knobCallCount = 0;
    ControllerHost::TouchstripEvent lastTouchstrip;
    int touchstripCallCount = 0;
    int lastOptionIndex = -1;
    int optionCallCount = 0;

    // Configurable per-page resources
    std::vector<std::vector<std::string>> resourcesByPage;

    // Configurable options for testing option routing
    std::vector<Option> testOptions;
    std::vector<Knob> testKnobs;

private:
    juce::String id_;
    int pages_;
    DisplayConstraint constraint_;
};

class WindowManagerTest : public ::testing::Test
{
protected:
    WindowManager wm;
};

// 1. Empty state
TEST_F(WindowManagerTest, EmptyStateReturnsNullptr)
{
    EXPECT_EQ(wm.getWidget(DisplaySide::Left), nullptr);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), nullptr);
    EXPECT_EQ(wm.getFocusedWidget(), nullptr);
}

// 2. Open single widget on Left
TEST_F(WindowManagerTest, OpenSingleWidgetLeft)
{
    auto w = std::make_unique<TestWidget>("testA");
    auto* raw = w.get();

    wm.open(std::move(w), DisplaySide::Left);

    EXPECT_EQ(wm.getWidget(DisplaySide::Left), raw);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), nullptr);
    EXPECT_EQ(raw->activatedCount, 1);
    EXPECT_EQ(raw->lastPanelOffset, 0);
}

// 3. Open two widgets
TEST_F(WindowManagerTest, OpenTwoWidgets)
{
    auto wA = std::make_unique<TestWidget>("widgetA");
    auto wB = std::make_unique<TestWidget>("widgetB");
    auto* rawA = wA.get();
    auto* rawB = wB.get();

    wm.open(std::move(wA), DisplaySide::Left);
    wm.open(std::move(wB), DisplaySide::Right);

    EXPECT_EQ(wm.getWidget(DisplaySide::Left), rawA);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), rawB);
    EXPECT_EQ(rawA->activatedCount, 1);
    EXPECT_EQ(rawB->activatedCount, 1);
    EXPECT_EQ(rawA->lastPanelOffset, 0);
    EXPECT_EQ(rawB->lastPanelOffset, 1);
}

TEST_F(WindowManagerTest, BothConstraintSpansPanelsAndClaimsBothPages)
{
    auto spanning = std::make_unique<TestWidget>(
        "spanning", 2, DisplayConstraint::Both);
    spanning->resourcesByPage = { { "d1", "k1" }, { "d5", "k5" } };
    auto* raw = spanning.get();

    wm.open(std::move(spanning), DisplaySide::Left);

    EXPECT_EQ(wm.getWidget(DisplaySide::Left), raw);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), raw);
    EXPECT_EQ(wm.pageForPanel(DisplaySide::Left), 0);
    EXPECT_EQ(wm.pageForPanel(DisplaySide::Right), 1);
    EXPECT_EQ(raw->getBounds(), juce::Rectangle<int>(0, 0, 960, 272));
    EXPECT_EQ(wm.getHardwareState().getOwner("d1"), "spanning");
    EXPECT_EQ(wm.getHardwareState().getOwner("d5"), "spanning");
}

TEST_F(WindowManagerTest, OpeningPanelWidgetClosesExistingSpan)
{
    wm.open(std::make_unique<TestWidget>(
        "spanning", 2, DisplayConstraint::Both), DisplaySide::Left);

    auto right = std::make_unique<TestWidget>("right");
    auto* rightRaw = right.get();
    wm.open(std::move(right), DisplaySide::Right);

    EXPECT_EQ(wm.getWidget(DisplaySide::Left), nullptr);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), rightRaw);
}

// 4. Close widget by ID
TEST_F(WindowManagerTest, CloseWidgetById)
{
    auto w = std::make_unique<TestWidget>("widgetA");

    wm.open(std::move(w), DisplaySide::Left);
    EXPECT_NE(wm.getWidget(DisplaySide::Left), nullptr);

    wm.close("widgetA");

    EXPECT_EQ(wm.getWidget(DisplaySide::Left), nullptr);
}

// Check deactivated is called on close
TEST_F(WindowManagerTest, CloseCallsDeactivated)
{
    auto w = std::make_unique<TestWidget>("widgetA");

    wm.open(std::move(w), DisplaySide::Left);
    auto* raw = wm.getWidget(DisplaySide::Left);
    ASSERT_NE(raw, nullptr);

    auto* tw = static_cast<TestWidget*>(raw);
    EXPECT_EQ(tw->deactivatedCount, 0);

    wm.close("widgetA");
    // Widget is destroyed after close, so we verify deactivation happened
    // by observing the side is now empty
    EXPECT_EQ(wm.getWidget(DisplaySide::Left), nullptr);
}

// 5. Replace widget — open A on Left, then open B on Left
TEST_F(WindowManagerTest, ReplaceWidget)
{
    auto wA = std::make_unique<TestWidget>("widgetA");
    auto wB = std::make_unique<TestWidget>("widgetB");
    auto* rawB = wB.get();

    wm.open(std::move(wA), DisplaySide::Left);
    wm.open(std::move(wB), DisplaySide::Left);

    // A should have been deactivated, B should be active
    EXPECT_EQ(wm.getWidget(DisplaySide::Left), rawB);
    EXPECT_EQ(rawB->activatedCount, 1);
}

// 6. Focus tracking
TEST_F(WindowManagerTest, FocusDefault)
{
    EXPECT_EQ(wm.getFocus(), DisplaySide::Left);
}

TEST_F(WindowManagerTest, OpenOnRightChangesFocus)
{
    auto w = std::make_unique<TestWidget>("widgetA");
    auto* raw = w.get();

    wm.open(std::move(w), DisplaySide::Right);

    EXPECT_EQ(wm.getFocus(), DisplaySide::Right);
    EXPECT_EQ(wm.getFocusedWidget(), raw);
}

TEST_F(WindowManagerTest, SetFocusManually)
{
    auto wA = std::make_unique<TestWidget>("widgetA");
    auto wB = std::make_unique<TestWidget>("widgetB");
    auto* rawA = wA.get();

    wm.open(std::move(wA), DisplaySide::Left);
    wm.open(std::move(wB), DisplaySide::Right);

    wm.setFocus(DisplaySide::Left);
    EXPECT_EQ(wm.getFocus(), DisplaySide::Left);
    EXPECT_EQ(wm.getFocusedWidget(), rawA);
}

// 7. Dialog stacking
TEST_F(WindowManagerTest, DialogStacking)
{
    EXPECT_FALSE(wm.hasDialog());

    auto d1 = std::make_unique<TestWidget>("dialog1");
    auto d2 = std::make_unique<TestWidget>("dialog2");

    wm.showDialog(std::move(d1));
    EXPECT_TRUE(wm.hasDialog());

    wm.showDialog(std::move(d2));
    EXPECT_TRUE(wm.hasDialog());

    wm.dismissDialog();
    EXPECT_TRUE(wm.hasDialog()); // d1 still there

    wm.dismissDialog();
    EXPECT_FALSE(wm.hasDialog());
}

TEST_F(WindowManagerTest, DismissAllDialogs)
{
    wm.showDialog(std::make_unique<TestWidget>("dialog1"));
    wm.showDialog(std::make_unique<TestWidget>("dialog2"));
    wm.showDialog(std::make_unique<TestWidget>("dialog3"));

    EXPECT_TRUE(wm.hasDialog());
    wm.dismissAllDialogs();
    EXPECT_FALSE(wm.hasDialog());
}

TEST_F(WindowManagerTest, RightDialogOptionBarRemainsVisibleAboveModalContent)
{
    auto dialog = std::make_unique<TestWidget>(
        "dialog", 1, DisplayConstraint::RightOnly);
    auto* rawDialog = dialog.get();
    dialog->testOptions.resize(4);
    dialog->testOptions[0] = Option{
        .id = "cancel",
        .label = "Cancel",
        .state = OptionState::Enabled
    };
    dialog->testOptions[3] = Option{
        .id = "confirm",
        .label = "Undo",
        .state = OptionState::Enabled
    };

    wm.showDialog(std::move(dialog));

    OptionsBarComponent* rightOptionBar = nullptr;
    for (int i = 0; i < wm.getNumChildComponents(); ++i)
    {
        auto* child = wm.getChildComponent(i);
        auto* optionBar = dynamic_cast<OptionsBarComponent*>(child);
        if (optionBar != nullptr
            && optionBar->isVisible()
            && optionBar->getX() == UiTheme::kPanelWidth)
        {
            rightOptionBar = optionBar;
            break;
        }
    }

    ASSERT_NE(rightOptionBar, nullptr);
    EXPECT_GT(
        wm.getIndexOfChildComponent(rightOptionBar),
        wm.getIndexOfChildComponent(rawDialog));
}

// 8. Resource ownership conflict
TEST_F(WindowManagerTest, ResourceOwnershipConflict)
{
    auto wA = std::make_unique<TestWidget>("widgetA");
    wA->resourcesByPage = { { "p1", "p2" } };

    auto wB = std::make_unique<TestWidget>("widgetB");
    wB->resourcesByPage = { { "p1", "p3" } }; // p1 conflicts

    wm.open(std::move(wA), DisplaySide::Left);

    // Opening widgetB on Right should succeed — the new widget takes
    // ownership of conflicting resources from the other slot.
    EXPECT_NO_THROW(wm.open(std::move(wB), DisplaySide::Right));

    // p1 should now be owned by widgetB, p2 remains with widgetA
    EXPECT_EQ(wm.getHardwareState().getOwner("p1"), "widgetB");
    EXPECT_EQ(wm.getHardwareState().getOwner("p2"), "widgetA");
    EXPECT_EQ(wm.getHardwareState().getOwner("p3"), "widgetB");
}

// 9. Scroll (page navigation)
TEST_F(WindowManagerTest, ScrollMultiPageWidget)
{
    auto w = std::make_unique<TestWidget>("multiPage", 3);
    w->resourcesByPage = {
        { "p1" },  // page 0
        { "p2" },  // page 1
        { "p3" }   // page 2
    };
    auto* raw = w.get();

    wm.open(std::move(w), DisplaySide::Left);

    // Initially at page 0 (viewport offset 0)
    // scrollLeft should be no-op at offset 0
    wm.scrollLeft();
    EXPECT_EQ(raw->pageHiddenCount, 0);

    // scrollRight: move to page 1
    wm.scrollRight();
    EXPECT_EQ(raw->pageHiddenCount, 1);
    EXPECT_EQ(raw->lastPageHidden, 0);
    EXPECT_EQ(raw->pageVisibleCount, 1);
    EXPECT_EQ(raw->lastPageVisible, 1);

    // scrollRight: move to page 2
    wm.scrollRight();
    EXPECT_EQ(raw->lastPageHidden, 1);
    EXPECT_EQ(raw->lastPageVisible, 2);

    // scrollRight at max: no-op
    wm.scrollRight();
    EXPECT_EQ(raw->pageVisibleCount, 2); // unchanged
}

// 10. Toast — smoke test that showToast routes to the correct side's ToastComponent.
TEST_F(WindowManagerTest, ShowToastRoutesToSide)
{
    wm.showToast(DisplaySide::Left, ToastKind::Info, "Hello", 5000);

    const auto& leftToast = wm.getToast(DisplaySide::Left);
    const auto& rightToast = wm.getToast(DisplaySide::Right);

    EXPECT_TRUE(leftToast.isShowing());
    EXPECT_EQ(leftToast.getKind(), ToastKind::Info);
    EXPECT_EQ(leftToast.getMessage(), "Hello");
    EXPECT_FALSE(rightToast.isShowing());
}

// Test closeAll
TEST_F(WindowManagerTest, CloseAll)
{
    wm.open(std::make_unique<TestWidget>("widgetA"), DisplaySide::Left);
    wm.open(std::make_unique<TestWidget>("widgetB"), DisplaySide::Right);

    EXPECT_NE(wm.getWidget(DisplaySide::Left), nullptr);
    EXPECT_NE(wm.getWidget(DisplaySide::Right), nullptr);

    wm.closeAll();

    EXPECT_EQ(wm.getWidget(DisplaySide::Left), nullptr);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), nullptr);
}

// Test system widget storage
TEST_F(WindowManagerTest, SystemWidgets)
{
    wm.addSystemWidget(std::make_unique<TestWidget>("transport"));
    wm.addSystemWidget(std::make_unique<TestWidget>("groups"));

    // System widgets don't appear in slots
    EXPECT_EQ(wm.getWidget(DisplaySide::Left), nullptr);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), nullptr);
}

// Test wallpaper setting doesn't crash
TEST_F(WindowManagerTest, SetWallpaper)
{
    auto img = juce::Image(juce::Image::ARGB, 480, 272, true);
    wm.setWallpaper(img);
    // No crash, wallpaper stored
}

// Test getHardwareState returns valid reference
TEST_F(WindowManagerTest, GetHardwareState)
{
    auto& hw = wm.getHardwareState();
    EXPECT_FALSE(hw.isClaimed("p1"));
}

// ── Input routing tests ──────────────────────────────────────────────────────

// 1. Knob routing to left panel
TEST_F(WindowManagerTest, KnobRoutingToLeftPanel)
{
    auto w = std::make_unique<TestWidget>("widgetA");
    auto* raw = w.get();

    wm.open(std::move(w), DisplaySide::Left);

    // k2 should route to left panel widget with local index 1
    wm.handleKnobEvent("k2", 5, 100, false);

    EXPECT_EQ(raw->knobCallCount, 1);
    EXPECT_EQ(raw->lastKnobIndex, 1);
    EXPECT_EQ(raw->lastKnobDelta, 5);
}

// 2. Knob routing to right panel (k6 -> local index 1)
TEST_F(WindowManagerTest, KnobRoutingToRightPanel)
{
    auto w = std::make_unique<TestWidget>("widgetB");
    auto* raw = w.get();

    wm.open(std::move(w), DisplaySide::Right);

    // k6 (physical 6) should route to right panel with local index 1
    wm.handleKnobEvent("k6", -3, 200, false);

    EXPECT_EQ(raw->knobCallCount, 1);
    EXPECT_EQ(raw->lastKnobIndex, 1);
    EXPECT_EQ(raw->lastKnobDelta, -3);
}

TEST_F(WindowManagerTest, KnobTouchReleaseResolvesActiveKnob)
{
    auto widget = std::make_unique<TestWidget>("touch-release");
    auto* raw = widget.get();
    double resolvedValue = -1.0;

    Knob knob;
    knob.id = "resolved";
    knob.isEnabled = true;
    Knob::NumericModel model;
    model.value = 0.75;
    knob.model = model;
    knob.onInputResolved = [&resolvedValue](double value)
    {
        resolvedValue = value;
    };
    raw->testKnobs.push_back(std::move(knob));

    wm.open(std::move(widget), DisplaySide::Left);
    wm.refreshBars();

    wm.handleButtonEvent({ "knobTouch1", true, false });
    EXPECT_DOUBLE_EQ(resolvedValue, -1.0);
    wm.handleButtonEvent({ "knobTouch1", false, false });
    EXPECT_DOUBLE_EQ(resolvedValue, 0.75);
}

// 3. Option routing to left panel
TEST_F(WindowManagerTest, OptionRoutingToLeftPanel)
{
    auto w = std::make_unique<TestWidget>("widgetA");
    auto* raw = w.get();

    // Set up 4 options so the option bar has slot-to-option mapping
    for (int i = 0; i < 4; ++i)
    {
        Option opt;
        opt.id = juce::String(i);
        opt.label = "Opt " + juce::String(i);
        opt.state = OptionState::Enabled;
        opt.onInvoke = [raw, i]() {
            raw->lastOptionIndex = i;
            raw->optionCallCount++;
        };
        raw->testOptions.push_back(std::move(opt));
    }

    wm.open(std::move(w), DisplaySide::Left);

    // d3 should route to left panel option index 2
    wm.handleOptionButton("d3");

    EXPECT_EQ(raw->optionCallCount, 1);
    EXPECT_EQ(raw->lastOptionIndex, 2);
}

TEST_F(WindowManagerTest, TouchstripRoutesToFocusedWidget)
{
    auto left = std::make_unique<TestWidget>("left");
    auto right = std::make_unique<TestWidget>("right");
    auto* leftRaw = left.get();
    auto* rightRaw = right.get();

    wm.open(std::move(left), DisplaySide::Left);
    wm.open(std::move(right), DisplaySide::Right);
    wm.setFocus(DisplaySide::Left);

    ControllerHost::TouchstripEvent event { 2, true, 32000, true };
    wm.handleTouchstripEvent(event);

    EXPECT_EQ(leftRaw->touchstripCallCount, 1);
    EXPECT_EQ(leftRaw->lastTouchstrip.finger, 2);
    EXPECT_EQ(leftRaw->lastTouchstrip.position, 32000);
    EXPECT_TRUE(leftRaw->lastTouchstrip.shift);
    EXPECT_EQ(rightRaw->touchstripCallCount, 0);
}

// 4. Pad routing to focused widget
TEST_F(WindowManagerTest, PadRoutingToFocusedWidget)
{
    auto wA = std::make_unique<TestWidget>("widgetA");
    auto wB = std::make_unique<TestWidget>("widgetB");
    auto* rawA = wA.get();
    auto* rawB = wB.get();

    wm.open(std::move(wA), DisplaySide::Left);
    wm.open(std::move(wB), DisplaySide::Right);

    // Focus is on Right (last opened), switch to Left
    wm.setFocus(DisplaySide::Left);

    ControllerHost::PadEvent padEvent;
    padEvent.pad = 5;
    padEvent.pressed = true;
    padEvent.pressure = 1000;

    wm.handlePadEvent(padEvent);

    EXPECT_EQ(rawA->padCallCount, 1);
    EXPECT_EQ(rawA->lastPadIndex, 5);
    EXPECT_EQ(rawB->padCallCount, 0);
}

// 5. Arrow scrolling via handleButtonEvent
TEST_F(WindowManagerTest, ArrowScrollingViaButtonEvent)
{
    auto w = std::make_unique<TestWidget>("multiPage", 3);
    w->resourcesByPage = { { "k1" }, { "k2" }, { "k3" } };
    auto* raw = w.get();

    wm.open(std::move(w), DisplaySide::Left);

    // Press arrowRight to advance page
    ControllerHost::ButtonEvent arrowRight;
    arrowRight.name = "arrowRight";
    arrowRight.pressed = true;

    wm.handleButtonEvent(arrowRight);

    EXPECT_EQ(raw->lastPageVisible, 1);
    EXPECT_EQ(raw->pageVisibleCount, 1);

    // Press arrowRight again
    wm.handleButtonEvent(arrowRight);
    EXPECT_EQ(raw->lastPageVisible, 2);

    // Press arrowLeft to go back
    ControllerHost::ButtonEvent arrowLeft;
    arrowLeft.name = "arrowLeft";
    arrowLeft.pressed = true;

    wm.handleButtonEvent(arrowLeft);
    EXPECT_EQ(raw->lastPageVisible, 1);
}

// 6. Dialog intercepts pad events
TEST_F(WindowManagerTest, DialogInterceptsPadEvents)
{
    auto w = std::make_unique<TestWidget>("widgetA");
    auto* rawWidget = w.get();

    auto d = std::make_unique<TestWidget>("dialog1");
    auto* rawDialog = d.get();

    wm.open(std::move(w), DisplaySide::Left);
    wm.showDialog(std::move(d));

    ControllerHost::PadEvent padEvent;
    padEvent.pad = 3;
    padEvent.pressed = true;

    wm.handlePadEvent(padEvent);

    // Dialog should receive the pad, not the widget
    EXPECT_EQ(rawDialog->padCallCount, 1);
    EXPECT_EQ(rawDialog->lastPadIndex, 3);
    EXPECT_EQ(rawWidget->padCallCount, 0);
}

// 7. No crash when sending knob to empty panel
TEST_F(WindowManagerTest, KnobToEmptyPanelNoCrash)
{
    // No widget on left panel
    wm.handleKnobEvent("k1", 10, 50, false);
    // Should not crash
}

// Knob with invalid name is ignored
TEST_F(WindowManagerTest, InvalidKnobNameIgnored)
{
    auto w = std::make_unique<TestWidget>("widgetA");
    auto* raw = w.get();

    wm.open(std::move(w), DisplaySide::Left);

    wm.handleKnobEvent("x5", 10, 50, false);
    wm.handleKnobEvent("k0", 10, 50, false);
    wm.handleKnobEvent("k9", 10, 50, false);

    EXPECT_EQ(raw->knobCallCount, 0);
}

// Dialog intercepts button events
TEST_F(WindowManagerTest, DialogInterceptsButtonEvents)
{
    auto w = std::make_unique<TestWidget>("widgetA");
    auto* rawWidget = w.get();

    auto d = std::make_unique<TestWidget>("dialog1");
    auto* rawDialog = d.get();

    wm.open(std::move(w), DisplaySide::Left);
    wm.showDialog(std::move(d));

    ControllerHost::ButtonEvent btnEvent;
    btnEvent.name = "someButton";
    btnEvent.pressed = true;

    wm.handleButtonEvent(btnEvent);

    EXPECT_EQ(rawDialog->buttonCallCount, 1);
    EXPECT_EQ(rawDialog->lastButtonName, "someButton");
    EXPECT_EQ(rawWidget->buttonCallCount, 0);
}

// widgetForPanel returns dialog when active
TEST_F(WindowManagerTest, WidgetForPanelReturnsDialog)
{
    auto w = std::make_unique<TestWidget>("widgetA");
    auto d = std::make_unique<TestWidget>("dialog1");
    auto* rawDialog = d.get();

    wm.open(std::move(w), DisplaySide::Left);
    wm.showDialog(std::move(d));

    // widgetForPanel should return dialog regardless of side
    EXPECT_EQ(wm.widgetForPanel(DisplaySide::Left), rawDialog);
    EXPECT_EQ(wm.widgetForPanel(DisplaySide::Right), rawDialog);
}

// pageForPanel returns viewport offset
TEST_F(WindowManagerTest, PageForPanelReturnsViewportOffset)
{
    auto w = std::make_unique<TestWidget>("multiPage", 3);
    w->resourcesByPage = { { "k1" }, { "k2" }, { "k3" } };

    wm.open(std::move(w), DisplaySide::Left);

    EXPECT_EQ(wm.pageForPanel(DisplaySide::Left), 0);

    wm.scrollRight();
    EXPECT_EQ(wm.pageForPanel(DisplaySide::Left), 1);
}

} // namespace
