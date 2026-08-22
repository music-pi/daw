#include "MixerWidget.h"

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/KeyboardInstrumentBank.h"
#include "../../engine/SamplerInstrument.h"
#include "../../engine/commands/SetGainDbCommand.h"
#include "../../engine/commands/SetPanCommand.h"
#include "../components/mixer/MixerUtils.h"
#include "../theme/UiTheme.h"
#include "MixerStateKeys.h"

#include <algorithm>

#include <juce_audio_basics/juce_audio_basics.h>

namespace
{
    juce::String levelToString(MixerWidget::MixerLevel l)
    {
        return l == MixerWidget::MixerLevel::Global ? "global" : "sound";
    }

    te::LevelMeterPlugin* findLevelMeter(te::PluginList& plugins)
    {
        for (auto* plugin : plugins.getPlugins())
            if (auto* meter = dynamic_cast<te::LevelMeterPlugin*>(plugin))
                return meter;
        return nullptr;
    }
}

MixerWidget::MixerWidget(std::optional<te::EditItemID> initialGroup,
                         std::optional<te::EditItemID> initialChannel)
    : initialGroup_(initialGroup)
    , preferredChannel_(initialChannel)
{
    addAndMakeVisible(stripsComponent_);
}

MixerWidget::~MixerWidget()
{
    // Belt-and-suspenders: if onDeactivated didn't run (destroyed on a path
    // that skipped the normal lifecycle), still deregister clients so the
    // audio thread doesn't dereference freed Client objects.
    if (lastSeenEdit_ != nullptr)
    {
        for (auto& [id, reg] : meterClients_)
        {
            if (reg.plugin != nullptr && reg.client != nullptr)
                reg.plugin->measurer.removeClient(*reg.client);
        }
    }
    meterClients_.clear();
}

WidgetDescriptor MixerWidget::describe() const
{
    return { "mixer", 1, false, DisplayConstraint::Any };
}

juce::String MixerWidget::getTitle() const
{
    return "Mixer";
}

juce::String MixerWidget::getTitleSubtitle() const
{
    if (level_ == MixerLevel::Global)
        return "Global";
    if (focusedGroup_.has_value() && engine().getEdit() != nullptr)
    {
        auto* track = te::findTrackForID(*engine().getEdit(), *focusedGroup_);
        if (track != nullptr)
            return "Sounds \xC2\xB7 " + track->getName();
    }
    return "Sounds";
}

void MixerWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    hw().setLed("mixer", HardwareConstants::kLedBright, describe().id.toStdString());
    hw().setLed("solo", HardwareConstants::kLedDim, "solo");
    hw().setLed("muteChoke", HardwareConstants::kLedDim, "muteChoke");

    auto* edit = engine().getEdit();
    auto* requestedGroup = initialGroup_.has_value() && edit != nullptr
        ? dynamic_cast<te::FolderTrack*>(te::findTrackForID(*edit, *initialGroup_))
        : nullptr;
    auto* retainedGroup = !initialGroup_.has_value()
                              && edit != nullptr
                              && edit == lastSeenEdit_
                              && level_ == MixerLevel::Sound
                              && focusedGroup_.has_value()
        ? dynamic_cast<te::FolderTrack*>(te::findTrackForID(*edit, *focusedGroup_))
        : nullptr;
    const bool retainGlobal = !initialGroup_.has_value()
                           && edit != nullptr
                           && edit == lastSeenEdit_
                           && level_ == MixerLevel::Global;
    initialGroup_.reset();
    auto& sampler = engine().getSampler();
    if (requestedGroup != nullptr)
    {
        level_ = MixerLevel::Sound;
        focusedGroup_ = requestedGroup->itemID;
    }
    else if (retainedGroup != nullptr)
    {
        level_ = MixerLevel::Sound;
        focusedGroup_ = retainedGroup->itemID;
    }
    else if (retainGlobal)
    {
        level_ = MixerLevel::Global;
        focusedGroup_.reset();
    }
    else if (sampler.getSelectedPad() >= 0 && sampler.getFolder() != nullptr)
    {
        level_ = MixerLevel::Sound;
        focusedGroup_ = sampler.getFolder()->itemID;
        if (auto* selectedTrack = sampler.getTrack(sampler.getSelectedPad()))
            preferredChannel_ = selectedTrack->itemID;
    }
    else
    {
        level_ = MixerLevel::Global;
        focusedGroup_.reset();
    }

    scrollOffset_ = 0;
    activeChannelIndex_ = -1;
    muteHeld_ = false;
    soloHeld_ = false;
    knobTransactionOpen_ = false;

    slowTickCounter_ = 0;

    engine().getSampler().addListener(this);
    padFlashDb_.clear();

    refreshChannels();
    pickInitialActiveChannel();
    updateNavLeds();
    writeMixerStateToValueTree();
}

void MixerWidget::onDeactivated()
{
    knobTransactionOpen_ = false;

    if (lastSeenEdit_ == engine().getEdit() && !hasStaleDescriptors())
    {
        if (auto* active = descriptorForChannel(scrollOffset_ + activeChannelIndex_);
            active != nullptr && active->track != nullptr)
            preferredChannel_ = active->track->itemID;
    }

    engine().getSampler().removeListener(this);
    padFlashDb_.clear();

    // Deregister any meter clients we registered during activation.
    for (auto& [id, reg] : meterClients_)
    {
        if (reg.plugin != nullptr && reg.client != nullptr)
            reg.plugin->measurer.removeClient(*reg.client);
    }
    meterClients_.clear();

    hw().setLed("solo", 0, "solo");
    hw().setLed("muteChoke", 0, "muteChoke");
    hw().setLed("navUp", 0, "navUp");
    hw().setLed("navDown", 0, "navDown");
    hw().setLed("arrowLeft", 0, "arrowLeft");
    hw().setLed("arrowRight", 0, "arrowRight");
}

void MixerWidget::onEditAboutToBeReplaced()
{
    knobTransactionOpen_ = false;
    for (auto& [id, registration] : meterClients_)
        if (registration.plugin != nullptr && registration.client != nullptr)
            registration.plugin->measurer.removeClient(*registration.client);
    meterClients_.clear();
    states_.clear();
    descriptors_.clear();
    descriptorsSignature_ = 0;
    lastSeenEdit_ = nullptr;
}

void MixerWidget::onEditReplaced()
{
    rebindStaleDescriptors();
    repaint();
}

void MixerWidget::onActiveSamplerAboutToChange()
{
    engine().getSampler().removeListener(this);
}

void MixerWidget::onActiveSamplerChanged()
{
    engine().getSampler().addListener(this);
    padFlashDb_.clear();
    refreshChannels();
    repaint();
}

std::vector<std::string> MixerWidget::requiredResources(int page)
{
    if (page != 0)
        return {};

    std::vector<std::string> resources;
    resources.push_back("mixer");
    resources.push_back("solo");
    resources.push_back("muteChoke");
    resources.push_back("navUp");
    resources.push_back("navDown");
    resources.push_back("arrowLeft");
    resources.push_back("arrowRight");

    const int base = (panelOffset_ == 0) ? 1 : 5;
    for (int i = 0; i < kChannelsPerPanel; ++i)
    {
        resources.push_back("d" + std::to_string(base + i));
        resources.push_back("k" + std::to_string(base + i));
    }
    return resources;
}

std::vector<Option> MixerWidget::getOptions(int page)
{
    if (page != 0)
        return {};
    return currentOptions_;
}

std::vector<Knob> MixerWidget::getKnobs(int page)
{
    if (page != 0)
        return {};
    return currentKnobs_;
}

void MixerWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void MixerWidget::paintPage(juce::Graphics&, int, juce::Rectangle<int>)
{
}

void MixerWidget::resized()
{
    stripsComponent_.setBounds(getLocalBounds());
}

void MixerWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (e.name == "solo")
    {
        soloHeld_ = e.pressed;
        if (soloHeld_)
            muteHeld_ = false;
        stripsComponent_.setSoloModifierHeld(soloHeld_);
        hw().setLed("solo",
            soloHeld_ ? HardwareConstants::kLedBright : HardwareConstants::kLedDim,
            "solo");
        return;
    }
    if (e.name == "muteChoke")
    {
        if (!soloHeld_)
            muteHeld_ = e.pressed;
        stripsComponent_.setMuteModifierHeld(muteHeld_);
        hw().setLed("muteChoke",
            muteHeld_ ? HardwareConstants::kLedBright : HardwareConstants::kLedDim,
            "muteChoke");
        return;
    }
    if (!e.pressed)
        return;

    rebindStaleDescriptors();

    if (e.name == "navUp") { drillUp(); return; }
    if (e.name == "navDown") { drillDown(); return; }
    if (e.name == "arrowLeft") { scrollBy(-kChannelsPerPanel); return; }
    if (e.name == "arrowRight") { scrollBy(kChannelsPerPanel); return; }
}

void MixerWidget::handleOption(int localIndex)
{
    rebindStaleDescriptors();

    if (localIndex < 0 || localIndex >= kChannelsPerPanel)
        return;

    auto* desc = descriptorForChannel(scrollOffset_ + localIndex);
    if (desc == nullptr)
        return;

    const bool isMaster = (desc->kind == ChannelDescriptor::Kind::Master);

    if (soloHeld_)
    {
        if (isMaster)
            return;
        if (desc->track != nullptr)
            desc->track->setSolo(!desc->track->isSolo(false));
        return;
    }
    if (muteHeld_)
    {
        if (isMaster)
            return;
        if (desc->track != nullptr)
            desc->track->setMute(!desc->track->isMuted(false));
        return;
    }

    activeChannelIndex_ = localIndex;
    stripsComponent_.setActiveIndex(activeChannelIndex_);
    if (level_ == MixerLevel::Sound)
        syncSelectedSound(*desc);
    updateNavLeds();
    writeMixerStateToValueTree();
}

void MixerWidget::handleKnob(int localIndex, int16_t delta, uint16_t, bool shift)
{
    rebindStaleDescriptors();

    if (localIndex < 0 || localIndex >= kChannelsPerPanel)
        return;

    auto* desc = descriptorForChannel(scrollOffset_ + localIndex);
    if (desc == nullptr)
        return;

    // Fall back to pulling the volume plugin live from the track: drill-down
    // pad descriptors sometimes enumerate before the track's plugin chain is
    // visible to getVolumePlugin(), leaving desc->volume null.
    te::VolumeAndPanPlugin* volume = desc->volume;
    if (volume == nullptr && desc->track != nullptr)
    {
        if (auto* at = dynamic_cast<te::AudioTrack*>(desc->track))
            volume = at->getVolumePlugin();
        else if (auto* ft = dynamic_cast<te::FolderTrack*>(desc->track))
            volume = ft->getVolumePlugin();
    }
    if (volume == nullptr)
        return;

    if (shift)
    {
        const float current = volume->getPan();
        const float next = juce::jlimit(-1.0f, 1.0f, current + static_cast<float>(delta) * 0.02f);
        performPanChange(desc->id, next);
    }
    else
    {
        // Step along the same skewed curve the meter uses: each tick is a
        // fixed fraction of visible fader travel, so the knob feels fine
        // near 0 dB and coarse in the low-level tail.
        constexpr float kKnobStepNorm = 0.002f;
        const float current = volume->getVolumeDb();
        const float next = MixerUtils::stepFaderDb(current, static_cast<float>(delta) * kKnobStepNorm);

        performGainChange(desc->id, next);
    }
}

void MixerWidget::onUiHostTick()
{
    // Project replacement can happen between heartbeats. Rebind descriptors
    // before the fast path touches any Tracktion-owned track/plugin pointers.
    rebindStaleDescriptors();

    // Fast path every tick (60 Hz): live values into atomics + knob readout.
    fastRefresh();

    // Pull live meter/fader/pan state into the strip children. Replaces
    // ChannelStripsComponent's former private 60 Hz timer — now coordinated
    // with the single UiHost heartbeat.
    stripsComponent_.refreshLiveState();

    // Slow path every 3rd tick (~20 Hz): re-enumerate descriptors in case TE
    // added/removed a track, and rebuild option/knob lists. This is the path
    // that allocates.
    if (++slowTickCounter_ >= 3)
    {
        slowTickCounter_ = 0;
        refreshChannels();
    }
}

void MixerWidget::setLevelForTest(MixerLevel level, std::optional<te::EditItemID> focused)
{
    level_ = level;
    focusedGroup_ = focused;
    scrollOffset_ = 0;
    activeChannelIndex_ = -1;
    refreshChannels();
    pickInitialActiveChannel();
    updateNavLeds();
}

void MixerWidget::refreshChannels()
{
    auto* edit = engine().getEdit();

    // Detect edit swap (project load). Any raw TE pointers we still hold point
    // into the now-destroyed edit — clear them before building new descriptors.
    if (edit != lastSeenEdit_)
    {
        // MeterRegistration pins its plugin, so it remains safe to detach the
        // client even after the old Edit released its ownership.
        for (auto& [id, reg] : meterClients_)
            if (reg.plugin != nullptr && reg.client != nullptr)
                reg.plugin->measurer.removeClient(*reg.client);
        meterClients_.clear();
        states_.clear();
        descriptors_.clear();
        descriptorsSignature_ = 0;
        lastSeenEdit_ = edit;
    }

    std::unique_ptr<ChannelSource> source;
    if (edit == nullptr)
    {
        descriptors_.clear();
    }
    else if (level_ == MixerLevel::Global)
    {
        source = std::make_unique<GlobalChannelSource>(*edit);
    }
    else if (focusedGroup_.has_value())
    {
        source = std::make_unique<GroupChannelSource>(*edit, *focusedGroup_);
    }

    if (source != nullptr)
        descriptors_ = source->enumerate();

    // Drop clients for tracks or meter plugins removed from the same Edit.
    // pluginLifetime keeps the old meter alive until removeClient completes.
    for (auto it = meterClients_.begin(); it != meterClients_.end();)
    {
        const auto descriptor = std::find_if(
            descriptors_.begin(), descriptors_.end(),
            [&it](const ChannelDescriptor& candidate)
            {
                return candidate.id == it->first;
            });
        if (descriptor == descriptors_.end() || descriptor->meter != it->second.plugin)
        {
            auto& reg = it->second;
            if (reg.plugin != nullptr && reg.client != nullptr)
                reg.plugin->measurer.removeClient(*reg.client);
            it = meterClients_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Sync states_ keys with descriptors_ — preserve existing state objects so
    // the channel strips keep reading the same shared_ptr, and create new
    // entries for any newly-enumerated descriptors.
    std::unordered_map<juce::String, std::shared_ptr<MixerChannelState>> kept;
    for (auto& desc : descriptors_)
    {
        auto it = states_.find(desc.id);
        auto state = (it != states_.end()) ? it->second
                                           : std::make_shared<MixerChannelState>();
        state->name = desc.name;
        state->subLabel = desc.subLabel;
        state->kind = desc.kind;
        state->canDrillDown = desc.canDrillDown;
        kept[desc.id] = std::move(state);
    }
    states_ = std::move(kept);

    // Skip rebuildChannelViews() when nothing about the visible channel set
    // has changed — avoids re-allocating option/knob vectors + closures and
    // re-pushing ChannelViews into the strip components every 20 Hz tick.
    const auto sig = computeDescriptorsSignature();
    if (sig != descriptorsSignature_)
    {
        descriptorsSignature_ = sig;
        rebuildChannelViews();
    }
    updateNavLeds();

    // Seed atomics so the first frame after a structural change shows
    // live values rather than the previous -inf placeholder.
    fastRefresh();
}

bool MixerWidget::hasStaleDescriptors() const
{
    auto* edit = engine().getEdit();
    if (edit == nullptr || edit != lastSeenEdit_)
        return edit != lastSeenEdit_;

    for (const auto& descriptor : descriptors_)
    {
        if (descriptor.kind == ChannelDescriptor::Kind::Master)
        {
            if (descriptor.volume != edit->getMasterVolumePlugin().get()
                || descriptor.meter != findLevelMeter(edit->getMasterPluginList()))
                return true;
            continue;
        }

        auto* liveTrack = te::findTrackForID(
            *edit, te::EditItemID::fromString(descriptor.id));
        if (liveTrack == nullptr || liveTrack != descriptor.track)
            return true;

        te::VolumeAndPanPlugin* liveVolume = nullptr;
        if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(liveTrack))
            liveVolume = audioTrack->getVolumePlugin();
        else if (auto* folderTrack = dynamic_cast<te::FolderTrack*>(liveTrack))
            liveVolume = folderTrack->getVolumePlugin();

        if (liveVolume != descriptor.volume
            || findLevelMeter(liveTrack->pluginList) != descriptor.meter)
            return true;
    }
    return false;
}

bool MixerWidget::rebindStaleDescriptors()
{
    const bool editChanged = engine().getEdit() != lastSeenEdit_;
    if (!editChanged && !hasStaleDescriptors())
        return false;

    if (editChanged)
    {
        auto& sampler = engine().getSampler();
        if (sampler.getSelectedPad() >= 0 && sampler.getFolder() != nullptr)
        {
            level_ = MixerLevel::Sound;
            focusedGroup_ = sampler.getFolder()->itemID;
            if (auto* selectedTrack = sampler.getTrack(sampler.getSelectedPad()))
                preferredChannel_ = selectedTrack->itemID;
        }
        else
        {
            level_ = MixerLevel::Global;
            focusedGroup_.reset();
            preferredChannel_.reset();
        }
        scrollOffset_ = 0;
        activeChannelIndex_ = -1;
    }
    else if (auto* active = descriptorForChannel(scrollOffset_ + activeChannelIndex_);
             active != nullptr && active->id != "master")
    {
        preferredChannel_ = te::EditItemID::fromString(active->id);
    }

    refreshChannels();
    scrollOffset_ = juce::jlimit(0, maxScrollOffset(), scrollOffset_);
    pickInitialActiveChannel();
    writeMixerStateToValueTree();
    return true;
}

std::size_t MixerWidget::computeDescriptorsSignature() const
{
    // Identity-level signature: which channels are visible in the current
    // panel, and are their enable flags / drill-down affordances the same.
    // Live values (fader, meter, mute, solo) belong to fastRefresh and are
    // intentionally excluded.
    std::size_t h = 0;
    auto mix = [&h](std::size_t v) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    mix(static_cast<std::size_t>(descriptors_.size()));
    mix(static_cast<std::size_t>(scrollOffset_));
    mix(static_cast<std::size_t>(activeChannelIndex_ + 1));

    for (int i = 0; i < kChannelsPerPanel; ++i)
    {
        const int idx = scrollOffset_ + i;
        if (idx < 0 || idx >= static_cast<int>(descriptors_.size()))
        {
            mix(0);
            continue;
        }
        const auto& d = descriptors_[static_cast<size_t>(idx)];
        mix(static_cast<std::size_t>(d.id.hashCode64()));
        mix(static_cast<std::size_t>(d.name.hashCode64()));
        mix(static_cast<std::size_t>(static_cast<int>(d.kind)));
        mix(static_cast<std::size_t>(d.canDrillDown ? 1 : 0));
        mix(static_cast<std::size_t>(d.volume != nullptr ? 1 : 0));
    }
    return h;
}

void MixerWidget::padTriggered(int padIndex)
{
    // Visual flash only — set an initial peak that fastRefresh will decay
    // over the next ~200 ms. This compensates for ungrouped pads' audio
    // flowing through the shared sequencer SamplerPlugin (not the pad
    // track) during sequenced playback.
    padFlashDb_[padIndex] = -3.0f;
}

void MixerWidget::fastRefresh()
{
    // HAZARD: SamplerInstrument::getPadsSnapshot() calls
    // LevelMeasurer::Client::getAndClearAudioLevel on every pad, which is
    // destructive. Take the snapshot ONCE per call and index into it below —
    // calling it inside the per-descriptor loop drains every pad's level on
    // the first descriptor and subsequent descriptors read zero.
    std::vector<SamplerInstrument::PadSnapshot> padSnapshots;
    bool padSnapshotsReady = false;
    auto lazySnapshots = [&]() -> const std::vector<SamplerInstrument::PadSnapshot>& {
        if (!padSnapshotsReady)
        {
            padSnapshots = engine().getSampler().getPadsSnapshot(
                SamplerInstrument::SnapshotContent::Levels);
            padSnapshotsReady = true;
        }
        return padSnapshots;
    };

    for (auto& desc : descriptors_)
    {
        auto it = states_.find(desc.id);
        if (it == states_.end())
            continue;
        auto& state = *it->second;

        // Drill-down pad descriptors sometimes enumerate with a null volume
        // pointer; re-fetch from the track and latch it back so the readout
        // follows knob changes.
        if (desc.volume == nullptr && desc.track != nullptr)
        {
            if (auto* at = dynamic_cast<te::AudioTrack*>(desc.track))
                desc.volume = at->getVolumePlugin();
            else if (auto* ft = dynamic_cast<te::FolderTrack*>(desc.track))
                desc.volume = ft->getVolumePlugin();
        }
        if (desc.volume != nullptr)
        {
            state.faderGainDb.store(desc.volume->getVolumeDb(), std::memory_order_relaxed);
            state.pan.store(desc.volume->getPan(), std::memory_order_relaxed);
        }
        if (desc.track != nullptr)
        {
            state.mute.store(desc.track->isMuted(false), std::memory_order_relaxed);
            state.solo.store(desc.track->isSolo(false), std::memory_order_relaxed);
        }

        float peakDb = kMeterFloorDbfs;
        bool gotLevel = false;

        // NOTE: pad-track meters only animate for audio that actually flows
        // through the pad's own AudioTrack — i.e., manual pad triggers and
        // choke-group playback. The step sequencer uses a single shared
        // multi-sample SamplerPlugin on the hidden "Sequencer" track, so
        // sequencer playback of ungrouped pads does NOT show up on
        // individual pad meters here. It does show on the Sampler folder
        // meter at Global level (the folder sums all children) — which is
        // the intended place to see sequencer output.
        if (desc.track != nullptr && desc.track->state.hasProperty("padIndex"))
        {
            const int padIdx = (int)desc.track->state.getProperty("padIndex");
            for (const auto& snap : lazySnapshots())
            {
                if (snap.id == padIdx)
                {
                    peakDb = juce::jlimit(kMeterFloorDbfs, 6.0f, snap.levelPeakDbfs);
                    gotLevel = true;
                    break;
                }
            }

            // Merge synthesized trigger flash so the pad meter animates for
            // sequenced ungrouped-pad hits (whose audio bypasses the pad
            // track). Real pad-track audio (manual triggers / choke-group
            // pads) takes priority via jmax.
            auto flashIt = padFlashDb_.find(padIdx);
            if (flashIt != padFlashDb_.end())
            {
                peakDb = juce::jmax(peakDb, flashIt->second);
                gotLevel = true;
                flashIt->second -= 1.0f;  // ~1 dB/tick ≈ 200 ms visible tail
                if (flashIt->second <= kMeterFloorDbfs)
                    padFlashDb_.erase(flashIt);
            }
        }

        if (!gotLevel && desc.meter != nullptr)
        {
            auto& reg = meterClients_[desc.id];
            if (reg.plugin != desc.meter)
            {
                if (reg.plugin != nullptr && reg.client != nullptr)
                    reg.plugin->measurer.removeClient(*reg.client);
                reg.client.reset();
                reg.pluginLifetime = nullptr;
                reg.plugin = desc.meter;
                reg.pluginLifetime = desc.meter;
                reg.client = std::make_unique<te::LevelMeasurer::Client>();
                reg.plugin->measurer.addClient(*reg.client);
            }
            const auto lvlL = reg.client->getAndClearAudioLevel(0);
            const auto lvlR = reg.client->getAndClearAudioLevel(1);
            peakDb = juce::jlimit(kMeterFloorDbfs, 6.0f, juce::jmax(lvlL.dB, lvlR.dB));
            gotLevel = true;
        }

        const float newPeak = gotLevel ? peakDb : kMeterFloorDbfs;
        state.inputPeakDbfs.store(newPeak, std::memory_order_relaxed);
        state.inputRmsDbfs.store(newPeak, std::memory_order_relaxed);
        state.peakHoldDbfs.store(newPeak, std::memory_order_relaxed);

        state.activePlugins.store(
            desc.track != nullptr ? desc.track->pluginList.getPlugins().size() : 0,
            std::memory_order_relaxed);
    }

    // Update the knob model's live value so the knob-bar readout follows
    // gain changes immediately, even between structural rebuilds.
    for (size_t i = 0; i < currentKnobs_.size(); ++i)
    {
        const int idx = scrollOffset_ + static_cast<int>(i);
        if (idx < 0 || idx >= static_cast<int>(descriptors_.size()))
            continue;
        auto* numModel = std::get_if<Knob::NumericModel>(&currentKnobs_[i].model);
        if (numModel == nullptr || descriptors_[(size_t)idx].volume == nullptr)
            continue;
        numModel->value = descriptors_[(size_t)idx].volume->getVolumeDb();
    }
}

void MixerWidget::rebuildChannelViews()
{
    currentOptions_.clear();
    currentKnobs_.clear();

    std::vector<ChannelStripsComponent::ChannelView> views;
    views.reserve(kChannelsPerPanel);

    for (int i = 0; i < kChannelsPerPanel; ++i)
    {
        const int idx = scrollOffset_ + i;
        ChannelStripsComponent::ChannelView v;
        if (idx >= 0 && idx < (int)descriptors_.size())
        {
            v.descriptor = descriptors_[(size_t)idx];
            auto it = states_.find(v.descriptor.id);
            if (it != states_.end())
                v.state = it->second;
        }
        views.push_back(v);

        Option opt;
        opt.id = v.descriptor.id.isNotEmpty() ? v.descriptor.id : juce::String(i);
        // Append a chevron ▼ to drillable folder channels so the user sees
        // the drill-down affordance directly in the tab label.
        opt.label = v.descriptor.canDrillDown
            ? v.descriptor.name + juce::String::fromUTF8(" \xE2\x96\xBC")  // ▼
            : v.descriptor.name;
        // Active track is indicated by the lighter strip background, not the
        // option bar highlight — keep populated channels Enabled so the bar
        // doesn't flash when selecting between them.
        opt.state = (v.state != nullptr) ? OptionState::Enabled : OptionState::Disabled;
        opt.onInvoke = [this, i]() { handleOption(i); };
        currentOptions_.push_back(std::move(opt));

        Knob knob;
        knob.id = v.descriptor.id.isNotEmpty() ? v.descriptor.id : juce::String(i);
        knob.label = {};
        knob.isEnabled = (v.descriptor.volume != nullptr);

        Knob::NumericModel model;
        if (v.descriptor.volume != nullptr)
        {
            model.value = v.descriptor.volume->getVolumeDb();
            model.minimum = kFaderMinDb;
            model.maximum = kFaderMaxDb;
            model.step = 0.5;
            model.formatter = [](double value) -> juce::String {
                if (value <= static_cast<double>(kFaderMinDb) + 0.05)
                    return "-INF dB";
                return juce::String(value, 1) + " dB";
            };
            // Capture a SafePointer + the descriptor's id, not the raw
            // VolumeAndPanPlugin pointer. A project swap (Edit change)
            // tears the old plugin down between when the closure is
            // built and when the user turns the knob — re-look it up
            // through the live descriptor to stay safe.
            juce::Component::SafePointer<MixerWidget> safe(this);
            const juce::String channelId = v.descriptor.id;
            model.onChange = [safe, channelId](double value) {
                auto* self = safe.getComponent();
                if (self != nullptr)
                    self->performGainChange(
                        channelId, static_cast<float>(value));
            };
        }
        knob.model = model;
        juce::Component::SafePointer<MixerWidget> safe(this);
        knob.onInputResolved = [safe](double) {
            if (auto* self = safe.getComponent())
                self->knobTransactionOpen_ = false;
        };
        currentKnobs_.push_back(std::move(knob));
    }

    stripsComponent_.setChannels(std::move(views));
    stripsComponent_.setActiveIndex(activeChannelIndex_);

}

void MixerWidget::beginKnobTransaction()
{
    if (knobTransactionOpen_)
        return;
    engine().getUndoManager().beginNewTransaction("Mixer knob");
    knobTransactionOpen_ = true;
}

void MixerWidget::performGainChange(const juce::String& channelId, float gainDb)
{
    beginKnobTransaction();
    engine().getUndoManager().perform(
        new SetGainDbCommand(engine(), channelId, gainDb));
}

void MixerWidget::performPanChange(const juce::String& channelId, float pan)
{
    beginKnobTransaction();
    engine().getUndoManager().perform(
        new SetPanCommand(engine(), channelId, pan));
}

void MixerWidget::drillDown()
{
    if (level_ != MixerLevel::Global)
        return;
    auto* desc = descriptorForChannel(scrollOffset_ + activeChannelIndex_);
    if (desc == nullptr || !desc->canDrillDown || desc->folder == nullptr)
        return;

    level_ = MixerLevel::Sound;
    focusedGroup_ = desc->folder->itemID;
    auto& sampler = engine().getSampler();
    if (desc->folder == sampler.getFolder())
    {
        if (auto* track = sampler.getTrack(sampler.getSelectedPad()))
            preferredChannel_ = track->itemID;
    }
    else
    {
        auto& bank = engine().getKeyboardBank();
        if (desc->folder == bank.getFolder())
            if (auto* track = bank.getTrack())
                preferredChannel_ = track->itemID;
    }
    scrollOffset_ = 0;
    activeChannelIndex_ = -1;
    refreshChannels();
    pickInitialActiveChannel();
    updateNavLeds();
    writeMixerStateToValueTree();
}

void MixerWidget::drillUp()
{
    if (level_ != MixerLevel::Sound)
        return;
    level_ = MixerLevel::Global;
    focusedGroup_.reset();
    scrollOffset_ = 0;
    activeChannelIndex_ = -1;
    refreshChannels();
    pickInitialActiveChannel();
    updateNavLeds();
    writeMixerStateToValueTree();
}

void MixerWidget::scrollBy(int delta)
{
    scrollOffset_ = juce::jlimit(0, maxScrollOffset(), scrollOffset_ + delta);
    activeChannelIndex_ = -1;
    refreshChannels();
    pickInitialActiveChannel();
    updateNavLeds();
    // Publish the new active channel so ChannelDetailsWidget's right-panel
    // view follows the scroll (without this, it snapped back to master).
    writeMixerStateToValueTree();
}

void MixerWidget::pickInitialActiveChannel()
{
    activeChannelIndex_ = -1;
    if (preferredChannel_.has_value())
    {
        for (int i = 0; i < static_cast<int>(descriptors_.size()); ++i)
        {
            if (descriptors_[static_cast<size_t>(i)].id
                != preferredChannel_->toString())
                continue;
            scrollOffset_ = (i / kChannelsPerPanel) * kChannelsPerPanel;
            activeChannelIndex_ = i - scrollOffset_;
            break;
        }
        preferredChannel_.reset();
    }
    for (int i = 0; i < kChannelsPerPanel; ++i)
    {
        if (activeChannelIndex_ >= 0)
            break;
        const int idx = scrollOffset_ + i;
        if (idx >= 0 && idx < (int)descriptors_.size())
        {
            activeChannelIndex_ = i;
            break;
        }
    }
    if (level_ == MixerLevel::Sound)
    {
        if (auto* active = descriptorForChannel(scrollOffset_ + activeChannelIndex_))
            syncSelectedSound(*active);
    }
    stripsComponent_.setActiveIndex(activeChannelIndex_);
    rebuildChannelViews();
}

int MixerWidget::maxScrollOffset() const
{
    if (descriptors_.empty())
        return 0;
    return ((static_cast<int>(descriptors_.size()) - 1) / kChannelsPerPanel)
        * kChannelsPerPanel;
}

void MixerWidget::syncSelectedSound(const ChannelDescriptor& descriptor)
{
    if (descriptor.track == nullptr)
        return;

    const int padIndex = static_cast<int>(
        descriptor.track->state.getProperty("padIndex", -1));
    if (padIndex >= 0)
    {
        engine().getSampler().selectPad(padIndex);
        return;
    }

    const int instrumentSlot = static_cast<int>(
        descriptor.track->state.getProperty("instrumentSlot", -1));
    if (instrumentSlot >= 0)
        engine().getKeyboardBank().setActiveSlot(instrumentSlot);
}

ChannelDescriptor* MixerWidget::descriptorForChannel(int channelIdx)
{
    if (channelIdx < 0 || channelIdx >= (int)descriptors_.size())
        return nullptr;
    return &descriptors_[(size_t)channelIdx];
}

void MixerWidget::updateNavLeds()
{
    using namespace HardwareConstants;

    hw().setLed("navUp", level_ == MixerLevel::Sound ? kLedDim : 0, "navUp");

    const bool canDrill = [&]() {
        if (level_ != MixerLevel::Global)
            return false;
        auto* desc = descriptorForChannel(scrollOffset_ + activeChannelIndex_);
        return desc != nullptr && desc->canDrillDown;
    }();
    hw().setLed("navDown", canDrill ? kLedDim : 0, "navDown");

    hw().setLed("arrowLeft", scrollOffset_ > 0 ? kLedDim : 0, "arrowLeft");
    hw().setLed("arrowRight", scrollOffset_ < maxScrollOffset() ? kLedDim : 0,
                "arrowRight");
}

void MixerWidget::writeMixerStateToValueTree()
{
    auto mixerState = engine().getMixerState();
    if (!mixerState.isValid())
        return;
    mixerState.setProperty(MixerStateKeys::Level, levelToString(level_), nullptr);
    mixerState.setProperty(MixerStateKeys::FocusedGroup,
        focusedGroup_.has_value() ? focusedGroup_->toString() : juce::String(),
        nullptr);

    juce::String activeId;
    if (auto* desc = descriptorForChannel(scrollOffset_ + activeChannelIndex_))
        activeId = desc->id;
    mixerState.setProperty(MixerStateKeys::ActiveChannel, activeId, nullptr);
}
