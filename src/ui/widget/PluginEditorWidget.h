#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include "../components/Toast.h"
#include "Widget.h"

namespace te = tracktion::engine;

struct PluginConfig;

/** Hardware-first plugin host. Pages: Pinned (from PluginConfig) → Presets
    (VST programs or file-scanned .syx) → Params (all automatable, 4 per
    page). The native AudioProcessorEditor is a lazy d4 overlay — creating
    it synchronously on open hangs the UI thread on some plugins. */
class PluginEditorWidget : public Widget
{
public:
    static constexpr int kParamsPerPage = 4;

    PluginEditorWidget() = default;
    ~PluginEditorWidget() override = default;

    WidgetDescriptor describe() const override;
    juce::String getTitle() const override;

    void setPlugin(te::Plugin* plugin);

    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onEditAboutToBeReplaced() override;
    void onEditReplaced() override;
    void onPageVisible(int page) override;
    void onPageHidden(int page) override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;

    void paint(juce::Graphics& g) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void resized() override;

    // Visible for tests.
    static std::vector<juce::MemoryBlock> splitSysexFrames(const juce::MemoryBlock& data);
    static juce::String extractVirusPresetName(const juce::MemoryBlock& frame);

private:
    // Page layout helpers — Pinned is always first, Editor is not a page.
    int pinnedPageCount() const;
    int paramPageCount() const;
    int firstPinnedPage() const { return 0; }
    int presetsPage()    const { return pinnedPageCount(); }
    int firstParamsPage() const { return presetsPage() + 1; }
    int totalPageCount() const { return firstParamsPage() + paramPageCount(); }

    enum class PageKind { Pinned, Presets, Params };
    PageKind classify(int page) const;

    // Param resolution.
    juce::Array<te::AutomatableParameter*> getParams() const;
    juce::Array<te::AutomatableParameter*> getParamsForPage(int paramsPageIndex) const;
    juce::Array<te::AutomatableParameter*> resolvePinnedParams() const;
    juce::Array<te::AutomatableParameter*> getPinnedForPage(int pinnedPageIndex) const;
    juce::String pinnedLabelFor(int pinnedIndex) const;
    bool hasPinnedParams() const;
    te::AutomatableParameter* findParamById(const juce::String& id) const;

    // Presets.
    juce::AudioProcessor* getProcessor() const;
    int  getNumPresets() const;
    juce::String getPresetName(int index) const;
    int  getCurrentPresetIndex() const;
    void loadPreset(int index);
    bool useFileScanPresets() const;
    void refreshFileScanPresets();

    // Send concatenated F0..F7 sysex frames to the plugin via the owning
    // track's live-MIDI queue. Safe to call from the message thread —
    // AudioTrack::injectLiveMidiMessage is the path keyboard-note playback
    // already uses.
    bool injectSysexBytes(const juce::MemoryBlock& bytes);

    void renderPinnedPage(juce::Graphics& g, int pinnedPageIndex, juce::Rectangle<int> bounds);
    void renderFourOscWaveform(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void renderPresetsPage(juce::Graphics& g, juce::Rectangle<int> bounds);
    void renderParamsPage(juce::Graphics& g, int paramsPageIndex, juce::Rectangle<int> bounds);

    // Native editor (lazy).
    void toggleNativeUi();
    void ensureEditorCreated();     // synchronous create, used by the async path

    te::Plugin* plugin_ { nullptr };
    te::ExternalPlugin* externalPlugin_ { nullptr };
    te::EditItemID pluginId_;
    juce::String pluginName_;
    const PluginConfig* config_ { nullptr };
    struct CuratedParam
    {
        juce::String id;
        juce::String label;
    };
    std::vector<CuratedParam> curatedParams_;
    std::vector<juce::String> curatedPageNames_;
    int numParams_ { 0 };
    int presetSelection_ { 0 };
    int currentScannedPreset_ { -1 };
    struct ScannedPreset
    {
        juce::String name;          // display name (from frame body if found, else filename)
        juce::File sourceFile;      // .syx file this came from
        juce::MemoryBlock bytes;    // exact bytes to inject on load (full file or a single frame)
    };
    std::vector<ScannedPreset> scannedPresets_;

    juce::Viewport viewport_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    bool editorVisible_ { false };
    bool editorCreationPending_ { false };
    ToastHandle loadingToast_;

    // Title memo: WindowManager polls getTitle() at 60 Hz, and the composed
    // string involves 4-5 concatenations + two int->String conversions.
    // Recompute only when the inputs (page, editorVisible, pluginName, page
    // counts) actually change.
    struct TitleKey
    {
        int currentPage { -1 };
        int pinnedCount { -1 };
        int paramCount { -1 };
        bool editorVisible { false };
        juce::String pluginName;
        bool operator==(const TitleKey& o) const {
            return currentPage == o.currentPage
                && pinnedCount == o.pinnedCount
                && paramCount == o.paramCount
                && editorVisible == o.editorVisible
                && pluginName == o.pluginName;
        }
    };
    mutable TitleKey titleKey_ {};
    mutable juce::String titleCached_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWidget)
};
