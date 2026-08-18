// tests/FilePreviewWidgetTests.cpp
#include <gtest/gtest.h>
#include "../src/ui/widget/FilePreviewWidget.h"
#include "../src/ui/widget/FileDialogStateKeys.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class FilePreviewWidgetTests : public ::testing::Test
{
protected:
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    WindowManager wm;

    juce::ValueTree ensureDialogState()
    {
        return harness.audio().getSettingsState()
            .getOrCreateChildWithName(FileDialogStateKeys::kRoot, nullptr);
    }
};

TEST_F(FilePreviewWidgetTests, DescriptorIsCorrect)
{
    FilePreviewWidget w;
    auto d = w.describe();
    EXPECT_EQ(d.id, "file_preview");
    EXPECT_EQ(d.pageCount, 1);
    EXPECT_FALSE(d.forceOnTop);
    EXPECT_EQ(d.display, DisplayConstraint::RightOnly);
}

TEST_F(FilePreviewWidgetTests, ReportsInvalidWhenNoSelection)
{
    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    wm.open(std::make_unique<FilePreviewWidget>(), DisplaySide::Right);
    auto* w = dynamic_cast<FilePreviewWidget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->isPreviewValid());
}

TEST_F(FilePreviewWidgetTests, ParsesMinimalMpiFile)
{
    // Build a synthetic .mpi ValueTree on disk.
    juce::ValueTree project("maschinepi_project");
    project.setProperty("version", 2, nullptr);

    juce::ValueTree state("maschinepi_state");
    juce::ValueTree transport("transport");
    transport.setProperty("tempoBpm", 124.0, nullptr);
    state.addChild(transport, -1, nullptr);

    juce::ValueTree pads("pads");
    for (int i = 0; i < 16; ++i)
    {
        juce::ValueTree pad("pad");
        pad.setProperty("index", i, nullptr);
        if (i == 0 || i == 5 || i == 10)
            pad.setProperty("sampleFile", "/tmp/fake.wav", nullptr);
        pads.addChild(pad, -1, nullptr);
    }
    state.addChild(pads, -1, nullptr);

    juce::ValueTree groups("groups");
    for (int i = 0; i < 8; ++i)
    {
        juce::ValueTree group("group");
        group.setProperty("index", i, nullptr);
        group.setProperty("hasData", i < 2, nullptr);
        groups.addChild(group, -1, nullptr);
    }
    state.addChild(groups, -1, nullptr);

    project.addChild(state, -1, nullptr);

    auto tempFile = juce::File::createTempFile("mpi");
    tempFile.replaceWithText(project.toXmlString());

    auto result = FilePreviewWidget::parseMpiFile(tempFile);
    EXPECT_TRUE(result.isValid);
    EXPECT_DOUBLE_EQ(result.tempoBpm, 124.0);
    EXPECT_EQ(result.padsLoaded, 3);
    EXPECT_EQ(result.groupsUsed, 2);
    EXPECT_TRUE(result.padLoadMap[0]);
    EXPECT_TRUE(result.padLoadMap[5]);
    EXPECT_TRUE(result.padLoadMap[10]);
    EXPECT_FALSE(result.padLoadMap[1]);

    tempFile.deleteFile();
}

TEST_F(FilePreviewWidgetTests, CorruptFileYieldsInvalidData)
{
    auto tempFile = juce::File::createTempFile("mpi");
    tempFile.replaceWithText("<<not-valid-xml>>");
    auto result = FilePreviewWidget::parseMpiFile(tempFile);
    EXPECT_FALSE(result.isValid);
    tempFile.deleteFile();
}

TEST_F(FilePreviewWidgetTests, UpdatesPreviewWhenSelectedFileChanges)
{
    juce::ValueTree project("maschinepi_project");
    project.setProperty("version", 2, nullptr);
    juce::ValueTree state("maschinepi_state");
    juce::ValueTree transport("transport");
    transport.setProperty("tempoBpm", 96.0, nullptr);
    state.addChild(transport, -1, nullptr);
    project.addChild(state, -1, nullptr);

    auto file = juce::File::createTempFile("mpi");
    file.replaceWithText(project.toXmlString());

    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    wm.open(std::make_unique<FilePreviewWidget>(), DisplaySide::Right);
    auto* w = dynamic_cast<FilePreviewWidget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(w, nullptr);

    // The async load path uses Thread::launch + callAsync, which require a running
    // message loop. In the headless test environment we exercise the same parse
    // logic synchronously via the testing hook.
    w->loadPreviewSynchronouslyForTesting(file);

    EXPECT_TRUE(w->isPreviewValid());
    EXPECT_DOUBLE_EQ(w->getPreviewData().tempoBpm, 96.0);

    file.deleteFile();
}
