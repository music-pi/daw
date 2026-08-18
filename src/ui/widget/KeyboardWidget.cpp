#include "KeyboardWidget.h"

#include "../../control/HardwareConstants.h"
#include "../../engine/MidiConstants.h"
#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"

namespace
{
juce::String noteName(int pitch)
{
    static const char* kNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int octave = (pitch / 12) - 1;
    const int name = ((pitch % 12) + 12) % 12;
    return juce::String(kNames[name]) + juce::String(octave);
}

bool isC(int pitch) { return (pitch % 12 + 12) % 12 == 0; }
bool isAccidental(int pitch)
{
    const int p = (pitch % 12 + 12) % 12;
    return p == 1 || p == 3 || p == 6 || p == 8 || p == 10;
}

uint8_t velocityColor(int velocity)
{
    constexpr int kGreenTierCount = 4;
    const int clamped = juce::jlimit(
        midi::kVelocityLiveMin, midi::kVelocityMax, velocity);
    const int tier = juce::jlimit(0, kGreenTierCount - 1,
        (clamped * kGreenTierCount) / (midi::kVelocityMax + 1));
    return static_cast<uint8_t>(HardwareConstants::kColorGreenLowest + tier);
}
}

void KeyboardWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    publishPadLeds();
    repaint();
}

void KeyboardWidget::onDeactivated()
{
    // Callbacks intentionally retained: UiHost owns the state they capture
    // and reuses this widget across stash/restore cycles (plugin browser).
}

std::vector<std::string> KeyboardWidget::requiredResources(int page)
{
    if (page != 0) return {};
    std::vector<std::string> r;
    r.reserve(16 + 4);
    for (int i = 1; i <= 16; ++i)
        r.push_back("p" + std::to_string(i));
    for (int i = 5; i <= 8; ++i)
        r.push_back("d" + std::to_string(i));
    return r;
}

std::vector<Option> KeyboardWidget::getOptions(int page)
{
    if (page != 0) return {};

    Option nav;
    nav.id = "kb.nav";
    nav.label = onEmptySlot_
        ? juce::String("(empty)")
        : (activeName_.isNotEmpty() ? activeName_ : juce::String("(unnamed)"));
    nav.span = 2;
    nav.isOperationNavigator = true;
    nav.state = OptionState::Enabled;
    nav.onOperationNavigatorPart = [this](bool isLeftPart) {
        if (isLeftPart) { if (onPrev_) onPrev_(); }
        else            { if (onNext_) onNext_(); }
    };

    Option add;
    if (onEmptySlot_) add.state = OptionState::Empty;
    else
    {
        add.id = "kb.add";
        add.label = "Add +";
        add.state = OptionState::Enabled;
        add.onInvoke = [this]() { if (onAdd_) onAdd_(); };
    }

    Option action;
    if (onEmptySlot_)
    {
        action.id = "kb.load";
        action.label = "Load Instr";
        action.state = OptionState::Enabled;
        action.onInvoke = [this]() { if (onLoad_) onLoad_(); };
    }
    else
    {
        action.id = "kb.delete";
        action.label = "Delete";
        action.state = OptionState::Enabled;
        action.onInvoke = [this]() { if (onDelete_) onDelete_(); };
    }
    return { nav, add, action };
}

void KeyboardWidget::setBasePitch(int pitch)
{
    pitch = juce::jlimit(midi::kNoteMin, midi::kNoteMax - 15, pitch);
    if (pitch == basePitch_) return;
    basePitch_ = pitch;
    if (hasHardware())
        publishPadLeds();
    repaint();
}

void KeyboardWidget::setSlotInfo(int activeSlot, int numSlots, bool onEmptySlot,
                                 juce::String activeName)
{
    activeSlot_ = activeSlot;
    numSlots_ = numSlots;
    onEmptySlot_ = onEmptySlot;
    activeName_ = std::move(activeName);
    repaint();
}

void KeyboardWidget::setVelocityMode(bool fixedVelocity, bool sixteenVelocities)
{
    if (fixedVelocity_ == fixedVelocity
        && sixteenVelocities_ == sixteenVelocities)
        return;
    fixedVelocity_ = fixedVelocity;
    sixteenVelocities_ = sixteenVelocities;
    if (hasHardware())
        publishPadLeds();
    repaint();
}

std::unordered_map<int, uint8_t> KeyboardWidget::getShiftPadOverlay(int /*page*/)
{
    // Keyboard-mode shift+pad actions (bound in UiHost): base-pitch shift on
    // pads 13..16. Pad 13 = octave −, 14 = octave +, 15 = semitone −, 16 = +.
    const uint8_t tool = HardwareConstants::kColorGoldMedium;
    return { { 12, tool }, { 13, tool }, { 14, tool }, { 15, tool } };
}

void KeyboardWidget::refreshPadLeds()
{
    if (hasHardware()) publishPadLeds();
}

void KeyboardWidget::publishPadLeds()
{
    const auto owner = describe().id.toStdString();
    for (int i = 0; i < HardwareConstants::kPadCount; ++i)
    {
        uint8_t color = HardwareConstants::kColorOff;
        const int pitch = basePitch_ + i;
        if (isC(pitch))                 color = HardwareConstants::kColorWhite;
        else if (! isAccidental(pitch)) color = HardwareConstants::kColorWhiteDim;
        hw().setLed("p" + std::to_string(i + 1), color, owner);
    }
}

void KeyboardWidget::setPadFlashed(int padIndex, bool flashed, int midiVelocity)
{
    if (padIndex < 0 || padIndex >= 16 || ! hasHardware()) return;

    const auto owner = describe().id.toStdString();
    const auto padName = "p" + std::to_string(padIndex + 1);

    if (flashed)
    {
        hw().setLed(padName, velocityColor(midiVelocity), owner);
    }
    else
    {
        uint8_t color = HardwareConstants::kColorOff;
        // Restore the layout colour for this pad's current pitch.
        const int pitch = basePitch_ + padIndex;
        if (isC(pitch))                 color = HardwareConstants::kColorWhite;
        else if (! isAccidental(pitch)) color = HardwareConstants::kColorWhiteDim;
        hw().setLed(padName, color, owner);
    }
}

void KeyboardWidget::paint(juce::Graphics& g)
{
    paintPage(g, 0, getLocalBounds());
}

void KeyboardWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    g.fillAll(UiTheme::kBackgroundDark);


    if (page != 0) return;

    // Header: slot X / N + active instrument name, or "(empty slot)"
    auto header = bounds.removeFromTop(28);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));

    juce::String label;
    if (numSlots_ == 0)
        label = "Slot 1 / 1  (empty)";
    else if (onEmptySlot_)
        label = "Slot " + juce::String(activeSlot_ + 1) + " / "
              + juce::String(numSlots_ + 1) + "  (empty)";
    else
        label = "Slot " + juce::String(activeSlot_ + 1) + " / "
              + juce::String(numSlots_) + "  "
              + (activeName_.isNotEmpty() ? activeName_ : juce::String("(unnamed)"));

    if (sixteenVelocities_)
        label += "  16-level velocity curve";
    else if (fixedVelocity_)
        label += "  Fixed " + juce::String(midi::kDefaultFixedVelocity);

    g.drawFittedText(label, header.reduced(8, 0),
                     juce::Justification::centredLeft, 1);

    // 4x4 pad grid — bottom-left is pad 1 (lowest pitch).
    const int cols = 4;
    const int rows = 4;
    const int gap = 4;
    const int cellW = (bounds.getWidth() - gap * (cols + 1)) / cols;
    const int cellH = (bounds.getHeight() - gap * (rows + 1)) / rows;

    for (int i = 0; i < HardwareConstants::kPadCount; ++i)
    {
        const int col = i % cols;
        const int row = rows - 1 - (i / cols);
        auto cell = juce::Rectangle<int>(
            bounds.getX() + gap + col * (cellW + gap),
            bounds.getY() + gap + row * (cellH + gap),
            cellW, cellH);

        const int pitch = basePitch_ + i;

        if (isC(pitch))
        {
            g.setColour(UiTheme::kTitlebarAccent);
            g.fillRoundedRectangle(cell.toFloat(), 3.0f);
            g.setColour(juce::Colours::black);
        }
        else if (isAccidental(pitch))
        {
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            g.drawRoundedRectangle(cell.toFloat(), 3.0f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.6f));
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawRoundedRectangle(cell.toFloat(), 3.0f, 1.0f);
            g.setColour(juce::Colours::white);
        }

        g.drawFittedText(noteName(pitch), cell, juce::Justification::centred, 1);
    }
}
