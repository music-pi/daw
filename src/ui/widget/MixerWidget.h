#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include "Widget.h"
#include "ChannelSource.h"
#include "../components/ChannelStripsComponent.h"
#include "../components/mixer/MixerChannelState.h"
#include "../../engine/SamplerInstrument.h"

namespace te = tracktion::engine;

/**
 * MixerWidget — NI Maschine-style drill-down mixer with Global and Sound levels.
 *
 * Global level shows top-level FolderTracks, loose AudioTracks, and the
 * master output. Sound level shows child tracks of a focused FolderTrack.
 * Drill via navUp/navDown, scroll channels via arrowLeft/arrowRight,
 * D buttons select the Active channel per panel; hold muteChoke/solo as
 * modifiers while pressing a D button to toggle mute/solo on that channel.
 * Shift + knob adjusts pan; plain knob adjusts gain.
 */
class MixerWidget : public Widget,
                    private SamplerInstrument::Listener
{
public:
    enum class MixerLevel { Global, Sound };

    explicit MixerWidget(
        std::optional<te::EditItemID> initialGroup = std::nullopt,
        std::optional<te::EditItemID> initialChannel = std::nullopt);
    ~MixerWidget() override;

    WidgetDescriptor describe() const override;

    juce::String getTitle() const override;
    juce::String getTitleSubtitle() const override;

    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onEditAboutToBeReplaced() override;
    void onEditReplaced() override;

    std::vector<std::string> requiredResources(int page) override;

    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;
    void handleOption(int localIndex) override;
    void handleButton(const controller_events::ButtonEvent& e) override;

    // Test accessors
    MixerLevel getLevel() const { return level_; }
    std::optional<te::EditItemID> getFocusedGroup() const { return focusedGroup_; }
    int getActiveChannelIndex() const { return activeChannelIndex_; }
    int getScrollOffset() const { return scrollOffset_; }
    int getChannelCountForTest() const { return static_cast<int>(descriptors_.size()); }
    bool isMuteHeld() const { return muteHeld_; }
    bool isSoloHeld() const { return soloHeld_; }

    void setLevelForTest(MixerLevel level, std::optional<te::EditItemID> focused);

    void onUiHostTick() override;

private:
    // Slow path (20 Hz + explicit triggers): enumerate descriptors, sync the
    // states_ map keys, rebuild options/knobs, refresh LEDs. Allocates.
    void refreshChannels();
    void rebuildChannelViews();
    void updateNavLeds();

    // Fast path (60 Hz): read live TE values into state atomics and update
    // currentKnobs_ live values. Zero allocation in steady state.
    void fastRefresh();
    bool hasStaleDescriptors() const;
    bool rebindStaleDescriptors();

    void drillDown();
    void drillUp();
    void scrollBy(int delta);
    void pickInitialActiveChannel();
    int maxScrollOffset() const;
    void syncSelectedSound(const ChannelDescriptor& descriptor);
    ChannelDescriptor* descriptorForChannel(int channelIdx);
    void writeMixerStateToValueTree();
    void beginKnobTransaction();
    void performGainChange(const juce::String& channelId, float gainDb);
    void performPanChange(const juce::String& channelId, float pan);

    MixerLevel level_ { MixerLevel::Global };
    std::optional<te::EditItemID> focusedGroup_;
    std::optional<te::EditItemID> initialGroup_;
    std::optional<te::EditItemID> preferredChannel_;

    int scrollOffset_ { 0 };
    int activeChannelIndex_ { -1 };

    bool muteHeld_ { false };
    bool soloHeld_ { false };
    bool knobTransactionOpen_ { false };

    ChannelStripsComponent stripsComponent_;

    std::vector<ChannelDescriptor> descriptors_;
    std::unordered_map<juce::String, std::shared_ptr<MixerChannelState>> states_;

    // Tracks the Edit we're currently referencing through descriptors_. When
    // the edit pointer changes between refreshes (e.g., project load), raw
    // pointers in descriptors_/states_ and meter clients attached to plugins
    // from the old edit become dangling — we drop everything and rebuild.
    te::Edit* lastSeenEdit_ { nullptr };

    // Tick counter for dividing 60 Hz timer into 60 Hz fast / 20 Hz slow paths.
    int slowTickCounter_ { 0 };

    // Cached signature of the last descriptor list options+knobs were built
    // against. rebuildChannelViews() allocates option/knob vectors and
    // closures; re-running it every slow tick is wasted work when the
    // descriptor identity hasn't changed. Compare against descriptorsSignature_
    // to decide whether a rebuild is actually needed.
    std::size_t descriptorsSignature_ { 0 };
    std::size_t computeDescriptorsSignature() const;

    // SamplerInstrument::Listener — visual "trigger flash" for per-pad meters
    // when the step sequencer fires an ungrouped pad (audio routes through
    // the shared Sequencer track's SamplerPlugin, so the pad-track meter
    // would otherwise see nothing). Keyed by pad index; decays to -inf.
    void padTriggered(int padIndex) override;
    std::unordered_map<int, float> padFlashDb_;

    // Meter clients keyed by descriptor id. A client is registered with the
    // corresponding LevelMeterPlugin's measurer on first use and removed
    // when the widget deactivates.
    struct MeterRegistration
    {
        te::LevelMeterPlugin* plugin { nullptr };
        std::unique_ptr<te::LevelMeasurer::Client> client;
        te::Plugin::Ptr pluginLifetime;
    };
    std::unordered_map<juce::String, MeterRegistration> meterClients_;

    std::vector<Option> currentOptions_;
    std::vector<Knob> currentKnobs_;

    static constexpr float kMeterFloorDbfs = -60.0f;
    static constexpr float kFaderMinDb = -48.0f;
    static constexpr float kFaderMaxDb = 6.0f;
    static constexpr int   kChannelsPerPanel = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerWidget)
};
