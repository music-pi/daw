// src/ui/components/T9StateMachine.h
#pragma once
#include <juce_core/juce_core.h>

class T9StateMachine
{
public:
    enum class Mode { Letters, Symbols, Numbers };
    enum class Caps { Lower, Title, Upper };

    struct Output
    {
        bool emitCommit = false;
        juce::juce_wchar committedChar = 0;
        bool emitBackspace = false;
        int cursorDelta = 0;
        bool endEdit = false;
        bool endEditCommitValue = false;
        bool symToggle = false;
        bool capsCycled = false;
        bool hasPendingCycle = false;
        int pendingPadIndex = -1;
        juce::juce_wchar pendingPreviewChar = 0;
    };

    /**
     * onPadTap — advance or start a cycle for a letter pad.
     *
     * @param padIndex  Phone-key number of the tapped pad, 0-9:
     *                    0 = space                (phone 0)
     *                    1 = symbols ".,!?1"      (phone 1)
     *                    2 = "abc2"               (phone 2)
     *                    3 = "def3"               (phone 3)
     *                    4 = "ghi4"               (phone 4)
     *                    5 = "jkl5"               (phone 5)
     *                    6 = "mno6"               (phone 6)
     *                    7 = "pqrs7"              (phone 7)
     *                    8 = "tuv8"               (phone 8)
     *                    9 = "wxyz9"              (phone 9)
     *                  Any other value is a no-op (returns empty Output).
     *
     * Callers translate their logical pad indices into phone-key numbers
     * before calling; see TextInputComponent for the canonical mapping.
     */
    Output onPadTap(int padIndex);
    Output onTimeout();
    Output onCursorLeft();
    Output onCursorRight();
    Output onDelete();
    Output onOk();
    Output onCapsTap();
    Output onSymTap();
    Output onNumToggle();   // toggles Letters ↔ Numbers (no effect on Symbols)

    Mode getMode() const { return mode_; }
    Caps getCaps() const { return caps_; }

    // Testing hooks (safe to expose):
    int pendingPadIndex() const { return pendingPad_; }
    int pendingCycleIndex() const { return pendingIndex_; }

private:
    Mode mode_ = Mode::Letters;
    Caps caps_ = Caps::Lower;
    int  pendingPad_ = -1;   // -1 = idle
    int  pendingIndex_ = 0;

    juce::juce_wchar pad2char(int padIndex, int index) const;
    int  cycleLength(int padIndex) const;
    juce::juce_wchar applyCaps(juce::juce_wchar c);
    void consumeTitleIfOneShot();
};
