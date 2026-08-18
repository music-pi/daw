#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include "../../input/InputManager.h"
#include "../components/PianoRollView.h"
#include "Widget.h"

namespace te = tracktion::engine;

/** Widget wrapper around PianoRollView, open on the Left panel for either a
    sampled-pad or keyboard-instrument MidiClip. Owns Close (d1) and Select All
    (d2) options, hosts shift+pad
    operations (quantise, pitch shift) and 4D encoder → navigateNotes via
    a View-priority input handler installed on activation. */
class PianoRollWidget : public Widget
{
public:
    PianoRollWidget() = default;
    ~PianoRollWidget() override = default;

    WidgetDescriptor describe() const override
    {
        return { "piano_roll", 1, false, DisplayConstraint::LeftOnly };
    }
    // Cached title: channelName_ changes only via setMidiClip, so the
    // titlebar poll at 60 Hz can read a pre-composed String instead of
    // allocating per frame.
    juce::String getTitle() const override { return cachedTitle_; }

    void setMidiClip(te::MidiClip* clip, juce::String channelName);
    void setOnClose(std::function<void()> cb) { onClose_ = std::move(cb); }

    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onEditAboutToBeReplaced() override;
    void onEditReplaced() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;
    std::unordered_map<int, uint8_t> getShiftPadOverlay(int page) override;

    void onUiHostTick() override;
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute,
                    bool shift) override;

    void paint(juce::Graphics& g) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void resized() override;

private:
    static constexpr InputManager::BindingId kInvalidBinding = 0;

    void installInputHandlers();
    void releaseInputHandlers();
    void applyQuantise();
    double quantiseGridBeats() const;

    PianoRollView view_;
    juce::String channelName_;
    juce::String cachedTitle_ { "Piano Roll > " };
    te::MidiClip* clip_ { nullptr };
    te::EditItemID clipId_;
    double lastPlayheadBeat_ { -1.0 };
    int quantiseStrengthPercent_ { 100 };
    int quantiseGridIndex_ { 1 }; // 0=1/8, 1=1/16, 2=1/32, 3=1/64
    std::array<int, 4> knobAccumulator_ { 0, 0, 0, 0 };
    std::function<void()> onClose_;

    InputManager::BindingId padBinding_ { kInvalidBinding };
    InputManager::BindingId stepperBinding_ { kInvalidBinding };
    InputManager::BindingId navUpBinding_    { kInvalidBinding };
    InputManager::BindingId navDownBinding_  { kInvalidBinding };
    InputManager::BindingId navLeftBinding_  { kInvalidBinding };
    InputManager::BindingId navRightBinding_ { kInvalidBinding };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollWidget)
};
