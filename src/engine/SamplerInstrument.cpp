#include "SamplerInstrument.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "AudioEngine.h"
#include "KeyboardInstrumentBank.h"
#include "MidiConstants.h"
#include "MidiStepView.h"
#include "RoundRobinMidiPlugin.h"
#include "commands/SetChokeGroupCommand.h"

namespace
{
constexpr int kSequencerTimerHz = 200;

float applyLayerVelocityCurve(
    float velocity, SamplerInstrument::LayerVelocityCurve curve)
{
    velocity = juce::jlimit(0.0f, 1.0f, velocity);
    switch (curve)
    {
        case SamplerInstrument::LayerVelocityCurve::Soft:
            return std::sqrt(velocity);
        case SamplerInstrument::LayerVelocityCurve::Hard:
            return velocity * velocity;
        case SamplerInstrument::LayerVelocityCurve::Fixed:
            return 1.0f;
        case SamplerInstrument::LayerVelocityCurve::Linear:
            return velocity;
    }
    return velocity;
}

SamplerInstrument::TriggerMode validTriggerModeForLayerCount(
    SamplerInstrument::TriggerMode mode, int layerCount)
{
    using Mode = SamplerInstrument::TriggerMode;
    if (layerCount <= 1)
    {
        if (mode == Mode::RoundRobinOrdered)
            return Mode::VelocityRoundRobinOrdered;
        if (mode == Mode::RoundRobinRandom)
            return Mode::VelocityRoundRobinRandom;
    }
    else
    {
        if (mode == Mode::VelocityRoundRobinOrdered)
            return Mode::RoundRobinOrdered;
        if (mode == Mode::VelocityRoundRobinRandom)
            return Mode::RoundRobinRandom;
    }
    return mode;
}

void removePluginRaw(te::Plugin* plugin)
{
    if (plugin == nullptr)
        return;

    te::Plugin::Ptr keepAlive(plugin);
    auto parent = plugin->state.getParent();
    if (parent.isValid())
        parent.removeChild(plugin->state, nullptr);
}
}

SamplerInstrument::SamplerInstrument(AudioEngine& engine)
    : engine_(engine)
{
}

SamplerInstrument::~SamplerInstrument()
{
    detach();
}

void SamplerInstrument::attachToEdit(te::Edit& edit)
{
    detach();
    edit_ = &edit;
    ensureInfrastructure();
    restorePadTracksFromEdit();
    rebuildChokeGroupIndex();   // pads populated; bucket-by-group the stored values

    // Polls the transport playhead to drive choke-group cutoffs and UI
    // listener fan-out. 200 Hz keeps the cutoff below one 128-sample block
    // at common sample rates while leaving the MIDI clips themselves native
    // and untouched during playback.
    startTimerHz(kSequencerTimerHz);
}

void SamplerInstrument::restorePadTracksFromEdit()
{
    if (edit_ == nullptr || folder_ == nullptr)
        return;

    // Scan the folder for existing pad tracks tagged with a "padIndex" property
    for (auto* track : folder_->getAllSubTracks(false))
    {
        auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
        if (audioTrack == nullptr)
            continue;

        if (!audioTrack->state.hasProperty("padIndex"))
            continue;

        int padIndex = static_cast<int>(audioTrack->state.getProperty("padIndex"));
        if (padIndex < 0 || padIndex >= kMaxPads)
            continue;

        // Grow pads_ to fit this index (fill gaps with empty pads)
        while (static_cast<int>(pads_.size()) <= padIndex)
        {
            PadInfo empty;
            empty.index = static_cast<int>(pads_.size());
            empty.name = juce::String("Pad ") + juce::String(empty.index + 1);
            empty.midiNote = kBaseMidiNote + empty.index;
            pads_.push_back(std::move(empty));
        }

        auto& pad = pads_[static_cast<size_t>(padIndex)];
        pad.track = audioTrack;
        pad.name = audioTrack->getName();
        pad.midiNote = kBaseMidiNote + padIndex;

        // Reconnect plugins
        setupTrackPlugins(pad);
        ensurePadPatternClip(pad);

        // Re-bind the bank to the restored track so its ClipSlotList reads
        // survive load. Without this the bank's track_ stays null and
        // readPattern/storePattern silently no-op, wiping inactive patterns.
        pad.patternBank.attach(*pad.track);
    }
}

void SamplerInstrument::detach()
{
    stopTimer();
    lastTriggeredStep_ = -1;
    wasPlaying_ = false;

    // Remove meter clients before clearing pads
    for (auto& pad : pads_)
    {
        if (pad.meter != nullptr && pad.meterClient != nullptr)
            pad.meter->measurer.removeClient(*pad.meterClient);
    }

    pads_.clear();
    for (auto& bucket : chokeGroupIndex_) bucket.clear();
    // Reseed with the default lane count for fresh songs. Load paths
    // (restorePadsFromState) clear and repopulate with whatever the save
    // contains, so legacy single-lane projects keep their original shape.
    songLanes_.assign(static_cast<size_t>(kDefaultSongLanes), SongLane{});
    folder_ = nullptr;
    edit_ = nullptr;
    selectedPad_ = -1;
    activePatternIndex_ = 0;
    numPatterns_ = 1;
    stepsPerBar_ = 16;
    bars_ = 1;
    patternNames_.assign(1, juce::String());
}

bool SamplerInstrument::ensureInfrastructure()
{
    if (edit_ == nullptr)
        return false;

    // Find or create folder track. After Task 8, SamplerInstrument owns only
    // the folder track; each pad track owns its own MidiClip + SamplerPlugin
    // chain (set up per-pad via ensurePadPatternClip / setupTrackPlugins).
    if (folder_ == nullptr)
    {
        for (auto* track : te::getTopLevelTracks(*edit_))
        {
            if (auto* f = dynamic_cast<te::FolderTrack*>(track))
            {
                if (f->getName() == "Sampler")
                {
                    folder_ = f;
                    break;
                }
            }
        }

        if (folder_ == nullptr)
        {
            folder_ = edit_->insertNewFolderTrack({ nullptr, nullptr }, nullptr, true);
            if (folder_ != nullptr)
                folder_->setName("Sampler");
        }
    }

    return folder_ != nullptr;
}

te::AudioTrack* SamplerInstrument::createPadTrack(int padIndex, const juce::String& name)
{
    if (edit_ == nullptr || folder_ == nullptr)
        return nullptr;

    auto trackPtr = edit_->insertNewAudioTrack({ folder_.get(), nullptr }, nullptr);
    if (trackPtr == nullptr)
        return nullptr;

    trackPtr->setName(name);
    trackPtr->state.setProperty("padIndex", padIndex, nullptr);
    return trackPtr.get();
}

void SamplerInstrument::setupTrackPlugins(PadInfo& pad)
{
    if (pad.track == nullptr || edit_ == nullptr)
        return;

    auto& plugins = pad.track->pluginList;

    // Detect an ExternalPlugin instrument first. If the pad-track was saved
    // with an instrument loaded (via keyboard-mode load), TE rehydrates it
    // as part of the Edit. Adopting it here sets pad.instrument and skips
    // the SamplerPlugin path so we don't end up with both plugins on the
    // track at once (that would sound like two overlapping voices and is
    // what the "pattern seems all weird" symptom looks like).
    pad.instrument = plugins.findFirstPluginOfType<te::ExternalPlugin>();

    // SamplerPlugin — skip creating if the pad already carries an instrument.
    pad.sampler = plugins.findFirstPluginOfType<te::SamplerPlugin>();
    if (pad.sampler == nullptr && pad.instrument == nullptr)
    {
        if (auto p = edit_->getPluginCache().createNewPlugin(te::SamplerPlugin::xmlTypeName, {}))
        {
            plugins.insertPlugin(p, 0, nullptr);
            pad.sampler = dynamic_cast<te::SamplerPlugin*>(p.get());
        }
    }

    // Round-robin is a MIDI router before the sampler, not a timer-driven
    // trigger path. That keeps both live and sequenced hits sample-accurate.
    pad.roundRobinMidi = plugins.findFirstPluginOfType<RoundRobinMidiPlugin>();
    if (pad.sampler != nullptr && pad.roundRobinMidi == nullptr)
    {
        if (auto p = edit_->getPluginCache().createNewPlugin(
                RoundRobinMidiPlugin::xmlTypeName, {}))
        {
            plugins.insertPlugin(p, 0, nullptr);
            pad.roundRobinMidi = dynamic_cast<RoundRobinMidiPlugin*>(p.get());
        }
    }

    // VolumeAndPanPlugin
    pad.volume = plugins.findFirstPluginOfType<te::VolumeAndPanPlugin>();
    if (pad.volume == nullptr)
    {
        if (auto p = edit_->getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::xmlTypeName, {}))
        {
            plugins.insertPlugin(p, -1, nullptr);
            pad.volume = dynamic_cast<te::VolumeAndPanPlugin*>(p.get());
        }
    }

    // LevelMeterPlugin
    pad.meter = plugins.findFirstPluginOfType<te::LevelMeterPlugin>();
    if (pad.meter == nullptr)
    {
        if (auto p = edit_->getPluginCache().createNewPlugin(te::LevelMeterPlugin::xmlTypeName, {}))
        {
            plugins.insertPlugin(p, -1, nullptr);
            pad.meter = dynamic_cast<te::LevelMeterPlugin*>(p.get());
        }
    }

    // Create and register our client with the meter's measurer for real-time level updates
    if (pad.meter != nullptr)
    {
        pad.meterClient = std::make_unique<te::LevelMeasurer::Client>();
        pad.meter->measurer.addClient(*pad.meterClient);
    }
}

void SamplerInstrument::ensurePadPatternClip(PadInfo& pad)
{
    if (pad.patternClip != nullptr || pad.track == nullptr || edit_ == nullptr)
        return;

    bool createdFresh = false;

    // Look for an existing MidiClip on the pad track (from a re-opened Edit).
    for (auto* clip : pad.track->getClips())
    {
        if (auto* mc = dynamic_cast<te::MidiClip*>(clip))
        {
            pad.patternClip = mc;
            break;
        }
    }

    if (pad.patternClip == nullptr)
    {
        // None yet — create a `bars_`-long MidiClip at time 0.
        const auto clipEnd = edit_->tempoSequence.toTime(
            tracktion::core::tempo::BarsAndBeats{ bars_, {} });
        const auto range = tracktion::core::TimeRange{
            tracktion::core::TimePosition{}, clipEnd };

        pad.patternClip = pad.track->insertMIDIClip(range, nullptr);
        createdFresh = true;
    }

    // Tag with the built-in swing groove template so applySwing() can take
    // effect. Only stamp a fresh clip — a reloaded clip already has its saved
    // grooveStrength and we must not clobber it with our default.
    if (pad.patternClip != nullptr && createdFresh)
    {
        if (pad.patternClip->getGrooveTemplate().isEmpty())
            pad.patternClip->setGrooveTemplate("Basic 16th Swing");
        pad.patternClip->setGrooveStrength(swingStrength_);
    }
    else if (pad.patternClip != nullptr && pad.patternClip->getGrooveTemplate().isEmpty())
    {
        // Legacy clips saved before the swing feature — migrate template name
        // but don't touch the saved strength.
        pad.patternClip->setGrooveTemplate("Basic 16th Swing");
    }
}

int SamplerInstrument::addPad(const juce::String& name)
{
    if (!ensureInfrastructure())
        return -1;

    if (static_cast<int>(pads_.size()) >= kMaxPads)
        return -1;

    const int padIndex = static_cast<int>(pads_.size());
    const juce::String padName = name.isEmpty()
        ? juce::String("Pad ") + juce::String(padIndex + 1)
        : name;

    auto* track = createPadTrack(padIndex, padName);
    if (track == nullptr)
        return -1;

    PadInfo pad;
    pad.index = padIndex;
    pad.name = padName;
    pad.midiNote = kBaseMidiNote + padIndex;
    pad.track = track;

    setupTrackPlugins(pad);
    ensurePadPatternClip(pad);

    // Attach the bank to the pad's track so slot-backed operations route
    // through TE's ClipSlotList. Then reserve the already-existing pattern
    // slots in this new pad's bank so createPattern's bookkeeping stays
    // consistent across all pads.
    pad.patternBank.attach(*pad.track);
    for (int i = 0; i < numPatterns_; ++i)
        pad.patternBank.addPattern();

    pads_.push_back(std::move(pad));
    return padIndex;
}

bool SamplerInstrument::loadSample(int padIndex, const juce::File& sampleFile)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return false;

    if (!sampleFile.existsAsFile())
        return false;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.sampler == nullptr)
        return false;

    // Clear any existing sound (invariant: ≤ 1 per pad). Loop is defensive
    // in case a stray sound slips in; the reverse direction is a no-op at
    // this count but keeps the idiom robust if the invariant changes.
    for (int i = pad.sampler->getNumSounds() - 1; i >= 0; --i)
        pad.sampler->removeSound(i);

    // Add new sound
    const auto error = pad.sampler->addSound(
        sampleFile.getFullPathName(),
        sampleFile.getFileNameWithoutExtension(),
        0.0, 0.0, 0.0f
    );

    if (error.isNotEmpty())
    {
        DBG("[SamplerInstrument] Failed to load sample: " + error);
        return false;
    }

    // Configure sound to respond to this pad's MIDI note
    const int soundIndex = pad.sampler->getNumSounds() - 1;
    pad.sampler->setSoundParams(soundIndex, pad.midiNote, pad.midiNote, pad.midiNote);
    pad.sampler->setSoundOpenEnded(
        soundIndex, pad.triggerMode != TriggerMode::HoldEnvelope);

    pad.sampleFile = sampleFile;
    pad.sampleLayers = { SampleLayer { .file = sampleFile } };
    pad.triggerMode = validTriggerModeForLayerCount(pad.triggerMode, 1);
    pad.name = sampleFile.getFileNameWithoutExtension();
    pad.rangeStartSeconds = 0.0;
    pad.rangeEndSeconds = 0.0;
    pad.normalizationGainDb = 0.0f;

    // Cache file metadata so getPadsSnapshot() avoids file I/O
    pad.hasSample = true;
    te::AudioFile audioFile(engine_.getEngine(), sampleFile);
    auto fileInfo = audioFile.getInfo();
    pad.cachedSampleRate = fileInfo.sampleRate;
    pad.cachedLengthInSamples = fileInfo.lengthInSamples;
    pad.cachedLengthSeconds = fileInfo.sampleRate > 0
        ? static_cast<double>(fileInfo.lengthInSamples) / fileInfo.sampleRate : 0.0;

    if (pad.track != nullptr)
        pad.track->setName(pad.name);

    configurePadTriggering(pad, true);

    return true;
}

bool SamplerInstrument::addSampleLayer(int padIndex, const juce::File& sampleFile)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size())
        || ! sampleFile.existsAsFile())
        return false;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (! pad.hasSample || pad.sampler == nullptr
        || static_cast<int>(pad.sampleLayers.size()) >= RoundRobinMidiPlugin::kMaxLayers)
        return false;

    const int layer = static_cast<int>(pad.sampleLayers.size());
    const auto error = pad.sampler->addSound(
        sampleFile.getFullPathName(), sampleFile.getFileNameWithoutExtension(),
        0.0, 0.0, pad.normalizationGainDb);
    if (error.isNotEmpty())
        return false;

    const int soundIndex = pad.sampler->getNumSounds() - 1;
    const int note = pad.midiNote + layer * RoundRobinMidiPlugin::kLayerNoteStride;
    pad.sampler->setSoundParams(soundIndex, note, note, note);
    pad.sampler->setSoundOpenEnded(soundIndex, true);
    pad.sampleLayers.push_back(SampleLayer { .file = sampleFile });

    if (pad.triggerMode == TriggerMode::VelocityRoundRobinRandom)
        pad.triggerMode = TriggerMode::RoundRobinRandom;
    else if (pad.triggerMode != TriggerMode::RoundRobinRandom)
        pad.triggerMode = TriggerMode::RoundRobinOrdered;
    configurePadTriggering(pad, true);
    return true;
}

bool SamplerInstrument::removeLastSampleLayer(int padIndex)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return false;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.sampler == nullptr || pad.sampleLayers.size() <= 1)
        return false;

    pad.sampler->removeSound(pad.sampler->getNumSounds() - 1);
    pad.sampleLayers.pop_back();
    pad.triggerMode = validTriggerModeForLayerCount(
        pad.triggerMode, static_cast<int>(pad.sampleLayers.size()));
    configurePadTriggering(pad, true);
    return true;
}

void SamplerInstrument::clearSample(int padIndex)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.sampler != nullptr)
    {
        // Clear any existing sound (invariant: ≤ 1 per pad). See loadSample
        // for the same pattern — defensive reverse iteration in case the
        // per-pad invariant ever grows beyond one sound.
        for (int i = pad.sampler->getNumSounds() - 1; i >= 0; --i)
            pad.sampler->removeSound(i);
    }

    pad.sampleFile = juce::File();
    pad.sampleLayers.clear();
    pad.name = "Pad " + juce::String(padIndex + 1);
    pad.rangeStartSeconds = 0.0;
    pad.rangeEndSeconds = 0.0;
    pad.normalizationGainDb = 0.0f;
    pad.hasSample = false;
    pad.cachedSampleRate = 0.0;
    pad.cachedLengthInSamples = 0;
    pad.cachedLengthSeconds = 0.0;

    configurePadTriggering(pad, true);

    if (pad.track != nullptr)
        pad.track->setName(pad.name);
}

//==============================================================================
// Raw variants (undo-safe — do not re-enter the UndoManager)
//==============================================================================
//
// SamplerPlugin::addSound/removeSound each push to the plugin's UndoManager
// (the Edit's UM). Inside juce::UndoManager::undo(), any nested
// UndoManager::perform() is silently blocked by isPerformingUndoRedo() — so
// state.removeChild(um) becomes a no-op, and the caller's
// `while (getNumSounds() > 0) removeSound()` loop spins forever.
//
// These raw variants manipulate the SamplerPlugin's ValueTree directly with
// a null UndoManager, so they can be called from UndoableAction::perform()
// and undo(). The C++ cache is kept in sync exactly like the non-raw
// variants. Track::setName is skipped (it also routes through a CachedValue
// that takes the Edit's UM and would deadlock the same way).

bool SamplerInstrument::loadSampleRaw(int padIndex, const juce::File& sampleFile)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return false;
    if (!sampleFile.existsAsFile())
        return false;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.sampler == nullptr)
        return false;

    auto state = pad.sampler->state;

    // Remove any existing SOUND children, no UM.
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
    {
        auto child = state.getChild(i);
        if (child.hasType(te::IDs::SOUND))
            state.removeChild(i, nullptr);
    }

    // Build a new SOUND child mirroring SamplerPlugin::addSound + setSoundParams +
    // setSoundOpenEnded, with a null UndoManager on every property.
    juce::ValueTree sound(te::IDs::SOUND);
    sound.setProperty(te::IDs::source,    sampleFile.getFullPathName(), nullptr);
    sound.setProperty(te::IDs::name,      sampleFile.getFileNameWithoutExtension(), nullptr);
    sound.setProperty(te::IDs::startTime, 0.0, nullptr);
    sound.setProperty(te::IDs::length,    0.0, nullptr);
    sound.setProperty(te::IDs::keyNote,   juce::jlimit(midi::kNoteMin, midi::kNoteMax, pad.midiNote), nullptr);
    sound.setProperty(te::IDs::minNote,   juce::jlimit(midi::kNoteMin, midi::kNoteMax, pad.midiNote), nullptr);
    sound.setProperty(te::IDs::maxNote,   juce::jlimit(midi::kNoteMin, midi::kNoteMax, pad.midiNote), nullptr);
    sound.setProperty(te::IDs::gainDb,    0.0f, nullptr);
    sound.setProperty(te::IDs::pan,       0.0, nullptr);
    sound.setProperty(te::IDs::openEnded,
                      pad.triggerMode != TriggerMode::HoldEnvelope, nullptr);
    state.addChild(sound, -1, nullptr);

    // C++ cache (exactly as loadSample does).
    pad.sampleFile = sampleFile;
    pad.sampleLayers = { SampleLayer { .file = sampleFile } };
    pad.triggerMode = validTriggerModeForLayerCount(pad.triggerMode, 1);
    pad.name = sampleFile.getFileNameWithoutExtension();
    pad.rangeStartSeconds = 0.0;
    pad.rangeEndSeconds = 0.0;
    pad.normalizationGainDb = 0.0f;
    pad.hasSample = true;

    te::AudioFile audioFile(engine_.getEngine(), sampleFile);
    auto fileInfo = audioFile.getInfo();
    pad.cachedSampleRate = fileInfo.sampleRate;
    pad.cachedLengthInSamples = fileInfo.lengthInSamples;
    pad.cachedLengthSeconds = fileInfo.sampleRate > 0
        ? static_cast<double>(fileInfo.lengthInSamples) / fileInfo.sampleRate : 0.0;

    configurePadTriggering(pad, false);

    return true;
}

bool SamplerInstrument::addSampleLayerRaw(int padIndex,
                                          const juce::File& sampleFile)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size())
        || ! sampleFile.existsAsFile())
        return false;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (! pad.hasSample || pad.sampler == nullptr
        || static_cast<int>(pad.sampleLayers.size()) >= RoundRobinMidiPlugin::kMaxLayers)
        return false;

    const int layer = static_cast<int>(pad.sampleLayers.size());
    const int note = pad.midiNote + layer * RoundRobinMidiPlugin::kLayerNoteStride;
    juce::ValueTree sound(te::IDs::SOUND);
    sound.setProperty(te::IDs::source, sampleFile.getFullPathName(), nullptr);
    sound.setProperty(te::IDs::name, sampleFile.getFileNameWithoutExtension(), nullptr);
    sound.setProperty(te::IDs::startTime, 0.0, nullptr);
    sound.setProperty(te::IDs::length, 0.0, nullptr);
    sound.setProperty(te::IDs::keyNote, note, nullptr);
    sound.setProperty(te::IDs::minNote, note, nullptr);
    sound.setProperty(te::IDs::maxNote, note, nullptr);
    sound.setProperty(te::IDs::gainDb, pad.normalizationGainDb, nullptr);
    sound.setProperty(te::IDs::pan, 0.0, nullptr);
    sound.setProperty(te::IDs::openEnded, true, nullptr);
    pad.sampler->state.addChild(sound, -1, nullptr);
    pad.sampleLayers.push_back(SampleLayer { .file = sampleFile });

    if (pad.triggerMode == TriggerMode::VelocityRoundRobinRandom)
        pad.triggerMode = TriggerMode::RoundRobinRandom;
    else if (pad.triggerMode != TriggerMode::RoundRobinRandom)
        pad.triggerMode = TriggerMode::RoundRobinOrdered;
    configurePadTriggering(pad, false);
    return true;
}

void SamplerInstrument::clearSampleRaw(int padIndex)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.sampler != nullptr)
    {
        auto state = pad.sampler->state;
        for (int i = state.getNumChildren() - 1; i >= 0; --i)
        {
            auto child = state.getChild(i);
            if (child.hasType(te::IDs::SOUND))
                state.removeChild(i, nullptr);
        }
    }

    pad.sampleFile = juce::File();
    pad.sampleLayers.clear();
    pad.name = "Pad " + juce::String(padIndex + 1);
    pad.rangeStartSeconds = 0.0;
    pad.rangeEndSeconds = 0.0;
    pad.normalizationGainDb = 0.0f;
    pad.hasSample = false;
    pad.cachedSampleRate = 0.0;
    pad.cachedLengthInSamples = 0;
    pad.cachedLengthSeconds = 0.0;
    configurePadTriggering(pad, false);
}

bool SamplerInstrument::setPadSampleRangeRaw(int padId, double startSeconds, double endSeconds)
{
    if (padId < 0 || padId >= static_cast<int>(pads_.size()))
        return false;

    auto& pad = pads_[static_cast<size_t>(padId)];
    if (!pad.hasSample || pad.cachedLengthSeconds <= 0.0)
        return false;

    const double totalLengthSeconds = pad.cachedLengthSeconds;
    const double clampedStart = juce::jlimit(0.0, totalLengthSeconds, startSeconds);
    const double clampedEnd = juce::jlimit(clampedStart, totalLengthSeconds,
                                            endSeconds > 0.0 ? endSeconds : totalLengthSeconds);
    if (clampedEnd <= clampedStart)
        return false;

    pad.rangeStartSeconds = clampedStart;
    pad.rangeEndSeconds = clampedEnd;
    applyPadPlaybackPropertiesRaw(pad);
    return true;
}

bool SamplerInstrument::setPadSampleNormalizationGainDbRaw(int padId, float gainDb)
{
    if (padId < 0 || padId >= static_cast<int>(pads_.size()))
        return false;

    auto& pad = pads_[static_cast<size_t>(padId)];
    if (!pad.hasSample)
        return false;

    pad.normalizationGainDb = gainDb;
    applyPadPlaybackPropertiesRaw(pad);
    return true;
}

void SamplerInstrument::applyPadPlaybackPropertiesRaw(PadInfo& pad)
{
    if (!pad.hasSample || pad.cachedLengthSeconds <= 0.0)
        return;

    const double totalLengthSeconds = pad.cachedLengthSeconds;
    const double excerptStart = juce::jlimit(0.0, totalLengthSeconds, pad.rangeStartSeconds);
    const double excerptEnd = juce::jlimit(excerptStart, totalLengthSeconds,
                                            pad.rangeEndSeconds > 0.0 ? pad.rangeEndSeconds : totalLengthSeconds);
    const double excerptLength = excerptEnd - excerptStart;
    if (excerptLength <= 0.0)
        return;

    auto writeSoundPropsRaw = [&](te::SamplerPlugin* plugin, int soundIndex)
    {
        if (plugin == nullptr || soundIndex < 0)
            return;
        auto sound = plugin->state.getChild(soundIndex);
        if (!sound.hasType(te::IDs::SOUND))
            return;
        const float layerGain = juce::isPositiveAndBelow(
            soundIndex, static_cast<int>(pad.sampleLayers.size()))
            ? pad.sampleLayers[static_cast<size_t>(soundIndex)].gainDb
            : 0.0f;
        sound.setProperty(te::IDs::startTime, excerptStart,         nullptr);
        sound.setProperty(te::IDs::length,    excerptLength,        nullptr);
        sound.setProperty(te::IDs::gainDb,
                          pad.normalizationGainDb + layerGain,
                          nullptr);
        sound.setProperty(te::IDs::pan,       0.0f,                 nullptr);
    };

    if (pad.sampler != nullptr && pad.sampler->getNumSounds() > 0)
    {
        writeSoundPropsRaw(pad.sampler, 0);
        for (int i = 1; i < pad.sampler->getNumSounds(); ++i)
        {
            if (! juce::isPositiveAndBelow(
                    i, static_cast<int>(pad.sampleLayers.size())))
                break;
            auto sound = pad.sampler->state.getChild(i);
            if (sound.hasType(te::IDs::SOUND))
                sound.setProperty(te::IDs::gainDb,
                                  pad.normalizationGainDb
                                      + pad.sampleLayers[static_cast<size_t>(i)].gainDb,
                                  nullptr);
        }
    }
}

const SamplerInstrument::PadInfo* SamplerInstrument::getPad(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return nullptr;
    return &pads_[static_cast<size_t>(padIndex)];
}

SamplerInstrument::PadInfo* SamplerInstrument::getPad(int padIndex)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return nullptr;
    return &pads_[static_cast<size_t>(padIndex)];
}

bool SamplerInstrument::hasInstrument(int padIndex) const
{
    if (const auto* p = getPad(padIndex))
        return p->instrument != nullptr;
    return false;
}

te::ExternalPlugin* SamplerInstrument::getPadInstrument(int padIndex) const
{
    if (const auto* p = getPad(padIndex))
        return p->instrument;
    return nullptr;
}

bool SamplerInstrument::setPadInstrumentRaw(int padIndex,
                                           const juce::PluginDescription& desc)
{
    auto* pad = getPad(padIndex);
    if (!pad || pad->track == nullptr || edit_ == nullptr)
        return false;

    // Find the slot the SamplerPlugin occupies; fall back to 0.
    int samplerSlot = 0;
    auto plugins = pad->track->pluginList.getPlugins();
    for (int i = 0; i < plugins.size(); ++i)
    {
        if (dynamic_cast<RoundRobinMidiPlugin*>(plugins[i].get())
            || dynamic_cast<te::SamplerPlugin*>(plugins[i].get()))
        {
            samplerSlot = i;
            break;
        }
    }

    auto plugin = edit_->getPluginCache().createNewPlugin(
        te::ExternalPlugin::xmlTypeName, desc);
    if (plugin == nullptr)
        return false;

    auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get());
    if (ext == nullptr)
        return false;

    juce::ValueTree pluginParent;
    if (pad->sampler != nullptr)
        pluginParent = pad->sampler->state.getParent();
    else if (pad->roundRobinMidi != nullptr)
        pluginParent = pad->roundRobinMidi->state.getParent();
    else if (pad->instrument != nullptr)
        pluginParent = pad->instrument->state.getParent();
    const auto existingPlugins = pad->track->pluginList.getPlugins();
    const int treeIndex = juce::isPositiveAndBelow(
        samplerSlot, existingPlugins.size())
        ? pluginParent.indexOf(existingPlugins[samplerSlot]->state)
        : -1;
    if (! pluginParent.isValid())
        return false;

    // Remove the SamplerPlugin first so the new instrument lands in the same
    // slot. deleteFromParent shifts following indices down by 1.
    if (pad->sampler != nullptr)
    {
        removePluginRaw(pad->sampler);
        pad->sampler = nullptr;
    }
    if (pad->roundRobinMidi != nullptr)
    {
        removePluginRaw(pad->roundRobinMidi);
        pad->roundRobinMidi = nullptr;
    }

    pluginParent.addChild(plugin->state, treeIndex, nullptr);
    pad->instrument = ext;

    // Rename the pad + track to the instrument name so the sequencer row +
    // mixer strip read correctly (the default "Pad N" label is only useful
    // for sample pads). Restored on clearPadInstrumentRaw.
    const auto instName = ext->getName();
    if (instName.isNotEmpty())
    {
        pad->name = instName;
        pad->track->state.setProperty(te::IDs::name, instName, nullptr);
    }
    return true;
}

bool SamplerInstrument::clearPadInstrumentRaw(int padIndex)
{
    auto* pad = getPad(padIndex);
    if (!pad || pad->instrument == nullptr || pad->track == nullptr)
        return false;

    auto pluginParent = pad->instrument->state.getParent();
    const int treeIndex = pluginParent.indexOf(pad->instrument->state);
    if (! pluginParent.isValid() || treeIndex < 0)
        return false;

    removePluginRaw(pad->instrument);
    pad->instrument = nullptr;

    // Restore the default pad label + track name (the setPadInstrumentRaw
    // path renamed them to the instrument's name).
    const auto defaultName = juce::String("Pad ") + juce::String(padIndex + 1);
    pad->name = defaultName;
    pad->track->state.setProperty(te::IDs::name, defaultName, nullptr);

    // Restore the MIDI router + SamplerPlugin pair in the slot the instrument
    // held so round-robin projects remain structurally consistent.
    if (auto rr = edit_->getPluginCache().createNewPlugin(
            RoundRobinMidiPlugin::xmlTypeName, {}))
    {
        pluginParent.addChild(rr->state, treeIndex, nullptr);
        pad->roundRobinMidi = dynamic_cast<RoundRobinMidiPlugin*>(rr.get());
    }
    if (auto sp = edit_->getPluginCache().createNewPlugin(
            te::SamplerPlugin::xmlTypeName, {}))
    {
        const int samplerTreeIndex = pad->roundRobinMidi != nullptr
            ? pluginParent.indexOf(pad->roundRobinMidi->state) + 1
            : treeIndex;
        pluginParent.addChild(sp->state, samplerTreeIndex, nullptr);
        pad->sampler = dynamic_cast<te::SamplerPlugin*>(sp.get());
    }
    configurePadTriggering(*pad, false);
    return true;
}

void SamplerInstrument::trigger(int padIndex, float velocity)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.sampler == nullptr && pad.track == nullptr)
        return;

    applyChokeGroup(padIndex);
    triggerPadTrack(pad, velocity);

    listeners_.call(&Listener::padTriggered, padIndex);
}

void SamplerInstrument::releasePad(int padIndex)
{
    if (getTriggerMode(padIndex) == TriggerMode::HoldEnvelope)
        stopPad(padIndex);
}

bool SamplerInstrument::triggerSampleLayer(int padIndex, int layerIndex,
                                            float velocity)
{
    auto* pad = getPad(padIndex);
    if (pad == nullptr || pad->track == nullptr
        || ! juce::isPositiveAndBelow(
            layerIndex, static_cast<int>(pad->sampleLayers.size())))
        return false;

    applyChokeGroup(padIndex);
    const auto& layer = pad->sampleLayers[static_cast<size_t>(layerIndex)];
    const float curvedVelocity = applyLayerVelocityCurve(
        velocity, layer.velocityCurve);
    const int midiVelocity = juce::jlimit(
        midi::kVelocityLiveMin, midi::kVelocityMax,
        juce::roundToInt(curvedVelocity * 127.0f));
    const int note = pad->midiNote
                   + layerIndex * RoundRobinMidiPlugin::kLayerNoteStride;
    // Channel 16 is reserved for explicit layer audition. The round-robin
    // router leaves that channel untouched, which disambiguates Layer 1's
    // canonical base note from an ordinary pad hit that should be routed.
    pad->track->playGuideNote(
        note, te::MidiChannel(RoundRobinMidiPlugin::kAuditionMidiChannel),
        midiVelocity,
                              true, true, false);
    listeners_.call(&Listener::padTriggered, padIndex);
    return true;
}

void SamplerInstrument::releaseSampleLayer(int padIndex, int layerIndex)
{
    auto* pad = getPad(padIndex);
    if (pad == nullptr
        || ! juce::isPositiveAndBelow(
            layerIndex, static_cast<int>(pad->sampleLayers.size())))
        return;

    if (pad->triggerMode == TriggerMode::HoldEnvelope)
        stopPad(padIndex);
}

void SamplerInstrument::stopPad(int padIndex)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.sampler != nullptr)
        pad.sampler->allNotesOff();
    if (pad.track != nullptr)
        pad.track->turnOffGuideNotes(te::MidiChannel(1));

    listeners_.call(&Listener::padStopped, padIndex);
}

void SamplerInstrument::stopAll()
{
    for (auto& pad : pads_)
    {
        if (pad.sampler != nullptr)
            pad.sampler->allNotesOff();
        if (pad.track != nullptr)
            pad.track->turnOffGuideNotes(te::MidiChannel(1));
    }
}

void SamplerInstrument::setTriggerMode(int padIndex, TriggerMode mode)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Direct variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    mode = validTriggerModeForLayerCount(
        mode, static_cast<int>(pad.sampleLayers.size()));
    if (pad.triggerMode == mode)
        return;
    pad.triggerMode = mode;
    configurePadTriggering(pad, true);
}

void SamplerInstrument::setTriggerModeDirect(int padIndex, TriggerMode mode)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;
    auto& pad = pads_[static_cast<size_t>(padIndex)];
    pad.triggerMode = validTriggerModeForLayerCount(
        mode, static_cast<int>(pad.sampleLayers.size()));
    configurePadTriggering(pad, false);
}

SamplerInstrument::TriggerMode SamplerInstrument::getTriggerMode(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return TriggerMode::HoldEnvelope;
    return pads_[static_cast<size_t>(padIndex)].triggerMode;
}

int SamplerInstrument::getSampleLayerCount(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return 0;
    return static_cast<int>(pads_[static_cast<size_t>(padIndex)].sampleLayers.size());
}

bool SamplerInstrument::setSampleLayerGainDb(int padIndex, int layerIndex,
                                             float gainDb)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    auto* pad = getPad(padIndex);
    if (pad == nullptr || pad->sampler == nullptr
        || ! juce::isPositiveAndBelow(layerIndex,
                                      static_cast<int>(pad->sampleLayers.size())))
        return false;

    auto& layer = pad->sampleLayers[static_cast<size_t>(layerIndex)];
    layer.gainDb = juce::jlimit(-48.0f, 24.0f, gainDb);
    pad->sampler->setSoundGains(
        layerIndex, layer.gainDb + pad->normalizationGainDb, 0.0f);
    configurePadTriggering(*pad, true);
    return true;
}

bool SamplerInstrument::setSampleLayerGainDbRaw(int padIndex, int layerIndex,
                                                float gainDb)
{
    auto* pad = getPad(padIndex);
    if (pad == nullptr || pad->sampler == nullptr
        || ! juce::isPositiveAndBelow(layerIndex,
                                      static_cast<int>(pad->sampleLayers.size())))
        return false;

    pad->sampleLayers[static_cast<size_t>(layerIndex)].gainDb =
        juce::jlimit(-48.0f, 24.0f, gainDb);
    applyPadPlaybackPropertiesRaw(*pad);
    configurePadTriggering(*pad, false);
    return true;
}

bool SamplerInstrument::setSampleLayerRandomWeight(int padIndex, int layerIndex,
                                                    float weight)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    auto* pad = getPad(padIndex);
    if (pad == nullptr
        || ! juce::isPositiveAndBelow(layerIndex,
                                      static_cast<int>(pad->sampleLayers.size())))
        return false;
    pad->sampleLayers[static_cast<size_t>(layerIndex)].randomWeight =
        juce::jlimit(0.0f, 1.0f, weight);
    configurePadTriggering(*pad, true);
    return true;
}

bool SamplerInstrument::setSampleLayerRandomWeightRaw(int padIndex, int layerIndex,
                                                       float weight)
{
    auto* pad = getPad(padIndex);
    if (pad == nullptr
        || ! juce::isPositiveAndBelow(layerIndex,
                                      static_cast<int>(pad->sampleLayers.size())))
        return false;
    pad->sampleLayers[static_cast<size_t>(layerIndex)].randomWeight =
        juce::jlimit(0.0f, 1.0f, weight);
    configurePadTriggering(*pad, false);
    return true;
}

bool SamplerInstrument::setSampleLayerVelocityCurve(
    int padIndex, int layerIndex, LayerVelocityCurve curve)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    auto* pad = getPad(padIndex);
    if (pad == nullptr
        || ! juce::isPositiveAndBelow(layerIndex,
                                      static_cast<int>(pad->sampleLayers.size())))
        return false;
    pad->sampleLayers[static_cast<size_t>(layerIndex)].velocityCurve = curve;
    configurePadTriggering(*pad, true);
    return true;
}

bool SamplerInstrument::setSampleLayerVelocityCurveRaw(
    int padIndex, int layerIndex, LayerVelocityCurve curve)
{
    auto* pad = getPad(padIndex);
    if (pad == nullptr
        || ! juce::isPositiveAndBelow(layerIndex,
                                      static_cast<int>(pad->sampleLayers.size())))
        return false;
    pad->sampleLayers[static_cast<size_t>(layerIndex)].velocityCurve = curve;
    configurePadTriggering(*pad, false);
    return true;
}

bool SamplerInstrument::setSampleLayerVelocityRange(
    int padIndex, int layerIndex, float minimum, float maximum)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    auto* pad = getPad(padIndex);
    if (pad == nullptr
        || ! juce::isPositiveAndBelow(
            layerIndex, static_cast<int>(pad->sampleLayers.size())))
        return false;
    auto& layer = pad->sampleLayers[static_cast<size_t>(layerIndex)];
    layer.velocityMinimum = juce::jlimit(0.01f, 1.0f, minimum);
    layer.velocityMaximum = juce::jlimit(
        layer.velocityMinimum, 1.0f, maximum);
    configurePadTriggering(*pad, true);
    return true;
}

bool SamplerInstrument::setSampleLayerVelocityRangeRaw(
    int padIndex, int layerIndex, float minimum, float maximum)
{
    auto* pad = getPad(padIndex);
    if (pad == nullptr
        || ! juce::isPositiveAndBelow(
            layerIndex, static_cast<int>(pad->sampleLayers.size())))
        return false;
    auto& layer = pad->sampleLayers[static_cast<size_t>(layerIndex)];
    layer.velocityMinimum = juce::jlimit(0.01f, 1.0f, minimum);
    layer.velocityMaximum = juce::jlimit(
        layer.velocityMinimum, 1.0f, maximum);
    configurePadTriggering(*pad, false);
    return true;
}

juce::String SamplerInstrument::triggerModeName(TriggerMode mode)
{
    switch (mode)
    {
        case TriggerMode::HoldEnvelope:      return "Hold/Env";
        case TriggerMode::OneShot:           return "One-shot";
        case TriggerMode::RoundRobinOrdered: return "RR Ordered";
        case TriggerMode::RoundRobinRandom:  return "RR Random";
        case TriggerMode::VelocityRoundRobinOrdered: return "Vel RR Ord";
        case TriggerMode::VelocityRoundRobinRandom:  return "Vel RR Rand";
    }
    return "Hold/Env";
}

juce::String SamplerInstrument::velocityCurveName(LayerVelocityCurve curve)
{
    switch (curve)
    {
        case LayerVelocityCurve::Linear: return "Linear";
        case LayerVelocityCurve::Soft:   return "Soft";
        case LayerVelocityCurve::Hard:   return "Hard";
        case LayerVelocityCurve::Fixed:  return "Fixed";
    }
    return "Linear";
}

void SamplerInstrument::configurePadTriggering(PadInfo& pad, bool useUndoManager)
{
    const bool openEnded = pad.triggerMode != TriggerMode::HoldEnvelope;
    if (pad.sampler != nullptr)
    {
        for (int i = 0; i < pad.sampler->getNumSounds(); ++i)
        {
            if (useUndoManager)
                pad.sampler->setSoundOpenEnded(i, openEnded);
            else
            {
                auto sound = pad.sampler->state.getChild(i);
                if (sound.hasType(te::IDs::SOUND))
                    sound.setProperty(te::IDs::openEnded, openEnded, nullptr);
            }
        }
    }

    if (pad.roundRobinMidi == nullptr)
        return;

    auto policy = RoundRobinMidiPlugin::Policy::Disabled;
    if (pad.triggerMode == TriggerMode::RoundRobinOrdered)
        policy = RoundRobinMidiPlugin::Policy::Ordered;
    else if (pad.triggerMode == TriggerMode::RoundRobinRandom)
        policy = RoundRobinMidiPlugin::Policy::Random;
    else if (pad.triggerMode == TriggerMode::VelocityRoundRobinOrdered)
        policy = RoundRobinMidiPlugin::Policy::Ordered;
    else if (pad.triggerMode == TriggerMode::VelocityRoundRobinRandom)
        policy = RoundRobinMidiPlugin::Policy::Random;

    const int layers = juce::jmax(1, static_cast<int>(pad.sampleLayers.size()));
    std::array<float, RoundRobinMidiPlugin::kMaxLayers> weights {};
    std::array<int, RoundRobinMidiPlugin::kMaxLayers> curves {};
    weights.fill(1.0f);
    curves.fill(static_cast<int>(LayerVelocityCurve::Linear));
    for (size_t i = 0; i < pad.sampleLayers.size() && i < weights.size(); ++i)
    {
        weights[i] = pad.sampleLayers[i].randomWeight;
        curves[i] = static_cast<int>(pad.sampleLayers[i].velocityCurve);
    }
    if (useUndoManager)
    {
        pad.roundRobinMidi->configure(pad.midiNote, layers, policy);
        pad.roundRobinMidi->setLayerMix(weights, curves);
        if (! pad.sampleLayers.empty())
            pad.roundRobinMidi->setSingleLayerVelocityRange(
                pad.sampleLayers.front().velocityMinimum,
                pad.sampleLayers.front().velocityMaximum);
    }
    else
    {
        pad.roundRobinMidi->configureRaw(pad.midiNote, layers, policy);
        pad.roundRobinMidi->setLayerMixRaw(weights, curves);
        if (! pad.sampleLayers.empty())
            pad.roundRobinMidi->setSingleLayerVelocityRangeRaw(
                pad.sampleLayers.front().velocityMinimum,
                pad.sampleLayers.front().velocityMaximum);
    }
}

void SamplerInstrument::applyChokeGroup(int triggeredPadIndex)
{
    if (triggeredPadIndex < 0 || triggeredPadIndex >= static_cast<int>(pads_.size()))
        return;

    // chokeGroup sentinel convention:
    //   0       — no choke (pad does not interrupt any other pads)
    //   negative — self-choke (mono mode: retrigger cuts this pad's own notes)
    //   positive — mutual-choke group id (all pads with the same positive value
    //              silence each other on trigger)
    const int group = pads_[static_cast<size_t>(triggeredPadIndex)].chokeGroup;
    if (group == 0)
        return;

    // Self-choke: silence this pad's own in-flight notes so the new trigger is mono.
    if (group < 0)
    {
        auto& self = pads_[static_cast<size_t>(triggeredPadIndex)];
        if (self.sampler != nullptr)
            self.sampler->allNotesOff();
        if (self.track != nullptr)
            self.track->turnOffGuideNotes(te::MidiChannel(1));
        return;
    }

    // Index-bounded: iterate only members of this choke group instead of all pads.
    if (group < 1 || group >= static_cast<int>(chokeGroupIndex_.size()))
        return;

    for (int padIndex : chokeGroupIndex_[static_cast<size_t>(group)])
    {
        if (padIndex == triggeredPadIndex)
            continue;
        if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
            continue;

        auto& other = pads_[static_cast<size_t>(padIndex)];
        if (other.sampler != nullptr)
            other.sampler->allNotesOff();
        if (other.track != nullptr)
            other.track->turnOffGuideNotes(te::MidiChannel(1));
    }
}

void SamplerInstrument::rebuildChokeGroupIndex()
{
    for (auto& bucket : chokeGroupIndex_) bucket.clear();
    for (size_t i = 0; i < pads_.size(); ++i)
    {
        const int g = pads_[i].chokeGroup;
        if (g >= 1 && g < static_cast<int>(chokeGroupIndex_.size()))
            chokeGroupIndex_[static_cast<size_t>(g)].push_back(static_cast<int>(i));
    }
}

void SamplerInstrument::updateChokeGroupIndex(int padIndex, int oldGroup, int newGroup)
{
    if (oldGroup == newGroup) return;
    if (oldGroup >= 1 && oldGroup < static_cast<int>(chokeGroupIndex_.size()))
    {
        auto& bucket = chokeGroupIndex_[static_cast<size_t>(oldGroup)];
        bucket.erase(std::remove(bucket.begin(), bucket.end(), padIndex), bucket.end());
    }
    if (newGroup >= 1 && newGroup < static_cast<int>(chokeGroupIndex_.size()))
        chokeGroupIndex_[static_cast<size_t>(newGroup)].push_back(padIndex);
}

void SamplerInstrument::triggerPadTrack(PadInfo& pad, float velocity)
{
    const auto midiVelocity = juce::jlimit(midi::kVelocityLiveMin, midi::kVelocityMax, juce::roundToInt(velocity * 127.0f));

    if (pad.track != nullptr)
    {
        // Hardware pads provide real press/release edges. Keep the guide note
        // alive until stopPad() handles the release instead of Tracktion's
        // 100 ms autorelease timer.
        pad.track->playGuideNote(pad.midiNote, te::MidiChannel(1), midiVelocity,
                                 true, true, false);
        return;
    }

    if (pad.sampler != nullptr)
    {
        pad.sampler->allNotesOff();

        juce::BigInteger notes;
        notes.setBit(pad.midiNote);
        pad.sampler->playNotes(notes);
    }
}

void SamplerInstrument::setGainDb(int padIndex, float db)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    const float clamped = juce::jlimit(-48.0f, 12.0f, db);
    if (pad.volume != nullptr)
        pad.volume->setVolumeDb(clamped);
}

void SamplerInstrument::setGainDbRaw(int padIndex, float db)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.volume == nullptr)
        return;

    const float clamped = juce::jlimit(-48.0f, 12.0f, db);
    const float sliderPosition = te::decibelsToVolumeFaderPosition(clamped);
    pad.volume->volParam->setParameter(
        sliderPosition, juce::dontSendNotification);
    pad.volume->state.setProperty(
        te::IDs::volume, sliderPosition, nullptr);
}

float SamplerInstrument::getGainDb(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return 0.0f;

    const auto& pad = pads_[static_cast<size_t>(padIndex)];
    return pad.volume != nullptr ? pad.volume->getVolumeDb() : 0.0f;
}

void SamplerInstrument::setMute(int padIndex, bool mute)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;
    if (auto* track = pads_[static_cast<size_t>(padIndex)].track)
        track->setMute(mute);
}

bool SamplerInstrument::isMuted(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return false;
    if (const auto* track = pads_[static_cast<size_t>(padIndex)].track)
        return track->isMuted(false);
    return false;
}

void SamplerInstrument::setSolo(int padIndex, bool solo)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;
    if (auto* track = pads_[static_cast<size_t>(padIndex)].track)
        track->setSolo(solo);
}

bool SamplerInstrument::isSoloed(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return false;
    if (const auto* track = pads_[static_cast<size_t>(padIndex)].track)
        return track->isSolo(false);
    return false;
}

float SamplerInstrument::getLevelDb(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return -100.0f;

    const auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pad.meter == nullptr)
        return -100.0f;

    // Read the same client data used by getPadsSnapshot(). The plugin's
    // levelCache is owned by Tracktion's external-controller path and is not
    // populated for these per-pad meters.
    if (pad.meterClient == nullptr)
        return -100.0f;

    const auto left = pad.meterClient->getAndClearAudioLevel(0).dB;
    const auto right = pad.meterClient->getAndClearAudioLevel(1).dB;
    return std::max(left, right);
}

void SamplerInstrument::setChokeGroup(int padIndex, int group)
{
    // Raw contract: this entry point creates an undo action. Commands must
    // call setChokeGroupDirect instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    const int oldGroup = pads_[static_cast<size_t>(padIndex)].chokeGroup;
    const int newGroup = juce::jlimit(-1, 8, group);

    if (oldGroup == newGroup)
        return;

    auto& undoManager = engine_.getUndoManager();
    undoManager.beginNewTransaction("Set Choke Group");
    undoManager.perform(new SetChokeGroupCommand(*this, padIndex, oldGroup, newGroup));
}

void SamplerInstrument::setChokeGroupDirect(int padIndex, int group)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    const int oldGroup = pad.chokeGroup;
    const int newGroup = juce::jlimit(-1, 8, group);
    pad.chokeGroup = newGroup;
    updateChokeGroupIndex(padIndex, oldGroup, newGroup);
}

int SamplerInstrument::getChokeGroup(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return 0;

    return pads_[static_cast<size_t>(padIndex)].chokeGroup;
}

void SamplerInstrument::selectPad(int padIndex)
{
    if (padIndex >= 0 && padIndex < static_cast<int>(pads_.size()))
        selectedPad_ = padIndex;
    else
        selectedPad_ = -1;
}

te::AudioTrack* SamplerInstrument::getTrack(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return nullptr;

    return pads_[static_cast<size_t>(padIndex)].track;
}

int SamplerInstrument::createPattern(const juce::String& /*name*/)
{
    // Reserve a slot in each pad's ClipSlot bank so the new index maps 1:1
    // to a bank slot. The slot is empty until a switch away from the active
    // pattern serialises the live clip into it.
    for (auto& pad : pads_)
        pad.patternBank.addPattern();

    // TE clamps per-track slot counts to the Edit's scene count on reload
    // (see AudioTrack::valueTreeParentChanged). Keep the scene list at
    // least as wide as the bank so all slots round-trip through save/load.
    if (edit_ != nullptr)
        edit_->getSceneList().ensureNumberOfScenes(numPatterns_ + 1);

    const int newIndex = numPatterns_;
    ++numPatterns_;
    patternNames_.emplace_back();
    return newIndex;
}

juce::String SamplerInstrument::getDefaultPatternName(int patternIndex) const
{
    return juce::String("Pattern ") + juce::String(patternIndex + 1);
}

juce::String SamplerInstrument::getPatternName(int patternIndex) const
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return getDefaultPatternName(patternIndex);
    const auto idx = static_cast<size_t>(patternIndex);
    if (idx < patternNames_.size() && patternNames_[idx].isNotEmpty())
        return patternNames_[idx];
    return getDefaultPatternName(patternIndex);
}

void SamplerInstrument::setPatternName(int patternIndex, const juce::String& name)
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return;
    if (patternNames_.size() < static_cast<size_t>(numPatterns_))
        patternNames_.resize(static_cast<size_t>(numPatterns_));
    patternNames_[static_cast<size_t>(patternIndex)] = name.trim();
}

juce::String SamplerInstrument::getPadName(int padIndex) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return {};
    return pads_[static_cast<size_t>(padIndex)].name;
}

void SamplerInstrument::setPadName(int padIndex, const juce::String& name)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    const auto trimmed = name.trim();
    pad.name = trimmed.isEmpty()
        ? juce::String("Pad ") + juce::String(padIndex + 1)
        : trimmed;

    // Mirror onto the TE track label so serialization paths + any
    // track-name-driven UI stay in sync (matches loadSample/clearSample).
    if (pad.track != nullptr)
        pad.track->setName(pad.name);
}

bool SamplerInstrument::setStep(int patternIndex, int padIndex, int step, bool enabled)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return false;

    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return false;

    const int numSteps = getStepCount();
    if (!juce::isPositiveAndBelow(step, numSteps))
        return false;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    const int velocity = enabled ? 83 : 0; // 0.65 * 127 — matches historical StepClip default

    if (patternIndex == activePatternIndex_)
    {
        if (pad.patternClip == nullptr)
            return false;
        MidiStepView view { pad.patternClip->getSequence(),
                            pad.midiNote,
                            stepsPerBar_,
                            bars_ };
        view.setStep(step, enabled, velocity);

        // Keep the bank in sync for active-pattern edits so
        // updateMaterializedClipsForPattern reads the fresh notes.
        pad.patternBank.storePattern(patternIndex,
                                     pad.patternClip->getSequence());
    }
    else
    {
        // Route the edit through the stored MidiList for that pattern slot.
        pad.patternBank.mutatePattern(patternIndex, [&](te::MidiList& list)
        {
            MidiStepView view { list, pad.midiNote, stepsPerBar_, bars_ };
            view.setStep(step, enabled, velocity);
        });
    }

    updateMaterializedClipsForPattern(patternIndex);
    return true;
}

bool SamplerInstrument::setStepVelocity(int patternIndex, int padIndex, int step, int velocity127)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return false;

    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return false;

    const int numSteps = getStepCount();
    if (!juce::isPositiveAndBelow(step, numSteps))
        return false;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    const int clampedVel = juce::jlimit(midi::kVelocityLiveMin, midi::kVelocityMax, velocity127);

    if (patternIndex == activePatternIndex_)
    {
        if (pad.patternClip == nullptr)
            return false;
        MidiStepView view { pad.patternClip->getSequence(),
                            pad.midiNote,
                            stepsPerBar_,
                            bars_ };
        // Preserve the historical guard: velocity updates only apply if the
        // step is already on — callers who want to enable the step must call
        // setStep() first.
        if (!view.isStepOn(step))
            return false;
        view.setStep(step, true, clampedVel);

        pad.patternBank.storePattern(patternIndex,
                                     pad.patternClip->getSequence());
    }
    else
    {
        bool wasOn = false;
        pad.patternBank.readPattern(patternIndex, [&](const te::MidiList& list)
        {
            MidiStepView view { list, pad.midiNote, stepsPerBar_, bars_ };
            wasOn = view.isStepOn(step);
        });
        if (!wasOn)
            return false;
        pad.patternBank.mutatePattern(patternIndex, [&](te::MidiList& list)
        {
            MidiStepView view { list, pad.midiNote, stepsPerBar_, bars_ };
            view.setStep(step, true, clampedVel);
        });
    }

    updateMaterializedClipsForPattern(patternIndex);
    return true;
}

std::optional<bool> SamplerInstrument::toggleStep(int patternIndex, int padIndex, int step)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
        return std::nullopt;

    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return std::nullopt;

    const int numSteps = getStepCount();
    if (!juce::isPositiveAndBelow(step, numSteps))
        return std::nullopt;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    bool current = false;

    if (patternIndex == activePatternIndex_)
    {
        if (pad.patternClip == nullptr)
            return std::nullopt;
        MidiStepView view { pad.patternClip->getSequence(),
                            pad.midiNote,
                            stepsPerBar_,
                            bars_ };
        current = view.isStepOn(step);
    }
    else
    {
        pad.patternBank.readPattern(patternIndex, [&](const te::MidiList& list)
        {
            MidiStepView view { list, pad.midiNote, stepsPerBar_, bars_ };
            current = view.isStepOn(step);
        });
    }

    const bool newState = !current;
    if (!setStep(patternIndex, padIndex, step, newState))
        return std::nullopt;
    return newState;
}

int SamplerInstrument::getStepCount() const
{
    return stepsPerBar_ * bars_;
}

void SamplerInstrument::setActivePatternIndex(int index)
{
    if (numPatterns_ <= 0)
        return;

    const int newIndex = juce::jlimit(0, numPatterns_ - 1, index);
    if (newIndex == activePatternIndex_)
        return;

    // Pause the transport across the swap so the audio thread can't read a
    // half-updated state (old pattern half-cleared + new pattern half-
    // populated). TE auto-invalidates its cachedLoopedSequence on SEQUENCE
    // tree mutations, but the node graph can still be mid-block when we
    // start stomping on notes — stalling the playhead first removes the
    // race entirely. Resume at the loop start so the new pattern always
    // plays from bar 1 after a swap, which is what the user expects from
    // a pattern change.
    auto* edit = edit_;
    const bool wasPlaying = edit != nullptr && edit->getTransport().isPlaying();
    if (wasPlaying)
        edit->getTransport().stop (false, false);

    for (auto& pad : pads_)
        if (pad.patternClip != nullptr)
            pad.patternBank.storePattern (activePatternIndex_,
                                          pad.patternClip->getSequence());

    activePatternIndex_ = newIndex;

    // Clear the live clip before the restore so an empty bank slot (no
    // MidiClip yet — the slot was reserved but never written) correctly
    // leaves the live clip empty. PadPatternBank::restorePattern silently
    // no-ops when the slot has no clip, which otherwise would leak the
    // previous pattern's notes onto the new active pattern.
    for (auto& pad : pads_)
    {
        if (pad.patternClip == nullptr)
            continue;
        // Bulk clears — MidiList::removeAll* reverse-iterates on the VT.
        auto& seq = pad.patternClip->getSequence();
        seq.removeAllNotes(nullptr);
        seq.removeAllControllers(nullptr);
    }

    for (auto& pad : pads_)
        if (pad.patternClip != nullptr)
            pad.patternBank.restorePattern (activePatternIndex_,
                                            pad.patternClip->getSequence());

    // Keep keyboard slots in lock-step with the sampler's pattern cursor so
    // recordings on instrument slots don't leak across pattern swaps.
    engine_.getKeyboardBank().setActivePatternIndex (activePatternIndex_);

    if (wasPlaying && edit != nullptr)
    {
        const auto loop = edit->getTransport().getLoopRange();
        edit->getTransport().setPosition (loop.getStart());
        edit->getTransport().play (false);
    }
}

bool SamplerInstrument::clearPattern(int patternIndex)
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return false;

    if (patternIndex == activePatternIndex_)
    {
        for (auto& pad : pads_)
        {
            if (pad.patternClip == nullptr)
                continue;
            auto& list = pad.patternClip->getSequence();
            list.removeAllNotes(nullptr);
            pad.patternBank.storePattern(patternIndex,
                                          pad.patternClip->getSequence());
        }
    }
    else
    {
        for (auto& pad : pads_)
        {
            pad.patternBank.mutatePattern(patternIndex, [](te::MidiList& list)
            {
                list.removeAllNotes(nullptr);
            });
        }
    }

    updateMaterializedClipsForPattern(patternIndex);
    return true;
}

bool SamplerInstrument::deletePattern(int patternIndex)
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return false;

    // Must keep at least one pattern; if this is the last, clear it in place.
    if (numPatterns_ <= 1)
        return clearPattern(patternIndex);

    for (auto& pad : pads_)
        pad.patternBank.removePattern(patternIndex);

    --numPatterns_;

    // Drop the matching name entry so later indices keep their stored names.
    if (static_cast<size_t>(patternIndex) < patternNames_.size())
        patternNames_.erase(patternNames_.begin() + patternIndex);

    if (activePatternIndex_ >= numPatterns_)
    {
        activePatternIndex_ = numPatterns_ - 1;
        // Clear the live clip before restore so an empty bank slot doesn't
        // leave stale notes behind (restorePattern is a no-op on empty slots).
        for (auto& pad : pads_)
        {
            if (pad.patternClip == nullptr)
                continue;
            auto& seq = pad.patternClip->getSequence();
            seq.removeAllNotes(nullptr);
            seq.removeAllControllers(nullptr);
        }
        // Reload active pattern into live clips from its new slot.
        for (auto& pad : pads_)
            if (pad.patternClip != nullptr)
                pad.patternBank.restorePattern(activePatternIndex_,
                                               pad.patternClip->getSequence());
    }

    return true;
}

double SamplerInstrument::getPlayheadStep() const
{
    if (edit_ == nullptr)
        return 0.0;

    const auto& transport = edit_->getTransport();
    if (!transport.isPlaying())
        return 0.0;

    const int stepCount = getStepCount();
    if (stepCount <= 0)
        return 0.0;

    auto range = getPatternEditTimeRange();
    if (!range.has_value())
        return 0.0;

    const double clipStartSec = range->getStart().inSeconds();
    const double clipEndSec = range->getEnd().inSeconds();
    const double clipLenSec = clipEndSec - clipStartSec;
    if (clipLenSec <= 0.0)
        return 0.0;

    const double posSeconds = transport.getPosition().inSeconds();
    const double relSec = posSeconds - clipStartSec;

    // Position within the clip, wrapped to [0, clipLenSec)
    double wrapped = std::fmod(relSec, clipLenSec);
    if (wrapped < 0.0)
        wrapped += clipLenSec;

    return wrapped / clipLenSec * static_cast<double>(stepCount);
}

std::optional<tracktion::core::TimeRange> SamplerInstrument::getPatternEditTimeRange() const
{
    // Use pad 0's MidiClip as the canonical pattern range — setPatternLength
    // keeps all pad clips length-synchronised.
    for (const auto& pad : pads_)
    {
        if (pad.patternClip != nullptr)
            return pad.patternClip->getEditTimeRange();
    }
    return std::nullopt;
}

void SamplerInstrument::refreshPatternClipRanges()
{
    if (edit_ == nullptr)
        return;

    // Pattern mode and Track-with-song both keep the live clip at native
    // bars — Track mode plays through materialized slot clips on the
    // timeline instead of extending the live clip.
    const int nativeBars = juce::jmax(1, bars_);

    const auto firstClipStart = [&]
    {
        for (const auto& pad : pads_)
            if (pad.patternClip != nullptr)
                return pad.patternClip->getPosition().getStart();
        return tracktion::core::TimePosition{};
    }();

    const auto spanEnd = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ nativeBars, {} });
    const auto newLength = tracktion::core::TimeDuration::fromSeconds(
        spanEnd.inSeconds() - firstClipStart.inSeconds());

    for (auto& pad : pads_)
    {
        if (pad.patternClip == nullptr)
            continue;

        pad.patternClip->setLength(newLength, false);
        // Clear any extended loop set by earlier versions of this method —
        // at native length the loop should be inert.
        pad.patternClip->setLoopRangeBeats({ tracktion::BeatPosition{},
                                              tracktion::BeatDuration{} });
    }
}

bool SamplerInstrument::setPatternLength(int patternIndex, int bars, int stepsPerBar)
{
    if (edit_ == nullptr)
        return false;

    bars = juce::jmax(1, bars);
    stepsPerBar = juce::jmax(1, stepsPerBar);

    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return false;

    // We currently apply pattern length globally (not per-pattern-index). The
    // parameter is retained for API compatibility with callers that always
    // pass patternIndex = active pattern.
    stepsPerBar_ = stepsPerBar;
    bars_ = bars;

    const auto firstClipStart = [&]
    {
        for (const auto& pad : pads_)
            if (pad.patternClip != nullptr)
                return pad.patternClip->getPosition().getStart();
        return tracktion::core::TimePosition{};
    }();

    const auto newEndPos = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ bars, {} });
    const auto newLength = tracktion::core::TimeDuration::fromSeconds(
        newEndPos.inSeconds() - firstClipStart.inSeconds());

    for (auto& pad : pads_)
        if (pad.patternClip != nullptr)
            pad.patternClip->setLength(newLength, false);

    // Keep keyboard slots in lockstep — same coupling model as setActivePatternIndex.
    engine_.getKeyboardBank().setPatternLength(patternIndex, bars, stepsPerBar);

    return true;
}

// ============================================================================
// Song timeline materialization
// ============================================================================
//
// Track mode used to play the song by extending each pad's live MidiClip to
// span the whole song and mutating its sequence at bar boundaries from a 16ms
// timer. That swap landed after TE had already scheduled the next block's
// notes, so step 0 of every pattern transition played the previous pattern
// for ~15–30ms.
//
// The fix: pre-build a MidiClip per song slot on the pad's timeline carrying
// a copy of the referenced pattern's content. Transport renders the timeline
// natively — sample-accurate bar boundaries, no mid-play mutations.

namespace
{
    // Drain every note/CC event from a MidiList. Used before repopulating a
    // materialized clip's sequence from bank data.
    void clearMidiList(te::MidiList& list)
    {
        list.removeAllNotes(nullptr);
        list.removeAllControllers(nullptr);
    }

    // Copy every note from `src` into `dest`. Caller is expected to have
    // cleared `dest` first.
    void copyNotes(const te::MidiList& src, te::MidiList& dest)
    {
        for (auto* n : src.getNotes())
            dest.addNote(n->getNoteNumber(), n->getStartBeat(), n->getLengthBeats(),
                         n->getVelocity(), n->getColour(), nullptr);
    }
}

void SamplerInstrument::materializeSongTimeline()
{
    if (edit_ == nullptr)
        return;

    // Count blocks across all lanes; bail if empty song.
    size_t totalBlocks = 0;
    for (const auto& lane : songLanes_)
        totalBlocks += lane.blocks.size();
    if (totalBlocks == 0)
        return;

    // If already materialized, tear down first so we rebuild cleanly.
    bool alreadyMaterialized = false;
    for (const auto& pad : pads_)
        if (!pad.materializedSlotClips.empty())
            alreadyMaterialized = true;
    if (alreadyMaterialized)
        dematerializeSongTimeline();

    // Sync the live clip into the active pattern's bank slot so the
    // materialized clip for that pattern picks up the user's latest edits.
    for (auto& pad : pads_)
        if (pad.patternClip != nullptr)
            pad.patternBank.storePattern(activePatternIndex_,
                                         pad.patternClip->getSequence());

    // Materialise keyboard slots onto the song timeline too. archiveLive...
    // inside materializeSong mirrors the pad "store live into bank before
    // parking" step, so user edits on the active pattern survive.
    engine_.getKeyboardBank().materializeSong(songLanes_);

    // 4/4 default — honour the first TimeSig so non-4/4 edits stay in step.
    int beatsPerBar = 4;
    if (auto* ts = edit_->tempoSequence.getTimeSig(0))
        beatsPerBar = juce::jmax(1, ts->numerator.get());

    const int nativeBars = juce::jmax(1, bars_);
    const auto loopLenBeats = tracktion::BeatDuration::fromBeats(
        (double) nativeBars * (double) beatsPerBar);

    const int totalBars = getSongTotalBars();

    // Park position for the live clip: well past the song end so TE doesn't
    // render it during Track-mode playback. We restore it to bar 0..nativeBars
    // in dematerializeSongTimeline.
    const int parkAtBar = juce::jmax(totalBars + 1000, 1000);
    const auto parkStart = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ parkAtBar, {} });
    const auto parkEnd = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ parkAtBar + nativeBars, {} });

    for (auto& pad : pads_)
    {
        if (pad.track == nullptr)
            continue;

        // Park the live clip past the song end so it doesn't compete with
        // the materialized block clips on the timeline.
        if (pad.patternClip != nullptr)
        {
            te::ClipPosition parkPos;
            parkPos.time = { parkStart, parkEnd };
            parkPos.offset = tracktion::core::TimeDuration{};
            pad.patternClip->setPosition(parkPos);
        }

        pad.materializedSlotClips.clear();
        pad.materializedSlotClips.reserve(totalBlocks);

        // Place one MidiClip per block, iterating lanes × sorted blocks. Two
        // blocks on different lanes may overlap in time — TE sums their MIDI
        // at render time so the overlapping clips stack as expected.
        for (const auto& lane : songLanes_)
        {
            for (const auto& block : lane.blocks)
            {
                const int blockBars = juce::jmax(1, block.bars);
                const int barStart = juce::jmax(0, block.startBar);
                const auto rangeStart = edit_->tempoSequence.toTime(
                    tracktion::core::tempo::BarsAndBeats{ barStart, {} });
                const auto rangeEnd = edit_->tempoSequence.toTime(
                    tracktion::core::tempo::BarsAndBeats{ barStart + blockBars, {} });

                auto newClip = pad.track->insertMIDIClip(
                    tracktion::core::TimeRange{ rangeStart, rangeEnd }, nullptr);

                if (newClip != nullptr)
                {
                    auto& destSeq = newClip->getSequence();
                    clearMidiList(destSeq);
                    pad.patternBank.readPattern(block.patternIndex,
                        [&](const te::MidiList& src) { copyNotes(src, destSeq); });

                    // Loop the native pattern length so blocks longer than one
                    // pattern replay the content every nativeBars.
                    newClip->setLoopRangeBeats(
                        { tracktion::BeatPosition{}, loopLenBeats });

                    // Match the live-clip swing setup.
                    if (newClip->getGrooveTemplate().isEmpty())
                        newClip->setGrooveTemplate("Basic 16th Swing");
                    newClip->setGrooveStrength(swingStrength_);
                }

                pad.materializedSlotClips.push_back(newClip);
            }
        }
    }
}

void SamplerInstrument::dematerializeSongTimeline()
{
    if (edit_ == nullptr)
        return;

    bool anyMaterialized = false;
    for (auto& pad : pads_)
    {
        if (pad.materializedSlotClips.empty())
            continue;
        anyMaterialized = true;

        for (auto& clip : pad.materializedSlotClips)
        {
            if (clip != nullptr)
                clip->removeFromParent();
        }
        pad.materializedSlotClips.clear();
    }

    if (!anyMaterialized)
        return;

    // Restore pad.patternClip back to bar 0..nativeBars — the canonical
    // Pattern-mode position. Uses the shared tempo sequence via
    // refreshPatternClipRanges; reset position first because the park moved
    // it out to a distant bar.
    const int nativeBars = juce::jmax(1, bars_);
    const auto restoreStart = tracktion::core::TimePosition{};
    const auto restoreEnd = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ nativeBars, {} });

    for (auto& pad : pads_)
    {
        if (pad.patternClip == nullptr)
            continue;

        te::ClipPosition restorePos;
        restorePos.time = { restoreStart, restoreEnd };
        restorePos.offset = tracktion::core::TimeDuration{};
        pad.patternClip->setPosition(restorePos);
        pad.patternClip->setLoopRangeBeats({ tracktion::BeatPosition{},
                                              tracktion::BeatDuration{} });
    }

    // Tear down keyboard-slot materialisation alongside pad tear-down so both
    // timelines return to the canonical single-live-clip shape together.
    engine_.getKeyboardBank().dematerializeSong();
}

void SamplerInstrument::updateMaterializedClipsForPattern(int patternIndex)
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return;

    // Fast exit when nothing is materialized (the common Pattern-mode case).
    bool anyMaterialized = false;
    for (const auto& pad : pads_)
        if (!pad.materializedSlotClips.empty())
            anyMaterialized = true;
    if (!anyMaterialized)
        return;

    // Does the edited pattern appear anywhere in the song? If not, the
    // materialisation is already up to date.
    bool usesPattern = false;
    for (const auto& lane : songLanes_)
        for (const auto& block : lane.blocks)
            if (block.patternIndex == patternIndex) { usesPattern = true; break; }

    if (!usesPattern)
        return;

    // Without a per-clip back-reference to (lane, block) we take the blunt
    // path: tear down and rebuild. Same race class as the old single-lane
    // code — TE may render one buffer mid-swap.
    dematerializeSongTimeline();
    materializeSongTimeline();
}

// ============================================================================
// Song mode — multi-lane grid of pattern blocks
// ============================================================================

int SamplerInstrument::getSongTotalBars() const
{
    int maxEnd = 0;
    for (const auto& lane : songLanes_)
        for (const auto& block : lane.blocks)
            maxEnd = juce::jmax(maxEnd, block.startBar + block.bars);
    return maxEnd;
}

// Rebuild materialized clips when the song structure changes mid-play. The
// audio thread will glitch briefly over the swap — edit-while-playing on the
// song arrangement isn't the common case, so we accept the jitter here.
static void rebuildIfMaterialized(SamplerInstrument& self, AudioEngine& engine)
{
    if (engine.getPlayMode() != AudioEngine::PlayMode::Track)
        return;
    auto* edit = engine.getEdit();
    if (edit == nullptr || !edit->getTransport().isPlaying())
        return;
    self.dematerializeSongTimeline();
    self.materializeSongTimeline();
}

namespace
{
    // Does [newStart, newEnd) overlap any block on `lane` except the one at
    // `ignoreBlockIndex`? Half-open ranges: touching at the edge is fine
    // (block A ending at bar 4 and block B starting at bar 4 don't overlap).
    bool rangeOverlapsLane(const SamplerInstrument::SongLane& lane,
                           int newStart, int newEnd, int ignoreBlockIndex)
    {
        for (int i = 0; i < static_cast<int>(lane.blocks.size()); ++i)
        {
            if (i == ignoreBlockIndex)
                continue;
            const auto& b = lane.blocks[static_cast<size_t>(i)];
            const int s = b.startBar;
            const int e = b.startBar + b.bars;
            if (newStart < e && s < newEnd)
                return true;
        }
        return false;
    }

    // Insert `block` into lane.blocks keeping startBar-ascending order.
    // Returns the resolved insertion index.
    int insertSorted(SamplerInstrument::SongLane& lane,
                     const SamplerInstrument::SongBlock& block)
    {
        auto it = std::lower_bound(lane.blocks.begin(), lane.blocks.end(), block.startBar,
            [](const SamplerInstrument::SongBlock& b, int startBar)
            { return b.startBar < startBar; });
        const int idx = static_cast<int>(std::distance(lane.blocks.begin(), it));
        lane.blocks.insert(it, block);
        return idx;
    }
}

int SamplerInstrument::insertBlock(int laneIndex, int startBar, int patternIndex, int bars)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return -1;
    const int clampedPattern = juce::jlimit(0, juce::jmax(0, numPatterns_ - 1), patternIndex);
    const int clampedBars = juce::jmax(1, bars);
    const int clampedStart = juce::jmax(0, startBar);

    auto& lane = songLanes_[static_cast<size_t>(laneIndex)];
    if (rangeOverlapsLane(lane, clampedStart, clampedStart + clampedBars, -1))
        return -1;

    SongBlock block;
    block.patternIndex = clampedPattern;
    block.startBar = clampedStart;
    block.bars = clampedBars;
    const int idx = insertSorted(lane, block);

    engine_.updateLoopRangeForPlayMode();
    rebuildIfMaterialized(*this, engine_);
    return idx;
}

bool SamplerInstrument::removeBlock(int laneIndex, int blockIndex)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return false;
    auto& lane = songLanes_[static_cast<size_t>(laneIndex)];
    if (!juce::isPositiveAndBelow(blockIndex, static_cast<int>(lane.blocks.size())))
        return false;

    lane.blocks.erase(lane.blocks.begin() + blockIndex);
    engine_.updateLoopRangeForPlayMode();
    rebuildIfMaterialized(*this, engine_);
    return true;
}

bool SamplerInstrument::setBlockPattern(int laneIndex, int blockIndex, int patternIndex)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return false;
    auto& lane = songLanes_[static_cast<size_t>(laneIndex)];
    if (!juce::isPositiveAndBelow(blockIndex, static_cast<int>(lane.blocks.size())))
        return false;
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
        return false;

    lane.blocks[static_cast<size_t>(blockIndex)].patternIndex = patternIndex;
    rebuildIfMaterialized(*this, engine_);
    return true;
}

bool SamplerInstrument::setBlockBars(int laneIndex, int blockIndex, int bars)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return false;
    auto& lane = songLanes_[static_cast<size_t>(laneIndex)];
    if (!juce::isPositiveAndBelow(blockIndex, static_cast<int>(lane.blocks.size())))
        return false;

    const int clampedBars = juce::jmax(1, bars);
    auto& block = lane.blocks[static_cast<size_t>(blockIndex)];
    if (rangeOverlapsLane(lane, block.startBar, block.startBar + clampedBars, blockIndex))
        return false;

    block.bars = clampedBars;
    engine_.updateLoopRangeForPlayMode();
    rebuildIfMaterialized(*this, engine_);
    return true;
}

bool SamplerInstrument::setBlockStart(int laneIndex, int blockIndex, int startBar)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return false;
    auto& lane = songLanes_[static_cast<size_t>(laneIndex)];
    if (!juce::isPositiveAndBelow(blockIndex, static_cast<int>(lane.blocks.size())))
        return false;

    const int clampedStart = juce::jmax(0, startBar);
    auto& block = lane.blocks[static_cast<size_t>(blockIndex)];
    if (rangeOverlapsLane(lane, clampedStart, clampedStart + block.bars, blockIndex))
        return false;

    // Pull the block out, mutate, re-insert sorted so lane stays ordered.
    SongBlock moved = block;
    moved.startBar = clampedStart;
    lane.blocks.erase(lane.blocks.begin() + blockIndex);
    insertSorted(lane, moved);

    engine_.updateLoopRangeForPlayMode();
    rebuildIfMaterialized(*this, engine_);
    return true;
}

bool SamplerInstrument::moveBlockToLane(int fromLane, int fromBlock, int toLane, int toStartBar)
{
    if (!juce::isPositiveAndBelow(fromLane, static_cast<int>(songLanes_.size())))
        return false;
    if (!juce::isPositiveAndBelow(toLane, static_cast<int>(songLanes_.size())))
        return false;
    auto& src = songLanes_[static_cast<size_t>(fromLane)];
    if (!juce::isPositiveAndBelow(fromBlock, static_cast<int>(src.blocks.size())))
        return false;

    const int clampedStart = juce::jmax(0, toStartBar);
    SongBlock moved = src.blocks[static_cast<size_t>(fromBlock)];
    moved.startBar = clampedStart;

    // Check destination overlap. When moving within the same lane, ignore the
    // source block (we're about to remove it anyway).
    auto& dst = songLanes_[static_cast<size_t>(toLane)];
    const int ignoreIndex = (fromLane == toLane) ? fromBlock : -1;
    if (rangeOverlapsLane(dst, clampedStart, clampedStart + moved.bars, ignoreIndex))
        return false;

    src.blocks.erase(src.blocks.begin() + fromBlock);
    // After erasing, insert into destination. If same lane, indices shifted;
    // insertSorted handles placement either way.
    insertSorted(dst, moved);

    engine_.updateLoopRangeForPlayMode();
    rebuildIfMaterialized(*this, engine_);
    return true;
}

int SamplerInstrument::insertLane(int atIndex)
{
    const int size = static_cast<int>(songLanes_.size());
    const int clampedAt = juce::jlimit(0, size, atIndex);
    songLanes_.insert(songLanes_.begin() + clampedAt, SongLane{});
    return clampedAt;
}

bool SamplerInstrument::removeLane(int laneIndex)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return false;
    // Never drop below one lane — UI consumers assume at least one exists.
    if (songLanes_.size() <= 1)
        return false;

    songLanes_.erase(songLanes_.begin() + laneIndex);
    engine_.updateLoopRangeForPlayMode();
    rebuildIfMaterialized(*this, engine_);
    return true;
}

int SamplerInstrument::insertPattern(int atIndex)
{
    const int clampedAt = juce::jlimit(0, numPatterns_, atIndex);

    // Keep each pad's ClipSlotList in lock-step with numPatterns_ — inserting
    // an empty slot here means the new index maps 1:1 to a bank slot, matching
    // createPattern's bookkeeping. storePattern/mutatePattern will create the
    // MidiClip on first write.
    for (auto& pad : pads_)
    {
        if (pad.track == nullptr)
            continue;
        pad.track->getClipSlotList().insertSlot(clampedAt);
    }

    // Keyboard slots carry their own per-pattern banks and must widen in
    // lockstep; otherwise a freshly-cloned pattern ends up with empty
    // keyboard content because bank[clampedAt] never existed.
    engine_.getKeyboardBank().insertPatternSlot(clampedAt);

    ++numPatterns_;

    // Mirror the scene-list widening that createPattern does, so per-track
    // slot counts survive round-tripping through Edit state.
    if (edit_ != nullptr)
        edit_->getSceneList().ensureNumberOfScenes(numPatterns_ + 1);

    // Keep patternNames_ paired with the shifted pattern indices — the new
    // slot starts out empty so the inserted pattern picks up the default.
    if (patternNames_.size() < static_cast<size_t>(numPatterns_ - 1))
        patternNames_.resize(static_cast<size_t>(numPatterns_ - 1));
    patternNames_.insert(patternNames_.begin() + clampedAt, juce::String());

    // An insertion at or before the active pattern pushes its index up by 1
    // so the live clip still references the same logical pattern.
    if (activePatternIndex_ >= clampedAt)
        ++activePatternIndex_;

    // Shift every song-block reference whose pattern was renumbered by the
    // insertion, across every lane.
    for (auto& lane : songLanes_)
        for (auto& block : lane.blocks)
            if (block.patternIndex >= clampedAt)
                ++block.patternIndex;

    return clampedAt;
}

int SamplerInstrument::clonePattern(int srcIndex)
{
    if (!juce::isPositiveAndBelow(srcIndex, numPatterns_))
        return -1;

    const int destIndex = srcIndex + 1;

    // Capture the source content BEFORE insertPattern renumbers. For the
    // active pattern we read straight from the live MidiClip; everything else
    // comes out of the pad's ClipSlot bank. Each pad's notes are stored as a
    // flat list and replayed into the clone after the insert.
    struct NoteCopy
    {
        int noteNumber;
        tracktion::BeatPosition start;
        tracktion::BeatDuration length;
        int velocity;
        int colour;
    };
    std::vector<std::vector<NoteCopy>> perPadNotes(pads_.size());

    for (size_t padIdx = 0; padIdx < pads_.size(); ++padIdx)
    {
        auto& pad = pads_[padIdx];
        auto snapshot = [&](const te::MidiList& list)
        {
            auto& dst = perPadNotes[padIdx];
            for (auto* n : list.getNotes())
                dst.push_back({ n->getNoteNumber(),
                                n->getStartBeat(),
                                n->getLengthBeats(),
                                n->getVelocity(),
                                n->getColour() });
        };

        if (srcIndex == activePatternIndex_ && pad.patternClip != nullptr)
            snapshot(pad.patternClip->getSequence());
        else
            pad.patternBank.readPattern(srcIndex, snapshot);
    }

    // Snapshot keyboard-slot content BEFORE insertPattern shifts slot indices.
    // Routes live-clip vs bank by comparing against the keyboard bank's own
    // active index, matching the per-pad live/bank branch above.
    std::vector<std::vector<KeyboardInstrumentBank::NoteCopy>> perKeyboardNotes;
    engine_.getKeyboardBank().snapshotPattern(srcIndex, perKeyboardNotes);

    insertPattern(destIndex);

    // Paint the captured notes into the freshly inserted bank slot.
    for (size_t padIdx = 0; padIdx < pads_.size(); ++padIdx)
    {
        auto& pad = pads_[padIdx];
        const auto& notes = perPadNotes[padIdx];
        if (notes.empty())
            continue;

        pad.patternBank.mutatePattern(destIndex, [&notes](te::MidiList& list)
        {
            for (const auto& n : notes)
                list.addNote(n.noteNumber, n.start, n.length, n.velocity, n.colour, nullptr);
        });
    }

    // Replay snapshotted keyboard notes into the newly inserted bank slot on
    // every keyboard slot — mirrors the per-pad replay loop above.
    engine_.getKeyboardBank().writePattern(destIndex, perKeyboardNotes);

    return destIndex;
}

int SamplerInstrument::uniqueSongBlock(int laneIndex, int blockIndex)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return -1;
    auto& lane = songLanes_[static_cast<size_t>(laneIndex)];
    if (!juce::isPositiveAndBelow(blockIndex, static_cast<int>(lane.blocks.size())))
        return -1;

    const int src = lane.blocks[static_cast<size_t>(blockIndex)].patternIndex;
    const int newIdx = clonePattern(src);
    if (newIdx < 0)
        return -1;

    // clonePattern ran insertPattern(src + 1) which already shifted every
    // song-block ref at or above src + 1. The block the user clicked still
    // points at src (pattern indices < src + 1 are untouched). Repoint it at
    // the fresh clone.
    songLanes_[static_cast<size_t>(laneIndex)]
        .blocks[static_cast<size_t>(blockIndex)].patternIndex = newIdx;
    return newIdx;
}

int SamplerInstrument::doubleSongBlock(int laneIndex, int blockIndex)
{
    if (!juce::isPositiveAndBelow(laneIndex, static_cast<int>(songLanes_.size())))
        return -1;
    auto& lane = songLanes_[static_cast<size_t>(laneIndex)];
    if (!juce::isPositiveAndBelow(blockIndex, static_cast<int>(lane.blocks.size())))
        return -1;

    // Capture source block fields BEFORE clonePattern — insertPattern shifts
    // the blocks vector's patternIndex refs but leaves startBar/bars intact.
    const SongBlock srcBlock = lane.blocks[static_cast<size_t>(blockIndex)];

    const int newPattern = clonePattern(srcBlock.patternIndex);
    if (newPattern < 0)
        return -1;

    // insertBlock returns -1 on overlap; keep the cloned pattern so the user
    // gets something to recover from but don't place a second block.
    return insertBlock(laneIndex,
                       srcBlock.startBar + srcBlock.bars,
                       newPattern,
                       srcBlock.bars);
}

// ============================================================================
// Compatibility methods
// ============================================================================

std::vector<SamplerInstrument::PadSnapshot> SamplerInstrument::getPadsSnapshot(
    SnapshotContent content) const
{
    std::vector<PadSnapshot> snapshots;
    snapshots.reserve(pads_.size());

    const auto contentBits = static_cast<unsigned>(content);
    const bool includePatterns = (contentBits
        & static_cast<unsigned>(SnapshotContent::Patterns)) != 0;
    const bool includeLevels = (contentBits
        & static_cast<unsigned>(SnapshotContent::Levels)) != 0;
    const bool levelsOnly = includeLevels && !includePatterns
                         && content == SnapshotContent::Levels;
    const int numSteps = includePatterns ? getStepCount() : 0;

    for (const auto& pad : pads_)
    {
        PadSnapshot snapshot;
        snapshot.id = pad.index;
        snapshot.hasSample = pad.hasSample;
        snapshot.gainDb = pad.volume != nullptr ? pad.volume->getVolumeDb() : 0.0f;

        if (!levelsOnly)
        {
            snapshot.name = pad.name;
            snapshot.midiChannel = 1;
            snapshot.midiNote = pad.midiNote;
            snapshot.sampleName = pad.name;
            snapshot.sampleFile = pad.sampleFile;
            snapshot.chokeGroup = pad.chokeGroup;
            snapshot.triggerMode = pad.triggerMode;
            snapshot.sampleLayerCount = static_cast<int>(pad.sampleLayers.size());
            snapshot.sampleLayers.reserve(pad.sampleLayers.size());
            for (const auto& layer : pad.sampleLayers)
            {
                snapshot.sampleLayers.push_back({
                    layer.file.getFileNameWithoutExtension(), layer.gainDb,
                    layer.randomWeight, layer.velocityCurve,
                    layer.velocityMinimum, layer.velocityMaximum });
            }
        }

        // Get level from meter client (real-time level data). The client
        // uses its own juce::SpinLock to serialize audio-thread updates
        // from message-thread reads — no outer mutex needed.
        if (includeLevels && pad.meter != nullptr && pad.meterClient != nullptr)
        {
            auto levelL = pad.meterClient->getAndClearAudioLevel(0);
            auto levelR = pad.meterClient->getAndClearAudioLevel(1);
            float peakDb = std::max(levelL.dB, levelR.dB);
            snapshot.levelPeakDbfs = peakDb;
            snapshot.levelRms = juce::Decibels::decibelsToGain(peakDb, -100.0f);
        }

        if (!levelsOnly && pad.hasSample
            && pad.cachedSampleRate > 0 && pad.cachedLengthInSamples > 0)
        {
            snapshot.sampleRate = pad.cachedSampleRate;
            snapshot.totalSamples = static_cast<int>(pad.cachedLengthInSamples);
            snapshot.totalLengthSeconds = pad.cachedLengthSeconds;

            snapshot.windowStartSeconds = pad.rangeStartSeconds;
            snapshot.windowEndSeconds = pad.rangeEndSeconds > 0 ? pad.rangeEndSeconds : snapshot.totalLengthSeconds;
            snapshot.windowStartSample = static_cast<int>(pad.rangeStartSeconds * pad.cachedSampleRate);
            snapshot.windowEndSample = static_cast<int>(snapshot.windowEndSeconds * pad.cachedSampleRate);
        }

        if (!includePatterns)
        {
            snapshots.push_back(std::move(snapshot));
            continue;
        }

        // Build pattern snapshot by reading notes from each pattern's backing
        // store. Active pattern reads from the live MidiClip; others read
        // from the per-pad ClipSlot bank.
        auto fillFromList = [&](int pi, const te::MidiList& list)
        {
            PadSnapshot::PatternSnapshot ps;
            ps.patternIndex = pi;
            ps.channelIndex = pad.index;
            ps.velocities.resize(static_cast<size_t>(numSteps), 0);

            MidiStepView view { list, pad.midiNote, stepsPerBar_, bars_ };
            for (int step = 0; step < numSteps; ++step)
            {
                if (view.isStepOn(step))
                {
                    ps.steps.setBit(step);
                    ps.velocities[static_cast<size_t>(step)] =
                        static_cast<uint8_t>(juce::jlimit(midi::kVelocityMin, midi::kVelocityMax, view.getStepVelocity(step)));
                }
            }
            snapshot.patterns.push_back(std::move(ps));
        };

        for (int pi = 0; pi < numPatterns_; ++pi)
        {
            if (pi == activePatternIndex_ && pad.patternClip != nullptr)
            {
                fillFromList(pi, pad.patternClip->getSequence());
            }
            else
            {
                bool filled = false;
                pad.patternBank.readPattern(pi, [&](const te::MidiList& list)
                {
                    fillFromList(pi, list);
                    filled = true;
                });
                if (!filled)
                {
                    PadSnapshot::PatternSnapshot ps;
                    ps.patternIndex = pi;
                    ps.channelIndex = pad.index;
                    ps.velocities.resize(static_cast<size_t>(numSteps), 0);
                    snapshot.patterns.push_back(std::move(ps));
                }
            }
        }

        snapshots.push_back(std::move(snapshot));
    }

    return snapshots;
}

bool SamplerInstrument::setPadSampleRange(int padId, double startSeconds, double endSeconds)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padId < 0 || padId >= static_cast<int>(pads_.size()))
        return false;

    auto& pad = pads_[static_cast<size_t>(padId)];
    if (!pad.hasSample || pad.cachedLengthSeconds <= 0.0)
        return false;

    const double totalLengthSeconds = pad.cachedLengthSeconds;
    const double clampedStart = juce::jlimit(0.0, totalLengthSeconds, startSeconds);
    const double clampedEnd = juce::jlimit(clampedStart, totalLengthSeconds, endSeconds > 0.0 ? endSeconds : totalLengthSeconds);
    if (clampedEnd <= clampedStart)
        return false;

    pad.rangeStartSeconds = clampedStart;
    pad.rangeEndSeconds = clampedEnd;
    applyPadPlaybackProperties(pad);

    return true;
}

bool SamplerInstrument::setPadSampleNormalizationGainDb(int padId, float gainDb)
{
    // Raw contract: nested undo-manager writes are discarded during undo/redo.
    // Commands must call the corresponding Raw variant instead.
    jassert(! engine_.getUndoManager().isPerformingUndoRedo());

    if (padId < 0 || padId >= static_cast<int>(pads_.size()))
        return false;

    auto& pad = pads_[static_cast<size_t>(padId)];
    if (!pad.hasSample)
        return false;

    pad.normalizationGainDb = gainDb;
    applyPadPlaybackProperties(pad);
    return true;
}

bool SamplerInstrument::truncatePadSample(int padId, int64_t startSample, int64_t endSample)
{
    if (padId < 0 || padId >= static_cast<int>(pads_.size()))
        return false;

    auto& pad = pads_[static_cast<size_t>(padId)];
    if (!pad.hasSample || pad.cachedSampleRate <= 0.0 || pad.cachedLengthInSamples <= 0)
        return false;

    const int64_t totalSamples = pad.cachedLengthInSamples;
    if (startSample < 0)
        startSample = 0;
    if (endSample <= startSample || endSample > totalSamples)
        endSample = totalSamples;

    const int64_t numSamples = endSample - startSample;
    if (numSamples <= 0)
        return false;

    const double sampleRate = pad.cachedSampleRate;
    const double startSeconds = static_cast<double>(startSample) / sampleRate;
    const double endSeconds = static_cast<double>(endSample) / sampleRate;
    if (!setPadSampleRange(padId, startSeconds, endSeconds))
        return false;

    DBG("[SamplerInstrument] truncatePadSample: set non-destructive range on pad " << padId
        << " from " << startSample << " to " << endSample << " samples");
    return true;
}

bool SamplerInstrument::normalizePadSample(int padId, double targetDb)
{
    if (padId < 0 || padId >= static_cast<int>(pads_.size()))
        return false;

    auto& pad = pads_[static_cast<size_t>(padId)];
    if (!pad.sampleFile.existsAsFile())
        return false;

    // Read the source file
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(pad.sampleFile));
    if (reader == nullptr)
        return false;

    const int numChannels = static_cast<int>(reader->numChannels);
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    if (numSamples <= 0)
        return false;

    // Read entire sample into buffer
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    // Find peak amplitude across all channels
    float peakAmplitude = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* channelData = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const float absValue = std::abs(channelData[i]);
            if (absValue > peakAmplitude)
                peakAmplitude = absValue;
        }
    }

    if (peakAmplitude <= 0.0f)
        return false; // Silent sample, can't normalize

    // Calculate target amplitude from dB
    const float targetAmplitude = juce::Decibels::decibelsToGain(static_cast<float>(targetDb));

    // Calculate gain factor
    const float gainFactor = targetAmplitude / peakAmplitude;

    const float gainDb = juce::Decibels::gainToDecibels(gainFactor, -48.0f);
    if (!setPadSampleNormalizationGainDb(padId, gainDb))
        return false;

    DBG("[SamplerInstrument] normalizePadSample: set non-destructive gain on pad " << padId
        << " to " << targetDb << "dB (gain factor: " << gainFactor << ")");
    return true;
}

void SamplerInstrument::applyPadPlaybackProperties(PadInfo& pad)
{
    if (!pad.hasSample || pad.cachedLengthSeconds <= 0.0)
        return;

    const double totalLengthSeconds = pad.cachedLengthSeconds;
    const double excerptStart = juce::jlimit(0.0, totalLengthSeconds, pad.rangeStartSeconds);
    const double excerptEnd = juce::jlimit(excerptStart, totalLengthSeconds,
                                           pad.rangeEndSeconds > 0.0 ? pad.rangeEndSeconds : totalLengthSeconds);
    const double excerptLength = excerptEnd - excerptStart;
    if (excerptLength <= 0.0)
        return;

    if (pad.sampler != nullptr && pad.sampler->getNumSounds() > 0)
    {
        pad.sampler->setSoundExcerpt(0, excerptStart, excerptLength);
        for (int i = 0; i < pad.sampler->getNumSounds(); ++i)
        {
            const float layerGain = juce::isPositiveAndBelow(
                i, static_cast<int>(pad.sampleLayers.size()))
                ? pad.sampleLayers[static_cast<size_t>(i)].gainDb : 0.0f;
            pad.sampler->setSoundGains(
                i, pad.normalizationGainDb + layerGain, 0.0f);
        }
    }
}

// ============================================================================
// Timer callback — UI feedback plus pad-track triggering for choke-group pads
// ============================================================================

void SamplerInstrument::timerCallback()
{
    if (edit_ == nullptr || edit_->isLoading())
        return;

    const auto& transport = edit_->getTransport();
    const bool isPlaying = transport.isPlaying();

    // Reset step tracking when playback state changes
    if (isPlaying != wasPlaying_)
    {
        wasPlaying_ = isPlaying;
        lastTriggeredStep_ = -1;

        // Silence any ringing samples when playback stops
        // (TE sends noteOff at clip boundaries, but stop mid-note needs explicit cutoff)
        if (!isPlaying)
            stopAll();

        return;
    }

    if (!isPlaying)
        return;

    // Track playhead position and dispatch every step crossed since the last
    // callback. The message thread can be delayed by UI work; skipping a
    // crossed step would miss its LED event and its choke cutoff.
    const double stepPos = getPlayheadStep();
    const int currentStep = static_cast<int>(stepPos);
    const int stepCount = getStepCount();

    if (currentStep < 0 || currentStep >= stepCount || currentStep == lastTriggeredStep_)
        return;

    if (lastTriggeredStep_ < 0)
    {
        lastTriggeredStep_ = currentStep;
        notifyPadTriggersForStep(currentStep);
        return;
    }

    if (currentStep > lastTriggeredStep_)
    {
        for (int step = lastTriggeredStep_ + 1; step <= currentStep; ++step)
            notifyPadTriggersForStep(step);
    }
    else
    {
        for (int step = lastTriggeredStep_ + 1; step < stepCount; ++step)
            notifyPadTriggersForStep(step);
        for (int step = 0; step <= currentStep; ++step)
            notifyPadTriggersForStep(step);
    }

    lastTriggeredStep_ = currentStep;
}

void SamplerInstrument::notifyPadTriggersForStep(int step)
{
    // Audio for each pad comes from TE natively rendering each pad track's
    // MidiClip through its own SamplerPlugin. The timer path handles the
    // message-thread-only choke command and fans out UI listeners. The choke
    // command cuts already-rendering voices; it does not mutate MidiLists, so
    // it cannot damage the source pattern while playback is running.
    //
    // A step can contain multiple pads from one mutual choke group. Since the
    // native clips have already reached the renderer by the time this timer
    // callback runs, applying the choke once for every active pad would make
    // those pads silence each other. Pick one deterministic winner instead;
    // the later pad wins, matching the stable iteration order used below.
    std::array<int, 9> lastActivePadByGroup {};
    lastActivePadByGroup.fill(-1);

    for (size_t padIndex = 0; padIndex < pads_.size(); ++padIndex)
    {
        const auto& pad = pads_[padIndex];
        if (pad.patternClip == nullptr)
            continue;

        MidiStepView view { pad.patternClip->getSequence(), pad.midiNote, stepsPerBar_, bars_ };
        if (view.isStepOn(step) && pad.chokeGroup > 0
            && pad.chokeGroup < static_cast<int>(lastActivePadByGroup.size()))
            lastActivePadByGroup[static_cast<size_t>(pad.chokeGroup)] = static_cast<int>(padIndex);
    }

    for (size_t padIndex = 0; padIndex < pads_.size(); ++padIndex)
    {
        auto& pad = pads_[padIndex];
        if (pad.patternClip == nullptr)
            continue;

        MidiStepView view { pad.patternClip->getSequence(), pad.midiNote, stepsPerBar_, bars_ };
        if (view.isStepOn(step))
        {
            // Positive groups are mutual choke groups. Self-choke is applied
            // before a live trigger, but applying it here after TE has started
            // the scheduled note would immediately silence that same note.
            const bool isGroupWinner = pad.chokeGroup > 0
                && pad.chokeGroup < static_cast<int>(lastActivePadByGroup.size())
                && lastActivePadByGroup[static_cast<size_t>(pad.chokeGroup)] == static_cast<int>(padIndex);
            if (isGroupWinner)
                applyChokeGroup(static_cast<int>(padIndex));
            listeners_.call(&Listener::padTriggered, static_cast<int>(padIndex));
        }
    }
}

// ============================================================================
// State Persistence
// ============================================================================

namespace
{
const juce::Identifier kPadNode { "pad" };
const juce::Identifier kPadIndexProp { "index" };
const juce::Identifier kPadNameProp { "name" };
const juce::Identifier kPadSampleFileProp { "sampleFile" };
const juce::Identifier kPadInstrumentNameProp { "instrumentName" };
const juce::Identifier kPadChokeGroupProp { "chokeGroup" };
const juce::Identifier kPadTriggerModeProp { "triggerMode" };
const juce::Identifier kPadLayerNode { "sampleLayer" };
const juce::Identifier kPadLayerFileProp { "file" };
const juce::Identifier kPadLayerGainProp { "gainDb" };
const juce::Identifier kPadLayerWeightProp { "weight" };
const juce::Identifier kPadLayerVelocityCurveProp { "velocityCurve" };
const juce::Identifier kPadLayerVelocityMinimumProp { "velocityMinimum" };
const juce::Identifier kPadLayerVelocityMaximumProp { "velocityMaximum" };
const juce::Identifier kPadPrimaryLayerGainProp { "primaryLayerGainDb" };
const juce::Identifier kPadPrimaryLayerWeightProp { "primaryLayerWeight" };
const juce::Identifier kPadPrimaryLayerVelocityCurveProp { "primaryLayerVelocityCurve" };
const juce::Identifier kPadPrimaryLayerVelocityMinimumProp { "primaryLayerVelocityMinimum" };
const juce::Identifier kPadPrimaryLayerVelocityMaximumProp { "primaryLayerVelocityMaximum" };
const juce::Identifier kPadGainDbProp { "gainDb" };
const juce::Identifier kPadRangeStartProp { "rangeStartSeconds" };
const juce::Identifier kPadRangeEndProp { "rangeEndSeconds" };
const juce::Identifier kPadNormalizationGainProp { "normalizationGainDb" };
const juce::Identifier kPadsRootActivePatternProp { "activePatternIndex" };
const juce::Identifier kPadsRootNumPatternsProp { "numPatterns" };
const juce::Identifier kPadsRootStepsPerBarProp { "stepsPerBar" };
const juce::Identifier kPadsRootBarsProp { "bars" };
// Legacy single-lane save format. Retained only for migration on load —
// new saves write the multi-lane format below.
const juce::Identifier kSongNode { "song" };
const juce::Identifier kSongSlotNode { "slot" };
const juce::Identifier kSongSlotPatternIndexProp { "patternIndex" };
const juce::Identifier kSongSlotBarsProp { "bars" };
// Multi-lane format: <songLanes><lane><block .../>...</lane>...</songLanes>.
const juce::Identifier kSongLanesNode { "songLanes" };
const juce::Identifier kSongLaneNode { "lane" };
const juce::Identifier kSongBlockNode { "block" };
const juce::Identifier kSongBlockStartBarProp { "startBar" };
const juce::Identifier kSongBlockBarsProp { "bars" };
const juce::Identifier kSongBlockPatternIndexProp { "patternIndex" };
const juce::Identifier kPatternNamesNode { "patternNames" };
const juce::Identifier kPatternNameNode { "name" };
const juce::Identifier kPatternNameIndexProp { "index" };
const juce::Identifier kPatternNameValueProp { "value" };
}

bool SamplerInstrument::serializePadsToState(juce::ValueTree& padsState,
                                              const juce::File& projectFile)
{
    bool success = true;

    // Clear existing children
    padsState.removeAllChildren(nullptr);

    // Sampler-wide pattern bookkeeping. Sits as properties on the root node so
    // it round-trips alongside the per-pad children.
    padsState.setProperty(kPadsRootActivePatternProp, activePatternIndex_, nullptr);
    padsState.setProperty(kPadsRootNumPatternsProp, numPatterns_, nullptr);
    padsState.setProperty(kPadsRootStepsPerBarProp, stepsPerBar_, nullptr);
    padsState.setProperty(kPadsRootBarsProp, bars_, nullptr);

    for (auto& pad : pads_)
    {
        juce::ValueTree padNode(kPadNode);
        padNode.setProperty(kPadIndexProp, pad.index, nullptr);
        padNode.setProperty(kPadNameProp, pad.name, nullptr);
        padNode.setProperty(kPadChokeGroupProp, pad.chokeGroup, nullptr);
        padNode.setProperty(kPadTriggerModeProp,
                            static_cast<int>(pad.triggerMode), nullptr);
        if (! pad.sampleLayers.empty())
        {
            const auto& primary = pad.sampleLayers.front();
            padNode.setProperty(kPadPrimaryLayerGainProp, primary.gainDb, nullptr);
            padNode.setProperty(kPadPrimaryLayerWeightProp,
                                primary.randomWeight, nullptr);
            padNode.setProperty(kPadPrimaryLayerVelocityCurveProp,
                                static_cast<int>(primary.velocityCurve), nullptr);
            padNode.setProperty(kPadPrimaryLayerVelocityMinimumProp,
                                primary.velocityMinimum, nullptr);
            padNode.setProperty(kPadPrimaryLayerVelocityMaximumProp,
                                primary.velocityMaximum, nullptr);
        }

        if (pad.sampleFile.existsAsFile())
        {
            auto savedSamplePath = pad.sampleFile.getFullPathName();

            if (projectFile.hasFileExtension("mpi"))
            {
                const auto sampleDirectory = projectFile.getSiblingFile(
                    projectFile.getFileNameWithoutExtension() + ".samples");
                if (sampleDirectory.createDirectory())
                {
                    const auto destination = sampleDirectory.getChildFile(
                        juce::String(pad.index + 1) + "-" + pad.sampleFile.getFileName());

                    if (pad.sampleFile == destination || pad.sampleFile.copyFileTo(destination))
                    {
                        savedSamplePath = destination.getRelativePathFrom(
                            projectFile.getParentDirectory());
                    }
                    else
                    {
                        success = false;
                        juce::Logger::writeToLog(
                            "[SamplerInstrument] failed to package sample '"
                            + pad.sampleFile.getFullPathName() + "'"
                            + " into project '" + projectFile.getFullPathName() + "'");
                    }
                }
                else
                {
                    success = false;
                    juce::Logger::writeToLog(
                        "[SamplerInstrument] failed to create sample package directory '"
                        + sampleDirectory.getFullPathName() + "'");
                }
            }

            padNode.setProperty(kPadSampleFileProp, savedSamplePath, nullptr);
        }

        for (size_t layerIndex = 1; layerIndex < pad.sampleLayers.size(); ++layerIndex)
        {
            const auto& layerFile = pad.sampleLayers[layerIndex].file;
            if (! layerFile.existsAsFile())
                continue;

            auto savedLayerPath = layerFile.getFullPathName();
            if (projectFile.hasFileExtension("mpi"))
            {
                const auto sampleDirectory = projectFile.getSiblingFile(
                    projectFile.getFileNameWithoutExtension() + ".samples");
                const auto destination = sampleDirectory.getChildFile(
                    juce::String(pad.index + 1) + "-layer"
                    + juce::String(static_cast<int>(layerIndex + 1)) + "-"
                    + layerFile.getFileName());
                if (sampleDirectory.createDirectory()
                    && (layerFile == destination || layerFile.copyFileTo(destination)))
                {
                    savedLayerPath = destination.getRelativePathFrom(
                        projectFile.getParentDirectory());
                }
                else
                {
                    success = false;
                }
            }

            juce::ValueTree layerNode(kPadLayerNode);
            layerNode.setProperty(kPadLayerFileProp, savedLayerPath, nullptr);
            const auto& layer = pad.sampleLayers[layerIndex];
            layerNode.setProperty(kPadLayerGainProp, layer.gainDb, nullptr);
            layerNode.setProperty(kPadLayerWeightProp, layer.randomWeight, nullptr);
            layerNode.setProperty(kPadLayerVelocityCurveProp,
                                  static_cast<int>(layer.velocityCurve), nullptr);
            layerNode.setProperty(kPadLayerVelocityMinimumProp,
                                  layer.velocityMinimum, nullptr);
            layerNode.setProperty(kPadLayerVelocityMaximumProp,
                                  layer.velocityMaximum, nullptr);
            padNode.addChild(layerNode, -1, nullptr);
        }

        if (pad.instrument != nullptr)
            padNode.setProperty(kPadInstrumentNameProp, pad.instrument->getName(), nullptr);

        // Save gain from VolumeAndPanPlugin
        if (pad.volume != nullptr)
            padNode.setProperty(kPadGainDbProp, pad.volume->getVolumeDb(), nullptr);

        // Save range + normalization
        padNode.setProperty(kPadRangeStartProp, pad.rangeStartSeconds, nullptr);
        padNode.setProperty(kPadRangeEndProp, pad.rangeEndSeconds, nullptr);
        padNode.setProperty(kPadNormalizationGainProp, pad.normalizationGainDb, nullptr);

        // Sync live edits on the timeline clip into the bank's slot so the next
        // load sees the most recent note data. Patterns themselves are persisted
        // by TE as part of Edit.state — no custom subtree needed here.
        if (pad.patternClip != nullptr)
            pad.patternBank.storePattern(activePatternIndex_, pad.patternClip->getSequence());

        padsState.addChild(padNode, -1, nullptr);
    }

    // Per-pattern names. Only non-empty entries are written so the subtree
    // stays small for the common (unnamed) case and old saves without one
    // restore to all-defaults.
    juce::ValueTree namesNode(kPatternNamesNode);
    for (int i = 0; i < numPatterns_ && i < static_cast<int>(patternNames_.size()); ++i)
    {
        const auto& nm = patternNames_[static_cast<size_t>(i)];
        if (nm.isEmpty())
            continue;
        juce::ValueTree nameNode(kPatternNameNode);
        nameNode.setProperty(kPatternNameIndexProp, i, nullptr);
        nameNode.setProperty(kPatternNameValueProp, nm, nullptr);
        namesNode.addChild(nameNode, -1, nullptr);
    }
    padsState.addChild(namesNode, -1, nullptr);

    // Song-mode grid. Stored as a <songLanes> tree; one <lane> per lane,
    // each holding <block startBar=".." bars=".." patternIndex=".."> leaves.
    juce::ValueTree lanesNode(kSongLanesNode);
    for (const auto& lane : songLanes_)
    {
        juce::ValueTree laneNode(kSongLaneNode);
        for (const auto& block : lane.blocks)
        {
            juce::ValueTree blockNode(kSongBlockNode);
            blockNode.setProperty(kSongBlockStartBarProp, block.startBar, nullptr);
            blockNode.setProperty(kSongBlockBarsProp, block.bars, nullptr);
            blockNode.setProperty(kSongBlockPatternIndexProp, block.patternIndex, nullptr);
            laneNode.addChild(blockNode, -1, nullptr);
        }
        lanesNode.addChild(laneNode, -1, nullptr);
    }
    padsState.addChild(lanesNode, -1, nullptr);
    return success;
}

void SamplerInstrument::restorePadsFromState(const juce::ValueTree& padsState,
                                             const juce::File& projectFile)
{
    if (!padsState.isValid())
        return;

    // Sampler-wide pattern bookkeeping. Defaults match a fresh sampler so old
    // save files that lack these props still load cleanly.
    stepsPerBar_ = juce::jmax(1, static_cast<int>(padsState.getProperty(kPadsRootStepsPerBarProp, 16)));
    bars_        = juce::jmax(1, static_cast<int>(padsState.getProperty(kPadsRootBarsProp, 1)));
    numPatterns_ = juce::jmax(1, static_cast<int>(padsState.getProperty(kPadsRootNumPatternsProp, 1)));
    const int savedActiveIndex = juce::jlimit(0, numPatterns_ - 1,
        static_cast<int>(padsState.getProperty(kPadsRootActivePatternProp, 0)));

    // Reset names to the restored pattern count; any non-empty entry in the
    // saved <patternNames> subtree below overwrites the default.
    patternNames_.assign(static_cast<size_t>(numPatterns_), juce::String());
    if (auto namesNode = padsState.getChildWithName(kPatternNamesNode); namesNode.isValid())
    {
        for (int i = 0; i < namesNode.getNumChildren(); ++i)
        {
            auto nameNode = namesNode.getChild(i);
            if (!nameNode.hasType(kPatternNameNode))
                continue;
            const int idx = nameNode.getProperty(kPatternNameIndexProp, -1);
            const juce::String value = nameNode.getProperty(kPatternNameValueProp, "").toString();
            if (juce::isPositiveAndBelow(idx, numPatterns_))
                patternNames_[static_cast<size_t>(idx)] = value;
        }
    }

    for (int i = 0; i < padsState.getNumChildren(); ++i)
    {
        auto padNode = padsState.getChild(i);
        if (!padNode.hasType(kPadNode))
            continue;

        const int padIndex = padNode.getProperty(kPadIndexProp, -1);
        if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size()))
            continue;

        auto& pad = pads_[static_cast<size_t>(padIndex)];

        // Restore name. Keep it separate from the sample filename because
        // packaged samples may have a pad-index prefix to avoid collisions.
        const auto savedPadName = padNode.getProperty(kPadNameProp, pad.name).toString();
        pad.name = savedPadName;
        if (pad.track != nullptr)
            pad.track->setName(pad.name);

        // Restore choke group
        const int chokeGroup = padNode.getProperty(kPadChokeGroupProp, 0);
        pad.chokeGroup = juce::jlimit(-1, 8, chokeGroup);
        const int triggerMode = juce::jlimit(
            static_cast<int>(TriggerMode::HoldEnvelope),
            static_cast<int>(TriggerMode::VelocityRoundRobinRandom),
            static_cast<int>(padNode.getProperty(
                kPadTriggerModeProp,
                static_cast<int>(TriggerMode::HoldEnvelope))));
        const auto savedTriggerMode = static_cast<TriggerMode>(triggerMode);
        pad.triggerMode = savedTriggerMode;

        // Restore sample
        const juce::String samplePath = padNode.getProperty(kPadSampleFileProp, "").toString();
        if (samplePath.isNotEmpty())
        {
            juce::File sampleFile(samplePath);
            if (!sampleFile.existsAsFile() && projectFile != juce::File()
                && !juce::File::isAbsolutePath(samplePath))
                sampleFile = projectFile.getParentDirectory().getChildFile(samplePath);
            if (sampleFile.existsAsFile())
            {
                loadSample(padIndex, sampleFile);
                pad.name = savedPadName;
                if (pad.track != nullptr)
                    pad.track->setName(pad.name);
            }
        }

        if (pad.hasSample)
        {
            setSampleLayerGainDb(padIndex, 0, static_cast<float>(
                padNode.getProperty(kPadPrimaryLayerGainProp, 0.0f)));
            setSampleLayerRandomWeight(padIndex, 0, static_cast<float>(
                padNode.getProperty(kPadPrimaryLayerWeightProp, 1.0f)));
            setSampleLayerVelocityCurve(
                padIndex, 0,
                static_cast<LayerVelocityCurve>(juce::jlimit(
                    static_cast<int>(LayerVelocityCurve::Linear),
                    static_cast<int>(LayerVelocityCurve::Fixed),
                    static_cast<int>(padNode.getProperty(
                        kPadPrimaryLayerVelocityCurveProp, 0)))));
            setSampleLayerVelocityRange(
                padIndex, 0,
                static_cast<float>(padNode.getProperty(
                    kPadPrimaryLayerVelocityMinimumProp, 0.85f)),
                static_cast<float>(padNode.getProperty(
                    kPadPrimaryLayerVelocityMaximumProp, 1.0f)));

            int restoredLayerIndex = 1;
            for (int layerIndex = 0; layerIndex < padNode.getNumChildren(); ++layerIndex)
            {
                const auto layerNode = padNode.getChild(layerIndex);
                if (! layerNode.hasType(kPadLayerNode))
                    continue;
                const auto savedPath = layerNode.getProperty(
                    kPadLayerFileProp, "").toString();
                if (savedPath.isEmpty())
                    continue;
                juce::File layerFile(savedPath);
                if (! layerFile.existsAsFile() && projectFile != juce::File()
                    && ! juce::File::isAbsolutePath(savedPath))
                    layerFile = projectFile.getParentDirectory().getChildFile(savedPath);
                if (layerFile.existsAsFile())
                {
                    if (addSampleLayerRaw(padIndex, layerFile))
                    {
                        setSampleLayerGainDb(padIndex, restoredLayerIndex,
                            static_cast<float>(layerNode.getProperty(
                                kPadLayerGainProp, 0.0f)));
                        setSampleLayerRandomWeight(padIndex, restoredLayerIndex,
                            static_cast<float>(layerNode.getProperty(
                                kPadLayerWeightProp, 1.0f)));
                        setSampleLayerVelocityCurve(
                            padIndex, restoredLayerIndex,
                            static_cast<LayerVelocityCurve>(juce::jlimit(
                                static_cast<int>(LayerVelocityCurve::Linear),
                                static_cast<int>(LayerVelocityCurve::Fixed),
                                static_cast<int>(layerNode.getProperty(
                                    kPadLayerVelocityCurveProp, 0)))));
                        setSampleLayerVelocityRange(
                            padIndex, restoredLayerIndex,
                            static_cast<float>(layerNode.getProperty(
                                kPadLayerVelocityMinimumProp, 0.85f)),
                            static_cast<float>(layerNode.getProperty(
                                kPadLayerVelocityMaximumProp, 1.0f)));
                        ++restoredLayerIndex;
                    }
                }
            }
        }
        setTriggerModeDirect(padIndex, savedTriggerMode);

        // Restore gain
        if (pad.volume != nullptr)
        {
            const float gainDb = padNode.getProperty(kPadGainDbProp, 0.0f);
            pad.volume->setVolumeDb(juce::jlimit(-48.0f, 12.0f, gainDb));
        }

        // Restore range + normalization
        pad.rangeStartSeconds = padNode.getProperty(kPadRangeStartProp, 0.0);
        pad.rangeEndSeconds = padNode.getProperty(kPadRangeEndProp, 0.0);
        pad.normalizationGainDb = padNode.getProperty(kPadNormalizationGainProp, 0.0f);
        applyPadPlaybackProperties(pad);

        // Reinstate the active pattern into the pad's live MidiClip from the
        // bank's slot. Edit.state restored the slot's MidiClip already; copy
        // its sequence into the live timeline clip so the live view matches
        // what the user last edited.
        if (pad.patternClip != nullptr)
            pad.patternBank.restorePattern(savedActiveIndex,
                                           pad.patternClip->getSequence());
    }

    activePatternIndex_ = savedActiveIndex;

    // Song-mode grid. Prefer the multi-lane format; fall back to the legacy
    // <song>/<slot> format by flattening into lane 0 with cumulative startBar.
    // Missing both → single empty lane.
    songLanes_.clear();
    auto lanesNode = padsState.getChildWithName(kSongLanesNode);
    if (lanesNode.isValid())
    {
        for (int li = 0; li < lanesNode.getNumChildren(); ++li)
        {
            auto laneNode = lanesNode.getChild(li);
            if (!laneNode.hasType(kSongLaneNode))
                continue;
            SongLane lane;
            for (int bi = 0; bi < laneNode.getNumChildren(); ++bi)
            {
                auto blockNode = laneNode.getChild(bi);
                if (!blockNode.hasType(kSongBlockNode))
                    continue;
                const int patternIndex = blockNode.getProperty(kSongBlockPatternIndexProp, -1);
                const int startBar = blockNode.getProperty(kSongBlockStartBarProp, 0);
                const int bars = blockNode.getProperty(kSongBlockBarsProp, 1);
                // Drop blocks that reference patterns that no longer exist.
                if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
                    continue;
                SongBlock block;
                block.patternIndex = patternIndex;
                block.startBar = juce::jmax(0, startBar);
                block.bars = juce::jmax(1, bars);
                lane.blocks.push_back(block);
            }
            // Keep each lane's blocks in startBar order regardless of save
            // layout — mutators rely on sorted invariant.
            std::sort(lane.blocks.begin(), lane.blocks.end(),
                [](const SongBlock& a, const SongBlock& b)
                { return a.startBar < b.startBar; });
            songLanes_.push_back(std::move(lane));
        }
    }
    else if (auto songNode = padsState.getChildWithName(kSongNode); songNode.isValid())
    {
        // Legacy format: one lane, blocks laid out in order with cumulative
        // startBar computed from each slot's bars.
        SongLane lane;
        int runningStart = 0;
        for (int i = 0; i < songNode.getNumChildren(); ++i)
        {
            auto slotNode = songNode.getChild(i);
            if (!slotNode.hasType(kSongSlotNode))
                continue;
            const int patternIndex = slotNode.getProperty(kSongSlotPatternIndexProp, -1);
            const int bars = juce::jmax(1, static_cast<int>(
                slotNode.getProperty(kSongSlotBarsProp, 1)));
            if (!juce::isPositiveAndBelow(patternIndex, numPatterns_))
            {
                runningStart += bars;
                continue;
            }
            SongBlock block;
            block.patternIndex = patternIndex;
            block.startBar = runningStart;
            block.bars = bars;
            lane.blocks.push_back(block);
            runningStart += bars;
        }
        songLanes_.push_back(std::move(lane));
    }

    // Maintain the "at least one lane" invariant even if load produced zero.
    if (songLanes_.empty())
        songLanes_.emplace_back();

    // Rebuild the choke-group bucket after the bulk chokeGroup writes above.
    rebuildChokeGroupIndex();
}

void SamplerInstrument::applySwing(float strength)
{
    const float clamped = juce::jlimit(0.0f, 1.0f, strength);
    swingStrength_ = clamped;
    for (auto& pad : pads_)
    {
        if (pad.patternClip == nullptr)
            continue;
        if (pad.patternClip->getGrooveTemplate().isEmpty())
            pad.patternClip->setGrooveTemplate("Basic 16th Swing");
        pad.patternClip->setGrooveStrength(clamped);
    }

    // Keep keyboard slots in lockstep — same coupling model as setActivePatternIndex.
    engine_.getKeyboardBank().applySwing(clamped);
}
