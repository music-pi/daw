#include "GroupDetailsDialog.h"

#include "../../control/ControllerHost.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/GroupManager.h"
#include "../../engine/SamplerInstrument.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

GroupDetailsDialog::GroupDetailsDialog() = default;

WidgetDescriptor GroupDetailsDialog::describe() const
{
    return { "group_details", 1, true, DisplayConstraint::Any };
}

void GroupDetailsDialog::onActivated(int panelOffset)
{
    panelOffset_ = panelOffset;
    refreshState();
}

void GroupDetailsDialog::onDeactivated()
{
}

std::vector<std::string> GroupDetailsDialog::requiredResources(int /*page*/)
{
    // Dialog uses left-side options/knobs (d1-d4, k1-k4)
    return { "d1", "d2", "d3", "d4", "k1", "k2", "k3", "k4" };
}

std::vector<Option> GroupDetailsDialog::getOptions(int /*page*/)
{
    if (confirmingDelete_)
    {
        std::vector<Option> opts(4);

        opts[0] = Option{
            .id = "cancel",
            .label = "Cancel",
            .state = OptionState::Enabled,
            .onInvoke = [this]() {
                confirmingDelete_ = false;
                repaint();
            },
            .onToggle = nullptr,
            .onOperationNavigatorPart = nullptr
        };

        // Slots 1-2: empty placeholders
        opts[1] = Option{ .id = "empty2", .label = "", .state = OptionState::Empty, .onInvoke = nullptr, .onToggle = nullptr, .onOperationNavigatorPart = nullptr };
        opts[2] = Option{ .id = "empty3", .label = "", .state = OptionState::Empty, .onInvoke = nullptr, .onToggle = nullptr, .onOperationNavigatorPart = nullptr };

        opts[3] = Option{
            .id = "confirm",
            .label = "Confirm",
            .state = OptionState::Enabled,
            .onInvoke = [this]() {
                confirmingDelete_ = false;

                auto& gm = engine().getGroupManager();
                gm.clearGroup(groupIndex_);

                auto* ch = controllerHost();
                if (ch != nullptr)
                    ch->refreshGroupLeds();

                refreshState();
            },
            .onToggle = nullptr,
            .onOperationNavigatorPart = nullptr
        };

        return opts;
    }

    bool hasGroup = false;
    if (groupIndex_ >= 0)
    {
        auto& gm = engine().getGroupManager();
        hasGroup = gm.isGroupActive(groupIndex_) || gm.hasGroupData(groupIndex_);
    }

    std::vector<Option> opts;

    // Slot 0: Close
    opts.push_back(Option{
        .id = "close",
        .label = "Close",
        .state = OptionState::Enabled,
        .onInvoke = [this]() {
            if (onDismiss_)
                onDismiss_();
        },
        .onToggle = nullptr,
        .onOperationNavigatorPart = nullptr
    });

    if (hasGroup)
    {
        // Slot 1: Delete for existing groups
        opts.push_back(Option{
            .id = "delete",
            .label = "Delete",
            .state = OptionState::Enabled,
            .onInvoke = [this]() {
                confirmingDelete_ = true;
                repaint();
            },
            .onToggle = nullptr,
            .onOperationNavigatorPart = nullptr
        });
    }
    else
    {
        // Slot 1: Create for empty groups
        opts.push_back(Option{
            .id = "create",
            .label = "Create",
            .state = (groupIndex_ >= 0) ? OptionState::Enabled : OptionState::Disabled,
            .onInvoke = [this]() {
                if (groupIndex_ < 0)
                    return;

                auto& gm = engine().getGroupManager();
                gm.createGroup(groupIndex_);

                auto* ch = controllerHost();
                if (ch != nullptr)
                    ch->refreshGroupLeds();

                if (onDismiss_)
                    onDismiss_();
            },
            .onToggle = nullptr,
            .onOperationNavigatorPart = nullptr
        });
    }

    return opts;
}

std::vector<Knob> GroupDetailsDialog::getKnobs(int /*page*/)
{
    std::vector<Knob> knobSlots(4);

    Knob colorKnob;
    colorKnob.id = "groupColor";
    colorKnob.label = "Color";
    colorKnob.isEnabled = (groupIndex_ >= 0 && (groupHasData_ || groupIsActive_));
    colorKnob.continuousMode = true;
    colorKnob.sensitivity = 1.0;

    Knob::ListModel listModel;
    listModel.entries.reserve(HardwareConstants::kIndexedColorPairs.size());
    for (const auto& color : HardwareConstants::kIndexedColorPairs)
        listModel.entries.push_back(juce::String::fromUTF8(
            color.name.data(), static_cast<int>(color.name.size())));
    listModel.selectedIndex = juce::jmax(0, GroupManager::colorIndex(groupColor_));
    listModel.onChange = [this](int index)
    {
        if (groupIndex_ < 0)
            return;

        const auto newColor = GroupManager::colorAt(index);
        engine().getGroupManager().setGroupColor(groupIndex_, newColor);
        groupColor_ = newColor;

        auto* ch = controllerHost();
        if (ch != nullptr)
            ch->refreshGroupLeds();

        repaint();
    };

    colorKnob.model = std::move(listModel);
    knobSlots[0] = std::move(colorKnob);

    return knobSlots;
}

void GroupDetailsDialog::paintPage(juce::Graphics& g, int /*page*/, juce::Rectangle<int> bounds)
{
    // Background
    g.setColour(juce::Colours::black.withAlpha(0.9f));
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    auto content = bounds.reduced(20, 12);

    if (groupIndex_ < 0)
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        g.drawFittedText("No group selected.",
                         content, juce::Justification::centred, 1);
        return;
    }

    const auto groupColour = UiTheme::ledIndexToColour(groupColor_);

    // Title
    auto titleArea = content.removeFromTop(36).reduced(8, 0);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kHeading, juce::Font::bold)));

    if (confirmingDelete_)
        g.drawText("Delete Group " + juce::String(groupIndex_ + 1) + "?",
                   titleArea, juce::Justification::centredLeft);
    else
        g.drawText("Group " + juce::String(groupIndex_ + 1),
                   titleArea, juce::Justification::centredLeft);

    content = content.reduced(12, 4);

    // Color swatch
    auto swatchRow = content.removeFromTop(40);
    auto swatchRect = swatchRow.removeFromLeft(120).toFloat();
    g.setColour(groupColour);
    g.fillRoundedRectangle(swatchRect, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawRoundedRectangle(swatchRect, 4.0f, 1.0f);

    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
    swatchRow.removeFromLeft(12);
    g.drawText("Index: " + juce::String(static_cast<int>(groupColor_)),
               swatchRow, juce::Justification::centredLeft);

    content.removeFromTop(12);

    // Info lines
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));

    auto infoLine = [&](const juce::String& text)
    {
        g.drawText(text, content.removeFromTop(22), juce::Justification::centredLeft);
    };

    infoLine("Samples: " + juce::String(loadedSampleCount_) + " / 16");

    juce::String status = groupIsActive_ ? "Active" : (groupHasData_ ? "Saved" : "Empty");
    infoLine("Status: " + status);
}

void GroupDetailsDialog::paint(juce::Graphics& g)
{
    paintPage(g, 0, getLocalBounds());
}

void GroupDetailsDialog::handleOption(int localIndex)
{
    auto opts = getOptions(0);
    if (localIndex >= 0 && localIndex < static_cast<int>(opts.size()))
    {
        if (opts[localIndex].state != OptionState::Disabled && opts[localIndex].state != OptionState::Empty && opts[localIndex].onInvoke)
            opts[localIndex].onInvoke();
    }
}

void GroupDetailsDialog::setGroupIndex(int index)
{
    groupIndex_ = index;
    confirmingDelete_ = false;
    // refreshState() is deferred to onActivated() since engine may not be injected yet
}

void GroupDetailsDialog::setDismissCallback(std::function<void()> callback)
{
    onDismiss_ = std::move(callback);
}

void GroupDetailsDialog::refreshState()
{
    if (groupIndex_ < 0)
        return;

    auto& gm = engine().getGroupManager();

    groupColor_ = gm.getGroupColor(groupIndex_);
    groupHasData_ = gm.hasGroupData(groupIndex_);
    groupIsActive_ = gm.isGroupActive(groupIndex_);

    auto snapshots = engine().getSampler().getPadsSnapshot(
        SamplerInstrument::SnapshotContent::State);
    loadedSampleCount_ = 0;
    for (const auto& snap : snapshots)
    {
        if (snap.hasSample)
            ++loadedSampleCount_;
    }

    repaint();
}
