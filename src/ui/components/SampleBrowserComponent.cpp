#include "SampleBrowserComponent.h"

#include <algorithm>

namespace
{
juce::String directoryDisplayName(const juce::File& directory)
{
    auto name = directory.getFileName();
    if (name.isEmpty())
        return directory.getFullPathName();

    return name;
}
} // namespace

const std::vector<juce::String> SampleBrowserComponent::kDefaultAllowedExtensions{
    ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg"
};

SampleBrowserComponent::SampleBrowserComponent()
    : allowedExtensions(kDefaultAllowedExtensions),
      emptyStateMessage("No supported files in this directory")
{
    setInterceptsMouseClicks(true, true);

    addAndMakeVisible(list);
    list.setEmptyMessage(emptyStateMessage);
    list.setRowHeight(28);

    list.setOnSelectionChanged([this](Item& item) { handleSelectionChanged(item); });
    list.setOnSelectionCleared([this]() { handleSelectionCleared(); });
    list.setOnItemActivated([this](Item& item) { handleActivation(item); });
    list.setOnItemExpansionChanged([this](Item& item, bool) { ensureChildrenLoaded(item); });
    list.setOnItemPreview([this](Item& item) { handlePreview(item); });

}

SampleBrowserComponent::~SampleBrowserComponent()
{
}

void SampleBrowserComponent::onActivated(int offset)
{
    panelOffset_ = offset;
    list.activateNavigation(false);
    repaint();
}

void SampleBrowserComponent::onDeactivated()
{
    if (onPreview)
        onPreview(juce::File());
}

std::vector<std::string> SampleBrowserComponent::requiredResources(int page)
{
    if (page != 0)
        return {};
    return { "d1", "d2", "d3", "d4" };
}

std::vector<Option> SampleBrowserComponent::getOptions(int page)
{
    if (page != 0)
        return {};

    Option cancel;
    cancel.id = "sample.cancel";
    cancel.label = "Cancel";
    cancel.state = OptionState::Enabled;
    cancel.onInvoke = [this]() { if (onCancel) onCancel(); };

    Option mode;
    mode.id = "sample.mode";
    mode.label = browseMode_ == BrowseMode::Folders ? "Categories" : "Folders";
    mode.state = browseMode_ == BrowseMode::Categories
                     ? OptionState::Active : OptionState::Enabled;
    mode.onInvoke = [this]()
    {
        setBrowseMode(browseMode_ == BrowseMode::Folders
                          ? BrowseMode::Categories : BrowseMode::Folders);
    };

    Option rescan;
    rescan.id = "sample.rescan";
    rescan.label = scanning_ ? "Scanning..." : "Rescan";
    rescan.state = onRescan_ != nullptr && !scanning_
                       ? OptionState::Enabled : OptionState::Disabled;
    rescan.onInvoke = [this]() { if (onRescan_) onRescan_(); };

    Option open;
    open.id = "sample.open";
    open.label = selectionIsFolder ? folderSelectLabel : fileSelectLabel;
    open.state = hasSelection ? OptionState::Enabled : OptionState::Disabled;
    open.onInvoke = [this]() { activateCurrentSelection(); };
    return { cancel, mode, rescan, open };
}

void SampleBrowserComponent::paintPage(juce::Graphics& g,
                                       int,
                                       juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colours::black.withAlpha(0.15f));
    g.fillRect(bounds);
}

void SampleBrowserComponent::paint(juce::Graphics& g)
{
    paintPage(g, currentPage(), getLocalBounds());
}

void SampleBrowserComponent::handleButton(const controller_events::ButtonEvent& event)
{
    if (!event.pressed)
        return;
    if (event.name == "navUp") list.moveSelection(-1);
    else if (event.name == "navDown") list.moveSelection(1);
    else if (event.name == "navLeft") list.collapseSelection();
    else if (event.name == "navRight") list.expandSelection();
    else if (event.name == "navPush") list.invokeSelection();
}

void SampleBrowserComponent::setOnFileChosen(std::function<void(const juce::File&)> callback)
{
    onFileChosen = std::move(callback);
}

void SampleBrowserComponent::setOnCancel(std::function<void()> callback)
{
    onCancel = std::move(callback);
}

void SampleBrowserComponent::setOnPreview(std::function<void(const juce::File&)> callback)
{
    onPreview = std::move(callback);
}

void SampleBrowserComponent::setOpenAudioEditorCallback(OpenAudioEditorCallback callback)
{
    openAudioEditorCallback = std::move(callback);
}

void SampleBrowserComponent::setRootDirectory(const juce::File& directory)
{
    rootDirectory = directory.exists() ? directory : defaultRootDirectory;
    rebuildCurrentMode();
}

void SampleBrowserComponent::rebuildCurrentMode()
{
    scannedDirectories.clear();

    currentSelection = juce::File();
    hasSelection = false;
    selectionIsFolder = false;

    if (browseMode_ == BrowseMode::Categories)
    {
        populateCategoryTree();
        updatePathLabel(rootDirectory);
        requestOptionsRefresh();
        return;
    }

    Item pseudoRoot;
    pseudoRoot.id = rootDirectory.getFullPathName();
    pseudoRoot.label = directoryDisplayName(rootDirectory);
    pseudoRoot.isFolder = true;
    pseudoRoot.isExpanded = true;
    pseudoRoot.isVirtualRoot = true;

    populateChildren(pseudoRoot, rootDirectory);

    std::vector<Item> roots;
    roots.push_back(std::move(pseudoRoot));
    list.setItems(std::move(roots));
    list.setEmptyMessage(emptyStateMessage);

    updatePathLabel(rootDirectory);

    requestOptionsRefresh();
}

void SampleBrowserComponent::setBrowseMode(BrowseMode mode)
{
    if (browseMode_ == mode)
        return;
    browseMode_ = mode;
    rebuildCurrentMode();
    if (onBrowseModeChanged_)
        onBrowseModeChanged_(mode);
}

void SampleBrowserComponent::setSampleIndex(SampleIndex index)
{
    const auto selectedPath = currentSelection.existsAsFile()
                                  ? currentSelection
                                  : juce::File();
    sampleIndex_ = std::move(index);
    if (browseMode_ == BrowseMode::Categories)
    {
        rebuildCurrentMode();
        if (selectedPath.existsAsFile() && sampleIndex_.find(selectedPath) != nullptr)
            focusPath(selectedPath);
    }
}

void SampleBrowserComponent::setScanning(bool scanning)
{
    scanning_ = scanning;
    if (browseMode_ == BrowseMode::Categories && sampleIndex_.empty())
        list.setEmptyMessage(scanning ? "Scanning sample library..."
                                      : (rootDirectory.isDirectory()
                                             ? "No categorized samples"
                                             : "Sample folder not found"));
    requestOptionsRefresh();
}

void SampleBrowserComponent::setOnBrowseModeChanged(
    std::function<void(BrowseMode)> callback)
{
    onBrowseModeChanged_ = std::move(callback);
}

void SampleBrowserComponent::setOnRescan(std::function<void()> callback)
{
    onRescan_ = std::move(callback);
    requestOptionsRefresh();
}

SampleBrowserComponent::Item SampleBrowserComponent::categoryItem(
    const SampleCategoryNode& node)
{
    Item item;
    item.id = node.isFile ? node.file.getFullPathName() : node.id;
    item.label = node.label;
    item.description = node.breadcrumb;
    item.isFolder = !node.isFile;
    item.isExpanded = false;
    item.isSelectable = true;
    item.children.reserve(node.children.size());
    for (const auto& child : node.children)
        item.children.push_back(categoryItem(child));
    return item;
}

void SampleBrowserComponent::populateCategoryTree()
{
    Item pseudoRoot;
    pseudoRoot.id = "category:root";
    pseudoRoot.label = "Categories";
    pseudoRoot.isFolder = true;
    pseudoRoot.isExpanded = true;
    pseudoRoot.isVirtualRoot = true;
    for (const auto& node : sampleIndex_.buildCategoryTree())
        pseudoRoot.children.push_back(categoryItem(node));
    list.setItems({ std::move(pseudoRoot) });
    list.setEmptyMessage(scanning_ ? "Scanning sample library..."
                                   : (rootDirectory.isDirectory()
                                          ? "No categorized samples"
                                          : "Sample folder not found"));
}

void SampleBrowserComponent::setDefaultRootDirectory(const juce::File& directory)
{
    defaultRootDirectory = directory;
}

void SampleBrowserComponent::focusPath(const juce::File& path)
{
    if (!path.exists())
        return;

    if (browseMode_ == BrowseMode::Categories)
    {
        std::function<bool(Item&)> reveal = [&](Item& item)
        {
            if (item.id == path.getFullPathName())
                return true;
            for (auto& child : item.children)
            {
                if (reveal(child))
                {
                    item.isExpanded = true;
                    return true;
                }
            }
            return false;
        };
        for (auto& root : list.getItems())
            if (reveal(root))
            {
                list.refreshContent();
                list.setSelectedId(path.getFullPathName());
                return;
            }
        return;
    }

    const bool pathIsFile = path.existsAsFile();
    juce::File targetDirectory = pathIsFile ? path.getParentDirectory() : path;

    if (targetDirectory != rootDirectory && !targetDirectory.isAChildOf(rootDirectory))
        return;

    auto& roots = list.getItems();
    if (roots.empty())
        return;

    Item* current = &roots.front();
    if (!current->isExpanded)
        current->isExpanded = true;

    ensureChildrenLoaded(*current);

    if (targetDirectory != rootDirectory)
    {
        const juce::String relativePath = targetDirectory.getRelativePathFrom(rootDirectory);
        juce::StringArray segments;
        segments.addTokens(relativePath, juce::File::getSeparatorString(), "");
        segments.trim();
        segments.removeEmptyStrings();

        juce::File currentDir = rootDirectory;

        for (const auto& segment : segments)
        {
            currentDir = currentDir.getChildFile(segment);

            ensureChildrenLoaded(*current);

            auto childIter = std::find_if(current->children.begin(),
                                          current->children.end(),
                                          [&](Item& child)
                                          {
                                              return child.id == currentDir.getFullPathName();
                                          });

            if (childIter == current->children.end())
                return;

            childIter->isExpanded = true;
            current = &(*childIter);
        }
    }

    list.refreshContent();

    if (pathIsFile)
    {
        ensureChildrenLoaded(*current);
        list.setSelectedId(path.getFullPathName());
    }
    else if (targetDirectory != rootDirectory)
    {
        list.setSelectedId(targetDirectory.getFullPathName());
    }
    else
    {
        list.clearSelection();
    }
}

void SampleBrowserComponent::refresh()
{
    rebuildCurrentMode();
}

void SampleBrowserComponent::focusList()
{
    list.grabKeyboardFocus();
}


void SampleBrowserComponent::resized()
{
    list.setBounds(getLocalBounds());
}

void SampleBrowserComponent::setAllowedExtensions(const std::vector<juce::String>& extensions)
{
    allowedExtensions.clear();
    allowedExtensions.reserve(extensions.size());
    for (auto ext : extensions)
    {
        allowedExtensions.emplace_back(ext.toLowerCase());
    }
    refresh();
}

void SampleBrowserComponent::setEmptyMessage(const juce::String& message)
{
    emptyStateMessage = message;
    list.setEmptyMessage(emptyStateMessage);
}

void SampleBrowserComponent::setSelectLabels(const juce::String& fileLabel, const juce::String& folderLabel)
{
    fileSelectLabel = fileLabel;
    folderSelectLabel = folderLabel;
    requestOptionsRefresh();
}

void SampleBrowserComponent::setBrowserTitle(const juce::String& title)
{
    browserTitle = title;
    repaint();
}

void SampleBrowserComponent::setPathDisplayName(const juce::String& name)
{
    pathDisplayName = name;
    const auto file = hasSelection ? currentSelection : rootDirectory;
    updatePathLabel(file);
}

bool SampleBrowserComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (onCancel)
            onCancel();
        return true;
    }

    if (key == juce::KeyPress(juce::KeyPress::returnKey))
    {
        activateCurrentSelection();
        return true;
    }

    return false;
}

void SampleBrowserComponent::ensureChildrenLoaded(Item& item)
{
    const juce::File directory(item.id);
    if (!item.isFolder || !directory.isDirectory())
        return;

    const auto path = directory.getFullPathName();
    if (!scannedDirectories.insert(path).second)
        return;

    populateChildren(item, directory);
    list.refreshContent();
}

void SampleBrowserComponent::populateChildren(Item& item, const juce::File& directory)
{
    std::vector<Item> directories;
    std::vector<Item> files;

    for (const auto& entry : juce::RangedDirectoryIterator(directory, false,
                                                            "*",
                                                            juce::File::findDirectories | juce::File::findFiles))
    {
        const auto file = entry.getFile();

        if (file.isDirectory())
        {
            Item child;
            child.id = file.getFullPathName();
            child.label = directoryDisplayName(file);
            child.isFolder = true;
            child.isExpanded = false;
            directories.push_back(std::move(child));
        }
        else if (isSupportedFile(file))
        {
            Item child;
            child.id = file.getFullPathName();
            child.label = file.getFileNameWithoutExtension();
            child.isFolder = false;
            child.isSelectable = true;
            child.description = formatFileSize(file);
            files.push_back(std::move(child));
        }
    }

    auto sortByLabel = [](const Item& a, const Item& b)
    {
        return a.label.compareIgnoreCase(b.label) < 0;
    };

    std::sort(directories.begin(), directories.end(), sortByLabel);
    std::sort(files.begin(), files.end(), sortByLabel);

    item.children.clear();
    item.children.reserve(directories.size() + files.size());

    for (auto& dir : directories)
        item.children.push_back(std::move(dir));

    for (auto& fileItem : files)
        item.children.push_back(std::move(fileItem));
}

bool SampleBrowserComponent::isSupportedFile(const juce::File& file) const
{
    if (!file.existsAsFile())
        return false;

    const auto extension = file.getFileExtension().toLowerCase();
    if (allowedExtensions.empty())
        return true;

    return std::find(allowedExtensions.begin(), allowedExtensions.end(), extension)
           != allowedExtensions.end();
}

void SampleBrowserComponent::handleSelectionChanged(Item& item)
{
    if (onPreview)
        onPreview(juce::File());

    const juce::File file(item.id);
    currentSelection = browseMode_ == BrowseMode::Folders
                           ? file
                           : (file.existsAsFile() ? file : juce::File());
    hasSelection = item.id.isNotEmpty();
    selectionIsFolder = item.isFolder;
    categoryBreadcrumb = item.description;
    updatePathLabel(browseMode_ == BrowseMode::Folders
                        ? currentSelection
                        : (currentSelection.existsAsFile() ? currentSelection : rootDirectory));
    requestOptionsRefresh();
}

void SampleBrowserComponent::handleSelectionCleared()
{
    if (onPreview)
        onPreview(juce::File());

    currentSelection = juce::File();
    hasSelection = false;
    selectionIsFolder = false;
    categoryBreadcrumb.clear();
    updatePathLabel(rootDirectory);
    requestOptionsRefresh();
}

void SampleBrowserComponent::handleActivation(Item& item)
{
    const juce::File file(item.id);
    if (!file.existsAsFile())
        return;

    // Check if we're in PadDetailsView context (onFileChosen is set)
    // If not, open audio editor instead
    if (onFileChosen)
    {
        onFileChosen(file);
    }
    else if (openAudioEditorCallback)
    {
        openAudioEditorCallback(file);
    }
}

void SampleBrowserComponent::handlePreview(Item& item)
{
    const juce::File file(item.id);
    if (!file.existsAsFile())
        return;

    if (onPreview)
        onPreview(file);
}

void SampleBrowserComponent::updatePathLabel(const juce::File& file)
{
    if (browseMode_ == BrowseMode::Categories)
    {
        currentPathLabel_ = categoryBreadcrumb.isNotEmpty()
                                ? categoryBreadcrumb : juce::String("Categories");
        repaint();
        return;
    }
    juce::String relativePath;

    if (file.exists())
    {
        if (file.isDirectory())
            relativePath = file.getRelativePathFrom(rootDirectory);
        else
            relativePath = file.getParentDirectory().getRelativePathFrom(rootDirectory);
    }

    relativePath = relativePath.replaceCharacter('\\', '/');
    relativePath = relativePath.trimCharactersAtStart("./");
    relativePath = relativePath.trimCharactersAtEnd("/");

    juce::String base = pathDisplayName;
    if (base.isEmpty())
        base = directoryDisplayName(rootDirectory);

    juce::String text = "./";
    if (base.isNotEmpty())
        text += base;

    if (relativePath.isNotEmpty() && relativePath != ".")
    {
        if (!text.endsWith("/"))
            text += "/";
        text += relativePath;
    }

    if (!text.endsWith("/"))
        text += "/";

    currentPathLabel_ = text;
    repaint();
}

void SampleBrowserComponent::requestOptionsRefresh()
{
    repaint();
}

void SampleBrowserComponent::activateCurrentSelection()
{
    if (!hasSelection)
        return;

    if (selectionIsFolder)
    {
        list.expandSelection();
        return;
    }

    // If we have a file selected and no onFileChosen callback (not in PadDetailsView),
    // use the audio editor callback
    if (!currentSelection.isDirectory() && currentSelection.existsAsFile())
    {
        if (onFileChosen)
        {
            onFileChosen(currentSelection);
        }
        else if (openAudioEditorCallback)
        {
            openAudioEditorCallback(currentSelection);
        }
    }
    else
    {
        list.invokeSelection();
    }
}

juce::String SampleBrowserComponent::formatFileSize(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};

    const auto size = static_cast<double>(file.getSize());

    if (size >= 1024.0 * 1024.0 * 1024.0)
        return juce::String(size / (1024.0 * 1024.0 * 1024.0), 2) + " GB";

    if (size >= 1024.0 * 1024.0)
        return juce::String(size / (1024.0 * 1024.0), 2) + " MB";

    if (size >= 1024.0)
        return juce::String(size / 1024.0, 1) + " KB";

    return juce::String(static_cast<int>(size)) + " B";
}
