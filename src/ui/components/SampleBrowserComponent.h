#pragma once

#include <functional>
#include <unordered_set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ListComponent.h"
#include "../../samples/SampleIndex.h"
#include "../widget/Widget.h"

class SampleBrowserComponent : public Widget
{
public:
    enum class BrowseMode
    {
        Folders,
        Categories
    };

    SampleBrowserComponent();
    ~SampleBrowserComponent() override;

    void setOnFileChosen(std::function<void(const juce::File&)> callback);
    void setOnCancel(std::function<void()> callback);
    void setOnPreview(std::function<void(const juce::File&)> callback);
    using OpenAudioEditorCallback = std::function<void(const juce::File&)>;
    void setOpenAudioEditorCallback(OpenAudioEditorCallback callback);

    void setRootDirectory(const juce::File& directory);
    const juce::File& getRootDirectory() const noexcept { return rootDirectory; }
    void setDefaultRootDirectory(const juce::File& directory);

    void focusPath(const juce::File& path);

    void setBrowseMode(BrowseMode mode);
    BrowseMode getBrowseMode() const noexcept { return browseMode_; }
    void setSampleIndex(SampleIndex index);
    void setScanning(bool scanning);
    bool isScanning() const noexcept { return scanning_; }
    void setOnBrowseModeChanged(std::function<void(BrowseMode)> callback);
    void setOnRescan(std::function<void()> callback);

    void refresh();
    void focusList();

    WidgetDescriptor describe() const override
    {
        return { "sample_browser", 1, false, DisplayConstraint::LeftOnly };
    }
    juce::String getTitle() const override
    {
        return browserTitle + (scanning_ ? "  scanning..." : juce::String());
    }
    juce::String getTitleSubtitle() const override { return currentPathLabel_; }
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int) override { return {}; }
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void handleButton(const controller_events::ButtonEvent& event) override;
    void paint(juce::Graphics& g) override;

    ListComponent& getListComponent() noexcept { return list; }

    void resized() override;

    void setAllowedExtensions(const std::vector<juce::String>& extensions);
    void setEmptyMessage(const juce::String& message);
    void setSelectLabels(const juce::String& fileLabel, const juce::String& folderLabel);
    void setBrowserTitle(const juce::String& title);
    void setPathDisplayName(const juce::String& name);

#if MASCHINEPI_TESTS
    const std::vector<ListComponent::Item>& itemsForTesting() const noexcept { return list.getItems(); }
    std::vector<juce::String> visibleIdsForTesting() const { return list.visibleIdsForTesting(); }
    juce::String currentSelectionPathForTesting() const { return currentSelection.getFullPathName(); }
    juce::String currentPathLabelForTesting() const { return currentPathLabel_; }
    void triggerModeToggleForTesting()
    {
        auto options = getOptions(0);
        if (options.size() > 1 && options[1].onInvoke) options[1].onInvoke();
    }
    void triggerRescanForTesting()
    {
        auto options = getOptions(0);
        if (options.size() > 2 && options[2].onInvoke) options[2].onInvoke();
    }
#endif

    bool keyPressed(const juce::KeyPress& key) override;

private:
    using Item = ListComponent::Item;

    void ensureChildrenLoaded(Item& item);
    void populateChildren(Item& item, const juce::File& directory);
    void populateCategoryTree();
    static Item categoryItem(const SampleCategoryNode& node);
    void rebuildCurrentMode();
    bool isSupportedFile(const juce::File& file) const;
    void handleSelectionChanged(Item& item);
    void handleSelectionCleared();
    void handleActivation(Item& item);
    void handlePreview(Item& item);
    void updatePathLabel(const juce::File& file);
    void requestOptionsRefresh();
    void activateCurrentSelection();
    static juce::String formatFileSize(const juce::File& file);

    ListComponent list;

    juce::File rootDirectory;
    std::unordered_set<juce::String> scannedDirectories;

    std::function<void(const juce::File&)> onFileChosen;
    std::function<void()> onCancel;
    std::function<void(const juce::File&)> onPreview;
    OpenAudioEditorCallback openAudioEditorCallback;

    juce::File defaultRootDirectory;
    juce::File currentSelection;
    bool hasSelection { false };
    bool selectionIsFolder { false };

    std::vector<juce::String> allowedExtensions;
    juce::String emptyStateMessage;
    juce::String fileSelectLabel { "Select" };
    juce::String folderSelectLabel { "Open" };
    juce::String pathDisplayName { "samples" };
    juce::String browserTitle { "Sample Browser" };
    juce::String currentPathLabel_;
    juce::String categoryBreadcrumb;

    BrowseMode browseMode_ { BrowseMode::Folders };
    SampleIndex sampleIndex_;
    bool scanning_ { false };
    std::function<void(BrowseMode)> onBrowseModeChanged_;
    std::function<void()> onRescan_;

    static const std::vector<juce::String> kDefaultAllowedExtensions;
};
