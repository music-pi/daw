#include <gtest/gtest.h>

#include "../src/ui/components/SampleBrowserComponent.h"

#include <algorithm>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
juce::File createTemporarySamplesDirectory()
{
    auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("maschinepi_sample_browser_test_" + juce::Uuid().toString());

    if (tempRoot.exists())
        tempRoot.deleteRecursively();

    if (!tempRoot.createDirectory())
        return {};

    const auto drums = tempRoot.getChildFile("Drums");
    if (!drums.createDirectory())
        return {};

    const auto nested = drums.getChildFile("Acoustic");
    if (!nested.createDirectory())
        return {};

    const auto sampleFile = tempRoot.getChildFile("Kick.wav");
    if (!sampleFile.replaceWithText("dummy audio data"))
        return {};

    return tempRoot;
}

class SampleBrowserComponentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tempRoot = createTemporarySamplesDirectory();
        ASSERT_TRUE(tempRoot.exists());
        browser.setRootDirectory(tempRoot);
        pathLabelBase = tempRoot.getFileName();
        if (pathLabelBase.isEmpty())
            pathLabelBase = tempRoot.getFullPathName();
        browser.setPathDisplayName(pathLabelBase);
    }

    void TearDown() override
    {
        if (tempRoot.exists())
        {
            EXPECT_TRUE(tempRoot.deleteRecursively());
        }
    }

#if MASCHINEPI_TESTS
    const auto& rootItems() const { return browser.itemsForTesting(); }
    std::vector<juce::String> visibleIds() const { return browser.visibleIdsForTesting(); }
#endif

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::File tempRoot;
    SampleBrowserComponent browser;
    juce::String pathLabelBase;
};

TEST_F(SampleBrowserComponentTest, BuildsVirtualRootAndVisibleListing)
{
#if MASCHINEPI_TESTS
    const auto descriptor = browser.describe();
    EXPECT_EQ(descriptor.id, "sample_browser");
    EXPECT_EQ(descriptor.display, DisplayConstraint::LeftOnly);
    EXPECT_EQ(browser.requiredResources(0),
              (std::vector<std::string>{ "d1", "d2", "d3", "d4" }));

    const auto options = browser.getOptions(0);
    ASSERT_EQ(options.size(), 4u);
    EXPECT_EQ(options[0].id, "sample.cancel");
    EXPECT_EQ(options[1].id, "sample.mode");
    EXPECT_EQ(options[2].id, "sample.rescan");
    EXPECT_EQ(options[3].id, "sample.open");
    ASSERT_EQ(rootItems().size(), 1);
    const auto& root = rootItems().front();
    EXPECT_TRUE(root.isVirtualRoot);
    EXPECT_TRUE(root.isFolder);
    EXPECT_FALSE(root.children.empty());

    EXPECT_EQ(browser.getRootDirectory(), tempRoot);

    const auto visible = visibleIds();
    EXPECT_FALSE(visible.empty());
    const bool rootVisible = std::any_of(visible.begin(), visible.end(),
                                         [&](const juce::String& id)
                                         {
                                             return id == tempRoot.getFullPathName();
                                         });
    EXPECT_FALSE(rootVisible);

    EXPECT_TRUE(browser.currentSelectionPathForTesting().isEmpty());
    const juce::String expectedRootLabel = "./" + pathLabelBase + "/";
    EXPECT_EQ(browser.currentPathLabelForTesting(), expectedRootLabel);
#else
    GTEST_SKIP() << "Sample browser internals only exposed when MASCHINEPI_TESTS is enabled.";
#endif
}

TEST_F(SampleBrowserComponentTest, FocusPathUpdatesSelectionAndVisibleNodes)
{
#if MASCHINEPI_TESTS
    const auto sampleDir = tempRoot.getChildFile("Drums");
    ASSERT_TRUE(sampleDir.exists());
    const auto sampleFile = tempRoot.getChildFile("Kick.wav");
    ASSERT_TRUE(sampleFile.existsAsFile());

    const juce::String expectedRootLabel = "./" + pathLabelBase + "/";

    browser.focusPath(sampleFile);
    EXPECT_EQ(browser.getRootDirectory(), tempRoot);
    EXPECT_EQ(browser.currentSelectionPathForTesting(), sampleFile.getFullPathName());

    const auto afterFocusIds = visibleIds();
    const bool sampleDirVisible = std::any_of(afterFocusIds.begin(), afterFocusIds.end(),
                                              [&](const juce::String& id)
                                              {
                                                  return id == sampleDir.getFullPathName();
                                              });
    EXPECT_TRUE(sampleDirVisible);

    const auto expectedFileLabel = "./" + pathLabelBase + "/";
    EXPECT_EQ(browser.currentPathLabelForTesting(), expectedFileLabel);

    browser.focusPath(sampleDir);
    const auto expectedDirLabel = "./" + pathLabelBase + "/Drums/";
    EXPECT_EQ(browser.currentPathLabelForTesting(), expectedDirLabel);

    browser.focusPath(tempRoot);
    EXPECT_TRUE(browser.currentSelectionPathForTesting().isEmpty());
    EXPECT_EQ(browser.currentPathLabelForTesting(), expectedRootLabel);
#else
    GTEST_SKIP() << "Sample browser internals only exposed when MASCHINEPI_TESTS is enabled.";
#endif
}

TEST_F(SampleBrowserComponentTest, CategoryModeBuildsTreeAndKeepsRealFileCallbacks)
{
#if MASCHINEPI_TESTS
    const auto sampleFile = tempRoot.getChildFile("Kick.wav");
    SampleIndex index;
    SampleIndexEntry entry;
    entry.file = sampleFile;
    entry.modificationTimeMs = sampleFile.getLastModificationTime().toMilliseconds();
    entry.size = sampleFile.getSize();
    entry.tags.category = SampleCategory::OneShot;
    entry.tags.instrumentType = SampleInstrumentType::Drum;
    entry.tags.subType = "kick";
    index.set(std::move(entry));

    SampleBrowserComponent::BrowseMode reportedMode =
        SampleBrowserComponent::BrowseMode::Folders;
    browser.setOnBrowseModeChanged([&](auto mode) { reportedMode = mode; });
    browser.setSampleIndex(std::move(index));
    browser.setBrowseMode(SampleBrowserComponent::BrowseMode::Categories);

    EXPECT_EQ(browser.getBrowseMode(), SampleBrowserComponent::BrowseMode::Categories);
    EXPECT_EQ(reportedMode, SampleBrowserComponent::BrowseMode::Categories);

    auto& list = browser.getListComponent();
    list.moveSelection(1);
    for (int step = 0; step < 6; ++step)
        list.expandSelection();
    const auto expandedIds = browser.visibleIdsForTesting();
    EXPECT_NE(std::find(expandedIds.begin(), expandedIds.end(),
                        sampleFile.getFullPathName()), expandedIds.end());
    EXPECT_GE(list.contentHeightForTesting(),
              static_cast<int>(expandedIds.size()) * 28);

    browser.focusPath(sampleFile);
    EXPECT_EQ(browser.currentSelectionPathForTesting(), sampleFile.getFullPathName());
    EXPECT_TRUE(browser.currentPathLabelForTesting().contains("One-Shots"));
    EXPECT_TRUE(browser.currentPathLabelForTesting().contains("Kick"));

    juce::File selected;
    browser.setOnFileChosen([&](const juce::File& file) { selected = file; });
    browser.getListComponent().invokeSelection();
    EXPECT_EQ(selected, sampleFile);

    browser.triggerModeToggleForTesting();
    EXPECT_EQ(browser.getBrowseMode(), SampleBrowserComponent::BrowseMode::Folders);
#endif
}

TEST_F(SampleBrowserComponentTest, RescanAndScanningStateRemainUsable)
{
#if MASCHINEPI_TESTS
    int rescans = 0;
    browser.setOnRescan([&]() { ++rescans; });
    browser.triggerRescanForTesting();
    EXPECT_EQ(rescans, 1);

    browser.setBrowseMode(SampleBrowserComponent::BrowseMode::Categories);
    browser.setScanning(true);
    EXPECT_TRUE(browser.isScanning());
    browser.setScanning(false);
    EXPECT_FALSE(browser.isScanning());
#endif
}

} // namespace
