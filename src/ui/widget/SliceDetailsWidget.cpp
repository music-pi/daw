#include "SliceDetailsWidget.h"

#include <cmath>

#include "AudioEditorWidget.h"
#include "../theme/UiTheme.h"

SliceDetailsWidget::SliceDetailsWidget(AudioEditorWidget& editor)
    : editor_(&editor)
{
}

WidgetDescriptor SliceDetailsWidget::describe() const
{
    return { "slice_details", 1, false, DisplayConstraint::RightOnly };
}

juce::String SliceDetailsWidget::getTitleSubtitle() const
{
    if (editor_ == nullptr)
        return {};

    const auto state = editor_->getSliceDetailsState();
    if (state.selectedSlice < 0 || state.ranges.empty())
        return "No slice selected";
    return "Slice " + juce::String(state.selectedSlice + 1)
        + " / " + juce::String(static_cast<int>(state.ranges.size()));
}

juce::Colour SliceDetailsWidget::getAccentColour() const
{
    return editor_ != nullptr
        ? editor_->getAccentColour()
        : UiTheme::kTitlebarAccent;
}

void SliceDetailsWidget::onActivated(int panelOffset)
{
    panelOffset_ = panelOffset;
    lastSignature_ = stateSignature();
}

void SliceDetailsWidget::onDeactivated()
{
}

std::vector<std::string> SliceDetailsWidget::requiredResources(int page)
{
    if (page != 0)
        return {};
    return { "d8" };
}

std::vector<Option> SliceDetailsWidget::getOptions(int page)
{
    std::vector<Option> options(4);
    for (auto& option : options)
        option.state = OptionState::Empty;

    if (page != 0 || editor_ == nullptr)
        return options;

    const auto state = editor_->getSliceDetailsState();
    Option overlap;
    overlap.id = "slice.details.overlapping";
    overlap.label = "Overlapping";
    overlap.state = !state.active
        ? OptionState::Disabled
        : (state.overlapping ? OptionState::Active : OptionState::Enabled);

    juce::Component::SafePointer<AudioEditorWidget> safeEditor(editor_);
    overlap.onToggle = [safeEditor](bool active)
    {
        if (safeEditor != nullptr)
            safeEditor->setManualSliceOverlapping(active);
    };
    options[3] = std::move(overlap);
    return options;
}

std::vector<Knob> SliceDetailsWidget::getKnobs(int)
{
    return {};
}

void SliceDetailsWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void SliceDetailsWidget::paintPage(juce::Graphics& g, int page,
                                   juce::Rectangle<int> bounds)
{
    if (page != 0)
        return;

    auto area = bounds.reduced(UiTheme::kPadding);
    if (editor_ == nullptr)
        return;

    const auto state = editor_->getSliceDetailsState();
    if (state.ranges.empty() || state.selectedSlice < 0
        || state.selectedSlice >= static_cast<int>(state.ranges.size()))
    {
        g.setColour(UiTheme::kTextSecondary);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        g.drawFittedText("Press Pad 1 to begin Manual slicing.",
                         area, juce::Justification::centred, 2);
        return;
    }

    const auto& selected =
        state.ranges[static_cast<size_t>(state.selectedSlice)];
    const double duration =
        juce::jmax(0.0, selected.endSeconds - selected.startSeconds);

    auto values = area.removeFromTop(56);
    g.setColour(UiTheme::kTextPrimary);
    g.setFont(juce::Font(juce::FontOptions(
        UiTheme::Fonts::kBody, juce::Font::bold)));
    g.drawText("START", values.removeFromLeft(values.getWidth() / 3),
               juce::Justification::centredLeft);
    g.drawText("END", values.removeFromLeft(values.getWidth() / 2),
               juce::Justification::centredLeft);
    g.drawText("LENGTH", values, juce::Justification::centredLeft);

    auto times = area.removeFromTop(28);
    g.setColour(UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
    const int third = times.getWidth() / 3;
    g.drawText(juce::String(selected.startSeconds, 3) + "s",
               times.removeFromLeft(third), juce::Justification::centredLeft);
    g.drawText(juce::String(selected.endSeconds, 3) + "s",
               times.removeFromLeft(third), juce::Justification::centredLeft);
    g.drawText(juce::String(duration, 3) + "s",
               times, juce::Justification::centredLeft);

    area.removeFromTop(12);
    auto timeline = area.removeFromTop(72);
    g.setColour(UiTheme::kOptionFillEmpty);
    g.fillRect(timeline);
    g.setColour(UiTheme::kOptionBorderEmpty);
    g.drawRect(timeline);

    double first = state.ranges.front().startSeconds;
    double last = state.ranges.front().endSeconds;
    for (const auto& range : state.ranges)
    {
        first = juce::jmin(first, range.startSeconds);
        last = juce::jmax(last, range.endSeconds);
    }
    const double span = juce::jmax(0.001, last - first);
    const int laneHeight = juce::jmax(
        3, timeline.getHeight() / static_cast<int>(state.ranges.size()));
    for (size_t i = 0; i < state.ranges.size(); ++i)
    {
        const auto& range = state.ranges[i];
        const float x0 = timeline.getX()
            + static_cast<float>((range.startSeconds - first) / span)
                * timeline.getWidth();
        const float x1 = timeline.getX()
            + static_cast<float>((range.endSeconds - first) / span)
                * timeline.getWidth();
        const int y = timeline.getY() + static_cast<int>(i) * laneHeight;
        g.setColour(static_cast<int>(i) == state.selectedSlice
                        ? getAccentColour()
                        : getAccentColour().withAlpha(0.28f));
        g.fillRect(juce::Rectangle<float>(
            x0, static_cast<float>(y),
            juce::jmax(1.0f, x1 - x0),
            static_cast<float>(juce::jmax(2, laneHeight - 1))));
    }

    area.removeFromTop(10);
    g.setColour(UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
    g.drawFittedText(
        state.overlapping
            ? "Overlapping ON · Start/End are independent"
            : "Contiguous · End is the next slice Start",
        area.removeFromTop(24), juce::Justification::centredLeft, 1);
    g.drawFittedText("Left/Right or Select + Pad to choose a slice",
                     area, juce::Justification::centredLeft, 1);
}

void SliceDetailsWidget::onUiHostTick()
{
    const auto signature = stateSignature();
    if (signature != lastSignature_)
    {
        lastSignature_ = signature;
        repaint();
    }
}

std::size_t SliceDetailsWidget::stateSignature() const
{
    if (editor_ == nullptr)
        return 0;

    const auto state = editor_->getSliceDetailsState();
    std::size_t signature = static_cast<std::size_t>(state.selectedSlice + 1);
    signature = signature * 31u + static_cast<std::size_t>(state.overlapping);
    signature = signature * 31u + state.ranges.size();
    for (const auto& range : state.ranges)
    {
        signature = signature * 31u
            + static_cast<std::size_t>(std::llround(range.startSeconds * 1000.0));
        signature = signature * 31u
            + static_cast<std::size_t>(std::llround(range.endSeconds * 1000.0));
    }
    return signature;
}
