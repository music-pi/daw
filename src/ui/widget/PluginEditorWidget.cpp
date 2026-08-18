#include "PluginEditorWidget.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/KeyboardInstrumentBank.h"
#include "../../engine/PluginConfig.h"
#include "../theme/UiTheme.h"

#include <array>
#include <cmath>

namespace
{
constexpr float kKnobClicksPerSpan = 64.0f;    // ~2.5 encoder turns per param span
}

// ── Page layout ──────────────────────────────────────────────────────────────

int PluginEditorWidget::pinnedPageCount() const
{
    int n = static_cast<int>(curatedParams_.size());
    if (n == 0 && config_ != nullptr)
        n = static_cast<int>(config_->pinned.size());
    if (n == 0) return 0;
    return (n + kParamsPerPage - 1) / kParamsPerPage;
}

int PluginEditorWidget::paramPageCount() const
{
    if (numParams_ <= 0) return 1;
    return (numParams_ + kParamsPerPage - 1) / kParamsPerPage;
}

PluginEditorWidget::PageKind PluginEditorWidget::classify(int page) const
{
    if (page < presetsPage()) return PageKind::Pinned;
    if (page == presetsPage()) return PageKind::Presets;
    return PageKind::Params;
}

WidgetDescriptor PluginEditorWidget::describe() const
{
    return { "plugin_editor", juce::jmax(1, totalPageCount()), false,
             DisplayConstraint::LeftOnly };
}

juce::String PluginEditorWidget::getTitle() const
{
    // Memoize: inputs are stable across most frames. Recompute only on change.
    TitleKey key;
    key.currentPage    = currentPage_;
    key.pinnedCount    = pinnedPageCount();
    key.paramCount     = paramPageCount();
    key.editorVisible  = editorVisible_;
    key.pluginName     = pluginName_;
    if (!titleCached_.isEmpty() && key == titleKey_)
        return titleCached_;

    juce::String pageName;
    switch (classify(currentPage_))
    {
        case PageKind::Pinned:
        {
            const int idx = currentPage_ - firstPinnedPage() + 1;
            if (! curatedPageNames_.empty()
                && idx <= static_cast<int>(curatedPageNames_.size()))
                pageName = curatedPageNames_[static_cast<size_t>(idx - 1)];
            else
                pageName = "Pinned " + juce::String(idx) + "/"
                         + juce::String(pinnedPageCount());
            break;
        }
        case PageKind::Presets:
            pageName = "Presets"; break;
        case PageKind::Params:
        {
            const int idx = currentPage_ - firstParamsPage() + 1;
            pageName = "Params " + juce::String(idx) + "/" + juce::String(paramPageCount());
            break;
        }
    }
    const juce::String uiTag = editorVisible_ ? "  [UI]" : juce::String();
    titleCached_ = "Plugin > " + pluginName_ + "  (" + pageName + ")" + uiTag;
    titleKey_ = std::move(key);
    return titleCached_;
}

// ── Plugin + processor accessors ─────────────────────────────────────────────

void PluginEditorWidget::setPlugin(te::Plugin* plugin)
{
    plugin_ = plugin;
    pluginId_ = plugin != nullptr ? plugin->itemID : te::EditItemID();
    externalPlugin_ = dynamic_cast<te::ExternalPlugin*>(plugin);
    pluginName_ = plugin != nullptr ? plugin->getName() : juce::String();
    numParams_ = plugin != nullptr ? plugin->getAutomatableParameters().size() : 0;
    config_ = nullptr;
    curatedParams_.clear();
    curatedPageNames_.clear();
    if (dynamic_cast<te::FourOscPlugin*>(plugin) != nullptr)
    {
        // FourOsc has a large generic parameter list. Put its most useful
        // performance controls first, grouped into four hardware pages;
        // every automatable parameter remains available on later pages.
        curatedParams_ = {
            { "level1", "Osc 1" },
            { "level2", "Osc 2" },
            { "level3", "Osc 3" },
            { "level4", "Osc 4" },
            { "filterFreq", "Cutoff" },
            { "filterResonance", "Resonance" },
            { "filterAmount", "Env Amount" },
            { "filterKey", "Key Track" },
            { "ampAttack", "Attack" },
            { "ampDecay", "Decay" },
            { "ampSustain", "Sustain" },
            { "ampRelease", "Release" },
            { "tune1", "Tune" },
            { "fineTune1", "Fine" },
            { "pulseWidth1", "Pulse Width" },
            { "masterLevel", "Master" },
        };
        curatedPageNames_ = { "Osc Mix", "Filter", "Amp Env", "Osc 1" };
    }
    presetSelection_ = 0;
    scannedPresets_.clear();
    currentScannedPreset_ = -1;
    editorVisible_ = false;
    editorCreationPending_ = false;
    titleCached_.clear();
    titleKey_ = {};
}

void PluginEditorWidget::onEditAboutToBeReplaced()
{
    editor_.reset();
    editorVisible_ = false;
    editorCreationPending_ = false;
    loadingToast_ = {};
    plugin_ = nullptr;
    externalPlugin_ = nullptr;
    config_ = nullptr;
}

void PluginEditorWidget::onEditReplaced()
{
    te::Plugin* replacement = nullptr;
    if (auto* edit = engine().getEdit(); edit != nullptr && pluginId_.isValid())
        replacement = te::findPluginForID(*edit, pluginId_).get();
    setPlugin(replacement);
}

juce::AudioProcessor* PluginEditorWidget::getProcessor() const
{
    return externalPlugin_ != nullptr
        ? externalPlugin_->getWrappedAudioProcessor() : nullptr;
}

juce::Array<te::AutomatableParameter*>
PluginEditorWidget::getParams() const
{
    if (plugin_ == nullptr) return {};
    return plugin_->getAutomatableParameters();
}

juce::Array<te::AutomatableParameter*>
PluginEditorWidget::getParamsForPage(int paramsPageIndex) const
{
    juce::Array<te::AutomatableParameter*> out;
    auto all = getParams();
    const int offset = paramsPageIndex * kParamsPerPage;
    for (int i = 0; i < kParamsPerPage; ++i)
    {
        const int idx = offset + i;
        if (idx >= 0 && idx < all.size())
            out.add(all[idx]);
    }
    return out;
}

// ── Pinned param resolution ─────────────────────────────────────────────────

juce::Array<te::AutomatableParameter*>
PluginEditorWidget::resolvePinnedParams() const
{
    juce::Array<te::AutomatableParameter*> out;
    auto all = getParams();

    if (! curatedParams_.empty())
    {
        for (const auto& curated : curatedParams_)
        {
            te::AutomatableParameter* match = nullptr;
            for (auto* p : all)
                if (p != nullptr && p->paramID == curated.id)
                {
                    match = p;
                    break;
                }
            out.add(match);
        }
        return out;
    }

    if (config_ == nullptr || config_->pinned.empty()) return out;

    for (const auto& pin : config_->pinned)
    {
        te::AutomatableParameter* match = nullptr;

        if (pin.id.isNotEmpty())
            for (auto* p : all)
                if (p != nullptr && p->paramID == pin.id) { match = p; break; }

        if (match == nullptr && pin.nameMatch.isNotEmpty())
            for (auto* p : all)
                if (p != nullptr
                    && p->getParameterName().containsIgnoreCase(pin.nameMatch))
                { match = p; break; }

        out.add(match);
    }
    return out;
}

juce::Array<te::AutomatableParameter*>
PluginEditorWidget::getPinnedForPage(int pinnedPageIndex) const
{
    juce::Array<te::AutomatableParameter*> out;
    auto all = resolvePinnedParams();
    const int offset = pinnedPageIndex * kParamsPerPage;
    for (int i = 0; i < kParamsPerPage; ++i)
    {
        const int idx = offset + i;
        out.add(idx >= 0 && idx < all.size() ? all[idx] : nullptr);
    }
    return out;
}

juce::String PluginEditorWidget::pinnedLabelFor(int pinnedIndex) const
{
    if (pinnedIndex >= 0
        && pinnedIndex < static_cast<int>(curatedParams_.size()))
        return curatedParams_[static_cast<size_t>(pinnedIndex)].label;

    if (config_ == nullptr
        || pinnedIndex < 0
        || pinnedIndex >= static_cast<int>(config_->pinned.size()))
        return {};
    return config_->pinned[static_cast<size_t>(pinnedIndex)].label;
}

bool PluginEditorWidget::hasPinnedParams() const
{
    return ! curatedParams_.empty()
        || (config_ != nullptr && ! config_->pinned.empty());
}

te::AutomatableParameter*
PluginEditorWidget::findParamById(const juce::String& id) const
{
    for (auto* param : getParams())
        if (param != nullptr && param->paramID == id)
            return param;
    return nullptr;
}

// ── Presets: source selection ───────────────────────────────────────────────

bool PluginEditorWidget::useFileScanPresets() const
{
    return config_ != nullptr
        && config_->presets.source == PluginConfig::PresetSource::FileScan
        && config_->presets.glob.isNotEmpty();
}

int PluginEditorWidget::getNumPresets() const
{
    if (useFileScanPresets()) return static_cast<int>(scannedPresets_.size());
    auto* ap = getProcessor();
    return ap != nullptr ? ap->getNumPrograms() : 0;
}

juce::String PluginEditorWidget::getPresetName(int index) const
{
    if (useFileScanPresets())
    {
        if (index < 0 || index >= static_cast<int>(scannedPresets_.size())) return {};
        return scannedPresets_[static_cast<size_t>(index)].name;
    }
    auto* ap = getProcessor();
    return ap != nullptr ? ap->getProgramName(index) : juce::String();
}

int PluginEditorWidget::getCurrentPresetIndex() const
{
    if (useFileScanPresets()) return currentScannedPreset_;
    auto* ap = getProcessor();
    return ap != nullptr ? ap->getCurrentProgram() : -1;
}

void PluginEditorWidget::refreshFileScanPresets()
{
    scannedPresets_.clear();
    if (config_ == nullptr) return;

    PluginConfig::GlobSplit split;
    if (! PluginConfig::splitGlob(config_->presets.glob, split)) return;

    const juce::File dir(split.baseDir);

    auto absorbFile = [this](const juce::File& f)
    {
        juce::MemoryBlock bytes;
        if (! f.loadFileAsData(bytes) || bytes.getSize() == 0) return;

        auto frames = splitSysexFrames(bytes);
        if (frames.empty())
        {
            // Not a sysex file we recognise — keep it as a single entry
            // named by the filename so the user can still load the raw
            // bytes manually.
            ScannedPreset p;
            p.name = f.getFileNameWithoutExtension();
            p.sourceFile = f;
            p.bytes = std::move(bytes);
            scannedPresets_.push_back(std::move(p));
            return;
        }

        for (auto& frame : frames)
        {
            ScannedPreset p;
            p.name = extractVirusPresetName(frame);
            if (p.name.isEmpty()) p.name = f.getFileNameWithoutExtension();
            p.sourceFile = f;
            p.bytes = std::move(frame);
            scannedPresets_.push_back(std::move(p));
        }
    };

    if (split.pattern.isEmpty())
    {
        if (dir.existsAsFile()) absorbFile(dir);
    }
    else if (dir.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator(
                 dir, split.recursive, split.pattern, juce::File::findFiles))
        {
            absorbFile(entry.getFile());
        }
    }

    // Stable, human-friendly order: by filename then by preset name so
    // soundsets cluster together.
    std::sort(scannedPresets_.begin(), scannedPresets_.end(),
        [](const ScannedPreset& a, const ScannedPreset& b)
        {
            const int byFile = a.sourceFile.getFullPathName()
                .compareIgnoreCase(b.sourceFile.getFullPathName());
            return byFile != 0 ? byFile < 0
                               : a.name.compareIgnoreCase(b.name) < 0;
        });
}

// ── SysEx: parsing helpers (static, unit-tested) ─────────────────────────────

std::vector<juce::MemoryBlock>
PluginEditorWidget::splitSysexFrames(const juce::MemoryBlock& data)
{
    std::vector<juce::MemoryBlock> out;
    const auto* b = static_cast<const uint8_t*>(data.getData());
    const size_t n = data.getSize();

    size_t i = 0;
    while (i < n)
    {
        if (b[i] != 0xF0) { ++i; continue; }
        const size_t start = i;
        while (i < n && b[i] != 0xF7) ++i;
        if (i >= n) break;
        const size_t len = i - start + 1;    // inclusive of F7
        juce::MemoryBlock frame;
        frame.append(b + start, len);
        out.push_back(std::move(frame));
        ++i;
    }
    return out;
}

juce::String
PluginEditorWidget::extractVirusPresetName(const juce::MemoryBlock& frame)
{
    const auto* b = static_cast<const uint8_t*>(frame.getData());
    const size_t n = frame.getSize();
    if (n < 20 || b[0] != 0xF0 || b[n - 1] != 0xF7) return {};
    if (b[6] != 0x10) return {};    // not DUMP_SINGLE

    // Scan the parameter block (from offset 9 to just before the checksum)
    // for the longest run of printable ASCII. Virus names are a 10-byte field,
    // but the exact position shifts between legacy OSes and TI/TI2. Picking
    // the longest printable run dodges the offset ambiguity and tolerates
    // future revisions.
    const size_t paramStart = 9;
    const size_t paramEnd = n >= 2 ? n - 2 : paramStart;   // exclude checksum + F7

    size_t bestStart = 0, bestLen = 0;
    size_t runStart = paramStart, runLen = 0;
    for (size_t k = paramStart; k < paramEnd; ++k)
    {
        const uint8_t c = b[k];
        const bool printable = c >= 32 && c < 127;
        if (printable)
        {
            if (runLen == 0) runStart = k;
            ++runLen;
            if (runLen > bestLen) { bestLen = runLen; bestStart = runStart; }
        }
        else
        {
            runLen = 0;
        }
    }

    if (bestLen < 4) return {};    // noise, not a real name
    juce::String name;
    for (size_t k = 0; k < bestLen; ++k)
        name += juce::String::charToString((juce::juce_wchar) b[bestStart + k]);
    return name.trim();
}

// ── SysEx: send via TE's live-MIDI injection path ────────────────────────────
//
// The earlier prototype called plugin_->getWrappedAudioProcessor()->processBlock
// directly from the message thread to both send the request and capture the
// plugin's response. That raced with TE's audio thread driving the same
// AudioProcessor — undefined behaviour. We now route sysex through
// AudioTrack::injectLiveMidiMessage which queues messages for the audio thread
// to drain during its own processBlock call (same path keyboard-note playback
// already uses). Downside: we can't observe the plugin's reply from here, so
// the "fetch presets from plugin" feature was removed. Loading a patch via
// .syx file is the supported path.

bool PluginEditorWidget::injectSysexBytes(const juce::MemoryBlock& bytes)
{
    if (plugin_ == nullptr || bytes.getSize() == 0) return false;
    auto* ownerTrack = plugin_->getOwnerTrack();
    auto* audioTrack = dynamic_cast<te::AudioTrack*>(ownerTrack);
    if (audioTrack == nullptr) return false;

    const auto* b = static_cast<const uint8_t*>(bytes.getData());
    const size_t n = bytes.getSize();
    int injected = 0;

    size_t i = 0;
    while (i < n)
    {
        if (b[i] != 0xF0) { ++i; continue; }
        const size_t start = i;
        while (i < n && b[i] != 0xF7) ++i;
        if (i >= n) break;
        const size_t len = i - start + 1;
        if (len >= 2)
        {
            audioTrack->injectLiveMidiMessage(
                juce::MidiMessage::createSysExMessage(
                    b + start + 1,
                    static_cast<int>(len - 2)),
                {});
            ++injected;
        }
        ++i;
    }
    return injected > 0;
}

void PluginEditorWidget::loadPreset(int index)
{
    if (useFileScanPresets())
    {
        if (index < 0 || index >= static_cast<int>(scannedPresets_.size())) return;
        const auto& p = scannedPresets_[static_cast<size_t>(index)];
        presetSelection_ = index;
        if (injectSysexBytes(p.bytes))
        {
            currentScannedPreset_ = index;
            showToast(ToastKind::Info, "Loaded " + p.name);
        }
        else
        {
            showToast(ToastKind::Warning, "Preset load failed");
        }
        repaint();
        return;
    }

    auto* ap = getProcessor();
    const int n = getNumPresets();
    if (ap == nullptr || n <= 0) return;
    const int clamped = juce::jlimit(0, n - 1, index);
    ap->setCurrentProgram(clamped);
    presetSelection_ = clamped;
    repaint();
}

// ── Native editor: lazy create + toggle ─────────────────────────────────────

void PluginEditorWidget::ensureEditorCreated()
{
    if (editor_ != nullptr) return;
    if (auto* ap = getProcessor())
    {
        editor_.reset(ap->createEditorIfNeeded());
        if (editor_ != nullptr)
        {
            viewport_.setViewedComponent(editor_.get(), false);
            resized();
        }
    }
}

void PluginEditorWidget::toggleNativeUi()
{
    // Treat editorVisible_ as "user intent" and flip it eagerly; the async
    // create path checks it at completion time so a cancel-while-loading
    // doesn't force the editor visible after the user changed their mind.
    editorVisible_ = ! editorVisible_;

    if (editorVisible_)
    {
        if (editor_ == nullptr && ! editorCreationPending_)
        {
            editorCreationPending_ = true;
            loadingToast_ = showLoadingToast("Loading plugin UI...");

            juce::Component::SafePointer<PluginEditorWidget> safe(this);
            juce::MessageManager::callAsync([safe]() {
                if (safe == nullptr) return;
                safe->ensureEditorCreated();
                safe->editorCreationPending_ = false;
                safe->loadingToast_ = ToastHandle{};
                // Respect the most recent user intent — if they cancelled
                // while we were loading, stay hidden.
                safe->viewport_.setVisible(safe->editorVisible_);
                safe->repaint();
            });
            return;
        }
        viewport_.setVisible(editor_ != nullptr);
    }
    else
    {
        viewport_.setVisible(false);
    }
    repaint();
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void PluginEditorWidget::onActivated(int offset)
{
    panelOffset_ = offset;

    addChildComponent(viewport_);      // not visible until user toggles it on
    viewport_.setVisible(false);

    if (plugin_ != nullptr)
    {
        config_ = externalPlugin_ != nullptr
            ? engine().getPluginConfigRegistry().find(externalPlugin_->desc)
            : nullptr;
        if (useFileScanPresets()) refreshFileScanPresets();
        if (auto* ap = getProcessor())
            presetSelection_ = juce::jmax(0, ap->getCurrentProgram());
    }
    onPageVisible(currentPage_);
    resized();
}

void PluginEditorWidget::onDeactivated()
{
    loadingToast_ = ToastHandle{};
    viewport_.setViewedComponent(nullptr, false);
    editor_.reset();
    editorVisible_ = false;
    editorCreationPending_ = false;
}

void PluginEditorWidget::onPageVisible(int page)
{
    currentPage_ = page;

    if (classify(page) == PageKind::Presets)
    {
        if (useFileScanPresets())
            refreshFileScanPresets();
        if (auto* ap = getProcessor(); ap != nullptr && ! useFileScanPresets())
            presetSelection_ = juce::jmax(0, ap->getCurrentProgram());
    }
    repaint();
}

void PluginEditorWidget::onPageHidden(int /*page*/)
{
}

// ── Resources + knobs + options ──────────────────────────────────────────────

std::vector<std::string> PluginEditorWidget::requiredResources(int /*page*/)
{
    // Claim the superset on every page. The widget covers the whole left
    // panel so k1..k4 + d1..d4 belong to it no matter which page is live;
    // returning a per-page subset caused WindowManager to release/re-claim
    // across page changes, which blew up when a resource had been freed
    // earlier in the session (e.g. "Resource 'k2' is owned by ''").
    return { "k1", "k2", "k3", "k4", "d1", "d2", "d3", "d4" };
}

std::vector<Option> PluginEditorWidget::getOptions(int page)
{
    Option empty;
    empty.state = OptionState::Empty;

    Option uiToggle;
    uiToggle.id = "plugin.ui.toggle";
    uiToggle.label = externalPlugin_ == nullptr
        ? "No UI" : (editorVisible_ ? "Hide UI" : "Show UI");
    uiToggle.state = externalPlugin_ != nullptr
        ? OptionState::Enabled : OptionState::Disabled;
    uiToggle.onInvoke = [this]() { toggleNativeUi(); };

    if (classify(page) == PageKind::Presets)
    {
        Option load;
        load.id = "plugin.preset.load";
        load.label = "Load";
        load.state = getNumPresets() > 0 ? OptionState::Enabled : OptionState::Disabled;
        load.onInvoke = [this]() { loadPreset(presetSelection_); };

        return { load, empty, empty, uiToggle };
    }

    return { empty, empty, empty, uiToggle };
}

std::vector<Knob> PluginEditorWidget::getKnobs(int page)
{
    const auto kind = classify(page);

    if (kind == PageKind::Presets)
    {
        const int n = getNumPresets();
        Knob scroll;
        scroll.id = "plugin.preset.cursor";
        scroll.label = "Preset";
        scroll.isEnabled = n > 0;
        scroll.continuousMode = false;

        Knob::NumericModel m;
        m.minimum = 0;
        m.maximum = juce::jmax(0, n - 1);
        m.value = presetSelection_;
        m.step = 1;
        m.onChange = [this](double v) {
            const int n2 = getNumPresets();
            if (n2 <= 0) return;
            presetSelection_ = juce::jlimit(0, n2 - 1, static_cast<int>(v));
            repaint();
        };
        scroll.model = std::move(m);
        return { scroll };
    }

    const bool pinned = (kind == PageKind::Pinned);
    const int localPage = pinned
        ? (page - firstPinnedPage())
        : (page - firstParamsPage());
    auto params = pinned ? getPinnedForPage(localPage) : getParamsForPage(localPage);

    std::vector<Knob> out;
    for (int i = 0; i < kParamsPerPage; ++i)
    {
        auto* p = i < params.size() ? params[i] : nullptr;
        if (p == nullptr) continue;

        Knob k;
        k.id = (pinned ? "plugin.pinned" : "plugin.param")
             + juce::String(localPage * kParamsPerPage + i);
        k.label = pinned
            ? pinnedLabelFor(localPage * kParamsPerPage + i)
            : juce::String();
        if (k.label.isEmpty()) k.label = p->getParameterName();
        k.isEnabled = true;
        k.continuousMode = true;

        Knob::NumericModel m;
        const auto range = p->getValueRange();
        m.minimum = range.getStart();
        m.maximum = range.getEnd();
        m.value   = p->getCurrentValue();
        if (pinned && p->paramID.startsWith("level"))
        {
            m.minimum = -60.0;
            m.step = 1.0;
        }
        else if (pinned && (p->paramID.startsWith("tune")
                            || p->paramID == "filterFreq"))
        {
            m.step = 1.0;
        }
        else if (pinned && p->paramID.startsWith("fineTune"))
        {
            m.step = 1.0;
        }
        else
        {
            m.step = (range.getLength() > 0.0f)
                ? static_cast<double>(range.getLength()) / kKnobClicksPerSpan
                : 0.01;
        }
        m.onChange = [p](double v) {
            p->setParameter(static_cast<float>(v), juce::sendNotification);
        };
        k.model = std::move(m);
        out.push_back(std::move(k));
    }
    return out;
}

void PluginEditorWidget::handleKnob(int localIndex, int16_t delta, uint16_t,
                                    bool shift)
{
    const auto kind = classify(currentPage_);

    if (kind == PageKind::Presets)
    {
        if (localIndex != 0) return;
        const int n = getNumPresets();
        if (n <= 0) return;
        presetSelection_ = juce::jlimit(0, n - 1,
            presetSelection_ + static_cast<int>(delta));
        repaint();
        return;
    }

    if (localIndex < 0 || localIndex >= kParamsPerPage) return;
    const bool pinned = (kind == PageKind::Pinned);
    const int localPage = pinned
        ? (currentPage_ - firstPinnedPage())
        : (currentPage_ - firstParamsPage());
    auto params = pinned ? getPinnedForPage(localPage) : getParamsForPage(localPage);
    if (localIndex >= params.size()) return;
    auto* p = params[localIndex];
    if (p == nullptr) return;

    auto range = p->getValueRange();
    if (pinned && p->paramID.startsWith("level"))
        range = { -60.0f, range.getEnd() };
    float step = 0.0f;
    if (pinned && p->paramID.startsWith("level"))
        step = shift ? 0.1f : 1.0f;
    else if (pinned && (p->paramID.startsWith("tune")
                        || p->paramID == "filterFreq"))
        step = shift ? 0.1f : 1.0f;
    else if (pinned && p->paramID.startsWith("fineTune"))
        step = shift ? 0.1f : 1.0f;
    else
    {
        const float span = range.getLength() > 0 ? range.getLength() : 1.0f;
        step = span / kKnobClicksPerSpan;
    }
    const float next = juce::jlimit(range.getStart(), range.getEnd(),
        p->getCurrentValue() + static_cast<float>(delta) * step);
    p->setParameter(next, juce::sendNotification);
    repaint();
}

// ── Rendering ───────────────────────────────────────────────────────────────

void PluginEditorWidget::paint(juce::Graphics& g)
{
    paintPage(g, currentPage_, getLocalBounds());
}

void PluginEditorWidget::paintPage(juce::Graphics& g, int page,
                                   juce::Rectangle<int> bounds)
{
    g.fillAll(UiTheme::kBackgroundDark);

    // When the native UI is on, the viewport covers the page — paint nothing
    // behind it so we don't flash before the child paints over.
    if (editorVisible_ && editor_ != nullptr) return;

    switch (classify(page))
    {
        case PageKind::Pinned:
            renderPinnedPage(g, page - firstPinnedPage(), bounds);
            return;
        case PageKind::Presets:
            renderPresetsPage(g, bounds);
            return;
        case PageKind::Params:
            renderParamsPage(g, page - firstParamsPage(), bounds);
            return;
    }
}

void PluginEditorWidget::renderPinnedPage(juce::Graphics& g, int pinnedPageIndex,
                                           juce::Rectangle<int> bounds)
{
    if (! hasPinnedParams())
    {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        g.drawFittedText("No pinned params configured for this plugin\n\n"
                         "arrowRight — presets / params  ·  d4 — native UI",
                         bounds, juce::Justification::centred, 3);
        return;
    }

    if (! curatedParams_.empty())
        renderFourOscWaveform(g, bounds.removeFromTop(68).reduced(4, 2));

    auto params = getPinnedForPage(pinnedPageIndex);
    const int colW = bounds.getWidth() / kParamsPerPage;

    for (int i = 0; i < kParamsPerPage; ++i)
    {
        auto cellBounds = juce::Rectangle<int>(bounds.getX() + i * colW,
                                                bounds.getY(),
                                                colW, bounds.getHeight())
                          .reduced(4);

        auto* p = i < params.size() ? params[i] : nullptr;
        const juce::String configLabel = pinnedLabelFor(pinnedPageIndex * kParamsPerPage + i);
        const juce::String label = configLabel.isNotEmpty()
            ? configLabel
            : (p != nullptr ? p->getParameterName() : juce::String());

        auto top = cellBounds.removeFromTop(cellBounds.getHeight() * 2 / 3);
        g.setColour(UiTheme::kTitlebarAccent.withAlpha(p != nullptr ? 1.0f : 0.3f));
        g.drawRect(top, 1);

        if (p != nullptr)
        {
            const auto range = p->getValueRange();
            const float span = range.getLength() > 0 ? range.getLength() : 1.0f;
            const float frac = juce::jlimit(0.0f, 1.0f,
                (p->getCurrentValue() - range.getStart()) / span);
            auto fill = top.reduced(2);
            auto barHeight = juce::roundToInt(fill.getHeight() * frac);
            auto bar = fill.withY(fill.getBottom() - barHeight).withHeight(barHeight);
            g.setColour(UiTheme::kTitlebarAccent);
            g.fillRect(bar);
        }

        g.setColour(juce::Colours::white);
        g.drawFittedText(label.isNotEmpty() ? label : juce::String("—"),
                         cellBounds.removeFromTop(cellBounds.getHeight() / 2),
                         juce::Justification::centredTop, 1);

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawFittedText(p != nullptr ? p->getCurrentValueAsString()
                                      : juce::String("not found"),
                         cellBounds, juce::Justification::centredBottom, 1);
    }

    const int totalPages = pinnedPageCount();
    if (totalPages > 1)
    {
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        juce::String txt = juce::String(pinnedPageIndex + 1) + "/" + juce::String(totalPages);
        g.drawFittedText(txt, bounds.removeFromBottom(14),
                         juce::Justification::centredRight, 1);
    }
}

void PluginEditorWidget::renderFourOscWaveform(
    juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    if (plugin_ == nullptr || bounds.isEmpty()) return;

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawHorizontalLine(bounds.getCentreY(),
                         static_cast<float>(bounds.getX() + 4),
                         static_cast<float>(bounds.getRight() - 4));

    struct OscPreview
    {
        int wave { 0 };
        float gain { 0.0f };
        float tuneRatio { 1.0f };
        float pulseWidth { 0.5f };
    };
    std::array<OscPreview, 4> oscillators;
    float totalGain = 0.0f;

    for (int i = 0; i < 4; ++i)
    {
        const auto suffix = juce::String(i + 1);
        auto& osc = oscillators[static_cast<size_t>(i)];
        osc.wave = static_cast<int>(plugin_->state.getProperty(
            "waveShape" + suffix, i == 0 ? 1 : 0));
        if (auto* level = findParamById("level" + suffix))
            osc.gain = juce::Decibels::decibelsToGain(
                level->getCurrentValue(), -100.0f);
        if (auto* tune = findParamById("tune" + suffix))
            osc.tuneRatio = std::pow(2.0f, tune->getCurrentValue() / 12.0f);
        if (auto* pulse = findParamById("pulseWidth" + suffix))
            osc.pulseWidth = pulse->getCurrentValue();
        if (osc.wave != 0)
            totalGain += osc.gain;
    }

    if (totalGain <= 0.0f) return;

    const auto sampleWave = [](int wave, float phase, float pulseWidth)
    {
        const float cycle = phase - std::floor(phase);
        switch (wave)
        {
            case 1: return std::sin(juce::MathConstants<float>::twoPi * cycle);
            case 2: return cycle < pulseWidth ? 1.0f : -1.0f;
            case 3: return cycle * 2.0f - 1.0f;
            case 4: return 1.0f - 4.0f * std::abs(cycle - 0.5f);
            case 5:
                // Stable pseudo-noise keeps the preview from animating or
                // touching the audio thread while still showing the shape.
                return std::sin(cycle * 917.0f + 0.37f)
                     * std::sin(cycle * 313.0f + 1.21f);
            default: return 0.0f;
        }
    };

    juce::Path waveform;
    const float centreY = static_cast<float>(bounds.getCentreY());
    const float amplitude = static_cast<float>(bounds.getHeight()) * 0.39f;
    const int firstX = bounds.getX() + 4;
    const int lastX = bounds.getRight() - 4;
    const int width = juce::jmax(1, lastX - firstX);
    for (int x = firstX; x <= lastX; ++x)
    {
        const float normalizedX = static_cast<float>(x - firstX)
                                / static_cast<float>(width);
        float sample = 0.0f;
        for (const auto& osc : oscillators)
            if (osc.wave != 0)
                sample += sampleWave(
                    osc.wave, normalizedX * 2.0f * osc.tuneRatio,
                    osc.pulseWidth) * osc.gain;
        sample /= totalGain;
        const float y = centreY - sample * amplitude;
        if (x == firstX) waveform.startNewSubPath(static_cast<float>(x), y);
        else             waveform.lineTo(static_cast<float>(x), y);
    }

    g.setColour(UiTheme::kTitlebarAccent);
    g.strokePath(waveform, juce::PathStrokeType(1.5f));
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
    g.drawFittedText("4OSC waveform", bounds.reduced(6, 2),
                     juce::Justification::topLeft, 1);
}

void PluginEditorWidget::renderPresetsPage(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const int n = getNumPresets();
    if (n <= 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));

        juce::String msg;
        if (useFileScanPresets())
            msg = "No .syx files found\n\nGlob:\n" + config_->presets.glob;
        else
            msg = "This plugin exposes no presets";

        g.drawFittedText(msg, bounds, juce::Justification::centred, 4);
        return;
    }

    const int current = getCurrentPresetIndex();
    const int rowH = 16;
    const int topPad = 4;
    const int visibleRows = juce::jmax(1, (bounds.getHeight() - topPad - 14) / rowH);

    const int half = visibleRows / 2;
    int firstRow = juce::jlimit(0, juce::jmax(0, n - visibleRows),
                                presetSelection_ - half);

    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
    int y = bounds.getY() + topPad;
    for (int i = 0; i < visibleRows && (firstRow + i) < n; ++i)
    {
        const int idx = firstRow + i;
        auto row = juce::Rectangle<int>(bounds.getX() + 8, y,
                                        bounds.getWidth() - 16, rowH);

        if (idx == presetSelection_)
        {
            g.setColour(UiTheme::kTitlebarAccent.withAlpha(0.35f));
            g.fillRect(row);
        }

        g.setColour(idx == current
            ? UiTheme::kTitlebarAccent
            : juce::Colours::white.withAlpha(0.4f));
        g.drawFittedText(idx == current
                             ? juce::String(juce::CharPointer_UTF8 ("\xe2\x96\xb6"))
                             : juce::String(" "),
                         row.withWidth(14),
                         juce::Justification::centredLeft, 1);

        g.setColour(juce::Colours::white);
        g.drawFittedText(getPresetName(idx),
                         row.withTrimmedLeft(16).withTrimmedRight(60),
                         juce::Justification::centredLeft, 1);

        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawFittedText(juce::String(idx + 1) + "/" + juce::String(n),
                         row.removeFromRight(60),
                         juce::Justification::centredRight, 1);

        y += rowH;
    }

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawFittedText("k1: scroll  \u00b7  d1: Load",
                     bounds.removeFromBottom(14),
                     juce::Justification::centred, 1);
}

void PluginEditorWidget::renderParamsPage(juce::Graphics& g, int paramsPageIndex,
                                           juce::Rectangle<int> bounds)
{
    auto params = getParamsForPage(paramsPageIndex);
    if (params.isEmpty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        g.drawFittedText("No automatable parameters",
                         bounds, juce::Justification::centred, 1);
        return;
    }

    const int colW = bounds.getWidth() / kParamsPerPage;
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        if (p == nullptr) continue;

        auto cell = juce::Rectangle<int>(bounds.getX() + i * colW,
                                         bounds.getY(),
                                         colW, bounds.getHeight())
                    .reduced(4);

        auto top = cell.removeFromTop(cell.getHeight() * 2 / 3);
        g.setColour(UiTheme::kTitlebarAccent);
        g.drawRect(top, 1);

        const auto range = p->getValueRange();
        const float span = range.getLength() > 0 ? range.getLength() : 1.0f;
        const float frac = juce::jlimit(0.0f, 1.0f,
            (p->getCurrentValue() - range.getStart()) / span);

        auto fill = top.reduced(2);
        auto barHeight = juce::roundToInt(fill.getHeight() * frac);
        auto bar = fill.withY(fill.getBottom() - barHeight).withHeight(barHeight);
        g.setColour(UiTheme::kTitlebarAccent);
        g.fillRect(bar);

        g.setColour(juce::Colours::white);
        g.drawFittedText(p->getParameterName(),
                         cell.removeFromTop(cell.getHeight() / 2),
                         juce::Justification::centredTop, 1);
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawFittedText(p->getCurrentValueAsString(), cell,
                         juce::Justification::centredBottom, 1);
    }

    const int totalPages = paramPageCount();
    if (totalPages > 1)
    {
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        juce::String txt = juce::String(paramsPageIndex + 1) + "/"
                         + juce::String(totalPages);
        g.drawFittedText(txt, bounds.removeFromBottom(14),
                         juce::Justification::centredRight, 1);
    }
}

void PluginEditorWidget::resized()
{
    viewport_.setBounds(getLocalBounds());
    if (editor_ != nullptr && editor_->getWidth() == 0)
        editor_->setSize(viewport_.getWidth(), viewport_.getHeight());
}
