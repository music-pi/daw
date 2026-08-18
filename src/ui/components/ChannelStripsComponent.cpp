#include "ChannelStripsComponent.h"

#include "../theme/UiTheme.h"
#include "../UiRefresh.h"
#include "mixer/LevelMeterComponent.h"
#include "mixer/FaderMarkerOverlay.h"
#include "mixer/MixerUtils.h"
#include "mixer/PanRadialComponent.h"
#include "mixer/IndicatorButton.h"
#include "mixer/TransparentFaderLookAndFeel.h"

// ─── ChannelStrip ───────────────────────────────────────────────────────

class ChannelStripsComponent::ChannelStrip : public juce::Component
{
public:
    ChannelStrip()
    {
        faderLookAndFeel_ = std::make_unique<TransparentFaderLookAndFeel>();
        muteIndicator_ = std::make_unique<IndicatorButton>("MUTE", false);
        pluginsIndicator_ = std::make_unique<IndicatorButton>("PLGS", false);
        eqIndicator_ = std::make_unique<IndicatorButton>("EQ", false);
        auxIndicator_ = std::make_unique<IndicatorButton>("AUX", false);

        addAndMakeVisible(levelMeter_);
        addAndMakeVisible(faderOverlay_);
        addAndMakeVisible(panRadial_);
        addAndMakeVisible(muteIndicator_.get());
        addAndMakeVisible(pluginsIndicator_.get());
        addAndMakeVisible(eqIndicator_.get());
        addAndMakeVisible(auxIndicator_.get());

        gainSlider_.setRange(UiTheme::kFaderDbMin, UiTheme::kFaderDbMax, 0.01);
        gainSlider_.setSliderStyle(juce::Slider::LinearVertical);
        gainSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        gainSlider_.setLookAndFeel(faderLookAndFeel_.get());
        gainSlider_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(gainSlider_);

        // Ballistics: fast attack, quicker release so the meter tracks
        // transients responsively. Without smoothing, 60 Hz raw samples look
        // jittery as the meter jumps sample-to-sample.
        peakSmoother_.configure(UiTheme::kUiMeterUpdateRateHz, 0.03f, 0.12f);
        rmsSmoother_.configure(UiTheme::kUiMeterUpdateRateHz, 0.08f, 0.20f);

        // Intentionally NOT using setBufferedToImage: a previous attempt did,
        // but JUCE re-enters paint() for the dirty sub-rect inside the cached
        // image, and our paint() here fills the background + redraws the
        // header unconditionally (no g.getClipBounds() gate) — making the
        // buffered path strictly more work than no buffering. The change-
        // detection guards on child setters (LevelMeter::setLevels,
        // FaderMarkerOverlay::setGainDb, IndicatorButton::setIndicatorState,
        // PanRadialComponent::setPan) already prevent idle repaints.
    }

    ~ChannelStrip() override
    {
        gainSlider_.setLookAndFeel(nullptr);
    }

    void setChannel(const ChannelView& view)
    {
        state_ = view.state;
        descriptor_ = view.descriptor;
        setVisible(state_ != nullptr);
        repaint();
        requestUiRefresh(*this);
    }

    void setActive(bool active)
    {
        if (active_ == active)
            return;
        active_ = active;
        repaint();
        requestUiRefresh(*this);
    }

    void refreshFromState()
    {
        if (state_ == nullptr)
            return;

        const float peakRaw = juce::jlimit(UiTheme::kMeterDbMin, UiTheme::kMeterDbMax,
            MixerUtils::sanitizeDb(state_->inputPeakDbfs.load(std::memory_order_relaxed),
                                   UiTheme::kMeterDbMin));
        const float rmsRaw = juce::jlimit(UiTheme::kMeterDbMin, UiTheme::kMeterDbMax,
            MixerUtils::sanitizeDb(state_->inputRmsDbfs.load(std::memory_order_relaxed),
                                   UiTheme::kMeterDbMin));
        const float gainDb = juce::jlimit(UiTheme::kFaderDbMin, UiTheme::kFaderDbMax,
            state_->faderGainDb.load(std::memory_order_relaxed));
        const float panValue = juce::jlimit(-1.0f, 1.0f,
            state_->pan.load(std::memory_order_relaxed));
        const bool muted = state_->mute.load(std::memory_order_relaxed);
        const int pluginCount = state_->activePlugins.load(std::memory_order_relaxed);

        // Per-strip delta gate: when raw atomics match last tick AND the
        // peak-hold has already decayed to its floor, the whole downstream
        // pipeline (smoothers + child setters + String allocs) is a no-op.
        // Skip directly to cut 60 Hz worth of redundant work on idle strips.
        constexpr float kEps = 1e-6f;
        const bool peakHoldAtFloor = peakHoldDbfs_ <= UiTheme::kMeterDbMin + kEps;
        const bool rawMatches = lastSnapshotValid_
            && std::abs(peakRaw - lastSnapshot_.peakRaw) <= kEps
            && std::abs(rmsRaw - lastSnapshot_.rmsRaw) <= kEps
            && std::abs(gainDb - lastSnapshot_.gainDb) <= kEps
            && std::abs(panValue - lastSnapshot_.pan) <= kEps
            && muted == lastSnapshot_.muted
            && pluginCount == lastSnapshot_.pluginCount;
        if (rawMatches && peakHoldAtFloor)
            return;

        // Ballistics-smooth the meter; peak-hold follows the smoothed peak
        // with instant rise and a gentle decay after a 1.2s dwell.
        const float peakDisplay = peakSmoother_.process(peakRaw);
        const float rmsDisplay  = rmsSmoother_.process(rmsRaw);
        updatePeakHold(peakDisplay);

        levelMeter_.setLevels(peakDisplay, rmsDisplay, peakHoldDbfs_);
        faderOverlay_.setGainDb(gainDb);
        panRadial_.setPan(panValue);

        muteIndicator_->setIndicatorState(muted);
        pluginsIndicator_->setIndicatorState(pluginCount > 0);
        pluginsIndicator_->setBadgeText(juce::String(pluginCount));
        eqIndicator_->setIndicatorState(false);
        auxIndicator_->setIndicatorState(false);

        if (!gainSlider_.isMouseButtonDown())
            gainSlider_.setValue(gainDb, juce::dontSendNotification);

        lastSnapshot_ = { peakRaw, rmsRaw, gainDb, panValue, muted, pluginCount };
        lastSnapshotValid_ = true;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();

        g.setColour(active_ ? UiTheme::kStripBackground.brighter(0.18f)
                            : UiTheme::kStripBackground);
        g.fillRect(bounds);
        g.setColour(UiTheme::kStripBorder);
        g.drawRect(bounds, 1);

        // Name header
        auto header = bounds.removeFromTop(18);
        g.setColour(juce::Colours::white.withAlpha(active_ ? 1.0f : 0.75f));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawFittedText(descriptor_.name, header.reduced(4, 0),
                         juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto content = getLocalBounds().reduced(3, 3);
        content.removeFromTop(16);  // room for the header label in paint()

        // Right column: MUTE / PLGS / EQ / AUX + pan radial at bottom.
        auto indicatorColumn = content.removeFromRight(juce::jmax(44, content.getWidth() * 42 / 100));
        content.removeFromRight(3);

        const int rowSpacing = 3;
        const int panHeight = juce::jmax(36, indicatorColumn.getHeight() / 5);
        const int availableH = juce::jmax(0, indicatorColumn.getHeight() - rowSpacing * 4 - panHeight);
        const int indicatorH = juce::jmax(18, availableH / 4);

        auto takeRow = [&](int h) {
            auto row = indicatorColumn.removeFromTop(h);
            if (indicatorColumn.getHeight() > 0)
                indicatorColumn.removeFromTop(rowSpacing);
            return row;
        };

        muteIndicator_->setBounds(takeRow(indicatorH));
        pluginsIndicator_->setBounds(takeRow(indicatorH));
        eqIndicator_->setBounds(takeRow(indicatorH));
        auxIndicator_->setBounds(takeRow(indicatorH));
        panRadial_.setBounds(indicatorColumn);

        // Left column: just the meter / fader, no dB scale labels.
        auto meterArea = content.reduced(1, 0);
        levelMeter_.setBounds(meterArea);
        faderOverlay_.setBounds(meterArea);
        gainSlider_.setBounds(meterArea);
    }

private:
    std::shared_ptr<MixerChannelState> state_;
    ChannelDescriptor descriptor_;
    bool active_ { false };

    void updatePeakHold(float newPeakDb)
    {
        const double now = MixerUtils::currentTimeSeconds();
        const double delta = (lastUpdateSeconds_ > 0.0) ? (now - lastUpdateSeconds_) : 0.0;
        lastUpdateSeconds_ = now;

        if (newPeakDb > peakHoldDbfs_ + 0.1f)
        {
            peakHoldDbfs_ = newPeakDb;
            peakHoldExpiry_ = now + UiTheme::kPeakHoldSeconds;
        }
        else if (delta > 0.0 && now > peakHoldExpiry_)
        {
            peakHoldDbfs_ -= UiTheme::kHoldDecayDbPerSec * static_cast<float>(delta);
        }
        peakHoldDbfs_ = juce::jmax(UiTheme::kMeterDbMin, peakHoldDbfs_);
    }

    LevelMeterComponent levelMeter_;
    FaderMarkerOverlay faderOverlay_;
    PanRadialComponent panRadial_;

    MeterSmoother peakSmoother_;
    MeterSmoother rmsSmoother_;
    float peakHoldDbfs_ { UiTheme::kMeterDbMin };
    double peakHoldExpiry_ { 0.0 };
    double lastUpdateSeconds_ { 0.0 };
    juce::Slider gainSlider_;

    // Delta-gate: skip the full refresh when all raw inputs + peak-hold are stable.
    struct Snapshot
    {
        float peakRaw { 0.0f };
        float rmsRaw { 0.0f };
        float gainDb { 0.0f };
        float pan { 0.0f };
        bool muted { false };
        int pluginCount { 0 };
    };
    Snapshot lastSnapshot_ {};
    bool lastSnapshotValid_ { false };

    std::unique_ptr<IndicatorButton> muteIndicator_;
    std::unique_ptr<IndicatorButton> pluginsIndicator_;
    std::unique_ptr<IndicatorButton> eqIndicator_;
    std::unique_ptr<IndicatorButton> auxIndicator_;

    std::unique_ptr<TransparentFaderLookAndFeel> faderLookAndFeel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

// ─── ChannelStripsComponent ─────────────────────────────────────────────

ChannelStripsComponent::ChannelStripsComponent()
{
    for (int i = 0; i < 4; ++i)
    {
        auto* s = new ChannelStrip();
        addAndMakeVisible(s);
        strips_.add(s);
    }
}

ChannelStripsComponent::~ChannelStripsComponent() = default;

void ChannelStripsComponent::setChannels(std::vector<ChannelView> channels)
{
    channels_ = std::move(channels);
    while (channels_.size() < 4)
        channels_.push_back({});
    if (channels_.size() > 4)
        channels_.resize(4);

    for (int i = 0; i < 4; ++i)
    {
        strips_[i]->setChannel(channels_[(size_t)i]);
        strips_[i]->setActive(i == activeIndex_ && channels_[(size_t)i].state != nullptr);
    }
    layoutContent();
    repaint();
    requestUiRefresh(*this);
}

void ChannelStripsComponent::setActiveIndex(int index)
{
    if (activeIndex_ == index)
        return;
    activeIndex_ = index;
    for (int i = 0; i < 4; ++i)
    {
        strips_[i]->setActive(i == activeIndex_
                              && i < (int)channels_.size()
                              && channels_[(size_t)i].state != nullptr);
    }
    layoutContent();
    repaint();
    requestUiRefresh(*this);
}

void ChannelStripsComponent::setMuteModifierHeld(bool held)
{
    if (muteModifierHeld_ == held)
        return;
    muteModifierHeld_ = held;
    repaint();
    requestUiRefresh(*this);
}

void ChannelStripsComponent::setSoloModifierHeld(bool held)
{
    if (soloModifierHeld_ == held)
        return;
    soloModifierHeld_ = held;
    repaint();
    requestUiRefresh(*this);
}

void ChannelStripsComponent::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
}

void ChannelStripsComponent::resized()
{
    layoutContent();
}

void ChannelStripsComponent::layoutContent()
{
    auto bounds = getLocalBounds();
    const int stripWidth = bounds.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        const int x = i * stripWidth;
        const int w = (i == 3) ? (bounds.getWidth() - x) : stripWidth;
        strips_[i]->setBounds(x, 0, w, bounds.getHeight());
    }
}

void ChannelStripsComponent::refreshLiveState()
{
    // Push live atomic values into each strip. The strip's children use
    // change-detection guards, so only genuinely-moved values trigger a
    // repaint + UiHost dirty flip.
    for (auto* strip : strips_)
        strip->refreshFromState();
}
