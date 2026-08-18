#include <gtest/gtest.h>

#include "../src/ui/components/OptionsBarComponent.h"
#include "harness/JuceHarness.h"

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

class OptionsBarComponentTests : public ::testing::Test
{
protected:
    testharness::JuceFrameworkContext juceContext;
};

namespace
{
OptionsBarComponent::Option makeOption(juce::String label,
                                       OptionState state = OptionState::Enabled,
                                       int span = 1,
                                       bool navigator = false)
{
    OptionsBarComponent::Option opt;
    opt.label = std::move(label);
    opt.state = state;
    opt.span = span;
    opt.isOperationNavigator = navigator;
    return opt;
}
} // namespace

// setOptions with identical visible data twice must NOT run through the
// layout path a second time — the rebuild counter stays flat.
TEST_F(OptionsBarComponentTests, RebuildsOnlyOnVisibleChange)
{
    OptionsBarComponent bar;
    bar.setBounds(0, 0, 400, 32);

    std::vector<OptionsBarComponent::Option> opts = {
        makeOption("A"),
        makeOption("B", OptionState::Disabled),
        makeOption("C", OptionState::Enabled, 2, true),
    };

    bar.setOptions(opts);
    const int afterFirst = bar.getRebuildCountForTest();
    EXPECT_EQ(afterFirst, 1);

    // Identical second call must be a no-op.
    bar.setOptions(opts);
    EXPECT_EQ(bar.getRebuildCountForTest(), afterFirst)
        << "setOptions with unchanged data caused a spurious rebuild";

    // Changing a visible label must trigger a rebuild.
    opts[1].label = "B2";
    bar.setOptions(opts);
    EXPECT_EQ(bar.getRebuildCountForTest(), afterFirst + 1)
        << "setOptions ignored a visible label change";
}

// Option index lookup must continue to work after the no-op path is taken.
TEST_F(OptionsBarComponentTests, OptionIndexLookupSurvivesNoOp)
{
    OptionsBarComponent bar;
    bar.setBounds(0, 0, 400, 32);

    std::vector<OptionsBarComponent::Option> opts = {
        makeOption("A"),
        makeOption("B", OptionState::Enabled, 2, true),
        makeOption("C"),
    };

    bar.setOptions(opts);
    bar.setOptions(opts); // no-op second call

    EXPECT_EQ(bar.getOptionIndexForSlot(0), 0);
    EXPECT_EQ(bar.getOptionIndexForSlot(1), 1);
    EXPECT_EQ(bar.getOptionIndexForSlot(2), 1); // span follower
    EXPECT_EQ(bar.getOptionIndexForSlot(3), 2);
}

// State changes alone must trigger a rebuild (Active vs Enabled differ
// visually even if label is the same).
TEST_F(OptionsBarComponentTests, StateChangeTriggersRebuild)
{
    OptionsBarComponent bar;
    bar.setBounds(0, 0, 400, 32);

    std::vector<OptionsBarComponent::Option> opts = {
        makeOption("Play"),
    };
    bar.setOptions(opts);
    const int before = bar.getRebuildCountForTest();

    opts[0].state = OptionState::Active;
    bar.setOptions(opts);
    EXPECT_EQ(bar.getRebuildCountForTest(), before + 1)
        << "State change from Enabled to Active did not trigger rebuild";
}

// A change in option count must always rebuild.
TEST_F(OptionsBarComponentTests, OptionCountChangeTriggersRebuild)
{
    OptionsBarComponent bar;
    bar.setBounds(0, 0, 400, 32);

    std::vector<OptionsBarComponent::Option> opts = { makeOption("A") };
    bar.setOptions(opts);
    const int before = bar.getRebuildCountForTest();

    opts.push_back(makeOption("B"));
    bar.setOptions(opts);
    EXPECT_EQ(bar.getRebuildCountForTest(), before + 1);
}
