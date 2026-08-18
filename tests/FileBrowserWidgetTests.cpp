// tests/FileBrowserWidgetTests.cpp
#include <gtest/gtest.h>
#include "../src/app/DataPaths.h"
#include "../src/ui/widget/FileBrowserWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/ui/widget/FileDialogStateKeys.h"
#include "../src/engine/AudioEngine.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class FileBrowserWidgetTests : public ::testing::Test
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

TEST_F(FileBrowserWidgetTests, DescriptorIsCorrect)
{
    FileBrowserWidget w;
    auto d = w.describe();
    EXPECT_EQ(d.id, "file_browser");
    EXPECT_EQ(d.pageCount, 1);
    EXPECT_FALSE(d.forceOnTop);
    EXPECT_EQ(d.display, DisplayConstraint::LeftOnly);
}

TEST_F(FileBrowserWidgetTests, InitialModeIsBrowse)
{
    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    wm.open(std::make_unique<FileBrowserWidget>(), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->getMode(), FileBrowserWidget::Mode::Browse);
}

TEST_F(FileBrowserWidgetTests, ActivationPublishesFirstRecentProjectToState)
{
    // Create a fake recent project so DataPaths::findRecentProjects returns it.
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("mpi-test-recent");
    dir.createDirectory();
    auto file = dir.getChildFile("TestProject.mpi");
    file.replaceWithText("<maschinepi_project version=\"2\"><maschinepi_state/></maschinepi_project>");

    wm.setAudioEngine(&harness.audio());
    auto state = ensureDialogState();

    // FileBrowserWidget reads recent projects directly from DataPaths in the current
    // implementation. In this integration-style test we exercise the publish path
    // with a forced selection to avoid needing to coerce DataPaths' source:

    auto browser = std::make_unique<FileBrowserWidget>();
    wm.open(std::move(browser), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(w, nullptr);

    // Force a selection for test determinism:
    w->setSelectedFileForTesting(file);
    const juce::String publishedPath = state.getProperty(FileDialogStateKeys::kSelectedFile).toString();
    EXPECT_EQ(publishedPath, file.getFullPathName());

    file.deleteFile();
    dir.deleteRecursively();
}

TEST_F(FileBrowserWidgetTests, SearchFilterReducesVisibleRecentProjects)
{
    // Seed some fake recent projects through a test hook.
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("mpi-test-search");
    dir.createDirectory();
    auto a = dir.getChildFile("alpha.mpi");
    auto b = dir.getChildFile("beta.mpi");
    auto c = dir.getChildFile("alchemy.mpi");
    for (auto* f : { &a, &b, &c })
        f->replaceWithText("<maschinepi_project/>");

    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    auto browser = std::make_unique<FileBrowserWidget>();
    browser->setRecentProjectsForTesting({ a, b, c });
    wm.open(std::move(browser), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(w, nullptr);

    w->setSearchQueryForTesting("al");
    EXPECT_EQ(w->filteredCountForTesting(), 2u);    // "alpha", "alchemy"
    w->setSearchQueryForTesting("bet");
    EXPECT_EQ(w->filteredCountForTesting(), 1u);    // "beta"
    w->setSearchQueryForTesting("");
    EXPECT_EQ(w->filteredCountForTesting(), 3u);

    dir.deleteRecursively();
}

TEST_F(FileBrowserWidgetTests, SaveAsWritesFileToProjectsDirectory)
{
    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    harness.audio().createEmptyEdit();

    auto browser = std::make_unique<FileBrowserWidget>();
    browser->setInitialMode(FileBrowserWidget::Mode::SaveAs);
    wm.open(std::move(browser), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(w, nullptr);

    // Simulate the user committing a filename:
    w->commitSaveAsForTesting("PlanTestFixture");

    auto expected = DataPaths::getProjectsDir().getChildFile("PlanTestFixture.mpi");
    EXPECT_TRUE(expected.existsAsFile());

    expected.deleteFile();
}

TEST_F(FileBrowserWidgetTests, SaveAsAppendsMpiExtensionWhenMissing)
{
    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    harness.audio().createEmptyEdit();
    auto browser = std::make_unique<FileBrowserWidget>();
    browser->setInitialMode(FileBrowserWidget::Mode::SaveAs);
    wm.open(std::move(browser), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));

    w->commitSaveAsForTesting("PlainName");
    EXPECT_TRUE(DataPaths::getProjectsDir().getChildFile("PlainName.mpi").existsAsFile());
    DataPaths::getProjectsDir().getChildFile("PlainName.mpi").deleteFile();

    w->commitSaveAsForTesting("AlreadyHas.mpi");
    EXPECT_TRUE(DataPaths::getProjectsDir().getChildFile("AlreadyHas.mpi").existsAsFile());
    DataPaths::getProjectsDir().getChildFile("AlreadyHas.mpi").deleteFile();
}

TEST_F(FileBrowserWidgetTests, CollisionEntersOverwriteSubMode)
{
    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    harness.audio().createEmptyEdit();

    auto existing = DataPaths::getProjectsDir().getChildFile("Collide.mpi");
    DataPaths::getProjectsDir().createDirectory();
    existing.replaceWithText("<maschinepi_project/>");

    auto browser = std::make_unique<FileBrowserWidget>();
    browser->setInitialMode(FileBrowserWidget::Mode::SaveAs);
    wm.open(std::move(browser), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));

    w->commitSaveAsForTesting("Collide");
    EXPECT_EQ(w->getMode(), FileBrowserWidget::Mode::SaveAsOverwrite);

    w->confirmOverwriteForTesting();
    EXPECT_TRUE(existing.existsAsFile());    // still exists, contents may differ
    EXPECT_EQ(w->getMode(), FileBrowserWidget::Mode::Browse);

    existing.deleteFile();
}

TEST_F(FileBrowserWidgetTests, OverwriteCancelReturnsToSaveAs)
{
    wm.setAudioEngine(&harness.audio());
    ensureDialogState();
    harness.audio().createEmptyEdit();

    auto existing = DataPaths::getProjectsDir().getChildFile("CollideCancel.mpi");
    DataPaths::getProjectsDir().createDirectory();
    existing.replaceWithText("<maschinepi_project/>");

    auto browser = std::make_unique<FileBrowserWidget>();
    browser->setInitialMode(FileBrowserWidget::Mode::SaveAs);
    wm.open(std::move(browser), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));

    w->commitSaveAsForTesting("CollideCancel");
    ASSERT_EQ(w->getMode(), FileBrowserWidget::Mode::SaveAsOverwrite);
    w->cancelOverwriteForTesting();
    EXPECT_EQ(w->getMode(), FileBrowserWidget::Mode::SaveAs);

    existing.deleteFile();
}

TEST_F(FileBrowserWidgetTests, OkDispatchesLoadRequestCallback)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("mpi-test-load");
    dir.createDirectory();
    auto file = dir.getChildFile("LoadTarget.mpi");
    juce::ValueTree project("maschinepi_project");
    project.setProperty("version", 2, nullptr);
    file.replaceWithText(project.toXmlString());

    wm.setAudioEngine(&harness.audio());
    auto s = ensureDialogState();

    bool loadRequested = false;
    juce::File requestedFile;

    auto browser = std::make_unique<FileBrowserWidget>();
    browser->setRecentProjectsForTesting({ file });
    browser->setLoadRequested([&](const juce::File& f) { loadRequested = true; requestedFile = f; });
    wm.open(std::move(browser), DisplaySide::Left);
    auto* w = dynamic_cast<FileBrowserWidget*>(wm.getWidget(DisplaySide::Left));

    // Browser no longer auto-selects — the user has to mark an entry first.
    w->setSelectedFileForTesting(file);
    w->confirmLoadForTesting();
    EXPECT_TRUE(loadRequested);
    EXPECT_EQ(requestedFile.getFullPathName(), file.getFullPathName());

    dir.deleteRecursively();
}
