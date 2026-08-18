#include "ChannelDetailsWidget.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/ChannelInsertUtils.h"
#include "../theme/UiTheme.h"
#include "ChannelSource.h"
#include "MixerStateKeys.h"

#include <cmath>

namespace
{
    te::AutomatableParameter* freqParam(te::EqualiserPlugin& eq, ChannelDetailsWidget::EqBand b)
    {
        switch (b)
        {
            case ChannelDetailsWidget::EqBand::Low:  return eq.loFreq.get();
            case ChannelDetailsWidget::EqBand::Mid1: return eq.midFreq1.get();
            case ChannelDetailsWidget::EqBand::Mid2: return eq.midFreq2.get();
            case ChannelDetailsWidget::EqBand::High: return eq.hiFreq.get();
        }
        return nullptr;
    }

    te::AutomatableParameter* gainParam(te::EqualiserPlugin& eq, ChannelDetailsWidget::EqBand b)
    {
        switch (b)
        {
            case ChannelDetailsWidget::EqBand::Low:  return eq.loGain.get();
            case ChannelDetailsWidget::EqBand::Mid1: return eq.midGain1.get();
            case ChannelDetailsWidget::EqBand::Mid2: return eq.midGain2.get();
            case ChannelDetailsWidget::EqBand::High: return eq.hiGain.get();
        }
        return nullptr;
    }

    te::AutomatableParameter* qParam(te::EqualiserPlugin& eq, ChannelDetailsWidget::EqBand b)
    {
        switch (b)
        {
            case ChannelDetailsWidget::EqBand::Low:  return eq.loQ.get();
            case ChannelDetailsWidget::EqBand::Mid1: return eq.midQ1.get();
            case ChannelDetailsWidget::EqBand::Mid2: return eq.midQ2.get();
            case ChannelDetailsWidget::EqBand::High: return eq.hiQ.get();
        }
        return nullptr;
    }

}

ChannelDetailsWidget::ChannelDetailsWidget() = default;
ChannelDetailsWidget::~ChannelDetailsWidget() = default;

WidgetDescriptor ChannelDetailsWidget::describe() const
{
    return { "channel-details", 1, false, DisplayConstraint::Any };
}

juce::String ChannelDetailsWidget::getTitle() const
{
    return cachedTitle_;
}

juce::String ChannelDetailsWidget::getTitleSubtitle() const
{
    return sampleFileName_;
}

void ChannelDetailsWidget::onActivated(int offset)
{
    panelOffset_ = offset;

    watchedMixerState_ = engine().getMixerState();
    if (watchedMixerState_.isValid())
        watchedMixerState_.addListener(this);

    tickDiv_ = 0;
    resolveActiveChannel();
}

void ChannelDetailsWidget::onDeactivated()
{
    if (watchedMixerState_.isValid())
        watchedMixerState_.removeListener(this);
    watchedMixerState_ = juce::ValueTree();
}

void ChannelDetailsWidget::onEditAboutToBeReplaced()
{
    resumeAfterEditReplacement_ = watchedMixerState_.isValid();
    if (resumeAfterEditReplacement_)
        watchedMixerState_.removeListener(this);

    currentDescriptor_ = {};
    currentEq_ = nullptr;
    currentKnobs_.clear();
    insertsList_.clear();
    sendSlots_ = {};
    pluginCount_ = 0;
    sampleFileName_.clear();
    cachedTitle_ = "Channel > —";

    watchedMixerState_ = juce::ValueTree();
}

void ChannelDetailsWidget::onEditReplaced()
{
    if (resumeAfterEditReplacement_)
    {
        watchedMixerState_ = engine().getMixerState();
        if (watchedMixerState_.isValid())
            watchedMixerState_.addListener(this);
        resolveActiveChannel();
    }
    resumeAfterEditReplacement_ = false;
    repaint();
}

void ChannelDetailsWidget::valueTreePropertyChanged(juce::ValueTree& tree,
                                                    const juce::Identifier& property)
{
    if (tree != watchedMixerState_)
        return;
    const auto name = property.toString();
    if (name == MixerStateKeys::Level
        || name == MixerStateKeys::FocusedGroup
        || name == MixerStateKeys::ActiveChannel)
    {
        resolveActiveChannel();
        repaint();
    }
}

std::vector<std::string> ChannelDetailsWidget::requiredResources(int page)
{
    if (page != 0)
        return {};
    std::vector<std::string> res;
    const int base = (panelOffset_ == 0) ? 1 : 5;
    res.push_back("d" + std::to_string(base));
    res.push_back("d" + std::to_string(base + 1));
    return res;
}

std::vector<Option> ChannelDetailsWidget::getOptions(int page)
{
    if (page != 0)
        return {};

    const bool canAddInsert = onAddInsertRequested_ != nullptr
                               && resolveTargetPluginList() != nullptr;

    Option addInsert;
    addInsert.id = "channel.add-insert";
    addInsert.label = "Add Insert";
    addInsert.state = canAddInsert ? OptionState::Enabled : OptionState::Disabled;
    addInsert.onInvoke = [this]() { triggerAddInsert(); };

    Option equaliser;
    equaliser.id = "channel.eq";
    equaliser.label = currentEq_ != nullptr ? "Open EQ" : "Add EQ";
    equaliser.state = onEqRequested_ != nullptr && resolveTargetPluginList() != nullptr
                        ? OptionState::Enabled
                        : OptionState::Disabled;
    equaliser.onInvoke = [this]() { triggerEq(); };

    return { addInsert, equaliser };
}

std::vector<Knob> ChannelDetailsWidget::getKnobs(int page)
{
    if (page != 0)
        return {};
    return {};
}

void ChannelDetailsWidget::paintPage(juce::Graphics&, int, juce::Rectangle<int>)
{
}

void ChannelDetailsWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);

    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // EQ editing has its own full-panel view. Channel Details therefore gives
    // the insert chain most of the panel and keeps sends as a compact footer.
    const int insertsHeight = juce::jmax(90, (bounds.getHeight() * 68) / 100);
    auto insertsArea = bounds.removeFromTop(insertsHeight);

    paintInserts(g, insertsArea);
    paintSends(g, bounds);
}

void ChannelDetailsWidget::paintInserts(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(UiTheme::kStripBackground);
    g.fillRect(bounds);

    auto inner = bounds.reduced(8, 6);

    // Header.
    auto headerArea = inner.removeFromTop(14);
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kKnobBarTitle, juce::Font::bold)));
    g.drawFittedText("INSERTS", headerArea, juce::Justification::centredLeft, 1);

    inner.removeFromTop(2);

    // Rows — one per slot. Alternate subtle row shading for readability.
    const int rowCount = kInsertRowCount;
    if (rowCount <= 0 || inner.getHeight() <= 0)
        return;

    const int rowH = juce::jmax(12, inner.getHeight() / rowCount);

    for (int i = 0; i < rowCount; ++i)
    {
        auto row = inner.removeFromTop(rowH);
        if (row.isEmpty())
            break;

        const bool filled = i < static_cast<int>(insertsList_.size());
        const juce::String label = filled ? insertsList_[static_cast<size_t>(i)]
                                          : juce::String("Empty");

        if (i % 2 == 1)
        {
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.fillRect(row);
        }

        g.setColour(filled ? juce::Colours::white
                           : juce::Colours::white.withAlpha(0.25f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall,
                                               filled ? juce::Font::plain : juce::Font::italic)));
        g.drawFittedText(label, row.reduced(6, 0),
                         juce::Justification::centredLeft, 1);
    }
}

void ChannelDetailsWidget::paintSends(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(UiTheme::kStripBackground);
    g.fillRect(bounds);

    auto inner = bounds.reduced(8, 6);

    // Header.
    auto headerArea = inner.removeFromTop(14);
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kKnobBarTitle, juce::Font::bold)));
    g.drawFittedText("Sends", headerArea, juce::Justification::centredLeft, 1);

    inner.removeFromTop(2);

    if (inner.getHeight() <= 0 || inner.getWidth() <= 0)
        return;

    const int cellW = inner.getWidth()  / kSendCols;
    const int cellH = inner.getHeight() / kSendRows;
    if (cellW <= 0 || cellH <= 0)
        return;

    const juce::Colour activeColour   = UiTheme::kAccentGreen;
    const juce::Colour emptyRingColour = juce::Colours::white.withAlpha(0.18f);
    const juce::Colour labelActive    = juce::Colours::white;
    const juce::Colour labelEmpty     = juce::Colours::white.withAlpha(0.35f);

    for (int r = 0; r < kSendRows; ++r)
    {
        for (int c = 0; c < kSendCols; ++c)
        {
            const int idx = r * kSendCols + c;
            juce::Rectangle<int> cell(inner.getX() + c * cellW,
                                      inner.getY() + r * cellH,
                                      cellW, cellH);
            auto cellF = cell.toFloat().reduced(2.0f);

            // Circular knob indicator on the left of the cell.
            const float knobDiameter = juce::jmin(cellF.getHeight() - 4.0f, 18.0f);
            juce::Rectangle<float> knobRect(cellF.getX() + 2.0f,
                                            cellF.getCentreY() - knobDiameter * 0.5f,
                                            knobDiameter, knobDiameter);

            const auto& slot = sendSlots_[static_cast<size_t>(idx)];
            const bool active = slot.active;

            g.setColour(active ? activeColour.withAlpha(0.22f) : emptyRingColour);
            g.drawEllipse(knobRect, 1.2f);

            if (active)
            {
                // Green indicator tick running from 7 o'clock → through 12 →
                // toward a gain-driven end angle. For now show a static
                // "near full" arc so populated sends are visibly different.
                const auto centre = knobRect.getCentre();
                const float radius = knobRect.getWidth() * 0.5f - 1.0f;
                juce::Path arc;
                const float startAngle = juce::MathConstants<float>::pi * 1.25f;
                const float endAngle   = juce::MathConstants<float>::pi * 2.75f;
                arc.addCentredArc(centre.x, centre.y, radius, radius,
                                  0.0f, startAngle, endAngle, true);
                g.setColour(activeColour);
                g.strokePath(arc, juce::PathStrokeType(1.8f));
            }

            // Label sits to the right of the knob.
            juce::Rectangle<float> labelRect(knobRect.getRight() + 4.0f,
                                             cellF.getY(),
                                             cellF.getRight() - (knobRect.getRight() + 4.0f),
                                             cellF.getHeight());

            g.setColour(active ? labelActive : labelEmpty);
            g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall,
                                                   active ? juce::Font::bold : juce::Font::plain)));
            const juce::String text = active && slot.label.isNotEmpty() ? slot.label
                                                                        : juce::String("—");
            g.drawFittedText(text, labelRect.toNearestInt(),
                             juce::Justification::centredLeft, 1);
        }
    }
}

void ChannelDetailsWidget::paintEqCurve(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(UiTheme::kMeterBackground);
    g.fillRect(bounds);
    g.setColour(UiTheme::kStripBorder);
    g.drawRect(bounds, 1);

    auto curveArea = bounds.reduced(8, 8).toFloat();
    if (curveArea.isEmpty())
        return;

    if (currentEq_ == nullptr)
    {
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall)));
        g.drawFittedText("(EQ unavailable)", curveArea.toNearestInt(),
                         juce::Justification::centred, 1);
        return;
    }

    // 0 dB baseline.
    const float baselineY = curveArea.getCentreY();
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawLine(curveArea.getX(), baselineY, curveArea.getRight(), baselineY, 0.8f);

    const float minF = te::EqualiserPlugin::minFreq;
    const float maxF = te::EqualiserPlugin::maxFreq;
    const float logMin = std::log10(minF);
    const float logMax = std::log10(maxF);

    auto freqToX = [&](float freq)
    {
        const float clamped = juce::jlimit(minF, maxF, freq);
        const float u = (std::log10(clamped) - logMin) / (logMax - logMin);
        return curveArea.getX() + u * curveArea.getWidth();
    };

    auto dbToY = [&](float dB)
    {
        const float yNorm = juce::jlimit(-1.0f, 1.0f, dB / te::EqualiserPlugin::maxGain);
        return curveArea.getCentreY() - yNorm * (curveArea.getHeight() * 0.5f);
    };

    // Response curve.
    {
        const int nPoints = juce::jmax(2, static_cast<int>(curveArea.getWidth()));
        juce::Path curve;
        for (int i = 0; i < nPoints; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(nPoints - 1);
            const float freq = std::pow(10.0f, logMin + u * (logMax - logMin));
            const float dB = currentEq_->getDBGainAtFrequency(freq);
            const float x = curveArea.getX() + u * curveArea.getWidth();
            const float y = dbToY(dB);
            if (i == 0) curve.startNewSubPath(x, y);
            else        curve.lineTo(x, y);
        }
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    }

    // Band-point dots — one per band (merge Mid1/Mid2 into a single visual
    // indicator when they cluster, but still draw each band for clarity).
    const std::array<EqBand, 4> bands { EqBand::Low, EqBand::Mid1, EqBand::Mid2, EqBand::High };
    for (auto b : bands)
    {
        auto* fp = freqParam(*currentEq_, b);
        auto* gp = gainParam(*currentEq_, b);
        if (fp == nullptr || gp == nullptr)
            continue;

        const float x = freqToX(fp->getCurrentValue());
        const float y = dbToY(gp->getCurrentValue());
        const bool isSelected = (b == selectedBand_);
        const float r = isSelected ? 4.5f : 3.5f;

        g.setColour(isSelected ? UiTheme::kMeterYellow
                               : juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);

        if (isSelected)
        {
            g.setColour(UiTheme::kMeterYellow.withAlpha(0.35f));
            g.drawEllipse(x - r - 2.0f, y - r - 2.0f,
                          (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f, 1.0f);
        }
    }
}

void ChannelDetailsWidget::resized()
{
    // No child components — paint() does all the rendering.
}

void ChannelDetailsWidget::handleKnob(int localIndex, int16_t delta, uint16_t, bool /*shift*/)
{
    juce::ignoreUnused(localIndex, delta);
}

void ChannelDetailsWidget::handleOption(int localIndex)
{
    // d1 (local index 0) → Add Insert. Other option slots are currently unused.
    if (localIndex == 0)
        triggerAddInsert();
    else if (localIndex == 1)
        triggerEq();
}

te::PluginList* ChannelDetailsWidget::resolveTargetPluginList()
{
    if (currentDescriptor_.kind == ChannelDescriptor::Kind::Master)
    {
        // Master flows through edit.getMasterPluginList(), not a real track.
        auto* edit = engine().getEdit();
        if (edit == nullptr)
            return nullptr;
        return &edit->getMasterPluginList();
    }

    if (currentDescriptor_.track != nullptr)
        return &currentDescriptor_.track->pluginList;

    return nullptr;
}

void ChannelDetailsWidget::triggerAddInsert()
{
    if (onAddInsertRequested_ == nullptr)
        return;
    auto* list = resolveTargetPluginList();
    if (list == nullptr)
        return;
    onAddInsertRequested_(list);
}

void ChannelDetailsWidget::triggerEq()
{
    if (onEqRequested_ == nullptr)
        return;
    auto* list = resolveTargetPluginList();
    if (list == nullptr)
        return;
    onEqRequested_(list, ChannelInsertUtils::findEqualiser(*list));
}

void ChannelDetailsWidget::onUiHostTick()
{
    if (++tickDiv_ < 3) return;
    tickDiv_ = 0;
    // ValueTree listener handles structural changes (active channel swap);
    // this tick refreshes live data we can't observe via ValueTree — plugin
    // list, send levels, sampler sound (may change in-place via loadSample),
    // and the EQ response curve (parameters change per knob turn).
    const auto previousSignature = computeVisualSignature();
    currentEq_ = findEq();
    refreshPluginLists();
    if (currentDescriptor_.track != nullptr)
        sampleFileName_ = resolveSampleFilename();
    if (computeVisualSignature() != previousSignature)
        repaint();
}

std::size_t ChannelDetailsWidget::computeVisualSignature() const
{
    std::size_t signature = 0;
    const auto mix = [&signature](std::size_t value)
    {
        signature ^= value + 0x9e3779b97f4a7c15ULL
                   + (signature << 6) + (signature >> 2);
    };

    mix(static_cast<std::size_t>(sampleFileName_.hashCode64()));
    mix(static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(currentEq_)));
    mix(static_cast<std::size_t>(pluginCount_));
    if (currentEq_ != nullptr)
    {
        constexpr std::array<EqBand, 4> bands {
            EqBand::Low, EqBand::Mid1, EqBand::Mid2, EqBand::High
        };
        for (const auto band : bands)
        {
            if (const auto* parameter = freqParam(*currentEq_, band))
                mix(std::hash<float>{}(parameter->getCurrentValue()));
            if (const auto* parameter = gainParam(*currentEq_, band))
                mix(std::hash<float>{}(parameter->getCurrentValue()));
            if (const auto* parameter = qParam(*currentEq_, band))
                mix(std::hash<float>{}(parameter->getCurrentValue()));
        }
    }
    for (const auto& insert : insertsList_)
        mix(static_cast<std::size_t>(insert.hashCode64()));
    for (const auto& slot : sendSlots_)
    {
        mix(static_cast<std::size_t>(slot.label.hashCode64()));
        mix(std::hash<float>{}(slot.gainDb));
        mix(static_cast<std::size_t>(slot.active));
    }
    return signature;
}

void ChannelDetailsWidget::resolveActiveChannel()
{
    currentDescriptor_ = findActiveChannel();
    currentEq_ = findEq();
    sampleFileName_ = resolveSampleFilename();
    cachedTitle_ = "Channel > " + (currentDescriptor_.name.isNotEmpty()
                                       ? currentDescriptor_.name
                                       : juce::String("—"));

    refreshPluginLists();
}

void ChannelDetailsWidget::refreshPluginLists()
{
    insertsList_.clear();
    sendSlots_ = {};

    pluginCount_ = 0;
    auto* pluginList = resolveTargetPluginList();
    if (pluginList == nullptr)
        return;

    int nextSendSlot = 0;
    for (auto* p : pluginList->getPlugins())
    {
        if (p == nullptr)
            continue;
        ++pluginCount_;

        if (auto* aux = dynamic_cast<te::AuxSendPlugin*>(p))
        {
            if (nextSendSlot < kSendSlotCount)
            {
                auto& s = sendSlots_[static_cast<size_t>(nextSendSlot++)];
                s.active = true;
                s.gainDb = aux->getGainDb();
                const auto busName = aux->getBusName();
                s.label = busName.isNotEmpty()
                              ? busName
                              : (juce::String("FX") + juce::String(aux->getBusNumber() + 1));
            }
            continue;
        }

        if (ChannelInsertUtils::isInfrastructurePlugin(p))
            continue;

        insertsList_.push_back(p->getName());
    }
}

juce::String ChannelDetailsWidget::resolveSampleFilename() const
{
    if (currentDescriptor_.track == nullptr)
        return {};

    for (auto* p : currentDescriptor_.track->pluginList.getPlugins())
    {
        if (auto* sp = dynamic_cast<te::SamplerPlugin*>(p))
        {
            if (sp->getNumSounds() == 0)
                continue;
            const auto media = sp->getSoundMedia(0);
            if (media.isEmpty())
                continue;
            return juce::File(media).getFileName();
        }
    }
    return {};
}

void ChannelDetailsWidget::rebuildKnobs()
{
    currentKnobs_.clear();

    const bool eqAvailable = (currentEq_ != nullptr);

    // k1 — band selector
    {
        Knob k;
        k.id = "detail-band";
        k.label = "Band";
        k.isEnabled = eqAvailable;
        Knob::ListModel m;
        m.entries = { "Low", "Mid 1", "Mid 2", "High" };
        m.selectedIndex = static_cast<int>(selectedBand_);
        m.onChange = [this](int idx) {
            selectedBand_ = static_cast<EqBand>(juce::jlimit(0, 3, idx));
            rebuildKnobs();
            repaint();
        };
        k.model = m;
        currentKnobs_.push_back(std::move(k));
    }

    auto numericKnob = [&](juce::StringRef id, juce::StringRef label,
                           te::AutomatableParameter* param,
                           float lo, float hi, float step,
                           std::function<juce::String(double)> formatter)
    {
        Knob k;
        k.id = id;
        k.label = label;
        k.isEnabled = (param != nullptr);
        Knob::NumericModel m;
        if (param != nullptr)
        {
            m.value = param->getCurrentValue();
            m.minimum = lo;
            m.maximum = hi;
            m.step = step;
            m.formatter = std::move(formatter);
            m.onChange = [param](double v) {
                if (param != nullptr)
                    param->setParameter(static_cast<float>(v), juce::sendNotification);
            };
        }
        k.model = m;
        return k;
    };

    te::AutomatableParameter* freq = eqAvailable ? freqParam(*currentEq_, selectedBand_) : nullptr;
    te::AutomatableParameter* gain = eqAvailable ? gainParam(*currentEq_, selectedBand_) : nullptr;
    te::AutomatableParameter* q    = eqAvailable ? qParam   (*currentEq_, selectedBand_) : nullptr;

    currentKnobs_.push_back(numericKnob(
        "detail-freq", "Frequency", freq,
        te::EqualiserPlugin::minFreq, te::EqualiserPlugin::maxFreq, 1.0f,
        [](double v) -> juce::String {
            if (v >= 1000.0) return juce::String(v / 1000.0, 2) + " kHz";
            return juce::String((int)std::round(v)) + " Hz";
        }));

    currentKnobs_.push_back(numericKnob(
        "detail-gain", "Gain", gain,
        te::EqualiserPlugin::minGain, te::EqualiserPlugin::maxGain, 0.25f,
        [](double v) -> juce::String {
            return juce::String(v, 1) + " dB";
        }));

    currentKnobs_.push_back(numericKnob(
        "detail-q", "Q", q,
        te::EqualiserPlugin::minQ, te::EqualiserPlugin::maxQ, 0.05f,
        [](double v) -> juce::String {
            return juce::String(v, 2);
        }));
}

ChannelDescriptor ChannelDetailsWidget::findActiveChannel()
{
    ChannelDescriptor empty;
    auto* edit = engine().getEdit();
    if (edit == nullptr)
        return empty;

    auto mixerState = engine().getMixerState();
    if (!mixerState.isValid())
        return empty;

    const juce::String levelStr = mixerState.getProperty(MixerStateKeys::Level, "global").toString();
    const juce::String focusedStr = mixerState.getProperty(MixerStateKeys::FocusedGroup).toString();
    const juce::String activeId = mixerState.getProperty(MixerStateKeys::ActiveChannel).toString();

    std::unique_ptr<ChannelSource> source;
    if ((levelStr == "sound" || levelStr == "group")
        && focusedStr.isNotEmpty())
        source = std::make_unique<GroupChannelSource>(*edit, te::EditItemID::fromString(focusedStr));
    else
        source = std::make_unique<GlobalChannelSource>(*edit);

    auto channels = source->enumerate();
    for (auto& d : channels)
        if (d.id == activeId)
            return d;
    if (!channels.empty())
        return channels.front();
    return empty;
}

te::EqualiserPlugin* ChannelDetailsWidget::findEq()
{
    auto* list = resolveTargetPluginList();
    if (list == nullptr)
        return nullptr;
    return ChannelInsertUtils::findEqualiser(*list);
}
