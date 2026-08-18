#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "T9StateMachine.h"

class T9Widget;

class TextInputComponent : public juce::Component, private juce::Timer
{
public:
    struct Config
    {
        juce::String prompt;
        juce::String initialValue;
        int maxLength = 128;
        juce::String dictionaryScope;  // "" = no dictionary
        std::function<bool(juce::juce_wchar)> charFilter;
    };

    TextInputComponent();
    ~TextInputComponent() override;

    void configure(Config c);

    void setOnCommit(std::function<void(juce::String)> cb);
    void setOnCancel(std::function<void()> cb);
    void setOnTextChanged(std::function<void(juce::String)> cb);

    void beginEdit();
    void endEdit(bool commit);

    bool isEditing() const { return editing_; }
    juce::String getText() const { return buffer_; }
    void setText(juce::String s);

    // Inspection accessors (testing + future consumer wiring):
    int getSelectedCandidateIndex() const { return selectedCandidate_; }
    const std::vector<juce::String>& getCandidates() const { return candidates_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // juce::Timer — 700ms pending-cycle timeout
    void timerCallback() override;

    // State-machine response handling
    void applyOutput(const T9StateMachine::Output& out);
    void refreshCandidates();
    void pushWidgetState();
    void openWidget();
    void closeWidget();
    class Widget* findEnclosingWidget() const;
    class WindowManager* findWindowManager() const;

    Config config_;
    juce::String buffer_;
    int cursor_ = 0;
    bool editing_ = false;

    T9StateMachine sm_;
    T9Widget* widget_ = nullptr;  // non-owning; WindowManager owns

    std::vector<juce::String> candidates_;
    int selectedCandidate_ = 0;

    // Pending cycle state — when the user taps a pad but hasn't committed
    // yet, this holds the preview char so paint() can render it in place of
    // the cursor (flashing). Cleared on commit / cancel.
    juce::juce_wchar pendingPreview_ = 0;
    juce::int64      cycleStartedMs_ = 0;

    std::function<void(juce::String)> onCommit_;
    std::function<void()> onCancel_;
    std::function<void(juce::String)> onTextChanged_;

    static constexpr int kTimeoutMs  = 700;   // commit-after-idle
    static constexpr int kTickHz     = 4;     // cursor/pending blink rate

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextInputComponent)
};
