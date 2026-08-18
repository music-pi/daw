#include "T9Widget.h"
#include "../UiRefresh.h"

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../../input/InputManager.h"
#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"

// Logical pad index 0-15 -> display label + LED color.
namespace
{
struct TileSpec
{
    const char* glyph;      // letters mode label
    const char* symGlyph;   // symbols mode label (if different)
    const char* numGlyph;   // numbers mode label (digits for letter pads; utility labels otherwise)
    uint8_t     ledColor;   // default LED color; overridden for dynamic pads
    bool        isLetter;   // true if participates in cycle (pads 0-9 are letters)
};

// Order matches logical pad index 0..15 (also the ResourceIds::PadLeds index).
// MK3 physical pad layout is column-major from bottom-left:
//   Row 0 (top):    12 13 14 15
//   Row 1:           8  9 10 11
//   Row 2:           4  5  6  7
//   Row 3 (bottom):  0  1  2  3
// T9 layout places phone-keypad on cols 1-3 (matching top-to-bottom reading
// order: 1-2-3 / 4-5-6 / 7-8-9), functions on col 4, and sym/space/◀/▶ on
// the bottom row.
// Letter/number pads idle at the dim warm-white tier (~66 %) and the
// publishLeds flash path kicks them up to full bright when they're the
// active pad in the cycle.
// Colour scheme:
//   Letter keys   → light white (kColorWhiteDim) — the default "key" look
//   del (pad 15)  → bright yellow
//   num toggle    → bright blue (matches the Numbers mode palette)
//   sym toggle    → bright purple (matches the Symbols mode palette)
constexpr TileSpec kTiles[16] = {
    { "sym",   "sym",    "sym",   HardwareConstants::kColorPurpleBright, false }, // 0  sym toggle
    { "space", "space",  "0",     HardwareConstants::kColorWhiteDim,     true  }, // 1  phone 0 / space
    { "<",     "<",      "<",     HardwareConstants::kColorWhiteDim,     false }, // 2  cursor L
    { ">",     ">",      ">",     HardwareConstants::kColorWhiteDim,     false }, // 3  cursor R
    { "PQRS",  "/\\",    "7",     HardwareConstants::kColorWhiteDim,     true  }, // 4  phone 7
    { "TUV",   "+=",     "8",     HardwareConstants::kColorWhiteDim,     true  }, // 5  phone 8
    { "WXYZ",  "'\"",    "9",     HardwareConstants::kColorWhiteDim,     true  }, // 6  phone 9
    { "num",   "num",    "num",   HardwareConstants::kColorBlueBright,   false }, // 7  num toggle
    { "GHI",   "()",     "4",     HardwareConstants::kColorWhiteDim,     true  }, // 8  phone 4
    { "JKL",   "[]",     "5",     HardwareConstants::kColorWhiteDim,     true  }, // 9  phone 5
    { "MNO",   "{}",     "6",     HardwareConstants::kColorWhiteDim,     true  }, // 10 phone 6
    { "",      "",       "",      HardwareConstants::kColorOff,          false }, // 11 unused
    { ".,!?",  ".,!?;:", "1",     HardwareConstants::kColorWhiteDim,     true  }, // 12 phone 1
    { "ABC",   "#@",     "2",     HardwareConstants::kColorWhiteDim,     true  }, // 13 phone 2
    { "DEF",   "-_",     "3",     HardwareConstants::kColorWhiteDim,     true  }, // 14 phone 3
    { "del",   "del",    "del",   HardwareConstants::kColorYellowBright, false }, // 15 del
};

// Named pad indices (sym/num have per-mode colors).
constexpr int kPadSym = 0;
constexpr int kPadNum = 7;

constexpr double kFlashDurationMs = 220.0;
} // namespace

// ── Construction ──────────────────────────────────────────────────────────────

T9Widget::T9Widget()
{
    addAndMakeVisible(grid_);

    PadGridComponent::Style style;
    style.borderWidth  = 1.0f;
    style.cornerRadius = 3.0f;
    style.borderColour = juce::Colours::black.withAlpha(0.4f);
    style.gapPx        = 4;  // tile.reduced(2.0f) ≈ 4 px gap
    grid_.setStyle(style);
}

T9Widget::~T9Widget() = default;

// ── Identity ──────────────────────────────────────────────────────────────────

WidgetDescriptor T9Widget::describe() const
{
    return { "t9", 1, false, DisplayConstraint::Any };
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void T9Widget::setCallbacks(Callbacks cb)       { callbacks_ = std::move(cb); }
void T9Widget::setOnDeactivated(std::function<void()> cb) { onDeactivated_ = std::move(cb); }

// ── Display state setters ─────────────────────────────────────────────────────

void T9Widget::setPendingCycleHint(int padIndex, juce::juce_wchar previewChar)
{
    pendingPad_ = padIndex;
    pendingChar_ = previewChar;
    if (padIndex >= 0) startFlashTimer();
    else               stopFlashTimer();
    refreshGridState();
}

void T9Widget::setCandidates(std::vector<juce::String> items, int selectedIndex)
{
    candidates_ = std::move(items);
    selectedCandidate_ = selectedIndex;
    repaint();
}

void T9Widget::setCapsMode(T9StateMachine::Caps caps)
{
    caps_ = caps;
    refreshGridState();
}

void T9Widget::setMode(T9StateMachine::Mode mode)
{
    mode_ = mode;
    refreshGridState();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void T9Widget::onActivated(int offset)
{
    panelOffset_ = offset;
    refreshGridState();

    // Claim pad events with a View-priority InputManager handler so pad
    // presses route to T9's callback instead of triggering samples via the
    // global sampler handler UiHost registers at Global priority.
    if (auto* host = controllerHost())
    {
        if (auto* im = host->getInputManager())
        {
            padBinding_ = im->addPadHandler(
                InputManager::HandlerPriority::View,
                "",  // global context — always active while T9 is open
                [this](InputEvent& event) {
                    if (!event.metadata.contains("pressed")
                        || !static_cast<bool>(event.metadata["pressed"]))
                    {
                        event.consumed = true;
                        return;
                    }
                    int padIdx = -1;
                    if (event.metadata.contains("pad"))
                        padIdx = event.metadata["pad"];
                    if (callbacks_.onPadTap && padIdx >= 0)
                        callbacks_.onPadTap(padIdx);
                    event.consumed = true;
                });

            shiftBinding_ = im->addButtonHandler(
                InputManager::HandlerPriority::View,
                "",
                juce::String("shift"),
                [this](InputEvent& event) {
                    if (!event.metadata.contains("pressed")) return;
                    if (!static_cast<bool>(event.metadata["pressed"])) return;
                    if (callbacks_.onShiftPress) callbacks_.onShiftPress();
                });
        }
    }
}

void T9Widget::onDeactivated()
{
    stopFlashTimer();

    if (auto* host = controllerHost())
    {
        if (auto* im = host->getInputManager())
        {
            if (padBinding_   != kInvalidBinding) im->removeHandler(padBinding_);
            if (shiftBinding_ != kInvalidBinding) im->removeHandler(shiftBinding_);
        }
    }
    padBinding_   = kInvalidBinding;
    shiftBinding_ = kInvalidBinding;

    // Release all 16 pad LEDs explicitly
    const auto owner = describe().id.toStdString();
    for (int i = 0; i < 16; ++i)
        hw().setLed(ResourceIds::PadLeds[static_cast<size_t>(i)],
                    HardwareConstants::kColorOff, owner);
    if (onDeactivated_) onDeactivated_();
}

// ── Resources ─────────────────────────────────────────────────────────────────

std::vector<std::string> T9Widget::requiredResources(int page)
{
    if (page != 0) return {};
    std::vector<std::string> r;
    for (int i = 1; i <= 16; ++i) r.push_back("p" + std::to_string(i));
    // Slot 1 (Cancel) and slot 4 (OK) live on d1/d4 for Left-panel placement
    // or d5/d8 for Right-panel; k1/k5 drives the candidate picker.
    if (panelOffset_ == 0)
    {
        r.push_back("d1"); r.push_back("d4"); r.push_back("k1");
    }
    else
    {
        r.push_back("d5"); r.push_back("d8"); r.push_back("k5");
    }
    return r;
}

// ── Options & Knobs ───────────────────────────────────────────────────────────

std::vector<Option> T9Widget::getOptions(int page)
{
    if (page != 0) return {};

    Option cancel;
    cancel.id = "t9.cancel";
    cancel.label = "Cancel";
    cancel.state = OptionState::Enabled;
    cancel.onInvoke = [this]() { if (callbacks_.onOptionCancel) callbacks_.onOptionCancel(); };

    Option empty;
    empty.state = OptionState::Empty;

    Option ok;
    ok.id = "t9.ok";
    ok.label = "OK";
    ok.state = OptionState::Enabled;
    ok.onInvoke = [this]() { if (callbacks_.onOptionOk) callbacks_.onOptionOk(); };

    return { cancel, empty, empty, ok };
}

std::vector<Knob> T9Widget::getKnobs(int page)
{
    if (page != 0) return {};
    Knob k;
    k.id = "t9.candidate";
    k.label = "Candidate";
    k.isEnabled = true;
    k.onAdjust = [this](int delta, bool /*shift*/) {
        if (callbacks_.onKnobTurn) callbacks_.onKnobTurn(delta);
    };
    return { k };
}

// ── Input ─────────────────────────────────────────────────────────────────────

void T9Widget::handlePad(const controller_events::PadEvent& e)
{
    if (!e.pressed) return;
    if (callbacks_.onPadTap) callbacks_.onPadTap(static_cast<int>(e.pad));
}

void T9Widget::handleKnob(int localIndex, int16_t delta, uint16_t /*absolute*/, bool /*shift*/)
{
    if (localIndex != 0) return;
    if (callbacks_.onKnobTurn) callbacks_.onKnobTurn(static_cast<int>(delta));
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void T9Widget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void T9Widget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page != 0) return;

    // grid_ (child component) handles the 4×4 tile area. Only the candidate
    // strip at the bottom needs to be painted directly here.

    // Candidate strip
    auto candArea = bounds.removeFromBottom(kCandidateStripPx);
    g.setColour(juce::Colour(0xff121212));
    g.fillRect(candArea);

    if (candidates_.empty())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawFittedText("(no candidates)", candArea.reduced(4), juce::Justification::centredLeft, 1);
        return;
    }

    const int colCount = static_cast<int>(candidates_.size());
    const int colW = candArea.getWidth() / juce::jmax(1, colCount);
    for (int i = 0; i < colCount; ++i)
    {
        auto slot = juce::Rectangle<int>(candArea.getX() + i * colW, candArea.getY(), colW, candArea.getHeight()).reduced(2);
        const bool selected = (i == selectedCandidate_);
        g.setColour(selected ? juce::Colour(0xff4a7a4a) : juce::Colour(0xff252525));
        g.fillRoundedRectangle(slot.toFloat(), 2.0f);
        g.setColour(selected ? juce::Colours::white : juce::Colour(0xffaaaaaa));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawFittedText(candidates_[static_cast<size_t>(i)], slot.reduced(3), juce::Justification::centredLeft, 1);
    }
}

void T9Widget::resized()
{
    // Grid occupies everything except the candidate strip at the bottom.
    auto area = getLocalBounds()
                    .withTrimmedBottom(kCandidateStripPx)
                    .reduced(UiTheme::kPadding);
    grid_.setBounds(area);
}

// ── Private helpers ───────────────────────────────────────────────────────────

void T9Widget::refreshTiles()
{
    std::array<PadGridComponent::Tile, 16> tiles;
    const juce::Font labelFont { juce::FontOptions(12.0f) };

    for (int i = 0; i < 16; ++i)
    {
        auto& tile = tiles[static_cast<size_t>(i)];
        tile.labelFont   = labelFont;
        tile.labelColour = juce::Colours::white;

        // Fill colour mirrors the original paintPage logic
        juce::Colour fill;
        if (i == pendingPad_)
            fill = juce::Colours::white.withAlpha(0.8f);
        else if (kTiles[i].isLetter)
            fill = juce::Colours::white.withAlpha(0.15f);
        else if (i == kPadSym)
            fill = (mode_ == T9StateMachine::Mode::Symbols) ? juce::Colour(0xff2ed3d3) : juce::Colour(0xff1a6666);
        else if (i == kPadNum)
            fill = (mode_ == T9StateMachine::Mode::Numbers) ? juce::Colour(0xffffcd45) : juce::Colour(0xff665220);
        else if (i == 11)
            fill = juce::Colour(0xffa05050);   // red dim (del)
        else if (i == 15)
            fill = juce::Colour(0xff30a060);   // green (OK)
        else
            fill = juce::Colour(0xff3a3a3a);   // cursor & spare

        tile.fill = fill;

        // Label — same mode-switching logic as original
        juce::String label;
        if (mode_ == T9StateMachine::Mode::Symbols)
            label = kTiles[i].symGlyph;
        else if (mode_ == T9StateMachine::Mode::Numbers)
            label = kTiles[i].numGlyph;
        else
            label = kTiles[i].glyph;

        if (mode_ == T9StateMachine::Mode::Letters && kTiles[i].isLetter)
        {
            switch (caps_)
            {
                case T9StateMachine::Caps::Lower:
                    label = label.toLowerCase();
                    break;
                case T9StateMachine::Caps::Title:
                    if (label.isNotEmpty())
                        label = juce::String::charToString(juce::CharacterFunctions::toUpperCase(label[0]))
                              + label.substring(1).toLowerCase();
                    break;
                case T9StateMachine::Caps::Upper:
                    label = label.toUpperCase();
                    break;
            }
        }
        tile.label = label;
    }

    grid_.setTiles(tiles);
}

void T9Widget::refreshGridState()
{
    publishLeds();
    refreshTiles();
}

void T9Widget::publishLeds()
{
    const auto owner = describe().id.toStdString();

    for (int i = 0; i < 16; ++i)
    {
        uint8_t color = kTiles[i].ledColor;

        if (i == kPadNum)
            color = (mode_ == T9StateMachine::Mode::Numbers)
                        ? HardwareConstants::kColorGoldBright
                        : HardwareConstants::kColorGoldDim;

        if (i == kPadSym)
            color = (mode_ == T9StateMachine::Mode::Symbols)
                        ? HardwareConstants::kColorCyanBright
                        : HardwareConstants::kColorCyanDim;

        if (i == pendingPad_)
            color = HardwareConstants::kColorWhite;

        hw().setLed(ResourceIds::PadLeds[static_cast<size_t>(i)], color, owner);
    }
}

void T9Widget::onUiHostTick()
{
    if (! flashActive_) return;
    const auto nowMs = juce::Time::currentTimeMillis();
    if (nowMs - flashStartedMs_ > (juce::int64) kFlashDurationMs)
    {
        flashActive_ = false;
        refreshGridState();
    }
}

void T9Widget::startFlashTimer()
{
    flashStartedMs_ = juce::Time::currentTimeMillis();
    flashActive_ = true;
    publishLeds();
}

void T9Widget::stopFlashTimer()
{
    flashActive_ = false;
}
