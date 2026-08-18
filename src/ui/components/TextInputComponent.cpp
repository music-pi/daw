#include "TextInputComponent.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/T9Dictionary.h"
#include "../widget/T9Widget.h"
#include "../widget/Widget.h"
#include "../widget/WindowManager.h"
#include "../UiRefresh.h"

TextInputComponent::TextInputComponent() = default;
TextInputComponent::~TextInputComponent() = default;

void TextInputComponent::configure(Config c)
{
    // Re-configuring mid-edit would leave the state machine and timer running
    // against a buffer they no longer control. Force a cancel first so the
    // widget is torn down and sm_ is reset along with the new buffer.
    if (editing_) endEdit(false);
    sm_ = T9StateMachine{};
    stopTimer();
    candidates_.clear();
    selectedCandidate_ = 0;

    config_ = std::move(c);
    buffer_ = config_.initialValue;
    cursor_ = buffer_.length();
}

void TextInputComponent::setOnCommit(std::function<void(juce::String)> cb)   { onCommit_  = std::move(cb); }
void TextInputComponent::setOnCancel(std::function<void()> cb)               { onCancel_  = std::move(cb); }
void TextInputComponent::setOnTextChanged(std::function<void(juce::String)> cb) { onTextChanged_ = std::move(cb); }

void TextInputComponent::setText(juce::String s)
{
    buffer_ = std::move(s);
    cursor_ = buffer_.length();
    repaint();
    requestUiRefresh(*this);
}

Widget* TextInputComponent::findEnclosingWidget() const
{
    juce::Component* c = getParentComponent();
    while (c != nullptr)
    {
        if (auto* w = dynamic_cast<Widget*>(c)) return w;
        c = c->getParentComponent();
    }
    return nullptr;
}

WindowManager* TextInputComponent::findWindowManager() const
{
    juce::Component* c = getParentComponent();
    while (c != nullptr)
    {
        if (auto* wm = dynamic_cast<WindowManager*>(c)) return wm;
        c = c->getParentComponent();
    }
    return nullptr;
}

void TextInputComponent::beginEdit()
{
    if (editing_) return;
    openWidget();
    editing_ = (widget_ != nullptr);
    repaint();
    requestUiRefresh(*this);
}

void TextInputComponent::endEdit(bool commit)
{
    if (!editing_) return;
    editing_ = false;
    stopTimer();
    closeWidget();

    if (commit)
    {
        if (!candidates_.empty()
            && selectedCandidate_ >= 0 && selectedCandidate_ < (int)candidates_.size()
            && candidates_[(size_t)selectedCandidate_] != buffer_)
        {
            buffer_ = candidates_[(size_t)selectedCandidate_];
            cursor_ = buffer_.length();
        }

        if (!config_.dictionaryScope.isEmpty())
        {
            if (auto* wm = findWindowManager())
                if (auto* ae = wm->getAudioEngine())
                    ae->getT9Dictionary().record(config_.dictionaryScope, buffer_);
        }

        if (onCommit_) onCommit_(buffer_);
    }
    else
    {
        if (onCancel_) onCancel_();
    }
    repaint();
    requestUiRefresh(*this);
}

void TextInputComponent::openWidget()
{
    auto* host = findEnclosingWidget();
    auto* wm = findWindowManager();
    if (host == nullptr || wm == nullptr) { jassertfalse; return; }

    // Guard against opening T9 against a host that has already been swapped
    // out of both slots (e.g., a navigation event between a queued beginEdit
    // and its execution). getSide() would otherwise assert and silently fall
    // back to Left, spawning T9 on the wrong panel.
    if (wm->getWidget(DisplaySide::Left) != host &&
        wm->getWidget(DisplaySide::Right) != host)
    {
        jassertfalse;
        return;
    }

    const DisplaySide hostSide = wm->getSide(host);
    const DisplaySide oppoSide = (hostSide == DisplaySide::Left) ? DisplaySide::Right : DisplaySide::Left;

    if (auto* existing = wm->getWidget(oppoSide))
        if (existing->describe().id == "t9") { jassertfalse; return; }

    auto t9 = std::make_unique<T9Widget>();
    widget_ = t9.get();

    T9Widget::Callbacks cb;
    cb.onPadTap = [this](int padIdx) {
        // Map the 16 physical pad indices to T9StateMachine inputs. MK3 pads
        // are column-major from bottom-left (0 = bottom-left, 15 = top-right).
        // Layout places phone-keypad letters on cols 1-3 reading top-down
        // (phone 1-3 on top row, phone 7-9 on the row above the bottom),
        // functions on col 4, and sym/space/◀/▶ on the bottom row. See
        // T9Widget.cpp kTiles for the authoritative layout.
        switch (padIdx)
        {
            case 0:  applyOutput(sm_.onSymTap());      return; // sym
            case 1:  applyOutput(sm_.onPadTap(0));     return; // space (phone 0)
            case 2:  applyOutput(sm_.onCursorLeft());  return;
            case 3:  applyOutput(sm_.onCursorRight()); return;
            case 4:  applyOutput(sm_.onPadTap(7));     return; // phone 7 PQRS
            case 5:  applyOutput(sm_.onPadTap(8));     return; // phone 8 TUV
            case 6:  applyOutput(sm_.onPadTap(9));     return; // phone 9 WXYZ
            case 7:  applyOutput(sm_.onNumToggle());   return; // num-mode toggle
            case 8:  applyOutput(sm_.onPadTap(4));     return; // phone 4 GHI
            case 9:  applyOutput(sm_.onPadTap(5));     return; // phone 5 JKL
            case 10: applyOutput(sm_.onPadTap(6));     return; // phone 6 MNO
            case 11: /* unassigned — prev del slot */  return;
            case 12: applyOutput(sm_.onPadTap(1));     return; // phone 1 .,!?
            case 13: applyOutput(sm_.onPadTap(2));     return; // phone 2 ABC
            case 14: applyOutput(sm_.onPadTap(3));     return; // phone 3 DEF
            case 15: applyOutput(sm_.onDelete());      return; // del (was OK)
            default: return;
        }
    };
    cb.onKnobTurn = [this](int delta) {
        if (candidates_.empty()) return;
        const int n = static_cast<int>(candidates_.size());
        selectedCandidate_ = (((selectedCandidate_ + delta) % n) + n) % n;
        pushWidgetState();
        repaint();
        requestUiRefresh(*this);
    };
    cb.onOptionCancel = [this]() { endEdit(false); };
    cb.onOptionOk     = [this]() { applyOutput(sm_.onOk()); };
    cb.onShiftPress = [this]() { applyOutput(sm_.onCapsTap()); };
    t9->setCallbacks(std::move(cb));

    widget_->setOnDeactivated([this]() {
        widget_ = nullptr;   // clear before endEdit so closeWidget() doesn't re-enter wm->close
        if (editing_) endEdit(false);
    });
    wm->open(std::move(t9), oppoSide);
    pushWidgetState();
}

void TextInputComponent::closeWidget()
{
    auto* wm = findWindowManager();
    if (wm == nullptr) return;
    if (widget_ != nullptr)
    {
        wm->close("t9");
        widget_ = nullptr;
    }
}

namespace
{
// Translate T9StateMachine phone-number index (0=space, 1-9=phone 1-9) to
// the physical MK3 pad index (matches the keyTiles layout in T9Widget.cpp).
constexpr int phoneToPhysicalPad(int phone)
{
    switch (phone)
    {
        case 0: return 1;   // space → bottom row, col 2
        case 1: return 12;  // .,!?  → top-left
        case 2: return 13;  // ABC
        case 3: return 14;  // DEF
        case 4: return 8;   // GHI
        case 5: return 9;   // JKL
        case 6: return 10;  // MNO
        case 7: return 4;   // PQRS
        case 8: return 5;   // TUV
        case 9: return 6;   // WXYZ
        default: return -1;
    }
}
} // namespace

void TextInputComponent::applyOutput(const T9StateMachine::Output& out)
{
    // Commit character into buffer
    if (out.emitCommit && out.committedChar != 0)
    {
        const juce::juce_wchar c = out.committedChar;
        if (!config_.charFilter || config_.charFilter(c))
        {
            if (buffer_.length() < config_.maxLength)
            {
                buffer_ = buffer_.substring(0, cursor_) + juce::String::charToString(c)
                        + buffer_.substring(cursor_);
                ++cursor_;
                if (onTextChanged_) onTextChanged_(buffer_);
            }
        }
    }

    // Backspace
    if (out.emitBackspace && cursor_ > 0)
    {
        buffer_ = buffer_.substring(0, cursor_ - 1) + buffer_.substring(cursor_);
        --cursor_;
        if (onTextChanged_) onTextChanged_(buffer_);
    }

    // Cursor movement
    if (out.cursorDelta != 0)
    {
        cursor_ = juce::jlimit(0, buffer_.length(), cursor_ + out.cursorDelta);
    }

    // Pending-cycle state — track for paint() flashing + timeout commit
    if (out.hasPendingCycle)
    {
        pendingPreview_ = out.pendingPreviewChar;
        cycleStartedMs_ = juce::Time::currentTimeMillis();
        if (!isTimerRunning()) startTimerHz(kTickHz);
    }
    else
    {
        pendingPreview_ = 0;
        cycleStartedMs_ = 0;
        stopTimer();
    }

    // endEdit happens last
    if (out.endEdit)
    {
        endEdit(out.endEditCommitValue);
        return;
    }

    // Dictionary candidates refresh when the buffer changes
    if (out.emitCommit || out.emitBackspace)
        refreshCandidates();

    // Push state to T9 for rendering + LEDs. State machine's pendingPadIndex
    // is a phone-keypad number (0=space, 1-9=phone 1-9); translate to the
    // physical MK3 pad index the widget uses for rendering/LEDs.
    if (widget_ != nullptr)
    {
        if (out.hasPendingCycle)
        {
            const int physical = phoneToPhysicalPad(out.pendingPadIndex);
            widget_->setPendingCycleHint(physical, out.pendingPreviewChar);
        }
        else
        {
            widget_->setPendingCycleHint(-1, 0);
        }

        widget_->setCapsMode(sm_.getCaps());
        widget_->setMode(sm_.getMode());
    }
    repaint();
    requestUiRefresh(*this);
}

void TextInputComponent::timerCallback()
{
    // Fires at kTickHz while a pending cycle is active. Drives both the
    // 700 ms timeout commit and the cursor-/pending-preview blink.
    const auto now = juce::Time::currentTimeMillis();
    if (cycleStartedMs_ != 0 && (now - cycleStartedMs_) >= kTimeoutMs)
    {
        stopTimer();
        applyOutput(sm_.onTimeout());
        return;
    }
    // Otherwise just repaint to toggle the blink.
    repaint();
    requestUiRefresh(*this);
}

void TextInputComponent::pushWidgetState()
{
    if (widget_ == nullptr) return;
    widget_->setCandidates(candidates_, selectedCandidate_);
}

void TextInputComponent::refreshCandidates()
{
    candidates_.clear();
    selectedCandidate_ = 0;

    if (config_.dictionaryScope.isEmpty()) { pushWidgetState(); return; }

    auto* wm = findWindowManager();
    if (wm == nullptr) { pushWidgetState(); return; }
    auto* ae = wm->getAudioEngine();
    if (ae == nullptr) { pushWidgetState(); return; }

    candidates_ = ae->getT9Dictionary().completionsFor(config_.dictionaryScope, buffer_, 5);
    pushWidgetState();
}

void TextInputComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111111));
    g.setColour(juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    if (config_.prompt.isNotEmpty())
        g.drawFittedText(config_.prompt, getLocalBounds().withHeight(14).reduced(4, 0),
                         juce::Justification::centredLeft, 1);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(16.0f)));
    auto line = getLocalBounds().withTrimmedTop(14).reduced(4);

    const juce::String prefix = buffer_.substring(0, cursor_);
    const juce::String suffix = buffer_.substring(cursor_);

    // Blink phase: ~250ms on / 250ms off at kTickHz=4.
    const bool blinkOn = editing_
        && ((juce::Time::currentTimeMillis() / (1000 / kTickHz)) % 2 == 0);

    juce::String marker;
    if (editing_)
    {
        if (pendingPreview_ != 0)
            marker = blinkOn ? juce::String::charToString(pendingPreview_) : juce::String("_");
        else
            marker = blinkOn ? juce::String("|") : juce::String(" ");
    }

    const juce::String rendered = prefix + marker + suffix;
    g.drawFittedText(rendered, line, juce::Justification::centredLeft, 1);
}

void TextInputComponent::resized() {}
