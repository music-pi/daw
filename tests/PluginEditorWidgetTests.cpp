#include <gtest/gtest.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/KeyboardInstrumentBank.h"
#include "../src/ui/widget/PluginEditorWidget.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

TEST(PluginEditorWidgetTests, BuiltInInstrumentExposesHardwareParameters)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& bank = harness.audio().getKeyboardBank();
    const auto description = te::PluginManager::
        createBuiltInPluginDescription<te::FourOscPlugin>(true);
    ASSERT_TRUE(bank.loadInstrument(description));

    PluginEditorWidget widget;
    widget.setPlugin(bank.getPlugin());
    EXPECT_GT(widget.describe().pageCount, 4);

    const auto oscMix = widget.getKnobs(0);
    ASSERT_EQ(oscMix.size(), 4);
    EXPECT_EQ(oscMix[0].label, "Osc 1");
    EXPECT_EQ(oscMix[3].label, "Osc 4");

    const auto filter = widget.getKnobs(1);
    ASSERT_EQ(filter.size(), 4);
    EXPECT_EQ(filter[0].label, "Cutoff");
    EXPECT_EQ(filter[1].label, "Resonance");

    const auto ampEnvelope = widget.getKnobs(2);
    ASSERT_EQ(ampEnvelope.size(), 4);
    EXPECT_EQ(ampEnvelope[0].label, "Attack");
    EXPECT_EQ(ampEnvelope[3].label, "Release");

    EXPECT_TRUE(widget.getTitle().contains("Osc Mix"));

    const auto options = widget.getOptions(1);
    ASSERT_EQ(options.size(), 4);
    EXPECT_EQ(options[3].label, "No UI");
    EXPECT_EQ(options[3].state, OptionState::Disabled);
}

TEST(PluginEditorWidgetTests, FourOscTuneAndToneUseCoarseAndShiftFineSteps)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& bank = harness.audio().getKeyboardBank();
    const auto description = te::PluginManager::
        createBuiltInPluginDescription<te::FourOscPlugin>(true);
    ASSERT_TRUE(bank.loadInstrument(description));

    auto* plugin = bank.getPlugin();
    ASSERT_NE(plugin, nullptr);
    auto findParam = [plugin](const juce::String& id) {
        for (auto* param : plugin->getAutomatableParameters())
            if (param != nullptr && param->paramID == id)
                return param;
        return static_cast<te::AutomatableParameter*>(nullptr);
    };

    PluginEditorWidget widget;
    widget.setPlugin(plugin);

    auto* tune = findParam("tune1");
    ASSERT_NE(tune, nullptr);
    tune->setParameter(0.0f, juce::sendNotification);
    widget.onPageVisible(3);
    widget.handleKnob(0, 1, 0, false);
    EXPECT_NEAR(tune->getCurrentValue(), 1.0f, 0.001f);
    widget.handleKnob(0, 1, 0, true);
    EXPECT_NEAR(tune->getCurrentValue(), 1.1f, 0.001f);

    auto* cutoff = findParam("filterFreq");
    ASSERT_NE(cutoff, nullptr);
    cutoff->setParameter(69.0f, juce::sendNotification);
    widget.onPageVisible(1);
    widget.handleKnob(0, 1, 0, false);
    EXPECT_NEAR(cutoff->getCurrentValue(), 70.0f, 0.001f);
    widget.handleKnob(0, 1, 0, true);
    EXPECT_NEAR(cutoff->getCurrentValue(), 70.1f, 0.001f);

    auto* level = findParam("level1");
    ASSERT_NE(level, nullptr);
    level->setParameter(-12.0f, juce::sendNotification);
    widget.onPageVisible(0);
    widget.handleKnob(0, 1, 0, false);
    EXPECT_NEAR(level->getCurrentValue(), -11.0f, 0.001f);
    widget.handleKnob(0, 1, 0, true);
    EXPECT_NEAR(level->getCurrentValue(), -10.9f, 0.001f);
}

TEST(PluginEditorWidgetTests, SplitSysexFramesHandlesConcatenation)
{
    uint8_t raw[] = {
        0xF0, 0x00, 0x20, 0xF7,
        0xF0, 0x11, 0x22, 0x33, 0xF7
    };
    juce::MemoryBlock data(raw, sizeof(raw));
    auto frames = PluginEditorWidget::splitSysexFrames(data);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].getSize(), 4u);
    EXPECT_EQ(frames[1].getSize(), 5u);
}

TEST(PluginEditorWidgetTests, SplitSysexFramesIgnoresJunkBetweenFrames)
{
    uint8_t raw[] = {
        0x00, 0xFF,              // leading noise
        0xF0, 0xAA, 0xF7,
        0xCC, 0xDD,              // inter-frame noise
        0xF0, 0xBB, 0xF7
    };
    juce::MemoryBlock data(raw, sizeof(raw));
    auto frames = PluginEditorWidget::splitSysexFrames(data);
    ASSERT_EQ(frames.size(), 2u);
}

TEST(PluginEditorWidgetTests, ExtractVirusPresetNameFindsLongestPrintableRun)
{
    // Realistic DUMP_SINGLE matching the NyelMyel soundset byte layout:
    // the name sits near the end of the param block at a variable offset
    // that differs between legacy OSes and TI/TI2, so the parser must
    // scan for a printable-ASCII run rather than read a fixed offset.
    juce::MemoryBlock frame;
    const size_t frameLen = 267;
    frame.setSize(frameLen, true);
    auto* b = static_cast<uint8_t*>(frame.getData());
    b[0] = 0xF0;
    b[1] = 0x00; b[2] = 0x20; b[3] = 0x33;
    b[4] = 0x01;  b[5] = 0x00;
    b[6] = 0x10;   // DUMP_SINGLE
    b[7] = 0x01;   // bank
    b[8] = 0x00;   // prog

    const char* name = "WELCOME";
    const size_t nameStart = 250;
    for (size_t k = 0; k < std::strlen(name); ++k)
        b[nameStart + k] = (uint8_t) name[k];
    b[frameLen - 1] = 0xF7;

    EXPECT_EQ(PluginEditorWidget::extractVirusPresetName(frame), "WELCOME");
}

TEST(PluginEditorWidgetTests, ExtractVirusPresetNameRejectsNonDumpFrame)
{
    juce::MemoryBlock frame;
    frame.setSize(40, true);
    auto* b = static_cast<uint8_t*>(frame.getData());
    b[0] = 0xF0; b[6] = 0x30 /* REQUEST_SINGLE */; b[39] = 0xF7;
    EXPECT_EQ(PluginEditorWidget::extractVirusPresetName(frame), juce::String());
}

TEST(PluginEditorWidgetTests, ExtractVirusPresetNameRejectsNoisyFrame)
{
    juce::MemoryBlock frame;
    frame.setSize(300, false);
    auto* b = static_cast<uint8_t*>(frame.getData());
    std::memset(b, 0x00, 300);
    b[0] = 0xF0;
    b[6] = 0x10;
    b[299] = 0xF7;
    // No ASCII run → no name.
    EXPECT_EQ(PluginEditorWidget::extractVirusPresetName(frame), juce::String());
}
