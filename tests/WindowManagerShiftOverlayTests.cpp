#include <gtest/gtest.h>

#include "../src/control/HardwareConstants.h"
#include "../src/ui/widget/WindowManager.h"

namespace
{
class OverlayWidget : public Widget
{
public:
    OverlayWidget(juce::String id,
                  std::unordered_map<int, uint8_t> overlay = {},
                  uint8_t defaultPadColor = 0x10)
        : id_(std::move(id)),
          overlay_(std::move(overlay)),
          defaultColor_(defaultPadColor) {}

    WidgetDescriptor describe() const override
    {
        return { id_, 1, false, DisplayConstraint::Any };
    }

    void onActivated(int offset) override { panelOffset_ = offset; }
    void onDeactivated() override {}

    std::vector<std::string> requiredResources(int /*page*/) override
    {
        // Claim all 16 pads so we can confirm ownership hand-off.
        std::vector<std::string> r;
        for (int i = 1; i <= 16; ++i) r.push_back("p" + std::to_string(i));
        return r;
    }
    std::vector<Option> getOptions(int /*page*/) override { return {}; }
    std::vector<Knob> getKnobs(int /*page*/) override { return {}; }

    void paintPage(juce::Graphics&, int, juce::Rectangle<int>) override {}
    void paint(juce::Graphics&) override {}

    std::unordered_map<int, uint8_t> getShiftPadOverlay(int /*page*/) override
    {
        return overlay_;
    }

    void refreshPadLeds() override
    {
        ++refreshCount_;
        const auto owner = id_.toStdString();
        for (int i = 0; i < 16; ++i)
            hw().setLed("p" + std::to_string(i + 1), defaultColor_, owner);
    }

    int refreshCount() const { return refreshCount_; }

private:
    juce::String id_;
    std::unordered_map<int, uint8_t> overlay_;
    uint8_t defaultColor_;
    int refreshCount_ { 0 };
};

class WindowManagerShiftOverlayTest : public ::testing::Test
{
protected:
    WindowManager wm;
};

OverlayWidget* openWithOverlay(WindowManager& wm,
                               std::unordered_map<int, uint8_t> overlay,
                               uint8_t defaultColor = 0x10)
{
    auto w = std::make_unique<OverlayWidget>("overlay",
                                              std::move(overlay),
                                              defaultColor);
    auto* raw = w.get();
    wm.open(std::move(w), DisplaySide::Left);
    // Widget paints its default LED state synchronously during activation —
    // we mimic that here so the owner is populated before shift fires.
    raw->refreshPadLeds();
    return raw;
}
}

TEST_F(WindowManagerShiftOverlayTest, PressAppliesOverlayAndDimsOthers)
{
    const uint8_t hi = HardwareConstants::kColorGoldMedium;
    auto* w = openWithOverlay(wm, { { 0, hi }, { 3, hi } });

    wm.handleShiftModifierChanged(true);

    EXPECT_EQ(wm.getHardwareState().getLed("p1"), hi);      // pad 0 highlighted
    EXPECT_EQ(wm.getHardwareState().getLed("p4"), hi);      // pad 3 highlighted
    EXPECT_EQ(wm.getHardwareState().getLed("p2"), 0);       // pad 1 dimmed
    EXPECT_EQ(wm.getHardwareState().getLed("p16"), 0);      // pad 15 dimmed
    (void) w;   // silence unused warning
}

TEST_F(WindowManagerShiftOverlayTest, ReleaseRestoresPadOwnershipAndRefreshesWidget)
{
    const uint8_t hi = HardwareConstants::kColorGoldMedium;
    auto* w = openWithOverlay(wm, { { 0, hi } }, /*defaultColor=*/ 0x22);

    const int refreshesBefore = w->refreshCount();

    wm.handleShiftModifierChanged(true);
    wm.handleShiftModifierChanged(false);

    EXPECT_GT(w->refreshCount(), refreshesBefore);
    // After release the widget's default color should be back — proof that
    // setLed found the pad owned and accepted the write.
    EXPECT_EQ(wm.getHardwareState().getLed("p1"), 0x22);
    EXPECT_EQ(wm.getHardwareState().getLed("p8"), 0x22);
    // Owner should be the widget, not "_shift_overlay".
    EXPECT_EQ(wm.getHardwareState().getOwner("p1"), "overlay");
}

TEST_F(WindowManagerShiftOverlayTest, EmptyOverlayIsNoOp)
{
    auto* w = openWithOverlay(wm, {} /* no pinned pads */, /*defaultColor=*/ 0x33);
    const auto ownerBefore = wm.getHardwareState().getOwner("p1");
    const int refreshesBefore = w->refreshCount();

    wm.handleShiftModifierChanged(true);
    wm.handleShiftModifierChanged(false);

    EXPECT_EQ(wm.getHardwareState().getOwner("p1"), ownerBefore)
        << "empty overlay should not disturb existing pad ownership";
    EXPECT_EQ(w->refreshCount(), refreshesBefore)
        << "empty overlay should not trigger the release refresh fan-out";
}

TEST_F(WindowManagerShiftOverlayTest, DoublePressIsIdempotent)
{
    auto* w = openWithOverlay(wm, { { 0, HardwareConstants::kColorGoldMedium } });
    (void) w;

    EXPECT_NO_THROW(wm.handleShiftModifierChanged(true));
    EXPECT_NO_THROW(wm.handleShiftModifierChanged(true));
    EXPECT_NO_THROW(wm.handleShiftModifierChanged(false));
}

TEST_F(WindowManagerShiftOverlayTest, OpeningAnotherPadViewWhileShiftHeldIsSafe)
{
    auto* first = openWithOverlay(
        wm, { { 0, HardwareConstants::kColorGoldMedium } });
    (void) first;
    wm.handleShiftModifierChanged(true);
    ASSERT_EQ(wm.getHardwareState().getOwner("p1"), "_shift_overlay");

    auto replacement = std::make_unique<OverlayWidget>(
        "replacement", std::unordered_map<int, uint8_t>{}, 0x34);
    auto* replacementRaw = replacement.get();

    EXPECT_NO_THROW(wm.open(std::move(replacement), DisplaySide::Left));
    replacementRaw->refreshPadLeds();
    EXPECT_EQ(wm.getHardwareState().getOwner("p1"), "replacement");
    EXPECT_EQ(wm.getHardwareState().getLed("p1"), 0x34);

    // The eventual physical release must also be a harmless no-op.
    EXPECT_NO_THROW(wm.handleShiftModifierChanged(false));
}
