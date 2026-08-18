// src/ui/widget/FileBrowserWidget.cpp
#include "FileBrowserWidget.h"

#include "../../app/DataPaths.h"
#include "../../engine/AudioEngine.h"
#include "../theme/UiTheme.h"
#include "ConfirmDialog.h"
#include "FileDialogStateKeys.h"
#include "WindowManager.h"
#include "../UiRefresh.h"

FileBrowserWidget::FileBrowserWidget()
{
    addAndMakeVisible(list_);
    addChildComponent(field_);    // invisible until Search/SaveAs

    // Selection change publishes the new path into settings.fileDialog.selectedFile
    // so the preview widget (listening on the same tree) repaints. Also refresh
    // the option bar so d1 (Save) flips between disabled / Save / Save over.
    list_.setOnSelectionChanged([this](ListComponent::Item& item) {
        if (state_.isValid())
            state_.setProperty(FileDialogStateKeys::kSelectedFile, item.id, nullptr);
        if (auto* wm = windowManager()) wm->refreshBars();
    });

    // OK / double-click on a row = trigger the load-confirm dialog.
    list_.setOnItemActivated([this](ListComponent::Item&) {
        confirmLoadForTesting();
    });
}

FileBrowserWidget::~FileBrowserWidget() = default;

void FileBrowserWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    state_ = engine().getSettingsState()
        .getOrCreateChildWithName(FileDialogStateKeys::kRoot, nullptr);
    state_.addListener(this);

    if (!testingProjectsOverride_)
        refreshRecentProjects();
    rebuildList();
    enterMode(initialMode_);

    // Start with nothing selected so the preview stays blank and d1 (Save)
    // is disabled until the user deliberately marks an entry.
    if (state_.isValid())
        state_.setProperty(FileDialogStateKeys::kSelectedFile, juce::String(), nullptr);
}

void FileBrowserWidget::onDeactivated()
{
    // Clear callbacks before ending edit to prevent enterMode re-entry
    // during widget teardown (T9 onDeactivated fires when closeSlot runs).
    field_.setOnCancel(nullptr);
    field_.setOnCommit(nullptr);
    if (field_.isEditing()) field_.endEdit(false);
    if (state_.isValid()) state_.removeListener(this);
}

std::vector<std::string> FileBrowserWidget::requiredResources(int page)
{
    if (page != 0) return {};
    std::vector<std::string> r;
    for (int i = 1; i <= 4; ++i)
    {
        r.push_back("d" + std::to_string(i));
        r.push_back("k" + std::to_string(i));
    }
    return r;
}

std::vector<Option> FileBrowserWidget::getOptions(int page)
{
    if (page != 0) return {};
    std::vector<Option> opts;

    if (mode_ == Mode::Browse)
    {
        const bool haveCurrent = engine().getCurrentProjectFile().existsAsFile();
        const auto* selectedItem = list_.getSelectedItem();
        const bool haveSelection = selectedItem != nullptr;
        const juce::File selectedFile = haveSelection
            ? juce::File(selectedItem->id) : juce::File();

        // d1 Save — behaviour depends on context:
        //   - nothing saved + no selection → acts as Save As (no other save path)
        //   - a list entry is marked          → save over it (with confirm)
        //   - have a current project, no mark → save in place (no confirm needed)
        //   - else                            → disabled
        Option save;
        save.id = "file.save";
        if (haveSelection)
        {
            save.label = "Save over";
            save.state = OptionState::Enabled;
            save.onInvoke = [this, selectedFile]() {
                auto* wm = windowManager();
                if (wm == nullptr) return;
                const juce::String name = selectedFile.getFileNameWithoutExtension();
                wm->showDialog(std::make_unique<ConfirmDialog>(
                    juce::String("Overwrite?"),
                    juce::String("Save over '") + name + "'?",
                    juce::String("Overwrite"),
                    [this, selectedFile]() {
                        if (engine().saveProjectToFile(selectedFile))
                            showToast(ToastKind::Success, "Saved over '"
                                      + selectedFile.getFileNameWithoutExtension() + "'");
                        else
                            showToast(ToastKind::Error, "Save failed");
                    }));
            };
        }
        else if (haveCurrent)
        {
            save.label = "Save";
            save.state = OptionState::Enabled;
            save.onInvoke = [this]() {
                if (engine().saveCurrentProject()) showToast(ToastKind::Success, "Saved");
                else                               showToast(ToastKind::Error, "Save failed");
            };
        }
        else
        {
            save.label = "Save as...";
            save.state = OptionState::Enabled;
            save.onInvoke = [this]() { enterMode(Mode::SaveAs); };
        }

        Option saveAs;
        saveAs.id = "file.saveAs";
        saveAs.label = "Save as...";
        saveAs.state = OptionState::Enabled;
        saveAs.onInvoke = [this]() { enterMode(Mode::SaveAs); };

        // d3 New — fresh empty edit. Always confirms since it drops the
        // current state; there's no separate unsaved-changes tracking yet.
        Option newProject;
        newProject.id = "file.new";
        newProject.label = "New";
        newProject.state = OptionState::Enabled;
        newProject.onInvoke = [this]() {
            auto* wm = windowManager();
            if (wm == nullptr) return;
            wm->showDialog(std::make_unique<ConfirmDialog>(
                juce::String("New Project"),
                juce::String("Start a new project? Unsaved changes will be lost."),
                juce::String("New"),
                [this]() {
                    engine().createEmptyEdit();
                    if (dismissRequested_) dismissRequested_();
                    showToast(ToastKind::Info, "New project");
                }));
        };

        Option close;
        close.id = "file.close";
        close.label = "Close";
        close.state = OptionState::Enabled;
        close.onInvoke = [this]() {
            if (dismissRequested_) dismissRequested_();
        };

        // d1 Save · d2 Save as... · d3 New · d4 Close
        opts = { save, saveAs, newProject, close };
    }
    else if (mode_ == Mode::SaveAsOverwrite)
    {
        Option overwrite;
        overwrite.id = "file.overwrite";
        overwrite.label = "Overwrite";
        overwrite.state = OptionState::Enabled;
        overwrite.onInvoke = [this]() { confirmOverwriteForTesting(); };

        Option cancel;
        cancel.id = "file.overwrite.cancel";
        cancel.label = "Cancel";
        cancel.state = OptionState::Enabled;
        cancel.onInvoke = [this]() { cancelOverwriteForTesting(); };

        opts = { overwrite, cancel, {}, {} };
    }
    else
    {
        Option cancel;
        cancel.id = "file.cancel";
        cancel.label = "Cancel";
        cancel.state = OptionState::Enabled;
        cancel.onInvoke = [this]() {
            if (field_.isEditing()) field_.endEdit(false);
            enterMode(Mode::Browse);
        };
        opts = { cancel, {}, {}, {} };
    }
    return opts;
}

std::vector<Knob> FileBrowserWidget::getKnobs(int page)
{
    if (page != 0) return {};
    return {};
}

void FileBrowserWidget::enterMode(Mode m)
{
    mode_ = m;

    if (state_.isValid())
    {
        const juce::String modeName =
            m == Mode::Browse           ? juce::String("browse") :
            m == Mode::Search           ? juce::String("search") :
            m == Mode::SaveAs           ? juce::String("saveAs") :
                                          juce::String("saveAsOverwrite");
        state_.setProperty(FileDialogStateKeys::kMode, modeName, nullptr);
    }

    if (m == Mode::Search)
    {
        TextInputComponent::Config cfg;
        cfg.prompt = "Search";
        cfg.dictionaryScope = "project-names";
        cfg.maxLength = 64;
        cfg.charFilter = [](juce::juce_wchar c) {
            return c >= 0x20 && c <= 0x7e;  // printable ASCII
        };
        field_.configure(cfg);
        field_.setOnTextChanged([this](juce::String q) {
            if (state_.isValid())
                state_.setProperty(FileDialogStateKeys::kQuery, q, nullptr);
            applyQuery(q);
        });
        field_.setOnCommit([this](juce::String q) {
            if (state_.isValid())
                state_.setProperty(FileDialogStateKeys::kQuery, q, nullptr);
            applyQuery(q);
            enterMode(Mode::Browse);
        });
        field_.setOnCancel([this]() {
            if (state_.isValid())
                state_.setProperty(FileDialogStateKeys::kQuery, juce::String(), nullptr);
            applyQuery({});
            enterMode(Mode::Browse);
        });
        field_.beginEdit();
    }
    else if (m == Mode::SaveAs)
    {
        TextInputComponent::Config cfg;
        cfg.prompt = "Save project as";
        cfg.dictionaryScope = "project-names";
        cfg.maxLength = 64;
        cfg.charFilter = [](juce::juce_wchar c) {
            if (c < 0x20 || c > 0x7e) return false;
            switch (c) {
                case '/': case '\\': case ':': case '*':
                case '?': case '"': case '<':  case '>':
                case '|':
                    return false;
                default: return true;
            }
        };
        cfg.initialValue = engine().getCurrentProjectFile().getFileNameWithoutExtension();

        field_.configure(cfg);
        field_.setOnTextChanged(nullptr);
        field_.setOnCommit([this](juce::String name) { commitSaveAs(name); });
        field_.setOnCancel([this]() { enterMode(Mode::Browse); });
        field_.beginEdit();
    }
    else if (m == Mode::Rename)
    {
        TextInputComponent::Config cfg;
        cfg.prompt = "Rename project to";
        cfg.dictionaryScope = "project-names";
        cfg.maxLength = 64;
        cfg.charFilter = [](juce::juce_wchar c) {
            if (c < 0x20 || c > 0x7e) return false;
            switch (c) {
                case '/': case '\\': case ':': case '*':
                case '?': case '"': case '<':  case '>':
                case '|':
                    return false;
                default: return true;
            }
        };
        cfg.initialValue = renameSource_.getFileNameWithoutExtension();

        field_.configure(cfg);
        field_.setOnTextChanged(nullptr);
        field_.setOnCommit([this](juce::String name) { commitRename(name); });
        field_.setOnCancel([this]() { enterMode(Mode::Browse); });
        field_.beginEdit();
    }
    else if (m == Mode::SaveAsOverwrite)
    {
        // End any active T9 so the right panel is free. Clear the field's
        // cancel callback first — otherwise the SaveAs onCancel (which calls
        // enterMode(Browse)) fires re-entrantly from inside endEdit, writing
        // mode to "browse" before SaveAsOverwrite finishes configuring itself.
        if (field_.isEditing())
        {
            field_.setOnCancel(nullptr);
            field_.endEdit(false);
        }
    }
    else if (m == Mode::Browse)
    {
        // Same re-entrancy guard: onCancel from Search/SaveAs would call
        // enterMode(Browse) again, re-running this branch.
        if (field_.isEditing())
        {
            field_.setOnCancel(nullptr);
            field_.endEdit(false);
        }
    }

    list_.setVisible(m == Mode::Browse || m == Mode::Search);
    field_.setVisible(m == Mode::Search || m == Mode::SaveAs || m == Mode::Rename);
    resized();
    repaint();
}

void FileBrowserWidget::applyQuery(const juce::String& q)
{
    if (q.isEmpty())
    {
        filteredProjects_ = recentProjects_;
    }
    else
    {
        filteredProjects_.clear();
        for (const auto& f : recentProjects_)
        {
            if (f.getFileNameWithoutExtension().containsIgnoreCase(q))
                filteredProjects_.push_back(f);
        }
    }
    rebuildList();
}

void FileBrowserWidget::setRecentProjectsForTesting(std::vector<juce::File> projects)
{
    testingProjectsOverride_ = true;
    recentProjects_ = std::move(projects);
    filteredProjects_ = recentProjects_;
    rebuildList();
}

void FileBrowserWidget::setSearchQueryForTesting(const juce::String& q)
{
    if (state_.isValid())
        state_.setProperty(FileDialogStateKeys::kQuery, q, nullptr);
    applyQuery(q);
}

void FileBrowserWidget::refreshRecentProjects()
{
    // DataPaths returns both .mpi (project metadata) and .tracktionedit (its
    // companion audio file) entries, so every saved project appears twice.
    // Filter to .mpi only — the user-facing notion of a "project" is the
    // .mpi file.
    recentProjects_.clear();
    for (const auto& f : DataPaths::findRecentProjects(64))
        if (f.getFileExtension().equalsIgnoreCase(".mpi"))
            recentProjects_.push_back(f);
    if (recentProjects_.size() > 32)
        recentProjects_.resize(32);
    filteredProjects_ = recentProjects_;
}

void FileBrowserWidget::rebuildList()
{
    std::vector<ListComponent::Item> items;
    items.reserve(filteredProjects_.size());
    for (const auto& f : filteredProjects_)
    {
        ListComponent::Item item;
        item.id = f.getFullPathName();
        item.label = f.getFileNameWithoutExtension();
        item.isSelectable = true;
        items.push_back(std::move(item));
    }
    list_.setItems(std::move(items));
    list_.refreshContent();
    // No auto-select — Save (d1) must be explicitly unlocked by the user
    // marking an entry (or by an existing save file for save-in-place).
}

void FileBrowserWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void FileBrowserWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    // Task 1 renders only the list / empty state. Further states in later tasks.
    if (filteredProjects_.empty())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawFittedText("No recent projects — press Save as…",
                         bounds.reduced(8),
                         juce::Justification::centred, 2);
    }
}

void FileBrowserWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (!e.pressed) return;

    // Nav is only meaningful in the Browse mode — Search / SaveAs / overwrite
    // swallow nav via the active TextInputComponent or the option bar.
    if (mode_ != Mode::Browse) return;

    if (e.name == "navUp")        list_.moveSelection(-1);
    else if (e.name == "navDown") list_.moveSelection(+1);
    else if (e.name == "navPush") list_.invokeSelection();
}

void FileBrowserWidget::setSelectedFileForTesting(const juce::File& file)
{
    if (!state_.isValid()) return;
    state_.setProperty(FileDialogStateKeys::kSelectedFile, file.getFullPathName(), nullptr);
}

void FileBrowserWidget::resized()
{
    list_.setBounds(getLocalBounds());
    field_.setBounds(getLocalBounds().removeFromTop(40));
}

bool FileBrowserWidget::commitSaveAs(const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty()) return false;

    juce::String filename = trimmed;
    if (!filename.endsWithIgnoreCase(".mpi"))
        filename += ".mpi";

    auto target = DataPaths::getProjectsDir().getChildFile(filename);
    DataPaths::getProjectsDir().createDirectory();

    if (target.existsAsFile())
    {
        pendingOverwriteName_ = filename;
        if (state_.isValid())
            state_.setProperty(FileDialogStateKeys::kSaveAsName, filename, nullptr);
        enterMode(Mode::SaveAsOverwrite);
        return false;
    }

    const bool ok = engine().saveProjectToFile(target);
    if (!ok) return false;

    if (controllerHost() != nullptr)
        showToast(ToastKind::Success, "Saved");

    // After a successful save the File dialog's job is done — dismiss the
    // whole thing rather than dropping the user back into Browse.
    field_.endEdit(false);
    if (dismissRequested_) dismissRequested_();
    return true;
}

void FileBrowserWidget::commitSaveAsForTesting(const juce::String& name)
{
    commitSaveAs(name);
}

bool FileBrowserWidget::commitRename(const juce::String& newName)
{
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty()) return false;
    if (!renameSource_.existsAsFile()) return false;

    juce::String filename = trimmed;
    if (!filename.endsWithIgnoreCase(".mpi")) filename += ".mpi";

    auto target = DataPaths::getProjectsDir().getChildFile(filename);
    if (target == renameSource_) { enterMode(Mode::Browse); return true; } // no-op rename
    if (target.existsAsFile()) {
        // Collision: refuse and tell the user. A dedicated "rename-overwrite"
        // sub-mode is possible but out of scope for this iteration.
        showToast(ToastKind::Warning, "Name already in use");
        return false;
    }

    // Move the .mpi file and its companion .tracktionedit (if present).
    const auto companionSource = renameSource_.withFileExtension(".tracktionedit");
    const auto companionTarget = target.withFileExtension(".tracktionedit");

    if (!renameSource_.moveFileTo(target))
    {
        showToast(ToastKind::Error, "Rename failed");
        return false;
    }
    if (companionSource.existsAsFile())
    {
        // Best-effort; if it fails the .mpi still points at its old companion
        // path internally. That's recoverable by re-saving.
        companionSource.moveFileTo(companionTarget);
    }

    showToast(ToastKind::Success, "Renamed");
    field_.endEdit(false);
    renameSource_ = juce::File();
    if (dismissRequested_) dismissRequested_();
    return true;
}

void FileBrowserWidget::beginRename(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    renameSource_ = file;
    enterMode(Mode::Rename);
}

void FileBrowserWidget::refreshList()
{
    if (!testingProjectsOverride_)
        refreshRecentProjects();
    applyQuery(state_.isValid()
               ? state_.getProperty(FileDialogStateKeys::kQuery).toString()
               : juce::String());
}

void FileBrowserWidget::confirmOverwriteForTesting()
{
    if (mode_ != Mode::SaveAsOverwrite) return;
    const auto name = pendingOverwriteName_;
    auto target = DataPaths::getProjectsDir().getChildFile(name);
    const bool ok = engine().saveProjectToFile(target);
    pendingOverwriteName_.clear();
    if (ok)
    {
        showToast(ToastKind::Success, "Saved");
        if (dismissRequested_) { dismissRequested_(); return; }
    }
    enterMode(Mode::Browse);
}

void FileBrowserWidget::cancelOverwriteForTesting()
{
    if (mode_ != Mode::SaveAsOverwrite) return;
    pendingOverwriteName_.clear();
    enterMode(Mode::SaveAs);
}

void FileBrowserWidget::confirmLoadForTesting()
{
    if (filteredProjects_.empty()) return;
    if (state_.isValid())
    {
        const auto path = state_.getProperty(FileDialogStateKeys::kSelectedFile).toString();
        juce::File file(path);
        if (!file.existsAsFile()) return;
        // Delegate to UiHost: request a confirm dialog via the dismiss-callback pattern.
        if (loadRequested_) loadRequested_(file);
    }
}
