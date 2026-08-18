// src/ui/components/T9StateMachine.cpp
#include "T9StateMachine.h"

namespace
{
// Cycles indexed by phone-key number (0 = phone 0 / space, 1 = phone 1, …, 9 = phone 9).
// Downstream callers translate their logical pad layout into phone-key numbers
// before invoking onPadTap. See T9StateMachine.h for the full convention.
constexpr const char* kLettersCycles[10] = {
    " ",       // phone 0 → space
    ".,!?1",   // phone 1
    "abc2",    // phone 2
    "def3",    // phone 3
    "ghi4",    // phone 4
    "jkl5",    // phone 5
    "mno6",    // phone 6
    "pqrs7",   // phone 7
    "tuv8",    // phone 8
    "wxyz9",   // phone 9
};
constexpr const char* kSymbolsCycles[10] = {
    " ",       // 0 → space
    ".,!?;:",  // 1
    "#@",      // 2
    "-_",      // 3
    "()",      // 4
    "[]",      // 5
    "{}",      // 6
    "/\\",     // 7
    "+=",      // 8
    "'\"",     // 9
};
// Numbers mode: each phone key commits its own digit on tap. Single-char
// "cycles" keep the existing multi-tap machinery happy; onPadTap also
// short-circuits so there's no 700 ms wait for numeric entry.
constexpr const char* kNumbersCycles[10] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};
}

int T9StateMachine::cycleLength(int padIndex) const
{
    if (padIndex < 0 || padIndex > 9) return 0;
    const char* s = (mode_ == Mode::Letters) ? kLettersCycles[padIndex]
                  : (mode_ == Mode::Symbols) ? kSymbolsCycles[padIndex]
                                             : kNumbersCycles[padIndex];
    int len = 0; while (s[len] != '\0') ++len;
    return len;
}

juce::juce_wchar T9StateMachine::pad2char(int padIndex, int index) const
{
    if (padIndex < 0 || padIndex > 9) return 0;
    const char* s = (mode_ == Mode::Letters) ? kLettersCycles[padIndex]
                  : (mode_ == Mode::Symbols) ? kSymbolsCycles[padIndex]
                                             : kNumbersCycles[padIndex];
    int len = cycleLength(padIndex);
    if (len == 0) return 0;
    return static_cast<juce::juce_wchar>(s[index % len]);
}

juce::juce_wchar T9StateMachine::applyCaps(juce::juce_wchar c)
{
    if (c >= 'a' && c <= 'z' && caps_ != Caps::Lower)
        return static_cast<juce::juce_wchar>(c - 'a' + 'A');
    return c;
}

void T9StateMachine::consumeTitleIfOneShot()
{
    if (caps_ == Caps::Title)
        caps_ = Caps::Lower;
}

T9StateMachine::Output T9StateMachine::onPadTap(int padIndex)
{
    Output out;
    if (padIndex < 0 || padIndex > 9) return out;   // utility pads not handled here

    // Numbers mode commits immediately — there's only one digit per pad and
    // users shouldn't wait 700 ms for each keystroke.
    if (mode_ == Mode::Numbers)
    {
        // Commit any pending letter first (in case user tapped num-toggle mid-cycle).
        if (pendingPad_ >= 0)
        {
            out.emitCommit = true;
            out.committedChar = applyCaps(pad2char(pendingPad_, pendingIndex_));
            consumeTitleIfOneShot();
            pendingPad_ = -1;
            pendingIndex_ = 0;
            // Chain a second commit for the digit below — multiple commits per
            // Output aren't supported, so just return and have the caller re-tap.
            // Instead: emit just the digit and drop the pending letter silently.
            // That's surprising UX; prefer the two-commit approach by returning
            // early here on the pending-letter commit only.
            return out;
        }
        out.emitCommit = true;
        out.committedChar = static_cast<juce::juce_wchar>(kNumbersCycles[padIndex][0]);
        return out;
    }

    if (pendingPad_ == padIndex)
    {
        // Same pad: advance cycle
        pendingIndex_ = (pendingIndex_ + 1) % cycleLength(padIndex);
    }
    else
    {
        // Different pad: commit previous (if any), start new cycle
        if (pendingPad_ >= 0)
        {
            out.emitCommit = true;
            out.committedChar = applyCaps(pad2char(pendingPad_, pendingIndex_));
            consumeTitleIfOneShot();
        }
        pendingPad_ = padIndex;
        pendingIndex_ = 0;
    }

    out.hasPendingCycle = true;
    out.pendingPadIndex = pendingPad_;
    out.pendingPreviewChar = applyCaps(pad2char(pendingPad_, pendingIndex_));
    return out;
}

T9StateMachine::Output T9StateMachine::onTimeout()
{
    Output out;
    if (pendingPad_ < 0) return out;
    out.emitCommit = true;
    out.committedChar = applyCaps(pad2char(pendingPad_, pendingIndex_));
    consumeTitleIfOneShot();
    pendingPad_ = -1;
    pendingIndex_ = 0;
    return out;
}
T9StateMachine::Output T9StateMachine::onCursorLeft()
{
    Output out;
    pendingPad_ = -1;
    pendingIndex_ = 0;
    out.cursorDelta = -1;
    return out;
}

T9StateMachine::Output T9StateMachine::onCursorRight()
{
    Output out;
    if (pendingPad_ >= 0)
    {
        out.emitCommit = true;
        out.committedChar = applyCaps(pad2char(pendingPad_, pendingIndex_));
        consumeTitleIfOneShot();
        pendingPad_ = -1;
        pendingIndex_ = 0;
    }
    out.cursorDelta = 1;
    return out;
}

T9StateMachine::Output T9StateMachine::onDelete()
{
    Output out;
    if (pendingPad_ >= 0)
    {
        // Mid-cycle: discard pending, no buffer change
        pendingPad_ = -1;
        pendingIndex_ = 0;
    }
    else
    {
        out.emitBackspace = true;
    }
    return out;
}

T9StateMachine::Output T9StateMachine::onCapsTap()
{
    switch (caps_)
    {
        case Caps::Lower: caps_ = Caps::Title; break;
        case Caps::Title: caps_ = Caps::Upper; break;
        case Caps::Upper: caps_ = Caps::Lower; break;
    }
    Output out;
    out.capsCycled = true;
    return out;
}

T9StateMachine::Output T9StateMachine::onNumToggle()
{
    Output out;
    pendingPad_ = -1;
    pendingIndex_ = 0;
    mode_ = (mode_ == Mode::Numbers) ? Mode::Letters : Mode::Numbers;
    return out;
}

T9StateMachine::Output T9StateMachine::onSymTap()
{
    Output out;
    pendingPad_ = -1;
    pendingIndex_ = 0;
    mode_ = (mode_ == Mode::Letters) ? Mode::Symbols : Mode::Letters;
    out.symToggle = true;
    return out;
}

T9StateMachine::Output T9StateMachine::onOk()
{
    Output out;
    if (pendingPad_ >= 0)
    {
        out.emitCommit = true;
        out.committedChar = applyCaps(pad2char(pendingPad_, pendingIndex_));
        consumeTitleIfOneShot();
        pendingPad_ = -1;
        pendingIndex_ = 0;
    }
    out.endEdit = true;
    out.endEditCommitValue = true;
    return out;
}
