// src/ui/widget/FilePreviewWidget.h
#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Widget.h"

class FilePreviewWidget : public Widget, private juce::ValueTree::Listener
{
public:
    struct PreviewData
    {
        juce::String projectName;
        juce::Time modifiedTime;
        juce::int64 sizeBytes = 0;
        double tempoBpm = 0;
        int padsLoaded = 0;
        int instrumentsLoaded = 0;
        int groupsUsed = 0;
        std::array<bool, 16> padLoadMap { false };
        bool isValid = false;
    };

    FilePreviewWidget();
    ~FilePreviewWidget() override;

    WidgetDescriptor describe() const override
    {
        return { "file_preview", 1, false, DisplayConstraint::RightOnly };
    }

    juce::String getTitle() const override;

    void onActivated(int offset) override;
    void onDeactivated() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override { return {}; }

    /** UiHost wires these to the rename/delete flows. The preview fires the
        callback with whichever file is currently selected. */
    void setOnRenameRequested(std::function<void(const juce::File&)> cb) { onRename_ = std::move(cb); }
    void setOnDeleteRequested(std::function<void(const juce::File&)> cb) { onDelete_ = std::move(cb); }

    void paint(juce::Graphics& g) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;

    /** Parse a .mpi project file into PreviewData. Public + static so it can be
        called off the message thread AND unit-tested independently. */
    static PreviewData parseMpiFile(const juce::File& file);

    // Testing accessors:
    bool isPreviewValid() const { return preview_.isValid; }
    const PreviewData& getPreviewData() const { return preview_; }

    /** Load a preview synchronously — for test environments where the message
        loop is not running. */
    void loadPreviewSynchronouslyForTesting(const juce::File& file)
    {
        applyPreview(parseMpiFile(file));
    }

private:
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;
    void applyPreview(PreviewData data);

    void loadPreviewAsync(const juce::File& file);
    std::atomic<int> loadEpoch_ { 0 };

    juce::ValueTree state_;
    PreviewData preview_;

    std::function<void(const juce::File&)> onRename_;
    std::function<void(const juce::File&)> onDelete_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilePreviewWidget)
};
