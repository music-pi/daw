#include "PadOverviewWidget.h"

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/GroupManager.h"
#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

PadOverviewWidget::PadOverviewWidget()
{
    thumbnailFormatManager_.registerBasicFormats();
    addAndMakeVisible(grid_);
}

PadOverviewWidget::~PadOverviewWidget()
{
    if (listeningForPadTriggers_)
    {
        engine().getSampler().removeListener(this);
        listeningForPadTriggers_ = false;
    }
}

// ── Widget identity ──────────────────────────────────────────────────────────

WidgetDescriptor PadOverviewWidget::describe() const
{
    return { "pad_overview", 1, false, DisplayConstraint::Any };
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void PadOverviewWidget::onActivated(int offset)
{
    panelOffset_ = offset;

    // Register as SamplerInstrument::Listener for pad trigger flashes
    if (!listeningForPadTriggers_)
    {
        engine().getSampler().addListener(this);
        listeningForPadTriggers_ = true;
    }

    // Configure the grid style
    PadGridComponent::Style style;
    style.borderWidth   = UiTheme::kPadBorderWidth;
    style.cornerRadius  = 0.0f;
    style.borderColour  = UiTheme::kPadBorder;
    style.gapPx         = UiTheme::kPadGridGap / 4;  // kPadGridGap=8 → 2 px between tiles
    grid_.setStyle(style);

    // Delegate LED publishing to the grid component
    grid_.enableLedPublishing(hw(), describe().id.toStdString());

    // Waveform overlay callback — captures this by raw pointer (safe: grid_
    // is a member and is destroyed before *this).
    grid_.setTileOverlay([this](juce::Graphics& g, juce::Rectangle<float> tileBounds, int padIndex)
    {
        auto thumbIter = padThumbnails_.find(padIndex);
        if (thumbIter == padThumbnails_.end() || thumbIter->second == nullptr)
            return;

        auto& thumb = *thumbIter->second;
        if (thumb.getTotalLength() > 0.0)
        {
            double drawStart = 0.0;
            double drawEnd   = thumb.getTotalLength();
            if (padIndex >= 0
                && static_cast<size_t>(padIndex) < padSnapshots_.size())
            {
                const auto& snap = padSnapshots_[static_cast<size_t>(padIndex)];
                if (snap.windowEndSeconds > snap.windowStartSeconds
                    && snap.windowEndSeconds <= thumb.getTotalLength() + 1e-6)
                {
                    drawStart = juce::jmax(0.0, snap.windowStartSeconds);
                    drawEnd   = juce::jmin(thumb.getTotalLength(), snap.windowEndSeconds);
                }
            }
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            thumb.drawChannels(g, tileBounds.toNearestInt().reduced(4),
                               drawStart, drawEnd, 1.0f);
        }
    });

    refreshSnapshots();

    // Pre-light the mute/solo modifier buttons so the user knows they're
    // available while pad mode is focused.
    updateModifierLeds();

    // View-priority pad handler consumes pad presses when mute/solo is held
    // so the sample trigger at Global priority doesn't fire during a toggle.
    if (auto* host = controllerHost())
    {
        if (auto* im = host->getInputManager())
        {
            padHoldBinding_ = im->addPadHandler(
                InputManager::HandlerPriority::View, "",
                [this](InputEvent& e) {
                    if (!muteHeld_ && !soloHeld_)
                        return;
                    const bool pressed = e.metadata.contains("pressed")
                                      && static_cast<bool>(e.metadata["pressed"]);
                    if (!pressed)
                    {
                        e.consumed = true;
                        return;
                    }
                    int padIdx = -1;
                    if (e.metadata.contains("pad"))
                        padIdx = static_cast<int>(e.metadata["pad"]);
                    if (padIdx < 0 || padIdx >= 16)
                    {
                        e.consumed = true;
                        return;
                    }

                    auto& pads = engine().getSampler();
                    if (soloHeld_)
                    {
                        const bool next = !pads.isSoloed(padIdx);
                        pads.setSolo(padIdx, next);
                        showToast(ToastKind::Info,
                                  (next ? "Solo Pad " : "Unsolo Pad ")
                                      + juce::String(padIdx + 1));
                    }
                    else if (muteHeld_)
                    {
                        const bool next = !pads.isMuted(padIdx);
                        pads.setMute(padIdx, next);
                        showToast(ToastKind::Info,
                                  (next ? "Mute Pad " : "Unmute Pad ")
                                      + juce::String(padIdx + 1));
                    }

                    refreshSnapshots();
                    e.consumed = true;
                });
        }
    }
}

void PadOverviewWidget::onDeactivated()
{
    if (listeningForPadTriggers_)
    {
        engine().getSampler().removeListener(this);
        listeningForPadTriggers_ = false;
    }

    releasePadHoldBinding();
    muteHeld_ = false;
    soloHeld_ = false;

    // Turn off modifier LEDs we lit on activation. Must use the widget's
    // describe().id as ownerId to match the claim WindowManager made from
    // requiredResources().
    const auto owner = describe().id.toStdString();
    hw().setLed("solo",      HardwareConstants::kLedOff, owner);
    hw().setLed("muteChoke", HardwareConstants::kLedOff, owner);

    // Clear the HardwareState pointer the grid cached during onActivated so a
    // later stray setTiles() can't dereference a dangling ref.
    grid_.disableLedPublishing();
}

// ── Resources ────────────────────────────────────────────────────────────────

std::vector<std::string> PadOverviewWidget::requiredResources(int page)
{
    if (page != 0)
        return {};

    std::vector<std::string> resources;

    for (int i = 1; i <= 16; ++i)
        resources.push_back("p" + std::to_string(i));

    // Modifier buttons for the mute/solo hold workflow.
    resources.push_back("solo");
    resources.push_back("muteChoke");

    if (panelOffset_ == 0)
    {
        for (int i = 1; i <= 4; ++i)
        {
            resources.push_back("d" + std::to_string(i));
            resources.push_back("k" + std::to_string(i));
        }
    }
    else
    {
        for (int i = 5; i <= 8; ++i)
        {
            resources.push_back("d" + std::to_string(i));
            resources.push_back("k" + std::to_string(i));
        }
    }

    return resources;
}

// ── Options & Knobs ──────────────────────────────────────────────────────────

std::vector<Option> PadOverviewWidget::getOptions(int page)
{
    if (page != 0)
        return {};
    return {};
}

std::vector<Knob> PadOverviewWidget::getKnobs(int page)
{
    if (page != 0)
        return {};
    return {};
}

// ── Rendering ────────────────────────────────────────────────────────────────

void PadOverviewWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    // Grid is a child component — JUCE paints it automatically.
}

void PadOverviewWidget::paintPage(juce::Graphics& /*g*/, int /*page*/, juce::Rectangle<int> /*bounds*/)
{
    // Tile rendering is delegated to PadGridComponent (child component).
}

void PadOverviewWidget::resized()
{
    auto area = getLocalBounds().reduced(UiTheme::kPadding, UiTheme::kPadding);
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        area = getLocalBounds();
    grid_.setBounds(area);
}

// ── Input ────────────────────────────────────────────────────────────────────

void PadOverviewWidget::handlePad(const controller_events::PadEvent& e)
{
    if (!e.pressed)
        return;

    int padIndex = static_cast<int>(e.pad);
    if (padIndex < 0 || padIndex >= 16)
        return;

    const bool selectHeld = controllerHost() != nullptr && controllerHost()->isSelectPressed();
    if (selectHeld)
    {
        engine().getSampler().selectPad(padIndex);
        repaint();

        if (padSelectedCallback_)
            padSelectedCallback_(padIndex);
    }
    // If select not held, do NOT consume — let pad trigger through legacy path
}

void PadOverviewWidget::handleButton(const controller_events::ButtonEvent& e)
{
    // Mute/solo modifier tracking. The actual toggle happens in the
    // view-priority pad handler registered in onActivated so we can consume
    // the event before the global sample-trigger handler sees it.
    if (e.name == "solo")
    {
        soloHeld_ = e.pressed;
        if (soloHeld_)
            muteHeld_ = false;
        updateModifierLeds();
        return;
    }
    if (e.name == "muteChoke")
    {
        if (!soloHeld_)
            muteHeld_ = e.pressed;
        updateModifierLeds();
        return;
    }
}

void PadOverviewWidget::updateModifierLeds()
{
    const auto owner = describe().id.toStdString();
    hw().setLed("solo",
        soloHeld_ ? HardwareConstants::kLedBright : HardwareConstants::kLedDim,
        owner);
    hw().setLed("muteChoke",
        muteHeld_ ? HardwareConstants::kLedBright : HardwareConstants::kLedDim,
        owner);
}

void PadOverviewWidget::releasePadHoldBinding()
{
    if (padHoldBinding_ == kInvalidBinding)
        return;
    if (auto* host = controllerHost())
        if (auto* im = host->getInputManager())
            im->removeHandler(padHoldBinding_);
    padHoldBinding_ = kInvalidBinding;
}

// ── SamplerInstrument::Listener ──────────────────────────────────────────────

void PadOverviewWidget::padTriggered(int padId)
{
    juce::Component::SafePointer<PadOverviewWidget> safe(this);
    juce::MessageManager::callAsync([safe, padId]()
    {
        if (auto* w = safe.getComponent())
            w->handlePadTriggered(padId);
    });
}

// ── Private ──────────────────────────────────────────────────────────────────

void PadOverviewWidget::onUiHostTick()
{
    pruneExpiredFlashes();
    refreshSnapshots();
}

void PadOverviewWidget::refreshSnapshots()
{
    auto newSnapshots = engine().getSampler().getPadsSnapshot(
        SamplerInstrument::SnapshotContent::State);

    bool sampleStateChanged = (newSnapshots.size() != padSnapshots_.size());
    if (!sampleStateChanged)
    {
        for (size_t i = 0; i < newSnapshots.size(); ++i)
        {
            if (newSnapshots[i].hasSample != padSnapshots_[i].hasSample)
            {
                sampleStateChanged = true;
                break;
            }
        }
    }

    padSnapshots_ = std::move(newSnapshots);

    for (int i = 0; i < static_cast<int>(padSnapshots_.size()); ++i)
        updateThumbnailForPad(i, padSnapshots_[static_cast<size_t>(i)]);

    // Prune flash times for pads that no longer exist. std::erase_if is
    // C++20 and safe against the erase-while-iterating trap.
    std::erase_if(padFlashTimes_, [this](const auto& entry) {
        const int padId = entry.first;
        return !std::any_of(padSnapshots_.begin(), padSnapshots_.end(),
                            [padId](const SamplerInstrument::PadSnapshot& pad)
                            {
                                return pad.id == padId;
                            });
    });

    if (sampleStateChanged || !padFlashTimes_.empty())
        refreshTiles();
}

void PadOverviewWidget::refreshTiles()
{
    // Gather group colour
    juce::Colour groupColour = UiTheme::kPadWithSample;
    auto& gm = engine().getGroupManager();
    const int ag = gm.getActiveGroupIndex();
    if (ag >= 0)
        groupColour = UiTheme::ledIndexToColour(gm.getGroupColor(ag));

    uint8_t groupLedColor = 0;
    if (ag >= 0)
        groupLedColor = UiTheme::brightestHueVariant(gm.getGroupColor(ag));

    const int selectedPadId = engine().getSampler().getSelectedPad();
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    constexpr uint8_t kFlashLedColor = HardwareConstants::kColorWhite;

    std::array<PadGridComponent::Tile, 16> tiles;
    const juce::Font labelFont { juce::FontOptions(UiTheme::Fonts::kBody, juce::Font::bold) };

    for (int i = 0; i < 16; ++i)
    {
        auto& tile = tiles[static_cast<size_t>(i)];
        tile.labelFont = labelFont;

        if (i >= static_cast<int>(padSnapshots_.size()))
        {
            // No snapshot yet (sampler still initialising) — render as empty
            // rather than transparent so there's no flash of nothing.
            tile.fill        = UiTheme::kOptionFillEmpty;
            tile.labelColour = juce::Colours::transparentBlack;
            tile.ledColor    = 0;
            continue;
        }

        const auto& pad = padSnapshots_[static_cast<size_t>(i)];
        const bool hasSample  = pad.hasSample;
        const bool isSelected = pad.id == selectedPadId;

        float flashAmount = 0.0f;
        if (auto it = padFlashTimes_.find(pad.id); it != padFlashTimes_.end())
        {
            const double elapsed = nowMs - it->second;
            if (elapsed >= 0.0 && elapsed <= kFlashDurationMs)
                flashAmount = static_cast<float>(juce::jlimit(0.0, 1.0,
                                                              1.0 - elapsed / kFlashDurationMs));
        }

        if (!hasSample)
        {
            tile.fill        = UiTheme::kOptionFillEmpty;
            tile.labelColour = juce::Colours::transparentBlack;
            tile.ledColor    = 0;
        }
        else
        {
            juce::Colour fill = groupColour;
            if (isSelected)
                fill = fill.brighter(0.3f);
            if (flashAmount > 0.0f)
                fill = fill.interpolatedWith(juce::Colours::white, flashAmount * 0.7f);

            tile.fill        = fill;
            tile.labelColour = fill.contrasting(0.85f);
            tile.label       = (pad.sampleName.isNotEmpty()) ? pad.sampleName : "Empty";
            tile.ledColor    = (flashAmount > 0.0f) ? kFlashLedColor : groupLedColor;
        }
    }

    grid_.setTiles(tiles);
}

void PadOverviewWidget::handlePadTriggered(int padId)
{
    padFlashTimes_[padId] = juce::Time::getMillisecondCounterHiRes();
    refreshTiles();
}

void PadOverviewWidget::pruneExpiredFlashes()
{
    if (padFlashTimes_.empty())
        return;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    // std::erase_if is C++20 and safe against the erase-while-iterating trap.
    std::erase_if(padFlashTimes_, [nowMs](const auto& entry) {
        return nowMs - entry.second > kFlashDurationMs;
    });
}

void PadOverviewWidget::updateThumbnailForPad(int padIndex, const SamplerInstrument::PadSnapshot& pad)
{
    const juce::String samplePath = pad.sampleFile.getFullPathName();

    auto pathIter = loadedSamplePaths_.find(padIndex);
    if (pathIter != loadedSamplePaths_.end() && pathIter->second == samplePath)
        return;

    if (pad.hasSample && pad.sampleFile.existsAsFile())
    {
        if (padThumbnails_.find(padIndex) == padThumbnails_.end())
        {
            padThumbnails_[padIndex] = std::make_unique<juce::AudioThumbnail>(
                128, thumbnailFormatManager_, thumbnailCache_);
        }

        padThumbnails_[padIndex]->setSource(new juce::FileInputSource(pad.sampleFile));
        loadedSamplePaths_[padIndex] = samplePath;
    }
    else
    {
        if (auto iter = padThumbnails_.find(padIndex); iter != padThumbnails_.end())
            iter->second->clear();
        loadedSamplePaths_.erase(padIndex);
    }
}

void PadOverviewWidget::setPadSelectedCallback(std::function<void(int)> callback)
{
    padSelectedCallback_ = std::move(callback);
}
