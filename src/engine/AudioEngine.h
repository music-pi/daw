#pragma once
#include <atomic>
#include <memory>
#include <tracktion_engine/tracktion_engine.h>
#include <juce_core/juce_core.h>

namespace te  = tracktion::engine;
namespace tcc = tracktion::core;

class SamplerInstrument;
class GroupManager;
class KeyboardInstrumentBank;
class PluginConfigRegistry;
class Arpeggiator;
class T9Dictionary;

class AudioEngine {
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void editAboutToBeReplaced() {}
        virtual void editReplaced() {}
    };

    enum class PlayMode
    {
        Pattern,
        Track
    };

    struct TransportSnapshot
    {
        bool hasEdit { false };
        bool isPlaying { false };
        bool isRecording { false };
        bool isLooping { false };
        bool isPaused { false };
        PlayMode playMode { PlayMode::Pattern };
        double positionSeconds { 0.0 };
        double audiblePositionSeconds { 0.0 };
        double loopStartSeconds { 0.0 };
        double loopEndSeconds { 0.0 };
        double tempoBpm { 0.0 };
        double sampleRate { 0.0 };
        bool hasAudiblePosition { false };
        double swingPercent { 50.0 };
    };

    explicit AudioEngine(te::Engine& e);
    ~AudioEngine();

    bool initialiseAudioDevice(int sampleRate, int bufferSize);
    bool setBufferSize(int bufferSize);
    void shutdown();

    void createEmptyEdit();
    bool loadEditFromFile(const juce::File& file);
    bool loadProjectFromFile(const juce::File& file);
    bool saveEditToFile(const juce::File& file);
    bool saveEdit();
    bool saveProjectToFile(const juce::File& file);
    bool saveCurrentProject();
    juce::File getCurrentProjectFile() const;
    juce::File getCurrentEditFile() const;
    void play();
    void stop();
    void setLoopSeconds(double start, double end, bool enable);
    void enableClick(bool enable);
    bool isClickEnabled() const;
    void toggleLoop();
    void toggleRecord(bool allowWithoutArmedInputs = true);

    /** Immediately silence every live instrument without stopping transport.
        Clears the keyboard arpeggiator, sampler voices, and hosted plugin
        voices. Safe to call when no edit is loaded. */
    void panicAllNotes();

    void setPlayMode(PlayMode mode);
    PlayMode getPlayMode() const;

    bool isPlaying() const;
    bool isRecording() const;
    bool isLooping() const;
    bool canRecord() const;
    bool isPaused() const;
    bool hasEdit() const;

    // Record-arm is a UI-level intent flag separate from te::TransportControl's
    // record state: armed means "on the next transport tick, write incoming
    // notes/pad hits into the active clip". Lives here so the transport LED,
    // PatternWidget, and keyboard-mode recording all see the same truth.
    bool isRecordArmed() const;
    void setRecordArmed(bool armed);
    void toggleRecordArm();

    // True during TE's count-in pre-roll — transport is engaged but the
    // playhead is at a negative time (playback hasn't crossed zero yet).
    bool isCountingIn() const;

    /** Publish the dedicated audio-input recorder state for transport input
        routing and the shared record LED. This is deliberately separate from
        Tracktion's transport recording state. */
    void setAudioCaptureState(bool suiteActive, bool recording) noexcept;
    bool isAudioCaptureSuiteActive() const noexcept;
    bool isAudioCaptureRecording() const noexcept;

    TransportSnapshot getTransportSnapshot();  // Non-const: may trigger loop completion check

    [[nodiscard]] te::Engine& getEngine() noexcept { return engine; }
    [[nodiscard]] te::Edit*   getEdit()   noexcept { return edit.get(); }
    [[nodiscard]] const te::Edit* getEdit() const noexcept { return edit.get(); }
    juce::AudioDeviceManager& getAudioDeviceManager();

    SamplerInstrument& getSampler();
    const SamplerInstrument& getSampler() const;

    GroupManager& getGroupManager();
    const GroupManager& getGroupManager() const;

    KeyboardInstrumentBank& getKeyboardBank();
    const KeyboardInstrumentBank& getKeyboardBank() const;

    /** Per-plugin configuration registry (pinned params + preset sources).
        Loaded from the project-shipped config dir and the user dir at
        construction. */
    PluginConfigRegistry& getPluginConfigRegistry();
    const PluginConfigRegistry& getPluginConfigRegistry() const;

    /** Global keyboard-mode arpeggiator. Shared across all keyboard-bank
        slots — switching slots reassigns its target track but preserves
        mode/rate/etc settings. */
    Arpeggiator& getKeyboardArp();
    const Arpeggiator& getKeyboardArp() const;

    // Pad levels (delegates to SamplerInstrument)
    float getPadLevelDb(int padIndex) const;

    // Global swing: integer percent in [50, 75]. 50 = straight, 75 = hard shuffle.
    double getSwingPercent() const;
    void   setSwingPercent(double percent);

    [[nodiscard]] juce::ValueTree getAppState() const { return appState; }
    [[nodiscard]] juce::ValueTree getTransportState() const { return transportState; }
    [[nodiscard]] juce::ValueTree getPadsState() const { return padsState; }
    [[nodiscard]] juce::ValueTree getGroupsState() const { return groupsState; }
    [[nodiscard]] juce::ValueTree getMixerState() const { return mixerState; }
    [[nodiscard]] juce::ValueTree getUiState() const { return uiState; }
    [[nodiscard]] juce::ValueTree getSettingsState() const { return settingsState; }

    T9Dictionary& getT9Dictionary();
    const T9Dictionary& getT9Dictionary() const;

    juce::UndoManager& getUndoManager();
    const juce::UndoManager& getUndoManager() const;

    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();
    juce::File resolveSampleFileForPlayback(const juce::File& sourceFile);

    /** Refresh transport loop points to match the current play mode. Call after
        changing the pattern length or switching play mode. */
    void updateLoopRangeForPlayMode();

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit>   edit;
    bool paused { false };
    bool recordArmed_ { false };
    std::atomic<bool> audioCaptureSuiteActive_ { false };
    std::atomic<bool> audioCaptureRecording_ { false };
    // Click-track state captured when an armed play() forces the metronome
    // on for the count-in, so stop() can restore the user's preference.
    bool clickWasEnabledBeforeRecord_ { false };
    PlayMode playMode { PlayMode::Pattern };
    juce::File currentProjectFile;
    bool currentProjectIsHybrid { false };

    juce::ValueTree appState;
    juce::ValueTree transportState;
    juce::ValueTree padsState;
    juce::ValueTree groupsState;
    juce::ValueTree mixerState;
    juce::ValueTree uiState;
    juce::ValueTree settingsState;

    std::unique_ptr<SamplerInstrument> sampler_;
    std::unique_ptr<KeyboardInstrumentBank> keyboardBank_;
    std::unique_ptr<GroupManager> groupManager;
    std::unique_ptr<PluginConfigRegistry> pluginConfigRegistry_;
    std::unique_ptr<Arpeggiator> keyboardArp_;
    std::unique_ptr<T9Dictionary> t9Dictionary_;
    juce::ListenerList<Listener> listeners_;

    void initialiseStateTree();
    void adoptStateTree(juce::ValueTree stateNode);
    void ensureStateChildren();
    void seedTransportDefaults(bool force);
    static juce::ValueTree ensureChild(juce::ValueTree parent, const juce::Identifier& childId);
    bool installEdit(std::unique_ptr<te::Edit> newEdit, bool resetStateTree);
    static juce::File getHybridEditFileForProject(const juce::File& projectFile);

    double calculateArrangerEndSeconds() const;
    void teardownEdit() noexcept;
    void ensureTransportContextAllocated();
    void freeTransportContext();
    void validateInputDeviceConfigurations();
    void disableTrackInputDevice(te::AudioTrack& track);
    bool validateAndEnableTrackInputDevice(te::AudioTrack& track);
    bool hasValidInputChannelsForTrack(te::AudioTrack& track) const;
    juce::File getCachedSampleFile(const te::AudioFile& source) const;
    bool createCachedSampleFile(const te::AudioFile& source, const juce::File& destination);

    void applySwingToSampler();  // pushes current swing to all pad MidiClips

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
