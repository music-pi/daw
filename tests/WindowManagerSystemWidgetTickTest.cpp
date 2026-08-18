// Regression test: WindowManager::tickActiveWidgets must deliver onUiHostTick
// to system widgets (TransportWidget, GroupWidget, …) in addition to the two
// panel slots. See commit 681a8b8 — the iteration over systemWidgets_ was
// originally missing, which froze TransportWidget's record/paused/count-in
// blink animations after we migrated off private juce::Timer instances.
//
// This test uses a minimal Widget subclass that counts onUiHostTick calls.
// It does NOT depend on TransportWidget specifically — the invariant we pin
// is "any Widget registered via addSystemWidget gets ticked", which is what
// the production fix restored.

#include <gtest/gtest.h>

#include <functional>

#include "../src/ui/widget/WindowManager.h"

namespace
{

class TickCountingWidget : public Widget
{
public:
    explicit TickCountingWidget(juce::String id) : id_(std::move(id)) {}

    WidgetDescriptor describe() const override
    {
        return { id_, 1, false, DisplayConstraint::Any };
    }

    std::vector<std::string> requiredResources(int /*page*/) override { return {}; }
    std::vector<Option> getOptions(int /*page*/) override { return {}; }
    std::vector<Knob> getKnobs(int /*page*/) override { return {}; }

    void paintPage(juce::Graphics& /*g*/, int /*page*/,
                   juce::Rectangle<int> /*bounds*/) override {}
    void paint(juce::Graphics& /*g*/) override {}

    void onActivated(int /*panelOffset*/) override {}
    void onDeactivated() override {}

    void onUiHostTick() override
    {
        ++tickCount;
        if (onTick)
            onTick();
    }

    int tickCount = 0;
    std::function<void()> onTick;

private:
    juce::String id_;
};

TEST(WindowManagerSystemWidgetTick, SystemWidgetReceivesTickPerCall)
{
    WindowManager wm;

    auto sys = std::make_unique<TickCountingWidget>("sys");
    auto* raw = sys.get();
    wm.addSystemWidget(std::move(sys));

    EXPECT_EQ(raw->tickCount, 0);

    wm.tickActiveWidgets();
    EXPECT_EQ(raw->tickCount, 1);

    for (int i = 0; i < 9; ++i)
        wm.tickActiveWidgets();
    EXPECT_EQ(raw->tickCount, 10);
}

TEST(WindowManagerSystemWidgetTick, MultipleSystemWidgetsAllTicked)
{
    WindowManager wm;

    auto a = std::make_unique<TickCountingWidget>("sysA");
    auto b = std::make_unique<TickCountingWidget>("sysB");
    auto c = std::make_unique<TickCountingWidget>("sysC");
    auto* rawA = a.get();
    auto* rawB = b.get();
    auto* rawC = c.get();

    wm.addSystemWidget(std::move(a));
    wm.addSystemWidget(std::move(b));
    wm.addSystemWidget(std::move(c));

    constexpr int kTicks = 5;
    for (int i = 0; i < kTicks; ++i)
        wm.tickActiveWidgets();

    EXPECT_EQ(rawA->tickCount, kTicks);
    EXPECT_EQ(rawB->tickCount, kTicks);
    EXPECT_EQ(rawC->tickCount, kTicks);
}

TEST(WindowManagerSystemWidgetTick, SystemAndPanelWidgetsBothTicked)
{
    WindowManager wm;

    auto sys = std::make_unique<TickCountingWidget>("sys");
    auto panel = std::make_unique<TickCountingWidget>("panel");
    auto* rawSys = sys.get();
    auto* rawPanel = panel.get();

    wm.addSystemWidget(std::move(sys));
    wm.open(std::move(panel), DisplaySide::Left);

    wm.tickActiveWidgets();
    wm.tickActiveWidgets();
    wm.tickActiveWidgets();

    EXPECT_EQ(rawSys->tickCount, 3);
    EXPECT_EQ(rawPanel->tickCount, 3);
}

TEST(WindowManagerSystemWidgetTick, PanelReplacementDuringTickUsesCurrentWidget)
{
    WindowManager wm;
    std::unique_ptr<Widget> displacedRight;

    auto left = std::make_unique<TickCountingWidget>("left");
    auto oldRight = std::make_unique<TickCountingWidget>("oldRight");
    auto replacementRight = std::make_unique<TickCountingWidget>("replacementRight");
    auto* rawLeft = left.get();
    auto* rawOldRight = oldRight.get();
    auto* rawReplacementRight = replacementRight.get();

    rawLeft->onTick = [&]()
    {
        displacedRight = wm.takeWidget(DisplaySide::Right);
        wm.open(std::move(replacementRight), DisplaySide::Right);
    };

    wm.open(std::move(left), DisplaySide::Left);
    wm.open(std::move(oldRight), DisplaySide::Right);

    wm.tickActiveWidgets();

    EXPECT_EQ(rawLeft->tickCount, 1);
    EXPECT_EQ(rawOldRight->tickCount, 0);
    EXPECT_EQ(rawReplacementRight->tickCount, 1);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), rawReplacementRight);
}

} // namespace
