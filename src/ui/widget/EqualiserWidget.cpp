#include "EqualiserWidget.h"

#include "../../engine/AudioEngine.h"
#include "../theme/UiTheme.h"
#include "WindowManager.h"

#include <cmath>

namespace
{
const juce::Identifier EnabledBandsProperty { "maschinePiEqEnabledBands" };
const std::array<juce::Identifier, 4> StoredGainProperties {
    juce::Identifier("maschinePiEqStoredGainLow"),
    juce::Identifier("maschinePiEqStoredGainMid1"),
    juce::Identifier("maschinePiEqStoredGainMid2"),
    juce::Identifier("maschinePiEqStoredGainHigh")
};

const std::array<juce::String, 4> BandNames { "Low", "Mid 1", "Mid 2", "High" };
}

EqualiserWidget::EqualiserWidget(te::EqualiserPlugin& equaliser, juce::String channelName)
    : pluginLifetime_(&equaliser), equaliser_(&equaliser), pluginId_(equaliser.itemID),
      channelName_(std::move(channelName))
{
}

EqualiserWidget::~EqualiserWidget()
{
    if (listening_ && equaliser_ != nullptr)
        equaliser_->state.removeListener(this);
}

WidgetDescriptor EqualiserWidget::describe() const
{
    return { "equaliser", 1, false, DisplayConstraint::RightOnly };
}

juce::String EqualiserWidget::getTitle() const
{
    return "EQ > " + (channelName_.isNotEmpty() ? channelName_ : juce::String("Channel"));
}

void EqualiserWidget::onActivated(int panelOffset)
{
    panelOffset_ = panelOffset;
    if (equaliser_ != nullptr && !listening_)
    {
        equaliser_->state.addListener(this);
        listening_ = true;
    }
}

void EqualiserWidget::onDeactivated()
{
    if (equaliser_ != nullptr && listening_)
    {
        equaliser_->state.removeListener(this);
        listening_ = false;
    }
}

void EqualiserWidget::onEditAboutToBeReplaced()
{
    resumeAfterEditReplacement_ = listening_;
    if (listening_ && equaliser_ != nullptr)
        equaliser_->state.removeListener(this);
    listening_ = false;
    equaliser_ = nullptr;
    pluginLifetime_ = nullptr;
}

void EqualiserWidget::onEditReplaced()
{
    if (auto* edit = engine().getEdit(); edit != nullptr && pluginId_.isValid())
    {
        pluginLifetime_ = te::findPluginForID(*edit, pluginId_);
        equaliser_ = dynamic_cast<te::EqualiserPlugin*>(pluginLifetime_.get());
    }

    if (resumeAfterEditReplacement_ && equaliser_ != nullptr)
    {
        equaliser_->state.addListener(this);
        listening_ = true;
    }
    resumeAfterEditReplacement_ = false;
    repaint();
}

std::vector<std::string> EqualiserWidget::requiredResources(int page)
{
    if (page != 0)
        return {};

    std::vector<std::string> resources;
    const int first = panelOffset_ == 0 ? 1 : 5;
    for (int i = 0; i < 4; ++i)
    {
        resources.push_back("d" + std::to_string(first + i));
        resources.push_back("k" + std::to_string(first + i));
    }
    return resources;
}

std::vector<Option> EqualiserWidget::getOptions(int page)
{
    if (page != 0)
        return {};

    std::vector<Option> options;
    options.reserve(4);
    for (int i = 0; i < 4; ++i)
    {
        const auto band = static_cast<Band>(i);
        const bool enabled = isBandEnabled(band);

        Option option;
        option.id = "eq.band." + juce::String(i);
        option.label = enabled ? BandNames[static_cast<size_t>(i)]
                               : "+ " + BandNames[static_cast<size_t>(i)];
        option.state = band == selectedBand_ ? OptionState::Active : OptionState::Enabled;
        option.onInvoke = [this, i]() { invokeBandOption(i); };
        options.push_back(std::move(option));
    }
    return options;
}

std::vector<Knob> EqualiserWidget::getKnobs(int page)
{
    if (page != 0 || equaliser_ == nullptr)
        return {};

    const bool enabled = isBandEnabled(selectedBand_);
    auto* frequency = frequencyParameter(selectedBand_);
    auto* gain = gainParameter(selectedBand_);
    auto* q = qParameter(selectedBand_);

    auto makeNumeric = [this, enabled](juce::String id,
                                      juce::String label,
                                      te::AutomatableParameter* parameter,
                                      double minimum,
                                      double maximum,
                                      double step,
                                      std::function<juce::String(double)> formatter)
    {
        Knob knob;
        knob.id = std::move(id);
        knob.label = std::move(label);
        knob.isEnabled = enabled && parameter != nullptr;
        knob.continuousMode = true;

        Knob::NumericModel model;
        model.value = parameter != nullptr ? parameter->getCurrentValue() : minimum;
        model.minimum = minimum;
        model.maximum = maximum;
        model.step = step;
        model.formatter = std::move(formatter);
        model.onChange = [this, parameter](double value) {
            setParameter(parameter, static_cast<float>(value));
        };
        knob.model = std::move(model);
        return knob;
    };

    std::vector<Knob> knobs;
    knobs.reserve(4);
    knobs.push_back(makeNumeric(
        "eq.frequency", "Frequency", frequency,
        te::EqualiserPlugin::minFreq, te::EqualiserPlugin::maxFreq, 1.0,
        [](double value) {
            return value >= 1000.0 ? juce::String(value / 1000.0, 2) + " kHz"
                                   : juce::String(static_cast<int>(std::round(value))) + " Hz";
        }));
    knobs.push_back(makeNumeric(
        "eq.gain", "Gain", gain,
        te::EqualiserPlugin::minGain, te::EqualiserPlugin::maxGain, 0.25,
        [](double value) { return juce::String(value, 1) + " dB"; }));
    knobs.push_back(makeNumeric(
        "eq.q", "Q", q,
        te::EqualiserPlugin::minQ, te::EqualiserPlugin::maxQ, 0.05,
        [](double value) { return juce::String(value, 2); }));

    Knob active;
    active.id = "eq.enabled";
    active.label = "Point";
    active.isEnabled = true;
    active.continuousMode = false;
    Knob::ListModel activeModel;
    activeModel.entries = { "Off", "On" };
    activeModel.selectedIndex = enabled ? 1 : 0;
    activeModel.onChange = [this](int index) {
        setBandEnabled(selectedBand_, index != 0);
    };
    active.model = std::move(activeModel);
    knobs.push_back(std::move(active));
    return knobs;
}

void EqualiserWidget::paint(juce::Graphics& g)
{
    paintPage(g, 0, getLocalBounds());
}

void EqualiserWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    g.fillAll(UiTheme::kBackgroundDark);
    if (page != 0 || equaliser_ == nullptr || bounds.isEmpty())
        return;

    auto graph = bounds.reduced(14, 10).toFloat();
    g.setColour(UiTheme::kMeterBackground);
    g.fillRoundedRectangle(graph, 3.0f);
    g.setColour(UiTheme::kStripBorder);
    g.drawRoundedRectangle(graph, 3.0f, 1.0f);
    graph = graph.reduced(10.0f, 8.0f);

    const float minFrequency = te::EqualiserPlugin::minFreq;
    const float maxFrequency = te::EqualiserPlugin::maxFreq;
    const float logMinimum = std::log10(minFrequency);
    const float logMaximum = std::log10(maxFrequency);

    auto frequencyToX = [&](float frequency) {
        const float unit = (std::log10(juce::jlimit(minFrequency, maxFrequency, frequency))
                            - logMinimum) / (logMaximum - logMinimum);
        return graph.getX() + unit * graph.getWidth();
    };
    auto gainToY = [&](float gainDb) {
        const float unit = juce::jlimit(-1.0f, 1.0f,
                                        gainDb / te::EqualiserPlugin::maxGain);
        return graph.getCentreY() - unit * graph.getHeight() * 0.5f;
    };

    const std::array<float, 4> gainLines { -20.0f, -10.0f, 0.0f, 10.0f };
    for (float gainDb : gainLines)
    {
        const float y = gainToY(gainDb);
        g.setColour(juce::Colours::white.withAlpha(gainDb == 0.0f ? 0.22f : 0.08f));
        g.drawHorizontalLine(static_cast<int>(std::round(y)), graph.getX(), graph.getRight());
    }

    const std::array<float, 10> frequencyLines {
        20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
        1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
    };
    for (float frequency : frequencyLines)
    {
        const float x = frequencyToX(frequency);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawVerticalLine(static_cast<int>(std::round(x)), graph.getY(), graph.getBottom());
    }

    juce::Path response;
    const int pointCount = juce::jmax(2, static_cast<int>(graph.getWidth()));
    for (int i = 0; i < pointCount; ++i)
    {
        const float unit = static_cast<float>(i) / static_cast<float>(pointCount - 1);
        const float frequency = std::pow(10.0f, logMinimum + unit * (logMaximum - logMinimum));
        const float x = graph.getX() + unit * graph.getWidth();
        const float y = gainToY(equaliser_->getDBGainAtFrequency(frequency));
        if (i == 0)
            response.startNewSubPath(x, y);
        else
            response.lineTo(x, y);
    }
    g.setColour(UiTheme::kAccentGreen);
    g.strokePath(response, juce::PathStrokeType(2.0f));

    for (int i = 0; i < 4; ++i)
    {
        const auto band = static_cast<Band>(i);
        auto* frequency = frequencyParameter(band);
        auto* gain = gainParameter(band);
        if (frequency == nullptr || gain == nullptr)
            continue;

        const bool enabled = isBandEnabled(band);
        const bool selected = band == selectedBand_;
        const float x = frequencyToX(frequency->getCurrentValue());
        const float y = gainToY(gain->getCurrentValue());
        const float radius = selected ? 6.0f : 4.5f;

        if (!enabled)
        {
            g.setColour(juce::Colours::white.withAlpha(selected ? 0.5f : 0.2f));
            g.drawEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f, 1.2f);
            continue;
        }

        g.setColour(selected ? UiTheme::kMeterYellow : juce::Colours::white);
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        if (selected)
        {
            g.setColour(UiTheme::kMeterYellow.withAlpha(0.3f));
            g.drawEllipse(x - radius - 3.0f, y - radius - 3.0f,
                          (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f, 1.2f);
        }
    }
}

void EqualiserWidget::onUiHostTick()
{
    if (equaliser_ == nullptr || equaliser_->getOwnerList() != nullptr || closePosted_)
        return;

    // Undoing the insertion while this editor is open detaches the plugin.
    // Keep it alive long enough to close the view safely on the next message
    // turn instead of leaving controls bound to a non-processing ghost node.
    closePosted_ = true;
    juce::Component::SafePointer<EqualiserWidget> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() mutable {
        if (safeThis != nullptr)
            if (auto* manager = safeThis->windowManager())
                manager->close("equaliser");
    });
}

void EqualiserWidget::handleKnob(int localIndex, int16_t delta, uint16_t, bool shift)
{
    if (equaliser_ == nullptr || delta == 0)
        return;

    const float fine = shift ? 0.2f : 1.0f;
    if (localIndex == 3)
    {
        setBandEnabled(selectedBand_, delta > 0);
        return;
    }
    if (!isBandEnabled(selectedBand_))
        return;

    if (localIndex == 0)
    {
        auto* parameter = frequencyParameter(selectedBand_);
        if (parameter != nullptr)
        {
            const float factor = std::pow(2.0f, static_cast<float>(delta) * fine / 12.0f);
            setParameter(parameter, juce::jlimit(te::EqualiserPlugin::minFreq,
                                                 te::EqualiserPlugin::maxFreq,
                                                 parameter->getCurrentValue() * factor));
        }
    }
    else if (localIndex == 1)
    {
        auto* parameter = gainParameter(selectedBand_);
        if (parameter != nullptr)
            setParameter(parameter, juce::jlimit(te::EqualiserPlugin::minGain,
                                                 te::EqualiserPlugin::maxGain,
                                                 parameter->getCurrentValue()
                                                     + static_cast<float>(delta) * 0.25f * fine));
    }
    else if (localIndex == 2)
    {
        auto* parameter = qParameter(selectedBand_);
        if (parameter != nullptr)
            setParameter(parameter, juce::jlimit(te::EqualiserPlugin::minQ,
                                                 te::EqualiserPlugin::maxQ,
                                                 parameter->getCurrentValue()
                                                     + static_cast<float>(delta) * 0.05f * fine));
    }
}

void EqualiserWidget::handleOption(int localIndex)
{
    invokeBandOption(localIndex);
}

te::AutomatableParameter* EqualiserWidget::frequencyParameter(Band band) const
{
    if (equaliser_ == nullptr)
        return nullptr;
    switch (band)
    {
        case Band::Low:  return equaliser_->loFreq.get();
        case Band::Mid1: return equaliser_->midFreq1.get();
        case Band::Mid2: return equaliser_->midFreq2.get();
        case Band::High: return equaliser_->hiFreq.get();
    }
    return nullptr;
}

te::AutomatableParameter* EqualiserWidget::gainParameter(Band band) const
{
    if (equaliser_ == nullptr)
        return nullptr;
    switch (band)
    {
        case Band::Low:  return equaliser_->loGain.get();
        case Band::Mid1: return equaliser_->midGain1.get();
        case Band::Mid2: return equaliser_->midGain2.get();
        case Band::High: return equaliser_->hiGain.get();
    }
    return nullptr;
}

te::AutomatableParameter* EqualiserWidget::qParameter(Band band) const
{
    if (equaliser_ == nullptr)
        return nullptr;
    switch (band)
    {
        case Band::Low:  return equaliser_->loQ.get();
        case Band::Mid1: return equaliser_->midQ1.get();
        case Band::Mid2: return equaliser_->midQ2.get();
        case Band::High: return equaliser_->hiQ.get();
    }
    return nullptr;
}

int EqualiserWidget::enabledBandsMask() const
{
    if (equaliser_ == nullptr || !equaliser_->state.hasProperty(EnabledBandsProperty))
        return kAllBandsMask;
    return static_cast<int>(equaliser_->state.getProperty(EnabledBandsProperty, kAllBandsMask));
}

bool EqualiserWidget::isBandEnabled(Band band) const
{
    return (enabledBandsMask() & (1 << static_cast<int>(band))) != 0;
}

void EqualiserWidget::setBandEnabled(Band band, bool enabled)
{
    if (equaliser_ == nullptr || enabled == isBandEnabled(band))
        return;

    const int index = static_cast<int>(band);
    auto* gain = gainParameter(band);
    auto& undo = engine().getUndoManager();
    undo.beginNewTransaction(enabled ? "Add EQ point" : "Remove EQ point");

    int mask = enabledBandsMask();
    if (enabled)
    {
        mask |= 1 << index;
        equaliser_->state.setProperty(EnabledBandsProperty, mask, &undo);
        const float storedGain = static_cast<float>(
            equaliser_->state.getProperty(StoredGainProperties[static_cast<size_t>(index)], 0.0f));
        setParameter(gain, storedGain);
    }
    else
    {
        if (gain != nullptr)
            equaliser_->state.setProperty(StoredGainProperties[static_cast<size_t>(index)],
                                          gain->getCurrentValue(), &undo);
        mask &= ~(1 << index);
        equaliser_->state.setProperty(EnabledBandsProperty, mask, &undo);
        setParameter(gain, 0.0f);
    }

    repaint();
}

void EqualiserWidget::invokeBandOption(int bandIndex)
{
    if (!juce::isPositiveAndBelow(bandIndex, 4))
        return;

    const auto band = static_cast<Band>(bandIndex);
    if (band == selectedBand_)
        setBandEnabled(band, !isBandEnabled(band));
    else
    {
        selectedBand_ = band;
        if (!isBandEnabled(band))
            setBandEnabled(band, true);
        repaint();
    }
}

void EqualiserWidget::setParameter(te::AutomatableParameter* parameter, float value)
{
    if (parameter == nullptr)
        return;
    parameter->setParameter(value, juce::sendNotification);
    repaint();
}

void EqualiserWidget::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&)
{
    repaint();
}
