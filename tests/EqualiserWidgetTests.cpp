#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/ChannelInsertUtils.h"
#include "../src/engine/commands/InsertBuiltInPluginCommand.h"
#include "../src/ui/theme/UiTheme.h"
#include "../src/ui/widget/EqualiserWidget.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

namespace
{
te::EqualiserPlugin& addEqualiser(testharness::EngineHarness& harness)
{
    auto& audio = harness.audio();
    auto& list = audio.getEdit()->getMasterPluginList();
    const int insertAt = ChannelInsertUtils::insertionIndex(list);
    audio.getUndoManager().beginNewTransaction();
    EXPECT_TRUE(audio.getUndoManager().perform(new InsertBuiltInPluginCommand(
        audio, list, te::EqualiserPlugin::xmlTypeName, insertAt)));
    auto* equaliser = ChannelInsertUtils::findEqualiser(list);
    EXPECT_NE(equaliser, nullptr);
    return *equaliser;
}
}

TEST(EqualiserWidgetTests, IsDedicatedRightPanelWithFourBandOptionsAndKnobs)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& equaliser = addEqualiser(harness);

    EqualiserWidget widget(equaliser, "Master");
    widget.setAudioEngine(&harness.audio());
    widget.onActivated(1);

    const auto descriptor = widget.describe();
    EXPECT_EQ(descriptor.id, "equaliser");
    EXPECT_EQ(descriptor.display, DisplayConstraint::RightOnly);
    EXPECT_EQ(widget.getTitle(), "EQ > Master");

    const auto options = widget.getOptions(0);
    ASSERT_EQ(options.size(), 4u);
    EXPECT_EQ(options[0].label, "Low");
    EXPECT_EQ(options[0].state, OptionState::Active);

    const auto knobs = widget.getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].label, "Frequency");
    EXPECT_EQ(knobs[1].label, "Gain");
    EXPECT_EQ(knobs[2].label, "Q");
    EXPECT_EQ(knobs[3].label, "Point");

    const auto resources = widget.requiredResources(0);
    EXPECT_NE(std::find(resources.begin(), resources.end(), "d5"), resources.end());
    EXPECT_NE(std::find(resources.begin(), resources.end(), "d8"), resources.end());
    EXPECT_NE(std::find(resources.begin(), resources.end(), "k5"), resources.end());
    EXPECT_NE(std::find(resources.begin(), resources.end(), "k8"), resources.end());
    widget.onDeactivated();
}

TEST(EqualiserWidgetTests, BandOptionsSelectRemoveAndRestorePoints)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& equaliser = addEqualiser(harness);
    equaliser.setMidGain1(7.0f);

    EqualiserWidget widget(equaliser, "Master");
    widget.setAudioEngine(&harness.audio());
    widget.onActivated(1);

    auto options = widget.getOptions(0);
    ASSERT_NE(options[1].onInvoke, nullptr);
    options[1].onInvoke();
    EXPECT_EQ(widget.selectedBand(), EqualiserWidget::Band::Mid1);
    EXPECT_TRUE(widget.isBandEnabled(EqualiserWidget::Band::Mid1));

    options = widget.getOptions(0);
    options[1].onInvoke();
    EXPECT_FALSE(widget.isBandEnabled(EqualiserWidget::Band::Mid1));
    EXPECT_FLOAT_EQ(equaliser.midGain1->getCurrentValue(), 0.0f);
    EXPECT_EQ(widget.getOptions(0)[1].label, "+ Mid 1");

    options = widget.getOptions(0);
    options[1].onInvoke();
    EXPECT_TRUE(widget.isBandEnabled(EqualiserWidget::Band::Mid1));
    EXPECT_NEAR(equaliser.midGain1->getCurrentValue(), 7.0f, 0.01f);
    widget.onDeactivated();
}

TEST(EqualiserWidgetTests, KnobModelsWriteNativeTracktionParameters)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& equaliser = addEqualiser(harness);

    EqualiserWidget widget(equaliser, "Master");
    widget.setAudioEngine(&harness.audio());
    widget.onActivated(1);

    auto knobs = widget.getKnobs(0);
    std::get<Knob::NumericModel>(knobs[0].model).onChange(180.0);
    std::get<Knob::NumericModel>(knobs[1].model).onChange(5.5);
    std::get<Knob::NumericModel>(knobs[2].model).onChange(1.25);

    EXPECT_NEAR(equaliser.loFreq->getCurrentValue(), 180.0f, 0.01f);
    EXPECT_NEAR(equaliser.loGain->getCurrentValue(), 5.5f, 0.01f);
    EXPECT_NEAR(equaliser.loQ->getCurrentValue(), 1.25f, 0.01f);

    std::get<Knob::ListModel>(knobs[3].model).onChange(0);
    EXPECT_FALSE(widget.isBandEnabled(EqualiserWidget::Band::Low));
    widget.onDeactivated();
}

TEST(EqualiserWidgetTests, FullPanelGraphRendersNativeResponse)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& equaliser = addEqualiser(harness);
    equaliser.setMidFreq1(1000.0f);
    equaliser.setMidGain1(12.0f);
    equaliser.setMidQ1(1.0f);

    EqualiserWidget widget(equaliser, "Master");
    widget.setAudioEngine(&harness.audio());
    widget.onActivated(1);
    widget.setBounds(0, 0, UiTheme::kPanelWidth, 180);

    juce::Image image(juce::Image::RGB, UiTheme::kPanelWidth, 180, true);
    juce::Graphics graphics(image);
    widget.paintEntireComponent(graphics, true);

    int nonBackgroundPixels = 0;
    for (int y = 0; y < image.getHeight(); y += 4)
        for (int x = 0; x < image.getWidth(); x += 4)
            if (image.getPixelAt(x, y) != UiTheme::kBackgroundDark)
                ++nonBackgroundPixels;
    EXPECT_GT(nonBackgroundPixels, 1000);
    widget.onDeactivated();
}
