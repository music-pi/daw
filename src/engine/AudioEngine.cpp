#include "AudioEngine.h"

#include "Arpeggiator.h"
#include "SamplerInstrument.h"
#include "GroupManager.h"
#include "KeyboardInstrumentBank.h"
#include "PluginConfig.h"
#include "RoundRobinMidiPlugin.h"
#include "T9Dictionary.h"
#include "../app/DataPaths.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace
{
constexpr int kInitialPadCount = 16;
const juce::Identifier kProjectRoot { "maschinepi_project" };
const juce::Identifier kProjectVersionProp { "version" };
const juce::Identifier kProjectEditNode { "tracktion_edit" };
const juce::Identifier kProjectEditRefNode { "tracktion_edit_ref" };
const juce::Identifier kProjectEditPathProp { "path" };
const juce::Identifier kProjectStateNode { "maschinepi_state" };
const int kProjectFormatVersion = 2;

const juce::Identifier kStateRootNode { "maschinepi_state" };
const juce::Identifier kStateTransportNode { "transport" };
const juce::Identifier kStatePadsNode { "pads" };
const juce::Identifier kStateGroupsNode { "groups" };
const juce::Identifier kStateMixerNode { "pad_mixer" };
const juce::Identifier kStateUiNode { "ui" };
const juce::Identifier kStateSettingsNode { "settings" };
const juce::Identifier kStatePlayModeProp { "playMode" };

}

AudioEngine::AudioEngine(te::Engine& e)
    : engine(e)
    , paused(false)
    , keyboardBank_(std::make_unique<KeyboardInstrumentBank>(*this))
    , pluginConfigRegistry_(std::make_unique<PluginConfigRegistry>())
    , keyboardArp_(std::make_unique<Arpeggiator>(*this))
{
    samplers_[0] = std::make_unique<SamplerInstrument>(*this, 0);
    groupManager = std::make_unique<GroupManager>(*this);

    // Custom pad MIDI plugins must be registered before any Edit is loaded so
    // Tracktion can rehydrate them from saved track plugin chains.
    engine.getPluginManager().createBuiltInType<RoundRobinMidiPlugin>();

    initialiseStateTree();

    // Parameterized grooves (including "Basic 16th Swing") are opt-in; without this,
    // GrooveTemplateManager::getTemplateByName returns nullptr for any parameterized
    // template and swing has no audible effect.
    engine.getGrooveTemplateManager().useParameterizedGrooves(true);

    // T9 dictionary persisted under settings.t9Dictionary
    auto t9Node = settingsState.getOrCreateChildWithName("t9Dictionary", nullptr);
    t9Dictionary_ = std::make_unique<T9Dictionary>(t9Node);

    // Seed project-name scope from on-disk recent files (filename without extension).
    juce::StringArray names;
    for (const auto& f : DataPaths::findRecentProjects(32))
        names.add(f.getFileNameWithoutExtension());
    t9Dictionary_->seed("project-names", std::move(names));

    pluginConfigRegistry_->loadDefaults();
}

AudioEngine::~AudioEngine()
{
    teardownEdit();
}

void AudioEngine::teardownEdit() noexcept
{
    // Order is load-bearing. The keyboard bank must detach BEFORE
    // edit.reset() so livePlugin_ still refers to a plugin that exists;
    // otherwise its destructor calls deleteFromParent on a destroyed
    // plugin -- heap corruption on exit. Same for sampler/arp track refs.
    freeTransportContext();
    if (keyboardArp_ != nullptr)
        keyboardArp_->setTrack(nullptr);
    for (auto& sampler : samplers_)
        if (sampler != nullptr)
            sampler->detach();
    if (keyboardBank_ != nullptr)
        keyboardBank_->detach();
    edit.reset();
}

juce::File AudioEngine::getHybridEditFileForProject(const juce::File& projectFile)
{
    return projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension() + ".tracktionedit");
}

bool AudioEngine::installEdit(std::unique_ptr<te::Edit> newEdit, bool resetStateTree)
{
    if (newEdit == nullptr)
        return false;

    // Order matters: detach from the old edit BEFORE replacing the pointer,
    // attach to the new edit AFTER the pointer is stable. Kept as one list so
    // adding a new edit-bound subsystem only needs a single entry.
    auto forEachSubsystem = [&](auto&& fn) {
        for (auto& sampler : samplers_)
            if (sampler != nullptr)
                fn(*sampler);
        if (keyboardBank_ != nullptr) fn(*keyboardBank_);
    };

    listeners_.call([](Listener& listener) { listener.editAboutToBeReplaced(); });

    if (edit != nullptr)
        forEachSubsystem([](auto& sub) { sub.detach(); });

    // Group slots are reconstructed from the incoming project state after
    // the Edit is installed. Carrying banks across edits would create tracks
    // in the new project for groups belonging to the old one.
    resetSamplerGroups();

    edit = std::move(newEdit);
    edit->playInStopEnabled = true;
    // Count-in is a one-bar metronome pre-roll. TE handles it natively when
    // transport.record() is engaged — playhead starts at negative time,
    // click-track fires for the configured beats, then playback + record
    // kick in at zero. See isCountingIn() for the detection path.
    edit->setCountInMode(te::Edit::CountIn::oneBar);
    paused = false;

    forEachSubsystem([&](auto& sub) { sub.attachToEdit(*edit); });

    if (resetStateTree)
        initialiseStateTree();

    // Provision mixer meters deterministically at edit setup so the mixer
    // UI can read live levels without mutating the edit from a render path.
    // Pad tracks already get their meter via SamplerInstrument.
    //
    // NOTE on master: the master output flows through edit->getMasterPluginList(),
    // NOT through edit->getMasterTrack()->pluginList (per TE — see
    // tracktion_Edit.cpp where the traversal explicitly skips MasterTrack
    // "as that is covered by the masterPluginList above"). We insert into the
    // master plugin list so the meter actually sees summed output.
    auto ensureMeterOnList = [&](te::PluginList& list) {
        for (auto* p : list.getPlugins())
            if (dynamic_cast<te::LevelMeterPlugin*>(p) != nullptr)
                return;
        if (auto newPlugin = edit->getPluginCache().createNewPlugin(
                te::LevelMeterPlugin::xmlTypeName, {}))
            list.insertPlugin(newPlugin, -1, nullptr);
    };

    ensureMeterOnList(edit->getMasterPluginList());
    for (auto* track : te::getTopLevelTracks(*edit))
        if (auto* folder = dynamic_cast<te::FolderTrack*>(track))
            ensureMeterOnList(folder->pluginList);

    validateInputDeviceConfigurations();
    ensureTransportContextAllocated();
    listeners_.call([](Listener& listener) { listener.editReplaced(); });
    return true;
}

void AudioEngine::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void AudioEngine::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

bool AudioEngine::initialiseAudioDevice(int sampleRate, int bufferSize)
{
    // Tell PipeWire/JACK the quantum we want before opening the device
    if (bufferSize > 0 && sampleRate > 0)
        setenv("PIPEWIRE_QUANTUM", (std::to_string(bufferSize) + "/" + std::to_string(sampleRate)).c_str(), 1);

    auto& deviceManager = engine.getDeviceManager();
    deviceManager.initialise(0, 2); // we only need outputs by default

    auto& juceDeviceManager = deviceManager.deviceManager;

    auto setup = juceDeviceManager.getAudioDeviceSetup();
    if (sampleRate > 0)
        setup.sampleRate = static_cast<double>(sampleRate);
    if (bufferSize > 0)
        setup.bufferSize = bufferSize;

    juce::String err = juceDeviceManager.setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty())
        juce::Logger::writeToLog("[AudioEngine] device setup: " + err);

    deviceManager.enableAllWaveOutputs();

    // Disable all wave input devices if there are no input channels to prevent assertions
    if (auto* device = juceDeviceManager.getCurrentAudioDevice())
    {
        const auto activeInputChannels = device->getActiveInputChannels();
        const int numActiveInputs = activeInputChannels.countNumberOfSetBits();
        
        if (numActiveInputs == 0)
        {
            for (int i = 0; i < deviceManager.getNumWaveInDevices(); ++i)
            {
                if (auto* waveInput = deviceManager.getWaveInDevice(i))
                {
                    if (waveInput->isEnabled())
                        waveInput->setEnabled(false);
                }
            }
        }
    }

    return true;
}

bool AudioEngine::setBufferSize(int bufferSize)
{
    // Shell out to pw-metadata on a background thread so the message thread
    // never blocks on PipeWire I/O (timeouts, lock contention, slow startups).
    juce::Thread::launch([bufferSize]
    {
        juce::ChildProcess proc;
        const juce::StringArray args {
            "pw-metadata", "-n", "settings", "0",
            "clock.force-quantum", juce::String(bufferSize)
        };
        if (! proc.start(args))
        {
            juce::Logger::writeToLog("[AudioEngine] pw-metadata: failed to start");
            return;
        }
        // Bounded wait so a wedged PipeWire can't leak a zombie job.
        const bool finished = proc.waitForProcessToFinish(500);
        if (! finished)
        {
            proc.kill();
            juce::Logger::writeToLog("[AudioEngine] pw-metadata: timed out after 500ms");
            return;
        }
        const int exitCode = proc.getExitCode();
        if (exitCode != 0)
            juce::Logger::writeToLog("[AudioEngine] pw-metadata failed (exit "
                                      + juce::String(exitCode) + ")");
        else
            juce::Logger::writeToLog("[AudioEngine] Requested quantum: "
                                      + juce::String(bufferSize));
    });
    return true;
}

void AudioEngine::shutdown()
{
    teardownEdit();
    paused = false;
}

void AudioEngine::createEmptyEdit()
{
    freeTransportContext();
    auto newEdit = std::make_unique<te::Edit>(engine, te::Edit::EditRole::forEditing);
    currentProjectFile = juce::File();
    currentProjectIsHybrid = false;

    if (!installEdit(std::move(newEdit), true))
        return;

    // Free context before validation to prevent devices from being added to playback context
    freeTransportContext();

    auto& sampler = getSampler();
    for (int index = sampler.getPadCount(); index < kInitialPadCount; ++index)
        sampler.addPad("Pad " + juce::String(index + 1));

    // Validate once after all pads are created. Each pad adds the same kind of
    // track/wave-input device, and validateInputDeviceConfigurations walks every
    // device on every call; doing it 17 times per createEmptyEdit was pure overhead.
    validateInputDeviceConfigurations();

    // Now allocate context after all devices are validated and disabled
    ensureTransportContextAllocated();
}

bool AudioEngine::loadEditFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    freeTransportContext();
    auto newEdit = te::loadEditFromFile(engine, file, te::Edit::EditRole::forEditing);
    if (!installEdit(std::move(newEdit), true))
        return false;

    currentProjectFile = file;
    currentProjectIsHybrid = false;
    return true;
}

bool AudioEngine::loadProjectFromFile(const juce::File& file)
{
    // Log at every distinct failure point so the oncall path has a breadcrumb
    // for "why didn't my project load" — the caller only sees a bool.
    auto logFail = [&file](const juce::String& stage) {
        juce::Logger::writeToLog(
            juce::String("[AudioEngine::loadProjectFromFile] ") + stage
            + " for '" + file.getFullPathName() + "'");
    };

    if (!file.existsAsFile())
    {
        logFail("file does not exist");
        return false;
    }

    freeTransportContext();
    if (!file.hasFileExtension("mpi"))
        return loadEditFromFile(file);

    auto xml = juce::parseXML(file);
    if (xml == nullptr)
    {
        logFail("XML parse failed");
        return false;
    }

    auto projectTree = juce::ValueTree::fromXml(*xml);
    if (!projectTree.hasType(kProjectRoot))
    {
        logFail("wrong root node (not a maschinepi project)");
        return false;
    }

    std::unique_ptr<te::Edit> newEdit;

    const auto editRef = projectTree.getChildWithName(kProjectEditRefNode);
    if (editRef.isValid())
    {
        const auto relativePath = editRef.getProperty(kProjectEditPathProp).toString();
        if (relativePath.isNotEmpty())
        {
            const auto editFile = file.getSiblingFile(relativePath);
            if (!editFile.existsAsFile())
            {
                logFail("referenced edit file missing: " + relativePath);
                return false;
            }

            // Path-traversal guard: the edit file must live alongside the project.
            if (!editFile.isAChildOf(file.getParentDirectory()))
            {
                logFail("referenced edit file escapes project dir: " + relativePath);
                return false;
            }

            newEdit = te::loadEditFromFile(engine, editFile, te::Edit::EditRole::forEditing);
        }
    }

    if (newEdit == nullptr)
    {
        const auto editWrapper = projectTree.getChildWithName(kProjectEditNode);
        if (!editWrapper.isValid() || editWrapper.getNumChildren() == 0)
        {
            logFail("no edit reference or inline edit state");
            return false;
        }

        auto editState = editWrapper.getChild(0);
        newEdit = te::loadEditFromState(engine, editState, te::Edit::EditRole::forEditing);
    }

    if (!installEdit(std::move(newEdit), false))
    {
        logFail("installEdit failed");
        return false;
    }

    auto stateNode = projectTree.getChildWithName(kProjectStateNode);
    if (stateNode.isValid())
        adoptStateTree(stateNode);
    else
        initialiseStateTree();

    // Restore pad state from padsState
    if (samplers_[0] != nullptr && padsState.isValid())
        samplers_[0]->restorePadsFromState(padsState, file);

    for (int groupIndex = 0; groupIndex < kSamplerGroupCount; ++groupIndex)
    {
        auto* groupSampler = getSamplerForGroup(groupIndex);
        if (groupSampler == nullptr)
            continue;

        for (int i = 0; i < groupsState.getNumChildren(); ++i)
        {
            auto groupNode = groupsState.getChild(i);
            if (groupNode.hasType("group")
                && static_cast<int>(groupNode.getProperty("index", -1)) == groupIndex)
            {
                const auto bank = groupNode.getChildWithName("bank");
                if (bank.isValid() && groupIndex > 0)
                {
                    groupSampler->restorePadsFromState(bank, file);
                    break;
                }

                // One-time migration from the former snapshot-recall model.
                // That format could not preserve patterns per group, but its
                // samples, layers, names, gain, choke, and trigger settings
                // can become a real independent bank without data loss.
                const auto legacyPads = groupNode.getChildWithName("pads");
                for (int padNodeIndex = 0;
                     padNodeIndex < legacyPads.getNumChildren(); ++padNodeIndex)
                {
                    const auto padNode = legacyPads.getChild(padNodeIndex);
                    const int padIndex = static_cast<int>(
                        padNode.getProperty("index", -1));
                    if (padIndex < 0 || padIndex >= groupSampler->getPadCount())
                        continue;

                    const auto resolvePath = [&file](const juce::String& path)
                    {
                        juce::File sample(path);
                        if (!sample.existsAsFile() && !juce::File::isAbsolutePath(path))
                            sample = file.getParentDirectory().getChildFile(path);
                        return sample;
                    };

                    const auto primaryPath = padNode.getProperty(
                        "samplePath", "").toString();
                    const auto primaryFile = resolvePath(primaryPath);
                    bool sampleLoaded = false;
                    if (primaryFile.existsAsFile())
                        sampleLoaded = groupSampler->loadSample(padIndex, primaryFile);

                    int restoredLayer = 0;
                    for (int layerIndex = 0;
                         layerIndex < padNode.getNumChildren(); ++layerIndex)
                    {
                        const auto layerNode = padNode.getChild(layerIndex);
                        if (!layerNode.hasType("sampleLayer"))
                            continue;

                        const auto layerFile = resolvePath(
                            layerNode.getProperty("path", "").toString());
                        if (!sampleLoaded && layerFile.existsAsFile())
                            sampleLoaded = groupSampler->loadSample(padIndex, layerFile);
                        else if (restoredLayer > 0 && layerFile.existsAsFile())
                            sampleLoaded = groupSampler->addSampleLayer(
                                padIndex, layerFile) || sampleLoaded;

                        if (sampleLoaded)
                        {
                            groupSampler->setSampleLayerGainDbRaw(
                                padIndex, restoredLayer,
                                static_cast<float>(layerNode.getProperty("gainDb", 0.0f)));
                            groupSampler->setSampleLayerRandomWeightRaw(
                                padIndex, restoredLayer,
                                static_cast<float>(layerNode.getProperty("weight", 1.0f)));
                        }
                        ++restoredLayer;
                    }

                    groupSampler->setPadName(
                        padIndex, padNode.getProperty("name", "").toString());
                    groupSampler->setGainDbRaw(
                        padIndex,
                        static_cast<float>(padNode.getProperty("gainDb", 0.0f)));
                    groupSampler->setChokeGroupDirect(
                        padIndex,
                        static_cast<int>(padNode.getProperty("chokeGroup", 0)));
                    groupSampler->setTriggerModeDirect(
                        padIndex,
                        static_cast<SamplerInstrument::TriggerMode>(
                            static_cast<int>(padNode.getProperty("triggerMode", 0))));
                }
                break;
            }
        }
    }

    // Restore keyboard bank cursor. attachToEdit already rediscovered the
    // slot tracks from the Edit tree; the appState child just positions the
    // active-slot cursor back where the user left it.
    if (keyboardBank_ != nullptr && appState.isValid())
        keyboardBank_->restoreFromState(
            appState.getChildWithName("KeyboardInstrumentBank"));

    // Restore arp settings (mode / rate / oct / gate / latch).
    if (keyboardArp_ != nullptr && appState.isValid())
        keyboardArp_->restoreFromState(appState.getChildWithName("arpeggiator"));

    currentProjectFile = file;
    currentProjectIsHybrid = true;
    return true;
}

bool AudioEngine::saveEditToFile(const juce::File& file)
{
    if (edit == nullptr)
        return false;

    te::EditFileOperations fileOps(*edit);
    const bool ok = fileOps.saveAs(file, true); // true = force overwrite
    if (ok)
    {
        currentProjectFile = file;
        currentProjectIsHybrid = false;
    }
    return ok;
}

bool AudioEngine::saveEdit()
{
    if (edit == nullptr)
        return false;

    auto editFile = te::EditFileOperations(*edit).getEditFile();
    if (editFile == juce::File())
        return false; // No file associated with edit

    te::EditFileOperations fileOps(*edit);
    const bool ok = fileOps.save(false, false, false); // warnOfFailure=false, forceSave=false, offerToDiscard=false
    if (ok)
    {
        currentProjectFile = editFile;
        currentProjectIsHybrid = false;
    }
    return ok;
}

bool AudioEngine::saveProjectToFile(const juce::File& file)
{
    if (edit == nullptr)
        return false;

    if (!file.hasFileExtension("mpi"))
        return saveEditToFile(file);

    auto parent = file.getParentDirectory();
    if (!parent.exists())
        parent.createDirectory();

    // Both sampler and keyboard pattern banks are stored inside the Tracktion
    // Edit tree. Archive their currently-live timeline clips BEFORE writing
    // that tree; doing this after save silently persisted a stale active
    // pattern for keyboard recordings.
    if (samplers_[0] != nullptr && padsState.isValid()
        && !samplers_[0]->serializePadsToState(padsState, file))
        return false;
    for (int i = 0; i < groupsState.getNumChildren(); ++i)
    {
        auto groupNode = groupsState.getChild(i);
        if (auto legacyPads = groupNode.getChildWithName("pads");
            legacyPads.isValid())
            groupNode.removeChild(legacyPads, nullptr);
    }
    for (int groupIndex = 1; groupIndex < kSamplerGroupCount; ++groupIndex)
    {
        auto* groupSampler = getSamplerForGroup(groupIndex);
        if (groupSampler == nullptr)
            continue;

        for (int i = 0; i < groupsState.getNumChildren(); ++i)
        {
            auto groupNode = groupsState.getChild(i);
            if (!groupNode.hasType("group")
                || static_cast<int>(groupNode.getProperty("index", -1)) != groupIndex)
                continue;

            auto bank = groupNode.getOrCreateChildWithName("bank", nullptr);
            if (!groupSampler->serializePadsToState(bank, file))
                return false;
            break;
        }
    }
    if (keyboardBank_ != nullptr)
        keyboardBank_->archiveLiveClipsTo(
            keyboardBank_->getActivePatternIndex());

    const auto editFile = getHybridEditFileForProject(file);
    const auto editBackup = editFile.getSiblingFile(editFile.getFileName() + ".bak");
    const auto projectBackup = file.getSiblingFile(file.getFileName() + ".bak");
    const bool hadEdit = editFile.existsAsFile();
    const bool hadProject = file.existsAsFile();

    const auto snapshotPreviousGeneration = [](const juce::File& source,
                                               const juce::File& backup,
                                               bool existed)
    {
        if (!existed)
            return !backup.exists() || backup.deleteFile();
        return source.copyFileTo(backup);
    };
    if (!snapshotPreviousGeneration(editFile, editBackup, hadEdit)
        || !snapshotPreviousGeneration(file, projectBackup, hadProject))
        return false;

    const auto restorePreviousGeneration = [&]()
    {
        bool restored = true;
        if (hadEdit)
            restored = editBackup.copyFileTo(editFile) && restored;
        else if (editFile.existsAsFile())
            restored = editFile.deleteFile() && restored;

        if (hadProject)
            restored = projectBackup.copyFileTo(file) && restored;
        else if (file.existsAsFile())
            restored = file.deleteFile() && restored;
        return restored;
    };

    te::EditFileOperations fileOps(*edit);
    if (!fileOps.saveAs(editFile, true))
    {
        restorePreviousGeneration();
        return false;
    }

    juce::ValueTree projectTree(kProjectRoot);
    projectTree.setProperty(kProjectVersionProp, kProjectFormatVersion, nullptr);

    juce::ValueTree editRef(kProjectEditRefNode);
    editRef.setProperty(kProjectEditPathProp, editFile.getFileName(), nullptr);
    projectTree.addChild(editRef, -1, nullptr);

    // appState is an engine-lifetime invariant — initialiseStateTree sets it
    // at construction and adoptStateTree replaces it in place. No need to
    // guard on isValid() here; trust the internal invariant.
    jassert(appState.isValid());

    // Refresh the keyboard bank child so save captures the current
    // active-slot cursor. The bank's track structure lives in the Edit tree
    // and TE serialises it separately.
    if (keyboardBank_ != nullptr)
    {
        auto prev = appState.getChildWithName("KeyboardInstrumentBank");
        if (prev.isValid())
            appState.removeChild(prev, nullptr);
        appState.addChild(keyboardBank_->toState(), -1, nullptr);
    }

    if (keyboardArp_ != nullptr)
    {
        auto prev = appState.getChildWithName("arpeggiator");
        if (prev.isValid())
            appState.removeChild(prev, nullptr);
        appState.addChild(keyboardArp_->toState(), -1, nullptr);
    }

    projectTree.addChild(appState.createCopy(), -1, nullptr);

    if (auto xml = projectTree.createXml())
    {
        if (file.replaceWithText(xml->toString()))
        {
            currentProjectFile = file;
            currentProjectIsHybrid = true;
            return true;
        }
    }

    restorePreviousGeneration();
    return false;
}

bool AudioEngine::saveCurrentProject()
{
    auto file = getCurrentProjectFile();
    if (file == juce::File())
        return false;

    if (currentProjectIsHybrid || file.hasFileExtension("mpi"))
        return saveProjectToFile(file);

    return saveEdit();
}

juce::File AudioEngine::getCurrentProjectFile() const
{
    if (currentProjectIsHybrid && currentProjectFile != juce::File())
        return currentProjectFile;

    auto editFile = getCurrentEditFile();
    if (editFile != juce::File())
        return editFile;

    return currentProjectFile;
}

juce::File AudioEngine::getCurrentEditFile() const
{
    if (edit == nullptr)
        return juce::File();

    return te::EditFileOperations(*edit).getEditFile();
}

void AudioEngine::play()
{
    if (!edit)
        return;

    auto& transport = edit->getTransport();

#if JUCE_DEBUG
    juce::Logger::writeToLog("[AudioEngine] play() requested");
#endif

    // Update loop range based on play mode before playing
    updateLoopRangeForPlayMode();

    if (transport.isRecording())
    {
        transport.stopRecording();
        paused = false;
    }
    else if (transport.isPlaying())
    {
        transport.stop(false, false, false);
        paused = true;
    }
    else
    {
        // Materialize the song as real timeline clips before starting Track
        // playback so TE reads pattern transitions natively — no mid-play
        // MidiList mutations, no step-0 drops on bar boundaries.
        if (playMode == PlayMode::Track)
            for (auto& sampler : samplers_)
                if (sampler != nullptr)
                    sampler->materializeSongTimeline();

        // Starting from stopped should respect the play-mode loop, but resuming from
        // a paused position should let TE continue from the current transport state.
        if (!paused && (playMode == PlayMode::Pattern || playMode == PlayMode::Track))
        {
            const auto loopRange = transport.getLoopRange();
            const auto position = transport.getPosition();
            if (!loopRange.contains(position))
                transport.setPosition(loopRange.getStart());
        }

        // When armed, enter via transport.record() instead of play() so TE
        // runs its native one-bar count-in: the playhead is parked at a
        // negative pre-roll time, the click-track fires for the configured
        // beats, and actual clip playback + recording only begin once the
        // playhead crosses zero. `allowWithoutArmedInputs=true` lets this
        // fire even when no audio input channel is armed — we care about the
        // pre-roll for step/pattern recording, not audio capture.
        if (recordArmed_)
        {
            // Enable the metronome for the duration of the count-in. Saved so
            // stop() can restore the user's prior click preference.
            clickWasEnabledBeforeRecord_ = edit->clickTrackEnabled;
            edit->clickTrackEnabled = true;
            transport.record(false, /*allowWithoutArmedInputs*/ true);
        }
        else
        {
            transport.play(false);
        }
        paused = false;
    }

#if JUCE_DEBUG
    juce::Logger::writeToLog(transport.isPlaying() ? "[AudioEngine] transport is now playing"
                                                  : "[AudioEngine] transport failed to start");
#endif
}

void AudioEngine::stop()
{
    if (!edit)
        return;

    auto& transport = edit->getTransport();

#if JUCE_DEBUG
    juce::Logger::writeToLog("[AudioEngine] stop() requested");
#endif

    if (!transport.isPlaying() && !transport.isRecording())
    {
        transport.setPosition(tcc::TimePosition::fromSeconds(0.0));
    }

    transport.stop(false, false, true);
    paused = false;

    // Tear down any materialized song-timeline clips so the pad timeline
    // goes back to the canonical single-live-clip-per-pad shape. Safe no-op
    // when nothing was materialized (Pattern mode, or never-played Track).
    for (auto& sampler : samplers_)
        if (sampler != nullptr)
            sampler->dematerializeSongTimeline();

    // If we were counting-in / recording, restore the click-track preference
    // that the armed play() path overrode.
    if (recordArmed_ && edit)
        edit->clickTrackEnabled = clickWasEnabledBeforeRecord_;

#if JUCE_DEBUG
    juce::Logger::writeToLog(transport.isPlaying() ? "[AudioEngine] transport still playing"
                                                  : "[AudioEngine] transport stopped");
#endif
}

void AudioEngine::setLoopSeconds(double start, double end, bool enable)
{
    if (!edit) return;

    using tcc::TimePosition;
    using tcc::TimeRange;

    edit->getTransport().setLoopRange(TimeRange{
        TimePosition::fromSeconds(start),
        TimePosition::fromSeconds(end)
    });
    edit->getTransport().looping = enable;
}

void AudioEngine::enableClick(bool enable)
{
    if (!edit) return;
    edit->clickTrackEnabled = enable;
}

bool AudioEngine::isClickEnabled() const
{
    if (!edit) return false;
    return edit->clickTrackEnabled;
}

void AudioEngine::toggleLoop()
{
    if (!edit) return;
    auto& transport = edit->getTransport();
    transport.looping = !transport.looping;
}

void AudioEngine::toggleRecord(bool allowWithoutArmedInputs)
{
    if (!edit) return;

    auto& transport = edit->getTransport();

    if (transport.isRecording())
    {
        transport.stopRecording();
        paused = false;
        // After recording stops, validate devices again in case channels became invalid
        validateInputDeviceConfigurations();
        return;
    }

    if (!canRecord() && !allowWithoutArmedInputs)
    {
        juce::Logger::writeToLog("[AudioEngine] toggleRecord skipped: no active input channels");
        return;
    }

    // Before recording, enable and validate track input devices for all audio tracks
    // This is lazy enablement - we only enable when we actually need to record
    auto audioTracks = te::getAudioTracks(*edit);
    bool anyTrackReady = false;
    for (auto* track : audioTracks)
    {
        if (track != nullptr)
        {
            if (validateAndEnableTrackInputDevice(*track))
            {
                anyTrackReady = true;
            }
        }
    }

    if (!anyTrackReady && !allowWithoutArmedInputs)
    {
        juce::Logger::writeToLog("[AudioEngine] toggleRecord skipped: no tracks have valid input channels");
        return;
    }

    // Ensure playback context is allocated with valid devices
    ensureTransportContextAllocated();

    transport.record(false, allowWithoutArmedInputs);
    paused = false;
}

void AudioEngine::setPlayMode(PlayMode mode)
{
    if (playMode == mode)
        return;

    playMode = mode;
    if (transportState.isValid())
    {
        transportState.setProperty(kStatePlayModeProp, static_cast<int>(mode), nullptr);
    }

    // Update loop range when mode changes
    updateLoopRangeForPlayMode();

    // Any mode transition tears down materialized timeline clips so the
    // live pad.patternClip resumes its canonical position. play() will
    // re-materialize if we're going back into Track mode.
    for (auto& sampler : samplers_)
    {
        if (sampler == nullptr)
            continue;
        sampler->dematerializeSongTimeline();
        sampler->refreshPatternClipRanges();
    }
}

AudioEngine::PlayMode AudioEngine::getPlayMode() const
{
    return playMode;
}

double AudioEngine::calculateArrangerEndSeconds() const
{
    if (!edit)
        return 1.0;

    const double editLength = edit->getLength().inSeconds();
    return editLength > 0.0 ? editLength : 1.0;
}

void AudioEngine::updateLoopRangeForPlayMode()
{
    if (!edit)
        return;

    auto& transport = edit->getTransport();
    double loopStart = 0.0;
    double loopEnd = 0.0;

    if (playMode == PlayMode::Pattern)
    {
        // Use the shared per-pad pattern edit range (all pad MidiClips are
        // length-synchronised by setPatternLength).
        for (const auto& sampler : samplers_)
        {
            const auto range = sampler != nullptr
                ? sampler->getPatternEditTimeRange() : std::nullopt;
            if (!range.has_value())
                continue;

            loopStart = loopEnd == 0.0
                ? range->getStart().inSeconds()
                : juce::jmin(loopStart, range->getStart().inSeconds());
            loopEnd = juce::jmax(loopEnd, range->getEnd().inSeconds());
        }
        if (loopEnd <= loopStart)
        {
            const auto existingRange = transport.getLoopRange();
            loopStart = existingRange.getStart().inSeconds();
            loopEnd = existingRange.getEnd().inSeconds();
        }
    }
    else if (playMode == PlayMode::Track)
    {
        // Song mode: loop spans the full song — max(startBar + bars) across
        // every lane's blocks. Empty song falls back to the clip arrangement end.
        int totalBars = 0;
        for (const auto& sampler : samplers_)
            if (sampler != nullptr)
                totalBars = juce::jmax(totalBars, sampler->getSongTotalBars());
        if (totalBars > 0)
        {
            loopStart = 0.0;
            loopEnd = edit->tempoSequence.toTime(
                tracktion::core::tempo::BarsAndBeats{ totalBars, {} }).inSeconds();
        }
        else
        {
            loopStart = 0.0;
            loopEnd = calculateArrangerEndSeconds();
        }
    }
    else
    {
        // Fallback: use existing loop range
        const auto existingRange = transport.getLoopRange();
        loopStart = existingRange.getStart().inSeconds();
        loopEnd = existingRange.getEnd().inSeconds();
    }

    // Ensure valid range
    if (loopEnd <= loopStart)
        loopEnd = loopStart + 1.0;

    setLoopSeconds(loopStart, loopEnd, true);
}

juce::UndoManager& AudioEngine::getUndoManager()
{
    jassert(edit != nullptr);
    return edit->getUndoManager();
}

const juce::UndoManager& AudioEngine::getUndoManager() const
{
    jassert(edit != nullptr);
    return edit->getUndoManager();
}

bool AudioEngine::canUndo() const
{
    return edit != nullptr && edit->getUndoManager().canUndo();
}

bool AudioEngine::canRedo() const
{
    return edit != nullptr && edit->getUndoManager().canRedo();
}

bool AudioEngine::undo()
{
    if (edit == nullptr)
        return false;

    auto& um = edit->getUndoManager();
    if (!um.canUndo())
    {
        juce::Logger::writeToLog("[AudioEngine] Undo requested but no transactions are available.");
        return false;
    }

    const juce::String description = um.getUndoDescription();
    juce::Logger::writeToLog("[AudioEngine] Performing undo: " + (description.isNotEmpty() ? description : juce::String("unknown transaction")));
    const bool result = um.undo();
    juce::Logger::writeToLog(result ? "[AudioEngine] Undo completed successfully."
                                    : "[AudioEngine] Undo failed.");
    return result;
}

bool AudioEngine::redo()
{
    if (edit == nullptr)
        return false;

    auto& um = edit->getUndoManager();
    if (!um.canRedo())
    {
        juce::Logger::writeToLog("[AudioEngine] Redo requested but no transactions are available.");
        return false;
    }

    const juce::String description = um.getRedoDescription();
    juce::Logger::writeToLog("[AudioEngine] Performing redo: " + (description.isNotEmpty() ? description : juce::String("unknown transaction")));
    const bool result = um.redo();
    juce::Logger::writeToLog(result ? "[AudioEngine] Redo completed successfully."
                                    : "[AudioEngine] Redo failed.");
    return result;
}

juce::File AudioEngine::resolveSampleFileForPlayback(const juce::File& sourceFile)
{
    if (!sourceFile.existsAsFile())
        return sourceFile;

    te::AudioFile audioFile(engine, sourceFile);
    auto info = audioFile.getInfo();

    if (!info.needsCachedProxy)
        return sourceFile;

    const auto cacheFile = getCachedSampleFile(audioFile);

    if (cacheFile.existsAsFile()
        && cacheFile.getLastModificationTime() >= info.fileModificationTime)
        return cacheFile;

    if (createCachedSampleFile(audioFile, cacheFile))
        return cacheFile;

    return sourceFile;
}

void AudioEngine::ensureTransportContextAllocated()
{
#if !defined(MASCHINEPI_TESTS)
    if (edit != nullptr)
        edit->getTransport().ensureContextAllocated();
#endif
}

void AudioEngine::freeTransportContext()
{
#if !defined(MASCHINEPI_TESTS)
    if (edit != nullptr)
        edit->getTransport().freePlaybackContext();
#endif
}

void AudioEngine::validateInputDeviceConfigurations()
{
#if !defined(MASCHINEPI_TESTS)
    if (edit == nullptr)
        return;

    // Free playback context first to ensure devices aren't being processed
    // This prevents assertions during audio processing and ensures we can safely disable devices
    freeTransportContext();

    // Check if the current audio device has any active input channels
    const auto& deviceManager = engine.getDeviceManager().deviceManager;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto activeInputChannels = device->getActiveInputChannels();
        const int numActiveInputs = activeInputChannels.countNumberOfSetBits();
        // Get total number of input channels the device can provide
        const int totalInputChannels = device->getInputChannelNames().size();
        
        auto& dm = engine.getDeviceManager();
        
        // If no inputs available, disable ALL wave input devices immediately
        if (numActiveInputs == 0 || totalInputChannels == 0)
        {
            // Disable global wave input devices
            for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            {
                if (auto* waveInput = dm.getWaveInDevice(i))
                {
                    if (waveInput->isEnabled())
                    {
                        waveInput->setEnabled(false);
                    }
                }
            }
            
            // Disable ALL track wave input devices by default (lazy enablement)
            // They will only be enabled when recording is actually needed
            auto audioTracks = te::getAudioTracks(*edit);
            for (auto* track : audioTracks)
            {
                if (track != nullptr)
                {
                    disableTrackInputDevice(*track);
                }
            }
        }
        else
        {
            // Validate all wave input devices to ensure their channel indices are valid
            for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            {
                if (auto* waveInput = dm.getWaveInDevice(i))
                {
                    if (waveInput->isEnabled())
                    {
                        // Check if any channel index exceeds available channels or references inactive channels
                        bool hasInvalidChannels = false;
                        for (const auto& ci : waveInput->getChannels())
                        {
                            // Channel index must be valid and within device's total input channels
                            if (ci.indexInDevice < 0 || ci.indexInDevice >= totalInputChannels)
                            {
                                hasInvalidChannels = true;
                                break;
                            }
                            // Channel must be active
                            if (!activeInputChannels[ci.indexInDevice])
                            {
                                hasInvalidChannels = true;
                                break;
                            }
                        }
                        
                        if (hasInvalidChannels)
                        {
                            waveInput->setEnabled(false);
                        }
                    }
                }
            }
            
            // Disable track wave input devices by default (lazy enablement)
            // They will only be enabled when recording is actually needed and channels are valid
            auto audioTracks = te::getAudioTracks(*edit);
            for (auto* track : audioTracks)
            {
                if (track != nullptr)
                {
                    auto& waveInputDevice = track->getWaveInputDevice();
                    // Check if channels are valid
                    bool hasValidChannels = true;
                    for (const auto& ci : waveInputDevice.getChannels())
                    {
                        if (ci.indexInDevice < 0 || ci.indexInDevice >= totalInputChannels)
                        {
                            hasValidChannels = false;
                            break;
                        }
                        if (!activeInputChannels[ci.indexInDevice])
                        {
                            hasValidChannels = false;
                            break;
                        }
                    }
                    
                    // If channels are invalid, disable the device
                    // If channels are valid but device is enabled, keep it disabled by default
                    // (will be enabled lazily when recording starts)
                    if (!hasValidChannels || waveInputDevice.isEnabled())
                    {
                        disableTrackInputDevice(*track);
                    }
                }
            }
        }
    }
    else
    {
        // No device available - disable all wave input devices
        auto& dm = engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        {
            if (auto* waveInput = dm.getWaveInDevice(i))
            {
                if (waveInput->isEnabled())
                    waveInput->setEnabled(false);
            }
        }
        
        // Also disable all track wave input devices
        auto audioTracks = te::getAudioTracks(*edit);
        for (auto* track : audioTracks)
        {
            if (track != nullptr)
            {
                disableTrackInputDevice(*track);
            }
        }
    }
#endif
}

void AudioEngine::disableTrackInputDevice(te::AudioTrack& track)
{
    if (edit == nullptr)
        return;

    auto& waveInputDevice = track.getWaveInputDevice();
    if (waveInputDevice.isEnabled())
    {
        waveInputDevice.setEnabled(false);
    }
    
    auto& eid = edit->getEditInputDevices();
    auto inputInstances = eid.getDevicesForTargetTrack(track);
    for (auto* instance : inputInstances)
    {
        if (instance != nullptr)
        {
            auto& inputDevice = instance->owner;
            if (inputDevice.getDeviceType() == te::InputDevice::trackWaveDevice)
            {
                const auto result = instance->removeTarget(track.itemID, &edit->getUndoManager());
                if (result.failed())
                {
                    juce::Logger::writeToLog("[AudioEngine] Failed to remove track wave input device: " + result.getErrorMessage());
                }
                break; // Only one track wave device per track
            }
        }
    }
}

bool AudioEngine::hasValidInputChannelsForTrack(te::AudioTrack& track) const
{
    if (edit == nullptr)
        return false;

    const auto& deviceManager = engine.getDeviceManager().deviceManager;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto activeInputChannels = device->getActiveInputChannels();
        const int totalInputChannels = device->getInputChannelNames().size();
        
        if (totalInputChannels == 0)
            return false;
        
        auto& waveInputDevice = track.getWaveInputDevice();
        for (const auto& ci : waveInputDevice.getChannels())
        {
            if (ci.indexInDevice < 0 || ci.indexInDevice >= totalInputChannels)
                return false;
            if (!activeInputChannels[ci.indexInDevice])
                return false;
        }
        
        return true;
    }
    
    return false;
}

bool AudioEngine::validateAndEnableTrackInputDevice(te::AudioTrack& track)
{
    if (edit == nullptr)
        return false;

    // First check if channels are valid
    if (!hasValidInputChannelsForTrack(track))
    {
        juce::Logger::writeToLog("[AudioEngine] Track " + track.getName() + " has invalid input channels - cannot enable for recording");
        return false;
    }

    auto& waveInputDevice = track.getWaveInputDevice();
    
    // Ensure playback context is allocated so we can get input device instances
    ensureTransportContextAllocated();
    
    // Find or create the input device instance for this track's wave input device
    auto allInputDevices = edit->getAllInputDevices();
    te::InputDeviceInstance* instance = nullptr;
    
    // Look for existing instance
    for (auto* idi : allInputDevices)
    {
        if (idi != nullptr && &idi->getInputDevice() == &waveInputDevice)
        {
            instance = idi;
            break;
        }
    }
    
    // If no instance exists, we need to create one via the playback context
    // But instances are created automatically when devices are enabled and context is allocated
    // So we'll enable the device first, then find the instance
    if (instance == nullptr)
    {
        // Enable the device - this will cause it to be added to the playback context
        if (!waveInputDevice.isEnabled())
        {
            waveInputDevice.setEnabled(true);
        }
        
        // Re-allocate context to get the new instance
        freeTransportContext();
        ensureTransportContextAllocated();
        
        // Find the instance again
        allInputDevices = edit->getAllInputDevices();
        for (auto* idi : allInputDevices)
        {
            if (idi != nullptr && &idi->getInputDevice() == &waveInputDevice)
            {
                instance = idi;
                break;
            }
        }
    }
    
    if (instance == nullptr)
    {
        juce::Logger::writeToLog("[AudioEngine] Failed to get input device instance for track " + track.getName());
        return false;
    }
    
    // Assign the instance to the track if not already assigned
    auto targets = instance->getTargets();
    if (!targets.contains(track.itemID))
    {
        auto result = instance->setTarget(track.itemID, true, &edit->getUndoManager(), 0);
        if (!result.has_value())
        {
            juce::Logger::writeToLog("[AudioEngine] Failed to assign track wave input device: " + result.error());
            return false;
        }
    }
    
    // Ensure device is enabled
    if (!waveInputDevice.isEnabled())
    {
        waveInputDevice.setEnabled(true);
    }
    
    return true;
}

void AudioEngine::initialiseStateTree()
{
    appState = juce::ValueTree(kStateRootNode);
    ensureStateChildren();
    seedTransportDefaults(true);
    if (groupManager != nullptr)
        groupManager->refreshStateBinding();
}

juce::File AudioEngine::getCachedSampleFile(const te::AudioFile& source) const
{
    auto cacheDir = engine.getTemporaryFileManager()
                        .getTempDirectory()
                        .getChildFile("decoded_samples");

    if (!cacheDir.exists())
        cacheDir.createDirectory();

    const auto hashString = source.getHashString();
    return cacheDir.getChildFile(hashString + ".wav");
}

bool AudioEngine::createCachedSampleFile(const te::AudioFile& source, const juce::File& destination)
{
    juce::AudioFormat* format = nullptr;
    std::unique_ptr<juce::AudioFormatReader> reader(te::AudioFileUtils::createReaderFindingFormat(engine,
                                                                                                  source.getFile(),
                                                                                                  format));
    if (reader == nullptr)
        return false;

    auto parent = destination.getParentDirectory();
    if (!parent.exists() && !parent.createDirectory())
        return false;

    juce::WavAudioFormat wav;
    juce::TemporaryFile tempFile(destination);
    std::unique_ptr<juce::FileOutputStream> out(tempFile.getFile().createOutputStream());
    if (out == nullptr)
        return false;

    juce::AudioFormatWriterOptions writerOptions;
    writerOptions = writerOptions.withSampleRate(reader->sampleRate);
    writerOptions = writerOptions.withNumChannels(static_cast<int>(reader->numChannels));
    writerOptions = writerOptions.withBitsPerSample(24);

    std::unique_ptr<juce::OutputStream> writerStream(out.release());
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(writerStream,
                                                                        writerOptions));
    if (writer == nullptr)
        return false;

    const int bufferSize = 32768;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), bufferSize);

    juce::int64 position = 0;
    const auto totalSamples = reader->lengthInSamples;

    while (totalSamples <= 0 || position < totalSamples)
    {
        int samplesToRead = bufferSize;
        if (totalSamples > 0)
        {
            const juce::int64 remaining = totalSamples - position;
            samplesToRead = static_cast<int>(std::min<juce::int64>(static_cast<juce::int64>(samplesToRead),
                                                                   remaining));
        }

        if (samplesToRead <= 0)
            break;

        if (!reader->read(buffer.getArrayOfWritePointers(),
                          static_cast<int>(reader->numChannels),
                          position,
                          samplesToRead))
            break;

        writer->writeFromAudioSampleBuffer(buffer, 0, samplesToRead);
        position += samplesToRead;

        if (totalSamples <= 0)
        {
            bool blockSilent = true;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                if (buffer.getMagnitude(ch, 0, samplesToRead) > 1.0e-6f)
                {
                    blockSilent = false;
                    break;
                }
            }

            if (blockSilent)
                break;
        }
    }

    writer.reset();

    if (!tempFile.overwriteTargetFileWithTemporary())
        return false;

    if (auto sourceInfo = source.getInfo(); sourceInfo.fileModificationTime != juce::Time())
        destination.setLastModificationTime(sourceInfo.fileModificationTime);

    return true;
}

void AudioEngine::adoptStateTree(juce::ValueTree stateNode)
{
    if (stateNode.isValid() && stateNode.hasType(kStateRootNode))
        appState = stateNode.createCopy();
    else
        appState = juce::ValueTree(kStateRootNode);

    ensureStateChildren();
    seedTransportDefaults(false);
    
    // Restore play mode from state
    if (transportState.isValid() && transportState.hasProperty(kStatePlayModeProp))
    {
        const int modeInt = transportState.getProperty(kStatePlayModeProp, static_cast<int>(PlayMode::Pattern));
        playMode = static_cast<PlayMode>(modeInt);
    }
    
    if (groupManager != nullptr)
        groupManager->refreshStateBinding();
}


void AudioEngine::ensureStateChildren()
{
    transportState = ensureChild(appState, kStateTransportNode);
    padsState = ensureChild(appState, kStatePadsNode);
    // Pattern state is now managed by StepClip's native ValueTree - no custom state needed
    groupsState = ensureChild(appState, kStateGroupsNode);
    mixerState = ensureChild(appState, kStateMixerNode);
    uiState = ensureChild(appState, kStateUiNode);
    settingsState = ensureChild(appState, kStateSettingsNode);
}

void AudioEngine::seedTransportDefaults(bool force)
{
    if (!transportState.isValid())
        return;

    const auto seedBool = [this, force](const juce::Identifier& propertyId, bool value)
    {
        if (force || !transportState.hasProperty(propertyId))
            transportState.setProperty(propertyId, value, nullptr);
    };

    const auto seedDouble = [this, force](const juce::Identifier& propertyId, double value)
    {
        if (force || !transportState.hasProperty(propertyId))
            transportState.setProperty(propertyId, value, nullptr);
    };

    const auto seedInt = [this, force](const juce::Identifier& propertyId, int value)
    {
        if (force || !transportState.hasProperty(propertyId))
            transportState.setProperty(propertyId, value, nullptr);
    };

    seedBool("hasEdit", edit != nullptr);
    seedBool("isPlaying", false);
    seedBool("isRecording", false);
    seedBool("isLooping", false);
    seedBool("isPaused", paused);
    seedBool("hasAudiblePosition", false);

    seedInt(kStatePlayModeProp, static_cast<int>(playMode));

    seedDouble("positionSeconds", 0.0);
    seedDouble("audiblePositionSeconds", 0.0);
    seedDouble("loopStartSeconds", 0.0);
    seedDouble("loopEndSeconds", 0.0);
    seedDouble("tempoBpm", 0.0);
    seedDouble("sampleRate", 0.0);
    seedDouble("swingPercent", 50.0);
}

juce::ValueTree AudioEngine::ensureChild(juce::ValueTree parent, const juce::Identifier& childId)
{
    if (!parent.isValid())
        return {};

    auto child = parent.getChildWithName(childId);
    if (!child.isValid())
    {
        child = juce::ValueTree(childId);
        parent.addChild(child, -1, nullptr);
    }
    return child;
}
bool AudioEngine::isPlaying() const
{
    return edit && edit->getTransport().isPlaying();
}

bool AudioEngine::isRecording() const
{
    return edit && edit->getTransport().isRecording();
}

bool AudioEngine::isLooping() const
{
    return edit && edit->getTransport().looping;
}

bool AudioEngine::canRecord() const
{
    if (!edit)
        return false;

    const auto& dm = engine.getDeviceManager().deviceManager;
    if (auto* device = dm.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits() > 0;

    return false;
}

bool AudioEngine::isPaused() const
{
    if (!edit)
        return false;

    const auto& transport = edit->getTransport();
    if (transport.isPlaying() || transport.isRecording())
        return false;

    return paused;
}

bool AudioEngine::isRecordArmed() const
{
    return recordArmed_;
}

void AudioEngine::setRecordArmed(bool armed)
{
    recordArmed_ = armed;
}

void AudioEngine::toggleRecordArm()
{
    setRecordArmed(!recordArmed_);
}

bool AudioEngine::isCountingIn() const
{
    // TE's native count-in parks the playhead at a negative pre-roll time
    // while the click-track fires; nothing plays until the position crosses
    // zero. Detecting it is a pure position check — play() routes armed
    // starts through transport.record() with count-in configured, so any
    // negative position here means we're mid pre-roll.
    if (!edit)
        return false;

    const auto& transport = edit->getTransport();
    if (!transport.isPlaying() && !transport.isRecording())
        return false;

    return transport.getPosition().inSeconds() < 0.0;
}

void AudioEngine::setAudioCaptureState(bool suiteActive, bool recording) noexcept
{
    audioCaptureRecording_.store(suiteActive && recording,
                                 std::memory_order_relaxed);
    audioCaptureSuiteActive_.store(suiteActive, std::memory_order_relaxed);
}

bool AudioEngine::isAudioCaptureSuiteActive() const noexcept
{
    return audioCaptureSuiteActive_.load(std::memory_order_relaxed);
}

bool AudioEngine::isAudioCaptureRecording() const noexcept
{
    return audioCaptureRecording_.load(std::memory_order_relaxed);
}

void AudioEngine::panicAllNotes()
{
    if (keyboardArp_ != nullptr)
        keyboardArp_->allNotesOff();

    // SamplerPlugin does not override Plugin::midiPanic(), so stop every pad
    // explicitly. This also handles one-shot samples whose release edge was
    // lost by the controller.
    for (auto& sampler : samplers_)
        if (sampler != nullptr)
            for (int pad = 0; pad < sampler->getPadCount(); ++pad)
                sampler->stopPad(pad);

    if (edit == nullptr)
        return;

    // FourOsc and ExternalPlugin implement midiPanic() themselves. Walking
    // every track makes the gesture independent of the currently selected
    // instrument slot or visible UI mode.
    for (auto* track : te::getAllTracks(*edit))
    {
        if (track == nullptr)
            continue;
        for (auto* plugin : track->pluginList.getPlugins())
            if (plugin != nullptr)
                plugin->midiPanic();
    }

    for (auto* plugin : edit->getMasterPluginList().getPlugins())
        if (plugin != nullptr)
            plugin->midiPanic();
}

bool AudioEngine::hasEdit() const
{
    return edit != nullptr;
}

juce::AudioDeviceManager& AudioEngine::getAudioDeviceManager()
{
    return engine.getDeviceManager().deviceManager;
}

SamplerInstrument& AudioEngine::getSampler()
{
    const int activeGroup = groupManager != nullptr
        ? groupManager->getActiveGroupIndex() : 0;
    auto* sampler = getSamplerForGroup(activeGroup);
    jassert(sampler != nullptr);
    return *sampler;
}

const SamplerInstrument& AudioEngine::getSampler() const
{
    const int activeGroup = groupManager != nullptr
        ? groupManager->getActiveGroupIndex() : 0;
    auto* sampler = getSamplerForGroup(activeGroup);
    jassert(sampler != nullptr);
    return *sampler;
}

SamplerInstrument* AudioEngine::getSamplerForGroup(int groupIndex) noexcept
{
    if (groupIndex < 0 || groupIndex >= kSamplerGroupCount)
        return nullptr;
    return samplers_[static_cast<size_t>(groupIndex)].get();
}

const SamplerInstrument* AudioEngine::getSamplerForGroup(int groupIndex) const noexcept
{
    if (groupIndex < 0 || groupIndex >= kSamplerGroupCount)
        return nullptr;
    return samplers_[static_cast<size_t>(groupIndex)].get();
}

GroupManager& AudioEngine::getGroupManager()
{
    jassert(groupManager != nullptr);
    return *groupManager;
}

const GroupManager& AudioEngine::getGroupManager() const
{
    jassert(groupManager != nullptr);
    return *groupManager;
}

KeyboardInstrumentBank& AudioEngine::getKeyboardBank()
{
    jassert(keyboardBank_ != nullptr);
    return *keyboardBank_;
}

const KeyboardInstrumentBank& AudioEngine::getKeyboardBank() const
{
    jassert(keyboardBank_ != nullptr);
    return *keyboardBank_;
}

PluginConfigRegistry& AudioEngine::getPluginConfigRegistry()
{
    jassert(pluginConfigRegistry_ != nullptr);
    return *pluginConfigRegistry_;
}

const PluginConfigRegistry& AudioEngine::getPluginConfigRegistry() const
{
    jassert(pluginConfigRegistry_ != nullptr);
    return *pluginConfigRegistry_;
}

Arpeggiator& AudioEngine::getKeyboardArp()
{
    jassert(keyboardArp_ != nullptr);
    return *keyboardArp_;
}

const Arpeggiator& AudioEngine::getKeyboardArp() const
{
    jassert(keyboardArp_ != nullptr);
    return *keyboardArp_;
}

float AudioEngine::getPadLevelDb(int padIndex) const
{
    return getSampler().getLevelDb(padIndex);
}

AudioEngine::TransportSnapshot AudioEngine::getTransportSnapshot()
{
    TransportSnapshot snapshot;

    if (!edit)
        return snapshot;

    snapshot.hasEdit = true;

    auto& transport = edit->getTransport();

    snapshot.isPlaying   = transport.isPlaying();
    snapshot.isRecording = transport.isRecording();
    snapshot.isLooping   = transport.looping;
    snapshot.isPaused    = isPaused();
    snapshot.playMode    = playMode;

    const auto position = transport.getPosition();
    snapshot.positionSeconds = position.inSeconds();
    snapshot.audiblePositionSeconds = snapshot.positionSeconds;

    const auto loopRange = transport.getLoopRange();
    snapshot.loopStartSeconds = loopRange.getStart().inSeconds();
    snapshot.loopEndSeconds   = loopRange.getEnd().inSeconds();

    snapshot.tempoBpm = edit->tempoSequence.getBpmAt(position);

    if (transportState.isValid())
        snapshot.swingPercent = (double) transportState.getProperty("swingPercent", 50.0);

    if (auto* playbackContext = transport.getCurrentPlaybackContext())
    {
        snapshot.hasAudiblePosition = true;
        snapshot.audiblePositionSeconds = playbackContext->getAudibleTimelineTime().inSeconds();
        snapshot.sampleRate = playbackContext->getSampleRate();
    }

    return snapshot;
}

T9Dictionary& AudioEngine::getT9Dictionary()
{
    jassert(t9Dictionary_ != nullptr);
    return *t9Dictionary_;
}

const T9Dictionary& AudioEngine::getT9Dictionary() const
{
    jassert(t9Dictionary_ != nullptr);
    return *t9Dictionary_;
}

double AudioEngine::getSwingPercent() const
{
    if (!transportState.isValid())
        return 50.0;
    return (double) transportState.getProperty("swingPercent", 50.0);
}

void AudioEngine::setSwingPercent(double percent)
{
    const double clamped = juce::jlimit(50.0, 75.0, percent);
    if (transportState.isValid())
        transportState.setProperty("swingPercent", clamped, nullptr);
    applySwingToSampler();
}

void AudioEngine::applySwingToSampler()
{
    const double percent = getSwingPercent();
    const float  strength = static_cast<float>((percent - 50.0) / 25.0);  // [0.0, 1.0]
    for (auto& sampler : samplers_)
        if (sampler != nullptr)
            sampler->applySwing(strength);
}

bool AudioEngine::createSamplerGroup(int groupIndex)
{
    if (groupIndex < 0 || groupIndex >= kSamplerGroupCount)
        return false;

    auto& slot = samplers_[static_cast<size_t>(groupIndex)];
    if (slot != nullptr)
        return true;

    slot = std::make_unique<SamplerInstrument>(*this, groupIndex);
    if (edit == nullptr)
        return true;

    slot->attachToEdit(*edit);
    for (int index = slot->getPadCount(); index < kInitialPadCount; ++index)
        if (slot->addPad("Pad " + juce::String(index + 1)) < 0)
            return false;

    const double percent = getSwingPercent();
    slot->applySwing(static_cast<float>((percent - 50.0) / 25.0));
    return true;
}

void AudioEngine::removeSamplerGroup(int groupIndex)
{
    if (groupIndex < 0 || groupIndex >= kSamplerGroupCount)
        return;

    auto& slot = samplers_[static_cast<size_t>(groupIndex)];
    if (slot != nullptr)
        slot->removeFromEdit();
    slot.reset();
}

void AudioEngine::resetSamplerGroups()
{
    for (int i = 1; i < kSamplerGroupCount; ++i)
        samplers_[static_cast<size_t>(i)].reset();

    if (samplers_[0] == nullptr)
        samplers_[0] = std::make_unique<SamplerInstrument>(*this, 0);
}
