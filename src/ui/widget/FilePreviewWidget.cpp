// src/ui/widget/FilePreviewWidget.cpp
#include "FilePreviewWidget.h"

#include "../../engine/AudioEngine.h"
#include "../theme/UiTheme.h"
#include "FileDialogStateKeys.h"
#include "../UiRefresh.h"

FilePreviewWidget::FilePreviewWidget() = default;
FilePreviewWidget::~FilePreviewWidget() = default;

juce::String FilePreviewWidget::getTitle() const
{
    return preview_.isValid ? preview_.projectName : juce::String("Preview");
}

void FilePreviewWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    state_ = engine().getSettingsState()
        .getOrCreateChildWithName(FileDialogStateKeys::kRoot, nullptr);
    state_.addListener(this);
    preview_ = {};

    // Seed preview from whatever selectedFile is already in the tree. Browser
    // publishes the initial selection in its own onActivated, which runs before
    // ours — so the ValueTree::Listener never sees that first write.
    const auto initial = state_.getProperty(FileDialogStateKeys::kSelectedFile).toString();
    if (initial.isNotEmpty())
    {
        juce::File f(initial);
        if (f.existsAsFile())
            loadPreviewAsync(f);
    }
}

void FilePreviewWidget::onDeactivated()
{
    if (state_.isValid()) state_.removeListener(this);
}

std::vector<std::string> FilePreviewWidget::requiredResources(int page)
{
    if (page != 0) return {};
    // Claim d7 + d8 (slot 3 + 4 on the right panel) for Rename / Delete.
    return { "d7", "d8" };
}

std::vector<Option> FilePreviewWidget::getOptions(int page)
{
    if (page != 0) return {};

    Option rename;
    rename.id = "file.rename";
    rename.label = "Rename";
    rename.state = preview_.isValid ? OptionState::Enabled : OptionState::Disabled;
    rename.onInvoke = [this]() {
        if (!preview_.isValid || !state_.isValid()) return;
        const auto path = state_.getProperty(FileDialogStateKeys::kSelectedFile).toString();
        juce::File f(path);
        if (f.existsAsFile() && onRename_) onRename_(f);
    };

    Option del;
    del.id = "file.delete";
    del.label = "Delete";
    del.state = preview_.isValid ? OptionState::Enabled : OptionState::Disabled;
    del.onInvoke = [this]() {
        if (!preview_.isValid || !state_.isValid()) return;
        const auto path = state_.getProperty(FileDialogStateKeys::kSelectedFile).toString();
        juce::File f(path);
        if (f.existsAsFile() && onDelete_) onDelete_(f);
    };

    // Layout: d5 empty · d6 empty · d7 Rename · d8 Delete
    return { {}, {}, rename, del };
}

void FilePreviewWidget::applyPreview(PreviewData data)
{
    preview_ = std::move(data);
    repaint();
}

FilePreviewWidget::PreviewData FilePreviewWidget::parseMpiFile(const juce::File& file)
{
    PreviewData data;
    if (!file.existsAsFile()) return data;

    const auto xml = file.loadFileAsString();
    auto tree = juce::ValueTree::fromXml(xml);
    if (!tree.isValid() || !tree.hasType("maschinepi_project"))
        return data;

    data.projectName   = file.getFileNameWithoutExtension();
    data.modifiedTime  = file.getLastModificationTime();
    data.sizeBytes     = file.getSize();

    const auto state = tree.getChildWithName("maschinepi_state");
    if (state.isValid())
    {
        if (auto transport = state.getChildWithName("transport"); transport.isValid())
            data.tempoBpm = static_cast<double>(transport.getProperty("tempoBpm", 0.0));

        if (auto pads = state.getChildWithName("pads"); pads.isValid())
        {
            for (int i = 0; i < pads.getNumChildren(); ++i)
            {
                const auto pad = pads.getChild(i);
                const int index = static_cast<int>(pad.getProperty("index", -1));
                const juce::String sampleFile = pad.getProperty("sampleFile", "").toString();
                const juce::String instrName  = pad.getProperty("instrumentName", "").toString();
                if (index >= 0 && index < 16 && (sampleFile.isNotEmpty() || instrName.isNotEmpty()))
                {
                    data.padLoadMap[static_cast<size_t>(index)] = true;
                    ++data.padsLoaded;
                    if (instrName.isNotEmpty())
                        ++data.instrumentsLoaded;
                }
            }
        }

        if (auto groups = state.getChildWithName("groups"); groups.isValid())
        {
            for (int i = 0; i < groups.getNumChildren(); ++i)
            {
                const auto group = groups.getChild(i);
                if (static_cast<bool>(group.getProperty("hasData", false)))
                    ++data.groupsUsed;
            }
        }
    }

    data.isValid = true;
    return data;
}

void FilePreviewWidget::loadPreviewAsync(const juce::File& file)
{
    // relaxed — epoch is a pure staleness counter; all reads happen on the
    // message thread so no cross-thread publication ordering needed.
    const int epoch = loadEpoch_.fetch_add(1, std::memory_order_relaxed) + 1;
    juce::Component::SafePointer<FilePreviewWidget> safe(this);

    juce::Thread::launch([safe, file, epoch]() {
        auto data = parseMpiFile(file);
        juce::MessageManager::callAsync([safe, data, epoch]() {
            if (auto* self = safe.getComponent())
                if (self->loadEpoch_.load(std::memory_order_relaxed) == epoch)
                    self->applyPreview(data);
        });
    });
}

void FilePreviewWidget::valueTreePropertyChanged(juce::ValueTree& tree,
                                                 const juce::Identifier& property)
{
    if (property != FileDialogStateKeys::kSelectedFile) return;
    const juce::String path = tree.getProperty(property).toString();
    if (path.isEmpty())
    {
        preview_ = {};
        repaint();
        return;
    }
    loadPreviewAsync(juce::File(path));
}

void FilePreviewWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void FilePreviewWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (!preview_.isValid)
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawFittedText("Select a project to preview",
                         bounds.reduced(8),
                         juce::Justification::centred, 2);
        return;
    }

    auto area = bounds.reduced(UiTheme::kPadding);

    // Subtitle — modified time + size
    {
        auto header = area.removeFromTop(14);
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const auto timeStr = preview_.modifiedTime.formatted("%Y-%m-%d");
        const auto sizeKb = juce::String(preview_.sizeBytes / 1024) + " KB";
        g.drawFittedText("Modified " + timeStr + "  \xc2\xb7  " + sizeKb,
                         header, juce::Justification::centredLeft, 1);
    }

    area.removeFromTop(4);

    // Stats line
    {
        auto statsRow = area.removeFromTop(16);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        auto statsText = "BPM: " + juce::String(preview_.tempoBpm, 1)
                       + "   Pads: " + juce::String(preview_.padsLoaded) + "/16"
                       + "   Instr: " + juce::String(preview_.instrumentsLoaded)
                       + "   Groups: " + juce::String(preview_.groupsUsed);
        g.drawFittedText(statsText, statsRow, juce::Justification::centredLeft, 1);
    }

    area.removeFromTop(10);

    // 4x4 pad-load mini grid
    const int gridSize = juce::jmin(area.getWidth(), area.getHeight()) - 8;
    auto gridArea = area.removeFromTop(gridSize).withWidth(gridSize);
    const float cell = static_cast<float>(gridSize) / 4.0f;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const int index = (3 - row) * 4 + col;
            juce::Rectangle<float> tile(
                static_cast<float>(gridArea.getX()) + col * cell,
                static_cast<float>(gridArea.getY()) + row * cell,
                cell, cell);
            tile = tile.reduced(cell * 0.12f);
            g.setColour(preview_.padLoadMap[static_cast<size_t>(index)]
                            ? UiTheme::kPadWithSample
                            : UiTheme::kPadEmpty);
            g.fillRoundedRectangle(tile, 2.0f);
        }
    }
}
