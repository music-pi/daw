#include "KeyboardInstrumentBank.h"

#include "AudioEngine.h"

namespace
{
constexpr const char* kFolderName     = "Instruments";
constexpr const char* kSlotIndexProp  = "instrumentSlot";
constexpr const char* kDisplayNameProp = "slotName";
}

KeyboardInstrumentBank::KeyboardInstrumentBank(AudioEngine& e) : engine_(e) {}
KeyboardInstrumentBank::~KeyboardInstrumentBank() { detach(); }

void KeyboardInstrumentBank::attachToEdit(te::Edit& edit)
{
    detach();
    edit_ = &edit;
    activePatternIndex_ = 0;

    // Adopt the folder only if it already exists on this edit (e.g. after
    // loading a saved project). Fresh edits don't get a folder until the
    // first loadInstrument call so that empty bank states don't materialise
    // empty tracks that outlive the bank instance.
    for (auto* t : te::getTopLevelTracks(edit))
    {
        if (auto* f = dynamic_cast<te::FolderTrack*>(t))
        {
            if (f->getName() == kFolderName)
            {
                folder_ = f;
                ensureFolderPlugins();
                break;
            }
        }
    }
    rediscoverSlotsFromEdit();
    activeSlot_ = 0;
}

void KeyboardInstrumentBank::detach()
{
    slots_.clear();
    folder_ = nullptr;
    edit_ = nullptr;
    activeSlot_ = 0;
    activePatternIndex_ = 0;
}

void KeyboardInstrumentBank::ensureFolder()
{
    if (edit_ == nullptr) return;
    for (auto* t : te::getTopLevelTracks(*edit_))
    {
        if (auto* f = dynamic_cast<te::FolderTrack*>(t))
        {
            if (f->getName() == kFolderName)
            {
                folder_ = f;
                ensureFolderPlugins();
                return;
            }
        }
    }
    // Create the folder without Tracktion's full default chain, then add the
    // two plugins the Instruments mix bus actually needs in a deterministic
    // order below.
    folder_ = edit_->insertNewFolderTrack({ nullptr, nullptr }, nullptr, false);
    if (folder_ != nullptr)
    {
        folder_->setName(kFolderName);
        ensureFolderPlugins();
    }
}

void KeyboardInstrumentBank::ensureFolderPlugins()
{
    if (edit_ == nullptr || folder_ == nullptr) return;

    if (folder_->getVolumePlugin() == nullptr)
    {
        if (auto plugin = edit_->getPluginCache().createNewPlugin(
                te::VolumeAndPanPlugin::xmlTypeName, {}))
            folder_->pluginList.insertPlugin(plugin, -1, nullptr);
    }

    if (folder_->pluginList.findFirstPluginOfType<te::LevelMeterPlugin>() == nullptr)
    {
        if (auto plugin = edit_->getPluginCache().createNewPlugin(
                te::LevelMeterPlugin::xmlTypeName, {}))
            folder_->pluginList.insertPlugin(plugin, -1, nullptr);
    }
}

void KeyboardInstrumentBank::rediscoverSlotsFromEdit()
{
    slots_.clear();
    if (folder_ == nullptr) return;

    struct SlotWithIndex { int index; Slot slot; };
    std::vector<SlotWithIndex> tagged;

    for (auto* t : folder_->getAllSubTracks(false))
    {
        auto* at = dynamic_cast<te::AudioTrack*>(t);
        if (at == nullptr) continue;
        if (! at->state.hasProperty(kSlotIndexProp)) continue;

        SlotWithIndex entry;
        entry.index = static_cast<int>(at->state.getProperty(kSlotIndexProp, -1));

        Slot& s = entry.slot;
        s.track = at;
        s.plugin = at->pluginList.findFirstPluginOfType<te::ExternalPlugin>();
        if (s.plugin == nullptr)
            s.plugin = at->pluginList.findFirstPluginOfType<te::FourOscPlugin>();
        s.midiClip = nullptr;
        for (auto* clip : at->getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) { s.midiClip = mc; break; }
        if (s.midiClip == nullptr)
            s.midiClip = ensureMidiClip(*at);

        s.displayName = at->state.getProperty(kDisplayNameProp, at->getName()).toString();
        if (s.displayName.isEmpty() && s.plugin != nullptr)
            s.displayName = s.plugin->getName();
        // Bind the per-slot pattern archive so swaps can find the track's
        // ClipSlotList without another lookup.
        s.patternBank.attach(*at);
        tagged.push_back(std::move(entry));
    }

    // Subtrack order isn't guaranteed to match the saved slot index, so sort
    // explicitly — otherwise nav cursors land on the wrong slot after load.
    std::sort(tagged.begin(), tagged.end(),
        [](const SlotWithIndex& a, const SlotWithIndex& b) { return a.index < b.index; });

    for (auto& e : tagged) slots_.push_back(std::move(e.slot));
}

te::AudioTrack::Ptr KeyboardInstrumentBank::createSlotTrack(int slotIndex)
{
    if (edit_ == nullptr || folder_ == nullptr) return nullptr;
    auto track = edit_->insertNewAudioTrack({ folder_.get(), nullptr }, nullptr);
    if (track != nullptr)
    {
        track->state.setProperty(kSlotIndexProp, slotIndex, nullptr);
        track->setName("Instrument " + juce::String(slotIndex + 1));
    }
    return track;
}

te::MidiClip::Ptr KeyboardInstrumentBank::ensureMidiClip(te::AudioTrack& track)
{
    if (edit_ == nullptr) return nullptr;
    for (auto* clip : track.getClips())
        if (auto* mc = dynamic_cast<te::MidiClip*>(clip))
            return mc;

    // Honour the sampler's current pattern length so keyboard slots created
    // after a bars change still match the pad grid.
    const int nativeBars = juce::jmax(1, engine_.getSampler().getBarsPerPattern());
    const auto clipEnd = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats { nativeBars, {} });
    const auto range = tracktion::core::TimeRange {
        tracktion::core::TimePosition{}, clipEnd };
    auto clip = track.insertMIDIClip(range, nullptr);

    // Stamp the groove template up front so applySwing can adjust strength
    // without ever having to set the template on already-live clips.
    if (clip != nullptr)
    {
        if (clip->getGrooveTemplate().isEmpty())
            clip->setGrooveTemplate("Basic 16th Swing");
        clip->setGrooveStrength(swingStrength_);
    }
    return clip;
}

te::Plugin* KeyboardInstrumentBank::installPlugin(
    te::AudioTrack& track,
    const juce::PluginDescription& desc)
{
    if (edit_ == nullptr) return nullptr;

    // Remove the existing instrument source while leaving the track's volume
    // and meter infrastructure intact.
    if (auto* existing = track.pluginList.findFirstPluginOfType<te::ExternalPlugin>())
        existing->deleteFromParent();
    if (auto* existing = track.pluginList.findFirstPluginOfType<te::FourOscPlugin>())
        existing->deleteFromParent();

    te::Plugin::Ptr plugin;
    if (te::PluginManager::isBuiltInPlugin(desc))
    {
        plugin = edit_->getPluginCache().createNewPlugin(
            desc.fileOrIdentifier, {});
    }
    else
    {
        plugin = edit_->getPluginCache().createNewPlugin(
            te::ExternalPlugin::xmlTypeName, desc);
    }
    if (plugin == nullptr) return nullptr;

    if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get()))
    {
        // Tracktion's stock state enables only oscillator 1; initialise a
        // balanced, audible four-oscillator patch so Osc Mix is immediately
        // useful on a clean installation.
        static constexpr int kWaveShapes[] = { 1, 3, 2, 4 };
        static constexpr float kLevelsDb[] = { -6.0f, -12.0f, -18.0f, -24.0f };
        auto params = fourOsc->getAutomatableParameters();
        for (int i = 0; i < 4; ++i)
        {
            const auto suffix = juce::String(i + 1);
            fourOsc->state.setProperty(
                "waveShape" + suffix, kWaveShapes[i], nullptr);
            for (auto* param : params)
                if (param != nullptr && param->paramID == "level" + suffix)
                    param->setParameter(kLevelsDb[i], juce::sendNotification);
        }
        for (auto* param : params)
            if (param != nullptr && param->paramID == "masterLevel")
                param->setParameter(-3.0f, juce::sendNotification);
    }

    track.pluginList.insertPlugin(plugin, 0, nullptr);
    return plugin.get();
}

void KeyboardInstrumentBank::setActiveSlot(int index)
{
    activeSlot_ = juce::jlimit(0, getNumSlots(), index);
}

void KeyboardInstrumentBank::nextSlot()
{
    const int maxIndex = getNumSlots();
    setActiveSlot(activeSlot_ >= maxIndex ? 0 : activeSlot_ + 1);
}

void KeyboardInstrumentBank::prevSlot()
{
    const int maxIndex = getNumSlots();
    setActiveSlot(activeSlot_ <= 0 ? maxIndex : activeSlot_ - 1);
}

bool KeyboardInstrumentBank::loadInstrument(const juce::PluginDescription& desc)
{
    auto logFail = [&desc](const juce::String& reason) {
        juce::Logger::writeToLog(
            "[KeyboardInstrumentBank::loadInstrument] " + reason
            + " — plugin '" + desc.name + "' (" + desc.pluginFormatName + ")");
    };

    if (edit_ == nullptr)        { logFail("no edit attached"); return false; }
    ensureFolder();
    if (folder_ == nullptr)      { logFail("folder creation failed"); return false; }

    auto applyPluginToSlot = [this, &logFail](Slot& s, const juce::PluginDescription& d)
    {
        s.plugin = installPlugin(*s.track, d);
        if (s.plugin == nullptr) { logFail("installPlugin returned nullptr"); return false; }
        s.displayName = s.plugin->getName();
        s.track->setName(s.displayName);
        s.track->state.setProperty(kDisplayNameProp, s.displayName, nullptr);
        return true;
    };

    if (isOnEmptySlot())
    {
        Slot s;
        const int newIndex = getNumSlots();
        s.track = createSlotTrack(newIndex);
        if (s.track == nullptr) { logFail("createSlotTrack failed"); return false; }
        if (! applyPluginToSlot(s, desc)) return false;
        s.midiClip = ensureMidiClip(*s.track);
        // Bind the per-slot pattern archive now that the track exists.
        s.patternBank.attach(*s.track);
        slots_.push_back(std::move(s));
        activeSlot_ = newIndex;
        // The app's playback context is already running by the time an
        // instrument is loaded. Rebuild it so the new track receives live
        // MIDI injection and its instrument is present in the audio graph.
        edit_->restartPlayback();
        return true;
    }

    if (! applyPluginToSlot(slots_[static_cast<size_t>(activeSlot_)], desc))
        return false;

    edit_->restartPlayback();
    return true;
}

void KeyboardInstrumentBank::deleteActiveSlot()
{
    if (isOnEmptySlot()) return;
    removeSlotAt(activeSlot_);
    activeSlot_ = juce::jlimit(0, getNumSlots(), activeSlot_);
}

void KeyboardInstrumentBank::removeSlotAt(int index)
{
    if (index < 0 || index >= getNumSlots()) return;
    auto& s = slots_[static_cast<size_t>(index)];
    if (s.track != nullptr && edit_ != nullptr)
        edit_->deleteTrack(s.track.get());
    slots_.erase(slots_.begin() + index);
    if (edit_ != nullptr)
        edit_->restartPlayback();
    // Renumber the remaining slots' state property so reopening picks up
    // a contiguous slot index set.
    for (int i = 0; i < getNumSlots(); ++i)
        if (slots_[(size_t) i].track != nullptr)
            slots_[(size_t) i].track->state.setProperty(kSlotIndexProp, i, nullptr);
}

te::AudioTrack* KeyboardInstrumentBank::getTrack() const
{
    if (isOnEmptySlot()) return nullptr;
    return slots_[(size_t) activeSlot_].track.get();
}

te::Plugin* KeyboardInstrumentBank::getPlugin() const
{
    if (isOnEmptySlot()) return nullptr;
    return slots_[(size_t) activeSlot_].plugin;
}

te::MidiClip* KeyboardInstrumentBank::getMidiClip() const
{
    if (isOnEmptySlot()) return nullptr;
    return slots_[(size_t) activeSlot_].midiClip.get();
}

juce::String KeyboardInstrumentBank::getActiveName() const
{
    if (isOnEmptySlot()) return {};
    return slots_[(size_t) activeSlot_].displayName;
}

const KeyboardInstrumentBank::Slot*
KeyboardInstrumentBank::getSlot(int index) const
{
    if (index < 0 || index >= getNumSlots()) return nullptr;
    return &slots_[(size_t) index];
}

juce::ValueTree KeyboardInstrumentBank::toState() const
{
    // The actual plugins and pattern clips live in the Edit tree (TE
    // serialises them). Keep both cursors here so a fresh process knows which
    // saved live clip is currently represented on the timeline.
    juce::ValueTree v("KeyboardInstrumentBank");
    v.setProperty("activeSlot", activeSlot_, nullptr);
    v.setProperty("activePatternIndex", activePatternIndex_, nullptr);
    return v;
}

void KeyboardInstrumentBank::restoreFromState(const juce::ValueTree& v)
{
    // SamplerInstrument restores first and owns the canonical pattern cursor.
    // Use it as the migration fallback for projects saved before the keyboard
    // bank persisted its own cursor.
    const int samplerPattern = engine_.getSampler().getActivePatternIndex();
    if (! v.hasType("KeyboardInstrumentBank"))
    {
        activePatternIndex_ = samplerPattern;
        return;
    }
    const int n = getNumSlots();
    activeSlot_ = juce::jlimit(0, n, static_cast<int>(v.getProperty("activeSlot", 0)));
    activePatternIndex_ = juce::jmax(0, static_cast<int>(
        v.getProperty("activePatternIndex", samplerPattern)));
}

void KeyboardInstrumentBank::archiveLiveClipsTo(int patternIndex)
{
    if (patternIndex < 0) return;
    for (auto& s : slots_)
    {
        if (s.midiClip == nullptr) continue;
        s.patternBank.storePattern(patternIndex, s.midiClip->getSequence());
    }
}

bool KeyboardInstrumentBank::clearPattern(int patternIndex)
{
    if (patternIndex < 0) return false;

    if (patternIndex == activePatternIndex_)
    {
        for (auto& s : slots_)
        {
            if (s.midiClip == nullptr) continue;
            s.midiClip->getSequence().removeAllNotes(nullptr);
            s.midiClip->getSequence().removeAllControllers(nullptr);
            // Mirror the sampler: keep the bank slot in sync so a later
            // pattern-swap doesn't restore the pre-clear state.
            s.patternBank.storePattern(patternIndex, s.midiClip->getSequence());
        }
    }
    else
    {
        for (auto& s : slots_)
            s.patternBank.mutatePattern(patternIndex, [](te::MidiList& list) {
                list.removeAllNotes(nullptr);
                list.removeAllControllers(nullptr);
            });
    }
    return true;
}

namespace
{
    // Mirror of SamplerInstrument's helpers — drain a MidiList and copy notes.
    void clearList(te::MidiList& list)
    {
        list.removeAllNotes(nullptr);
        list.removeAllControllers(nullptr);
    }

    void copyListNotes(const te::MidiList& src, te::MidiList& dest)
    {
        for (auto* n : src.getNotes())
            dest.addNote(n->getNoteNumber(), n->getStartBeat(), n->getLengthBeats(),
                         n->getVelocity(), n->getColour(), nullptr);
    }
}

void KeyboardInstrumentBank::materializeSong(
    const std::vector<SamplerInstrument::SongLane>& lanes)
{
    if (edit_ == nullptr) return;

    size_t totalBlocks = 0;
    for (const auto& lane : lanes)
        totalBlocks += lane.blocks.size();
    if (totalBlocks == 0) return;

    // Tear down any stale materialisation so a rebuild is clean.
    bool alreadyMaterialized = false;
    for (const auto& s : slots_)
        if (! s.materializedClips.empty()) { alreadyMaterialized = true; break; }
    if (alreadyMaterialized)
        dematerializeSong();

    // Archive each slot's live clip into its bank at the active pattern index
    // so the user's latest instrument edits survive the swap — the live clip
    // is about to be parked offscreen.
    archiveLiveClipsTo(activePatternIndex_);

    // 4/4 default — honour the first TimeSig so non-4/4 edits stay in step.
    int beatsPerBar = 4;
    if (auto* ts = edit_->tempoSequence.getTimeSig(0))
        beatsPerBar = juce::jmax(1, ts->numerator.get());

    // Loop at the sampler's native pattern length so keyboard blocks replay
    // in lockstep with pad blocks of the same block.bars.
    const int nativeBars = juce::jmax(1, engine_.getSampler().getBarsPerPattern());
    const auto loopLenBeats = tracktion::BeatDuration::fromBeats(
        (double) nativeBars * (double) beatsPerBar);

    int totalBars = 0;
    for (const auto& lane : lanes)
        for (const auto& block : lane.blocks)
            totalBars = juce::jmax(totalBars, block.startBar + block.bars);

    // Park the live clip well past the song end so TE doesn't render it.
    const int parkAtBar = juce::jmax(totalBars + 1000, 1000);
    const auto parkStart = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ parkAtBar, {} });
    const auto parkEnd = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ parkAtBar + nativeBars, {} });

    for (auto& s : slots_)
    {
        if (s.track == nullptr) continue;

        if (s.midiClip != nullptr)
        {
            te::ClipPosition parkPos;
            parkPos.time   = { parkStart, parkEnd };
            parkPos.offset = tracktion::core::TimeDuration{};
            s.midiClip->setPosition(parkPos);
        }

        s.materializedClips.clear();
        s.materializedClips.reserve(totalBlocks);

        for (const auto& lane : lanes)
        {
            for (const auto& block : lane.blocks)
            {
                const int blockBars = juce::jmax(1, block.bars);
                const int barStart  = juce::jmax(0, block.startBar);
                const auto rangeStart = edit_->tempoSequence.toTime(
                    tracktion::core::tempo::BarsAndBeats{ barStart, {} });
                const auto rangeEnd = edit_->tempoSequence.toTime(
                    tracktion::core::tempo::BarsAndBeats{ barStart + blockBars, {} });

                auto newClip = s.track->insertMIDIClip(
                    tracktion::core::TimeRange{ rangeStart, rangeEnd }, nullptr);

                if (newClip != nullptr)
                {
                    auto& destSeq = newClip->getSequence();
                    clearList(destSeq);
                    s.patternBank.readPattern(block.patternIndex,
                        [&](const te::MidiList& src) { copyListNotes(src, destSeq); });

                    // Loop the native pattern length so long blocks replay it.
                    newClip->setLoopRangeBeats(
                        { tracktion::BeatPosition{}, loopLenBeats });

                    // Mirror sampler — placed clips groove in lockstep with pad blocks.
                    if (newClip->getGrooveTemplate().isEmpty())
                        newClip->setGrooveTemplate("Basic 16th Swing");
                    newClip->setGrooveStrength(swingStrength_);
                }

                s.materializedClips.push_back(newClip);
            }
        }
    }
}

void KeyboardInstrumentBank::dematerializeSong()
{
    if (edit_ == nullptr) return;

    bool anyMaterialized = false;
    for (auto& s : slots_)
    {
        if (s.materializedClips.empty()) continue;
        anyMaterialized = true;

        for (auto& clip : s.materializedClips)
            if (clip != nullptr) clip->removeFromParent();
        s.materializedClips.clear();
    }

    if (! anyMaterialized) return;

    // Restore the live clip to bar 0..nativeBars using the sampler's current
    // grid so pattern-mode playback spans the full pattern length.
    const int nativeBars = juce::jmax(1, engine_.getSampler().getBarsPerPattern());
    const auto restoreStart = tracktion::core::TimePosition{};
    const auto restoreEnd = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ nativeBars, {} });

    for (auto& s : slots_)
    {
        if (s.midiClip == nullptr) continue;
        te::ClipPosition restorePos;
        restorePos.time   = { restoreStart, restoreEnd };
        restorePos.offset = tracktion::core::TimeDuration{};
        s.midiClip->setPosition(restorePos);
        s.midiClip->setLoopRangeBeats({ tracktion::BeatPosition{},
                                         tracktion::BeatDuration{} });
    }
}

void KeyboardInstrumentBank::setActivePatternIndex(int newIndex)
{
    if (newIndex < 0) return;
    if (newIndex == activePatternIndex_) return;

    // Archive the live clip of each slot into its bank at the OLD pattern
    // index before stomping the live sequence — otherwise the user's current
    // edits are lost.
    for (auto& s : slots_)
    {
        if (s.midiClip == nullptr) continue;
        s.patternBank.storePattern(activePatternIndex_, s.midiClip->getSequence());
    }

    activePatternIndex_ = newIndex;

    // Clear the live clip first so empty bank slots leave the live clip
    // empty (restorePattern no-ops when the slot has no stored clip).
    for (auto& s : slots_)
    {
        if (s.midiClip == nullptr) continue;
        auto& seq = s.midiClip->getSequence();
        seq.removeAllNotes(nullptr);
        seq.removeAllControllers(nullptr);
    }

    for (auto& s : slots_)
    {
        if (s.midiClip == nullptr) continue;
        s.patternBank.restorePattern(activePatternIndex_, s.midiClip->getSequence());
    }
}

bool KeyboardInstrumentBank::setPatternLength(int patternIndex, int bars, int stepsPerBar)
{
    if (edit_ == nullptr) return false;

    bars        = juce::jmax(1, bars);
    stepsPerBar = juce::jmax(1, stepsPerBar);
    juce::ignoreUnused(patternIndex, stepsPerBar);

    // Anchor the new length at the live clip's current start — slots are
    // parked far out during song materialisation, so absolute bar 0 isn't
    // always the correct origin.
    const auto firstStart = [&]
    {
        for (const auto& s : slots_)
            if (s.midiClip != nullptr)
                return s.midiClip->getPosition().getStart();
        return tracktion::core::TimePosition{};
    }();

    const auto newEndPos = edit_->tempoSequence.toTime(
        tracktion::core::tempo::BarsAndBeats{ bars, {} });
    const auto newLength = tracktion::core::TimeDuration::fromSeconds(
        newEndPos.inSeconds() - firstStart.inSeconds());

    for (auto& s : slots_)
        if (s.midiClip != nullptr)
            s.midiClip->setLength(newLength, false);

    return true;
}

void KeyboardInstrumentBank::insertPatternSlot(int atIndex)
{
    // Mirror the sampler's per-pad loop: every keyboard slot's ClipSlotList
    // must widen in lockstep with numPatterns_, otherwise bank[i] no longer
    // maps to pattern i after an insertion.
    for (auto& s : slots_)
    {
        if (s.track == nullptr) continue;
        s.track->getClipSlotList().insertSlot(atIndex);
    }
}

void KeyboardInstrumentBank::snapshotPattern(int srcIndex,
    std::vector<std::vector<NoteCopy>>& out) const
{
    out.assign(slots_.size(), {});
    if (srcIndex < 0) return;

    for (size_t i = 0; i < slots_.size(); ++i)
    {
        const auto& s = slots_[i];
        auto capture = [&](const te::MidiList& list)
        {
            auto& dst = out[i];
            for (auto* n : list.getNotes())
                dst.push_back({ n->getNoteNumber(),
                                n->getStartBeat(),
                                n->getLengthBeats(),
                                n->getVelocity(),
                                n->getColour() });
        };

        if (srcIndex == activePatternIndex_ && s.midiClip != nullptr)
            capture(s.midiClip->getSequence());
        else
            s.patternBank.readPattern(srcIndex, capture);
    }
}

void KeyboardInstrumentBank::writePattern(int destIndex,
    const std::vector<std::vector<NoteCopy>>& in)
{
    if (destIndex < 0) return;

    const size_t n = juce::jmin(slots_.size(), in.size());
    for (size_t i = 0; i < n; ++i)
    {
        auto& s = slots_[i];
        const auto& notes = in[i];
        if (notes.empty()) continue;

        s.patternBank.mutatePattern(destIndex, [&notes](te::MidiList& list)
        {
            for (const auto& note : notes)
                list.addNote(note.noteNumber, note.start, note.length,
                             note.velocity, note.colour, nullptr);
        });
    }
}

void KeyboardInstrumentBank::applySwing(float strength)
{
    const float clamped = juce::jlimit(0.0f, 1.0f, strength);
    swingStrength_ = clamped;
    for (auto& s : slots_)
    {
        if (s.midiClip == nullptr) continue;
        if (s.midiClip->getGrooveTemplate().isEmpty())
            s.midiClip->setGrooveTemplate("Basic 16th Swing");
        s.midiClip->setGrooveStrength(clamped);
    }
}
