#include <gtest/gtest.h>

#include "../src/ui/IRefreshable.h"
#include "../src/ui/UiRefresh.h"
#include "../src/ui/widget/AudioEditorWidget.h"
#include "../src/ui/widget/ChannelDetailsWidget.h"
#include "../src/ui/widget/PadDetailsWidget.h"
#include "../src/ui/widget/PatternWidget.h"
#include "../src/ui/widget/PianoRollWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

namespace
{
class RefreshCountingHost : public juce::Component, public IRefreshable
{
public:
    RefreshCountingHost(AudioEngine& audio,
                        testharness::MockControllerHost& controller)
    {
        setBounds(0, 0, UiTheme::kTotalWidth, UiTheme::kPanelHeight);
        addAndMakeVisible(windowManager);
        windowManager.setBounds(getLocalBounds());
        windowManager.setAudioEngine(&audio);
        windowManager.setControllerHost(&controller);
    }

    void requestDisplayRefresh() noexcept override { ++refreshCount; }
    void requestDisplayRefresh(unsigned panelMask) noexcept override
    {
        ++refreshCount;
        requestedPanelMask |= panelMask;
    }

    void reset() noexcept
    {
        refreshCount = 0;
        requestedPanelMask = 0u;
    }

    WindowManager windowManager;
    int refreshCount { 0 };
    unsigned requestedPanelMask { 0u };
};

} // namespace

class UiIdleRefreshTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        host = std::make_unique<RefreshCountingHost>(harness.audio(), controller);
    }

    template<typename WidgetType>
    int countIdleRefreshes()
    {
        host->windowManager.open(std::make_unique<WidgetType>(), DisplaySide::Left);
        for (int i = 0; i < 120; ++i)
            host->windowManager.tickActiveWidgets();
        host->reset();
        for (int i = 0; i < 120; ++i)
            host->windowManager.tickActiveWidgets();
        return host->refreshCount;
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost controller;
    std::unique_ptr<RefreshCountingHost> host;
};

TEST_F(UiIdleRefreshTests, ComponentRefreshRequestsOnlyOccupiedPanels)
{
    juce::Component left;
    juce::Component right;
    juce::Component spanning;
    host->addAndMakeVisible(left);
    host->addAndMakeVisible(right);
    host->addAndMakeVisible(spanning);
    left.setBounds(20, 20, 100, 100);
    right.setBounds(600, 20, 100, 100);
    spanning.setBounds(450, 20, 100, 100);

    host->reset();
    requestUiRefresh(left);
    EXPECT_EQ(host->requestedPanelMask, 1u);

    host->reset();
    requestUiRefresh(right);
    EXPECT_EQ(host->requestedPanelMask, 2u);

    host->reset();
    requestUiRefresh(spanning);
    EXPECT_EQ(host->requestedPanelMask, 3u);
}

TEST_F(UiIdleRefreshTests, PatternDoesNotInvalidateAnIdleDisplay)
{
    EXPECT_EQ(countIdleRefreshes<PatternWidget>(), 0);
}

TEST_F(UiIdleRefreshTests, ChannelDetailsDoesNotInvalidateAnIdleDisplay)
{
    EXPECT_EQ(countIdleRefreshes<ChannelDetailsWidget>(), 0);
}

TEST_F(UiIdleRefreshTests, AudioEditorDoesNotInvalidateAnIdleDisplay)
{
    EXPECT_EQ(countIdleRefreshes<AudioEditorWidget>(), 0);
}

TEST_F(UiIdleRefreshTests, PadDetailsDoesNotInvalidateAnIdleDisplay)
{
    EXPECT_EQ(countIdleRefreshes<PadDetailsWidget>(), 0);
}

TEST_F(UiIdleRefreshTests, PianoRollPlayheadInvalidatesHardwareDisplay)
{
    auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->patternClip, nullptr);

    auto widget = std::make_unique<PianoRollWidget>();
    widget->setMidiClip(pad->patternClip, "Pad 1");
    host->windowManager.open(std::move(widget), DisplaySide::Left);
    harness.audio().play();
    harness.audio().getEdit()->getTransport().setPosition(
        tracktion::core::TimePosition::fromSeconds(1.0));
    host->reset();

    host->windowManager.tickActiveWidgets();

    EXPECT_GT(host->refreshCount, 0);
    harness.audio().stop();
}
