#include "SettingsDialog.h"

#include "../../engine/AudioEngine.h"
#include "../components/mixer/MixerUtils.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

SettingsDialog::SettingsDialog() = default;

WidgetDescriptor SettingsDialog::describe() const
{
    return { "settings", 1, true, DisplayConstraint::Any };
}

void SettingsDialog::onActivated(int panelOffset)
{
    panelOffset_ = panelOffset;

    auto settings = engine().getSettingsState();

    int storedBlock = settings.getProperty("audioBlockSize", 128);
    blockSizeIndex_ = 0;
    for (int i = 0; i < static_cast<int>(kBlockSizes.size()); ++i)
    {
        if (kBlockSizes[static_cast<size_t>(i)] == storedBlock)
        {
            blockSizeIndex_ = i;
            break;
        }
    }

    int storedRate = settings.getProperty("refreshRate", 60);
    for (int i = 0; i < static_cast<int>(kRefreshRates.size()); ++i)
    {
        if (kRefreshRates[static_cast<size_t>(i)] == storedRate)
        {
            refreshRateIndex_ = i;
            break;
        }
    }

    int storedMeterSkew = settings.getProperty("meterSkewCentreDb", -12);
    for (int i = 0; i < static_cast<int>(kMeterSkewPresets.size()); ++i)
    {
        if (kMeterSkewPresets[static_cast<size_t>(i)] == storedMeterSkew)
        {
            meterSkewIndex_ = i;
            break;
        }
    }
}

void SettingsDialog::onDeactivated()
{
}

std::vector<std::string> SettingsDialog::requiredResources(int /*page*/)
{
    // Claim all knobs (no option buttons needed — no tabs)
    return { "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8" };
}

std::vector<Option> SettingsDialog::getOptions(int /*page*/)
{
    return {}; // no option bar
}

std::vector<Knob> SettingsDialog::getKnobs(int /*page*/)
{
    return {}; // knob bar painted manually
}

void SettingsDialog::handleKnob(int rawIndex, int16_t delta, uint16_t /*absolute*/, bool /*shift*/)
{
    if (rawIndex < 0 || rawIndex >= static_cast<int>(knobAccumulator_.size()))
        return;

    knobAccumulator_[static_cast<size_t>(rawIndex)] += delta;

    // Meter zoom knob has a higher threshold so switching presets needs a
    // deliberate rotation, matching the "low sensitivity" feel.
    const int threshold = (rawIndex == 4) ? kMeterSkewKnobThreshold : kKnobThreshold;

    if (std::abs(knobAccumulator_[static_cast<size_t>(rawIndex)]) < threshold)
        return;

    int direction = knobAccumulator_[static_cast<size_t>(rawIndex)] > 0 ? 1 : -1;
    knobAccumulator_[static_cast<size_t>(rawIndex)] = 0;

    if (rawIndex == 0)
    {
        int newIndex = juce::jlimit(0, static_cast<int>(kBlockSizes.size()) - 1, blockSizeIndex_ + direction);
        if (newIndex != blockSizeIndex_)
        {
            blockSizeIndex_ = newIndex;
            repaint();
        }
    }
    else if (rawIndex == 1)
    {
        int newIndex = juce::jlimit(0, static_cast<int>(kRefreshRates.size()) - 1, refreshRateIndex_ + direction);
        if (newIndex != refreshRateIndex_)
        {
            refreshRateIndex_ = newIndex;
            repaint();
        }
    }
    else if (rawIndex == 4)
    {
        int newIndex = juce::jlimit(0, static_cast<int>(kMeterSkewPresets.size()) - 1,
                                    meterSkewIndex_ + direction);
        if (newIndex != meterSkewIndex_)
        {
            meterSkewIndex_ = newIndex;
            // Apply immediately so the user sees meters respond on any visible
            // mixer. Persistence still waits for knob-release below.
            MixerUtils::setMeterCentreDb(kMeterSkewPresets[static_cast<size_t>(meterSkewIndex_)]);
            repaint();
        }
    }
}

void SettingsDialog::handleButton(const controller_events::ButtonEvent& e)
{
    // Persist settings when knob is released (knobTouch1-8)
    if (e.name.substr(0, 9) == "knobTouch" && !e.pressed)
    {
        applyBlockSize();
        applyRefreshRate();
        applyMeterSkew();
    }
}

void SettingsDialog::paint(juce::Graphics& g)
{
    paintPage(g, 0, getLocalBounds());
}

void SettingsDialog::paintPage(juce::Graphics& g, int /*page*/, juce::Rectangle<int> bounds)
{
    g.setColour(UiTheme::kDialogPanel);
    g.fillRect(bounds);

    auto leftPanel = bounds.removeFromLeft(UiTheme::kPanelWidth);
    auto rightPanel = bounds;

    paintGeneralPanel(g, leftPanel);
    paintUserPanel(g, rightPanel);
}

void SettingsDialog::paintGeneralPanel(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Title bar
    auto titleBar = bounds.removeFromTop(UiTheme::kTitlebarHeight);
    g.setColour(UiTheme::kDialogBackground);
    g.fillRect(titleBar);
    g.setColour(UiTheme::kAccentPurple);
    g.fillRect(titleBar.removeFromLeft(UiTheme::kAccentWidth));

    auto titleInner = titleBar.reduced(8, 0);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody, juce::Font::bold)));
    g.drawText("Settings > General", titleInner, juce::Justification::centredLeft);

    g.setColour(juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kOptionLabel)));
    g.drawText("Version: 0.1.0", titleInner, juce::Justification::centredRight);

    // Knob bar at bottom
    auto knobBar = bounds.removeFromBottom(UiTheme::kKnobBarWithTitleHeight);
    g.setColour(UiTheme::kDialogBackground);
    g.fillRect(knobBar);
    g.setColour(UiTheme::kDialogDivider);
    g.drawLine(static_cast<float>(knobBar.getX()), static_cast<float>(knobBar.getY()),
               static_cast<float>(knobBar.getRight()), static_cast<float>(knobBar.getY()), 1.0f);

    // "Audio & Timing" section label
    auto sectionLabel = knobBar.removeFromTop(12);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
    g.drawText("Audio & Timing", sectionLabel.reduced(8, 0), juce::Justification::centredLeft);

    int blockSize = kBlockSizes[static_cast<size_t>(blockSizeIndex_)];
    double latencyMs = static_cast<double>(blockSize) / 48000.0 * 1000.0;
    juce::String bufferValue = juce::String(blockSize) + " (" + juce::String(latencyMs, 1) + "ms)";

    int refreshRate = kRefreshRates[static_cast<size_t>(refreshRateIndex_)];
    juce::String refreshValue = juce::String(refreshRate) + "hz";

    // Each knob slot is 120px, matching the option-bar grid. With only 2 knobs
    // the remaining 240px stays empty so K1/K2 align with D1/D2 on hardware.
    constexpr int kSlotWidth = 120;
    auto leftSlot = knobBar.removeFromLeft(kSlotWidth);
    auto rightSlot = knobBar.removeFromLeft(kSlotWidth);

    // Divider between slots
    g.setColour(UiTheme::kDialogDivider);
    g.drawLine(static_cast<float>(rightSlot.getX()), static_cast<float>(rightSlot.getY()),
               static_cast<float>(rightSlot.getX()), static_cast<float>(rightSlot.getBottom()), 1.0f);

    paintKnobSlot(g, leftSlot, "Buffer", bufferValue);
    paintKnobSlot(g, rightSlot, "Display Refresh Rate", refreshValue);

    // Content area
    auto content = bounds.reduced(12, 8);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSnackbar, juce::Font::bold)));
    g.drawText("System Info", content.removeFromTop(20), juce::Justification::centredLeft);

    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kOptionLabel)));

    auto drawBullet = [&](const juce::String& text) {
        auto line = content.removeFromTop(16);
        g.drawText(juce::String::charToString(0x2022) + " " + text,
                   line.withTrimmedLeft(8), juce::Justification::centredLeft);
    };

    drawBullet("Battery (mock)");
    drawBullet("Network: No connection");
    drawBullet("Free disk space");
    drawBullet("CPU/Memory consumption");

    content.removeFromTop(12);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSnackbar, juce::Font::bold)));
    g.drawText("Dev Helper", content.removeFromTop(20), juce::Justification::centredLeft);

    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kOptionLabel)));
    g.drawText("To reboot system hold down shift and press STOP 7 times",
               content.removeFromTop(16), juce::Justification::centredLeft);
}

void SettingsDialog::paintUserPanel(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Title bar
    auto titleBar = bounds.removeFromTop(UiTheme::kTitlebarHeight);
    g.setColour(UiTheme::kDialogBackground);
    g.fillRect(titleBar);
    g.setColour(UiTheme::kAccentOrange);
    g.fillRect(titleBar.removeFromLeft(UiTheme::kAccentWidth));

    auto titleInner = titleBar.reduced(8, 0);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody, juce::Font::bold)));
    g.drawText("Settings > User", titleInner, juce::Justification::centredLeft);

    // Knob bar at bottom
    auto knobBar = bounds.removeFromBottom(UiTheme::kKnobBarWithTitleHeight);
    g.setColour(UiTheme::kDialogBackground);
    g.fillRect(knobBar);
    g.setColour(UiTheme::kDialogDivider);
    g.drawLine(static_cast<float>(knobBar.getX()), static_cast<float>(knobBar.getY()),
               static_cast<float>(knobBar.getRight()), static_cast<float>(knobBar.getY()), 1.0f);

    auto sectionLabel = knobBar.removeFromTop(12);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
    g.drawText("User Preferences", sectionLabel.reduced(8, 0), juce::Justification::centredLeft);

    constexpr int kSlotWidth = 120;
    auto leftSlot = knobBar.removeFromLeft(kSlotWidth);
    auto rightSlot = knobBar.removeFromLeft(kSlotWidth);

    g.setColour(UiTheme::kDialogDivider);
    g.drawLine(static_cast<float>(rightSlot.getX()), static_cast<float>(rightSlot.getY()),
               static_cast<float>(rightSlot.getX()), static_cast<float>(rightSlot.getBottom()), 1.0f);

    int centreDb = kMeterSkewPresets[static_cast<size_t>(meterSkewIndex_)];
    juce::String meterSkewValue = juce::String(centreDb) + " dB";
    paintKnobSlot(g, leftSlot, "Meter Zoom", meterSkewValue);
    paintKnobSlot(g, rightSlot, "Knob Sensitivity (Step size)", "4");

    // Content area
    auto content = bounds.reduced(12, 8);

    juce::String username = juce::SystemStats::getLogonName();

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSnackbar, juce::Font::bold)));
    g.drawText("Linux Username: " + username, content.removeFromTop(20), juce::Justification::centredLeft);

    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kOptionLabel)));

    auto line1 = content.removeFromTop(16);
    g.drawText(juce::String::charToString(0x2022) + " Paths",
               line1.withTrimmedLeft(8), juce::Justification::centredLeft);

    auto line2 = content.removeFromTop(16);
    g.drawText(juce::String::charToString(0x2022) + " Samples: ~/samples",
               line2.withTrimmedLeft(24), juce::Justification::centredLeft);

    auto line3 = content.removeFromTop(16);
    g.drawText(juce::String::charToString(0x2022) + " Projects: ~/projects",
               line3.withTrimmedLeft(24), juce::Justification::centredLeft);
}

void SettingsDialog::paintKnobSlot(juce::Graphics& g, juce::Rectangle<int> bounds,
                                    const juce::String& label, const juce::String& value)
{
    auto inner = bounds.reduced(8, 2);

    g.setColour(juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
    g.drawText(label, inner.removeFromTop(12), juce::Justification::centredLeft);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody, juce::Font::bold)));
    g.drawText(value, inner, juce::Justification::centredLeft);
}

void SettingsDialog::setDismissCallback(std::function<void()> callback)
{
    onDismiss_ = std::move(callback);
}

void SettingsDialog::applyBlockSize()
{
    int blockSize = kBlockSizes[static_cast<size_t>(blockSizeIndex_)];
    auto settings = engine().getSettingsState();
    settings.setProperty("audioBlockSize", blockSize, nullptr);
    engine().setBufferSize(blockSize);
}

void SettingsDialog::applyRefreshRate()
{
    int rate = kRefreshRates[static_cast<size_t>(refreshRateIndex_)];
    auto settings = engine().getSettingsState();
    settings.setProperty("refreshRate", rate, nullptr);
}

void SettingsDialog::applyMeterSkew()
{
    int centreDb = kMeterSkewPresets[static_cast<size_t>(meterSkewIndex_)];
    auto settings = engine().getSettingsState();
    settings.setProperty("meterSkewCentreDb", centreDb, nullptr);
    MixerUtils::setMeterCentreDb(centreDb);
}
