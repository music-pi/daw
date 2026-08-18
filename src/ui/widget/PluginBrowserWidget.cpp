#include "PluginBrowserWidget.h"

#include "../theme/UiTheme.h"

PluginBrowserWidget::PluginBrowserWidget(PluginCatalog& catalog, Filter filter)
    : catalog_(catalog), filter_(filter)
{
    addAndMakeVisible(list_);

    list_.setOnSelectionChanged([this](ListComponent::Item&) {
        repaint();  // updates the d1 Load enabled state via Widget::getOptions re-query
    });
    list_.setOnItemActivated([this](ListComponent::Item&) {
        confirmSelection();
    });
    list_.setEmptyMessage(juce::String());  // we paint our own empty-state prompt

    refreshItems();
}

void PluginBrowserWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    refreshItems();

    // Auto-scan the first time no scanner-backed plugin of this kind is
    // known. The built-in FourOsc keeps Instruments immediately usable, but
    // must not suppress discovery of installed VST3/LV2 instruments.
    const bool wantInstruments = filter_ == Filter::Instruments;
    if (! catalog_.hasDiscoveredPlugins(wantInstruments)
        && ! catalog_.isScanning())
        beginScan();
}

void PluginBrowserWidget::onDeactivated()
{
    listener_ = nullptr;
    scanToast_.reset();
}

void PluginBrowserWidget::resized()
{
    list_.setBounds(getLocalBounds());
}

std::vector<std::string> PluginBrowserWidget::requiredResources(int page)
{
    if (page != 0) return {};
    std::vector<std::string> r;
    for (int i = 1; i <= 4; ++i)
    {
        r.push_back("d" + std::to_string(i));
        r.push_back("k" + std::to_string(i));
    }
    // navUp/navDown/navPush are button events, not claimable LED resources.
    // handleButton receives them regardless of claim state.
    return r;
}

std::vector<Option> PluginBrowserWidget::getOptions(int page)
{
    if (page != 0) return {};
    Option confirm;
    confirm.id = "plugin.confirm";
    confirm.label = "Load";
    confirm.state = (list_.getSelectedItem() != nullptr)
        ? OptionState::Enabled : OptionState::Disabled;
    confirm.onInvoke = [this]() { confirmSelection(); };

    Option scan;
    scan.id = "plugin.scan";
    scan.label = scanning_ ? "Scanning..." : "Scan";
    scan.state = scanning_ ? OptionState::Disabled : OptionState::Enabled;
    scan.onInvoke = [this]() { beginScan(); };

    Option back;
    back.id = "plugin.back";
    back.label = "Back";
    back.state = OptionState::Enabled;
    back.onInvoke = [this]() { cancel(); };

    return { confirm, scan, {}, back };
}

void PluginBrowserWidget::beginScan()
{
    if (scanning_) return;

    scanning_ = true;
    scanProgress_ = 0;
    scanTotal_ = 0;

    // Loading toast — auto-dismisses when scanToast_ is reset (handle RAII).
    scanToast_ = showLoadingToast("Scanning plugins...");
    repaint();

    juce::Component::SafePointer<PluginBrowserWidget> safe(this);
    catalog_.scan(
        [safe](PluginCatalog::ScanProgress p) {
            juce::MessageManager::callAsync([safe, p]() {
                if (auto* w = safe.getComponent())
                {
                    w->scanProgress_ = p.scanned;
                    w->scanTotal_ = p.total;
                    w->repaint();
                }
            });
        },
        [safe](int added, int failed) {
            juce::MessageManager::callAsync([safe, added, failed]() {
                if (auto* w = safe.getComponent())
                {
                    w->scanning_ = false;
                    w->scanToast_.reset();
                    w->refreshItems();
                    w->showToast(ToastKind::Success,
                                 "Plugins: " + juce::String(added) + " added"
                                 + (failed > 0 ? ", " + juce::String(failed) + " failed"
                                                : juce::String()));
                }
            });
        });
}

void PluginBrowserWidget::refreshItems()
{
    auto arr = (filter_ == Filter::Instruments)
                   ? catalog_.getInstruments()
                   : catalog_.getEffects();
    items_.assign(arr.begin(), arr.end());
    rebuildList();
    repaint();
}

void PluginBrowserWidget::rebuildList()
{
    std::vector<ListComponent::Item> rows;
    rows.reserve(items_.size());
    for (size_t i = 0; i < items_.size(); ++i)
    {
        ListComponent::Item row;
        row.id = juce::String((int) i);  // index into items_
        juce::String label = items_[i].manufacturerName;
        if (label.isNotEmpty()) label << " — ";
        label << items_[i].name;
        if (items_[i].pluginFormatName.isNotEmpty())
            label << "  [" << items_[i].pluginFormatName << "]";
        row.label = label;
        rows.push_back(std::move(row));
    }
    list_.setItems(std::move(rows));
    list_.refreshContent();
    // Selection intentionally left unset — first navUp/navDown engages row 0.
}

int PluginBrowserWidget::getSelectedIndex() const
{
    const auto id = list_.getSelectedId();
    if (id.isEmpty()) return -1;
    return id.getIntValue();
}

void PluginBrowserWidget::moveSelection(int delta)
{
    list_.moveSelection(delta);
    repaint();
}

void PluginBrowserWidget::confirmSelection()
{
    if (listener_ == nullptr) return;
    const int idx = getSelectedIndex();
    if (idx < 0 || idx >= (int) items_.size()) return;
    listener_->pluginSelected(items_[(size_t) idx]);
}

void PluginBrowserWidget::cancel()
{
    if (listener_ != nullptr)
        listener_->browserCancelled();
}

void PluginBrowserWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (! e.pressed) return;
    if (e.name == "navUp")        list_.moveSelection(-1);
    else if (e.name == "navDown") list_.moveSelection(+1);
    else if (e.name == "navPush") list_.invokeSelection();
}

void PluginBrowserWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page != 0) return;
    g.fillAll(UiTheme::kBackgroundDark);

    if (items_.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        juce::String msg;
        if (scanning_)
        {
            msg = scanTotal_ > 0
                ? "Scanning " + juce::String(scanProgress_) + " / " + juce::String(scanTotal_)
                : juce::String("Scanning...");
        }
        else
        {
            msg = "No plugins found.\n\n"
                  "Press Scan (d2) to rescan.\n\n"
                  "Install VST3 to ~/.vst3 or /usr/lib/vst3\n"
                  "Install LV2 to ~/.lv2 or /usr/lib/lv2";
        }
        g.drawFittedText(msg, bounds, juce::Justification::centred, 6);
    }
    // Non-empty state is painted by list_ (child component).
}
