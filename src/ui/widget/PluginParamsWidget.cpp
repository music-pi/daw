#include "PluginParamsWidget.h"

#include "../../engine/AudioEngine.h"
#include "../theme/UiTheme.h"

void PluginParamsWidget::setPlugin(te::Plugin* plugin)
{
    plugin_ = plugin;
    pluginId_ = plugin != nullptr ? plugin->itemID : te::EditItemID();
    page_ = 0;
    repaint();
}

void PluginParamsWidget::onEditReplaced()
{
    te::Plugin* replacement = nullptr;
    if (auto* edit = engine().getEdit(); edit != nullptr && pluginId_.isValid())
        replacement = te::findPluginForID(*edit, pluginId_).get();
    setPlugin(replacement);
}

int PluginParamsWidget::getNumPages() const
{
    if (plugin_ == nullptr) return 0;
    const int total = plugin_->getAutomatableParameters().size();
    if (total <= 0) return 0;
    return (total + kParamsPerPage - 1) / kParamsPerPage;
}

void PluginParamsWidget::setPage(int page)
{
    const int n = getNumPages();
    page_ = (n <= 0) ? 0 : juce::jlimit(0, n - 1, page);
    repaint();
}

int PluginParamsWidget::getVisibleParamCount() const
{
    if (plugin_ == nullptr) return 0;
    const auto all = plugin_->getAutomatableParameters();
    const int offset = page_ * kParamsPerPage;
    return juce::jlimit(0, kParamsPerPage, all.size() - offset);
}

juce::ReferenceCountedArray<te::AutomatableParameter>
PluginParamsWidget::getCurrentPageParams_() const
{
    juce::ReferenceCountedArray<te::AutomatableParameter> out;
    if (plugin_ == nullptr) return out;
    const auto all = plugin_->getAutomatableParameters();
    const int offset = page_ * kParamsPerPage;
    for (int i = 0; i < kParamsPerPage; ++i)
    {
        const int idx = offset + i;
        if (idx >= 0 && idx < all.size())
            out.add(all[idx]);
    }
    return out;
}

void PluginParamsWidget::onEncoderDelta(int localIndex, float normalisedDelta)
{
    if (localIndex < 0 || localIndex >= kParamsPerPage) return;
    auto params = getCurrentPageParams_();
    if (localIndex >= params.size()) return;

    auto* p = params[localIndex].get();
    if (p == nullptr) return;

    // Map the delta onto the parameter's own range.
    const auto range = p->getValueRange();
    const float span = range.getLength() > 0 ? range.getLength() : 1.0f;
    const float next = juce::jlimit(range.getStart(), range.getEnd(),
        p->getCurrentValue() + normalisedDelta * span * 0.5f);
    p->setParameter(next, juce::sendNotification);
}

std::vector<std::string> PluginParamsWidget::requiredResources(int page)
{
    if (page != 0) return {};
    std::vector<std::string> r;
    const int base = (panelOffset_ == 0) ? 1 : 5;
    for (int i = 0; i < kParamsPerPage; ++i)
        r.push_back("k" + std::to_string(base + i));
    return r;
}

std::vector<Knob> PluginParamsWidget::getKnobs(int page)
{
    if (page != 0) return {};
    auto params = getCurrentPageParams_();
    std::vector<Knob> out;
    out.reserve(static_cast<size_t>(params.size()));
    for (int i = 0; i < params.size(); ++i)
    {
        auto* param = params[i].get();
        Knob k;
        k.id = "param" + juce::String(page_ * kParamsPerPage + i);
        k.label = param != nullptr ? param->getParameterName() : juce::String();
        k.isEnabled = (param != nullptr);
        k.continuousMode = true;

        Knob::NumericModel m;
        if (param != nullptr)
        {
            const auto range = param->getValueRange();
            m.minimum = range.getStart();
            m.maximum = range.getEnd();
            m.value   = param->getCurrentValue();
            m.step    = (range.getLength() > 0.0f)
                        ? static_cast<double>(range.getLength()) / 128.0
                        : 0.01;
            // Capture a SafePointer + the param's absolute index, not the raw
            // AutomatableParameter pointer. A plugin swap (setPlugin) may
            // destroy the param between closure creation and invocation;
            // re-resolve through the live plugin each time to stay safe.
            juce::Component::SafePointer<PluginParamsWidget> safe(this);
            const int absIndex = page_ * kParamsPerPage + i;
            m.onChange = [safe, absIndex](double v) {
                auto* self = safe.getComponent();
                if (self == nullptr) return;
                auto* plugin = self->plugin_;
                if (plugin == nullptr) return;
                const auto all = plugin->getAutomatableParameters();
                if (absIndex < 0 || absIndex >= all.size()) return;
                if (auto* p = all[absIndex])
                    p->setParameter(static_cast<float>(v), juce::sendNotification);
            };
        }
        k.model = std::move(m);
        out.push_back(std::move(k));
    }
    return out;
}

juce::String PluginParamsWidget::titleForPlugin_() const
{
    if (plugin_ == nullptr) return "Params";
    return plugin_->getName();
}

void PluginParamsWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page != 0) return;
    g.fillAll(UiTheme::kBackgroundDark);
    g.setColour(juce::Colours::white);

    if (plugin_ == nullptr)
    {
        g.drawFittedText("No plugin", bounds, juce::Justification::centred, 1);
        return;
    }

    auto params = getCurrentPageParams_();
    if (params.isEmpty())
    {
        g.drawFittedText("No params", bounds, juce::Justification::centred, 1);
        return;
    }

    // Four columns, one per parameter.
    const int colW = bounds.getWidth() / kParamsPerPage;
    for (int i = 0; i < params.size(); ++i)
    {
        auto* param = params[i].get();
        if (param == nullptr) continue;

        auto cell = juce::Rectangle<int>(bounds.getX() + i * colW,
                                         bounds.getY(),
                                         colW, bounds.getHeight())
                    .reduced(4);
        auto top = cell.removeFromTop(cell.getHeight() * 2 / 3);
        g.setColour(UiTheme::kTitlebarAccent);
        g.drawRect(top, 1);

        const auto range = param->getValueRange();
        const float span = range.getLength() > 0 ? range.getLength() : 1.0f;
        const float frac = juce::jlimit(0.0f, 1.0f,
            (param->getCurrentValue() - range.getStart()) / span);

        auto fill = top.reduced(2);
        auto barHeight = juce::roundToInt(fill.getHeight() * frac);
        auto bar = fill.withY(fill.getBottom() - barHeight).withHeight(barHeight);
        g.setColour(UiTheme::kTitlebarAccent);
        g.fillRect(bar);

        g.setColour(juce::Colours::white);
        g.drawFittedText(param->getParameterName(), cell,
                         juce::Justification::centredTop, 1);
    }

    // Page indicator bottom-right
    if (getNumPages() > 1)
    {
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        juce::String txt = juce::String(page_ + 1) + "/" + juce::String(getNumPages());
        g.drawFittedText(txt, bounds.removeFromBottom(14), juce::Justification::centredRight, 1);
    }
}
