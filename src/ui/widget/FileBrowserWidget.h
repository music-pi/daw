// src/ui/widget/FileBrowserWidget.h
#pragma once

#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../components/ListComponent.h"
#include "../components/TextInputComponent.h"
#include "Widget.h"

class FileBrowserWidget : public Widget, private juce::ValueTree::Listener
{
public:
    enum class Mode { Browse, Search, SaveAs, SaveAsOverwrite, Rename };

    FileBrowserWidget();
    ~FileBrowserWidget() override;

    WidgetDescriptor describe() const override
    {
        return { "file_browser", 1, false, DisplayConstraint::LeftOnly };
    }

    juce::String getTitle() const override { return "File"; }

    void onActivated(int offset) override;
    void onDeactivated() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    void paint(juce::Graphics& g) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void resized() override;

    void handleButton(const controller_events::ButtonEvent& e) override;

    Mode getMode() const { return mode_; }
    void setInitialMode(Mode m) { initialMode_ = m; }

    /** Select a specific file and publish it to settings.fileDialog.selectedFile.
        Primarily for tests; production flow uses list navigation. */
    void setSelectedFileForTesting(const juce::File& file);

    /** Inject a test-local recent-projects vector instead of reading DataPaths.
        When set, onActivated will not call refreshRecentProjects(). */
    void setRecentProjectsForTesting(std::vector<juce::File> projects);

    /** Directly apply a query string and rebuild the filtered list. */
    void setSearchQueryForTesting(const juce::String& q);

    /** Current filtered list size. */
    size_t filteredCountForTesting() const { return filteredProjects_.size(); }

    /** Directly invoke the save-as commit path with a chosen name. Testing hook. */
    void commitSaveAsForTesting(const juce::String& name);

    /** Confirm overwrite when in SaveAsOverwrite sub-mode. Testing hook and d1 button. */
    void confirmOverwriteForTesting();

    /** Cancel overwrite and return to SaveAs mode. Testing hook and d2 button. */
    void cancelOverwriteForTesting();

    /** UiHost sets this callback so the browser can request full dialog dismissal. */
    void setDismissRequested(std::function<void()> cb) { dismissRequested_ = std::move(cb); }

    /** UiHost sets this callback so the browser can request a load-confirm dialog. */
    void setLoadRequested(std::function<void(const juce::File&)> cb) { loadRequested_ = std::move(cb); }

    /** Trigger the load-confirm dialog for the currently-highlighted file. */
    void confirmLoadForTesting();

    /** Enter Rename mode against a specific file (called by UiHost when the
        preview widget's Rename option is invoked). */
    void beginRename(const juce::File& file);

    /** Re-read recent projects from disk and repaint. Called after file
        operations (delete / rename) modify the directory. */
    void refreshList();

private:
    // Returns true if the save succeeded; false on write error or collision (see Task 8).
    bool commitSaveAs(const juce::String& name);
    bool commitRename(const juce::String& newName);
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override {}

    void enterMode(Mode m);
    void applyQuery(const juce::String& q);
    void rebuildList();
    void refreshRecentProjects();

    Mode mode_ = Mode::Browse;
    Mode initialMode_ = Mode::Browse;

    juce::ValueTree state_;
    std::vector<juce::File> recentProjects_;
    std::vector<juce::File> filteredProjects_;
    bool testingProjectsOverride_ = false;
    ListComponent list_;
    TextInputComponent field_;

    juce::String pendingOverwriteName_;
    juce::File   renameSource_;   // original file when Mode::Rename is active
    std::function<void()> dismissRequested_;
    std::function<void(const juce::File&)> loadRequested_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileBrowserWidget)
};
