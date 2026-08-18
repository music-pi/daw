#include "ArpDialog.h"

#include "../../engine/Arpeggiator.h"
#include "../../engine/AudioEngine.h"
#include "../theme/UiTheme.h"

namespace
{
const char* arpModeName(Arpeggiator::Mode m)
{
    switch (m)
    {
        case Arpeggiator::Mode::Off:       return "Off";
        case Arpeggiator::Mode::Up:        return "Up";
        case Arpeggiator::Mode::Down:      return "Down";
        case Arpeggiator::Mode::UpDown:    return "UpDown";
        case Arpeggiator::Mode::Random:    return "Random";
        case Arpeggiator::Mode::AsPlayed:  return "As Played";
        case Arpeggiator::Mode::Chord:     return "Chord";
    }
    return "?";
}

struct ArpRate { double beats; const char* label; };
constexpr ArpRate kArpRates[] = {
    { 1.0 / 4.0,  "1/32"   },
    { 1.0 / 3.0,  "1/16T"  },
    { 1.0 / 2.0,  "1/16"   },
    { 2.0 / 3.0,  "1/8T"   },
    { 1.0,        "1/8"    },
    { 4.0 / 3.0,  "1/4T"   },
    { 2.0,        "1/4"    },
};
constexpr int kNumRates = (int)(sizeof(kArpRates) / sizeof(kArpRates[0]));

int arpRateIndex(double beats)
{
    int best = 4;    // default 1/8
    double bestDist = 1e9;
    for (int i = 0; i < kNumRates; ++i)
    {
        const double d = std::abs(kArpRates[i].beats - beats);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}
}

void ArpDialog::onActivated(int offset)
{
    panelOffset_ = offset;
}

std::vector<std::string> ArpDialog::requiredResources(int /*page*/)
{
    // Dialogs get the full right-panel control surface. d1..d4 claimed so
    // left-panel buttons don't bleed into the dialog while it's up.
    return { "k5", "k6", "k7", "k8", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8" };
}

std::vector<Option> ArpDialog::getOptions(int /*page*/)
{
    auto& arp = engine().getKeyboardArp();
    const auto s = arp.getSettings();

    Option onOff;
    onOff.id = "arp.onoff";
    onOff.label = arp.isActive() ? "Arp: On" : "Arp: Off";
    onOff.state = OptionState::Enabled;
    onOff.onInvoke = [this]() {
        auto& a = engine().getKeyboardArp();
        auto cur = a.getSettings();
        cur.mode = (cur.mode == Arpeggiator::Mode::Off)
            ? Arpeggiator::Mode::Up : Arpeggiator::Mode::Off;
        a.setSettings(cur);
        repaint();
    };

    Option latch;
    latch.id = "arp.latch";
    latch.label = s.latch ? "Latch: On" : "Latch: Off";
    latch.state = OptionState::Enabled;
    latch.onInvoke = [this]() {
        auto& a = engine().getKeyboardArp();
        auto cur = a.getSettings();
        cur.latch = ! cur.latch;
        if (! cur.latch) a.allNotesOff();
        a.setSettings(cur);
        repaint();
    };

    Option empty;
    empty.state = OptionState::Empty;

    Option close;
    close.id = "arp.close";
    close.label = "Close";
    close.state = OptionState::Enabled;
    close.onInvoke = [this]() { if (onDismiss_) onDismiss_(); };

    return { onOff, latch, empty, close };
}

std::vector<Knob> ArpDialog::getKnobs(int /*page*/)
{
    auto& arp = engine().getKeyboardArp();
    const auto s = arp.getSettings();
    std::vector<Knob> out;

    {
        Knob k;
        k.id = "arp.mode";
        k.label = juce::String("Mode: ") + arpModeName(s.mode);
        k.isEnabled = true;
        k.continuousMode = false;
        Knob::NumericModel m;
        m.minimum = 0;
        m.maximum = static_cast<int>(Arpeggiator::Mode::Chord);
        m.value = static_cast<int>(s.mode);
        m.step = 1;
        m.onChange = [this](double v) {
            auto& a = engine().getKeyboardArp();
            auto cur = a.getSettings();
            cur.mode = static_cast<Arpeggiator::Mode>(
                juce::jlimit(0, (int) Arpeggiator::Mode::Chord, (int) v));
            a.setSettings(cur);
            repaint();
        };
        k.model = std::move(m);
        out.push_back(std::move(k));
    }

    {
        Knob k;
        k.id = "arp.rate";
        const int idx = arpRateIndex(s.rateBeats);
        k.label = juce::String("Rate: ") + kArpRates[idx].label;
        k.isEnabled = true;
        k.continuousMode = false;
        Knob::NumericModel m;
        m.minimum = 0;
        m.maximum = kNumRates - 1;
        m.value = idx;
        m.step = 1;
        m.onChange = [this](double v) {
            auto& a = engine().getKeyboardArp();
            auto cur = a.getSettings();
            const int i = juce::jlimit(0, kNumRates - 1, (int) v);
            cur.rateBeats = kArpRates[i].beats;
            a.setSettings(cur);
            repaint();
        };
        k.model = std::move(m);
        out.push_back(std::move(k));
    }

    {
        Knob k;
        k.id = "arp.oct";
        k.label = juce::String("Oct: ") + juce::String(s.octaves);
        k.isEnabled = true;
        k.continuousMode = false;
        Knob::NumericModel m;
        m.minimum = 1; m.maximum = 4; m.value = s.octaves; m.step = 1;
        m.onChange = [this](double v) {
            auto& a = engine().getKeyboardArp();
            auto cur = a.getSettings();
            cur.octaves = juce::jlimit(1, 4, (int) v);
            a.setSettings(cur);
            repaint();
        };
        k.model = std::move(m);
        out.push_back(std::move(k));
    }

    {
        Knob k;
        k.id = "arp.gate";
        k.label = juce::String("Gate: ") + juce::String((int) (s.gate * 100)) + "%";
        k.isEnabled = true;
        k.continuousMode = true;
        Knob::NumericModel m;
        m.minimum = 0.05; m.maximum = 1.0;
        m.value = s.gate; m.step = 0.05;
        m.onChange = [this](double v) {
            auto& a = engine().getKeyboardArp();
            auto cur = a.getSettings();
            cur.gate = juce::jlimit(0.05f, 1.0f, static_cast<float>(v));
            a.setSettings(cur);
            repaint();
        };
        k.model = std::move(m);
        out.push_back(std::move(k));
    }

    return out;
}

void ArpDialog::paint(juce::Graphics& g)
{
    paintPage(g, 0, getLocalBounds());
}

void ArpDialog::paintPage(juce::Graphics& g, int /*page*/, juce::Rectangle<int> bounds)
{
    g.fillAll(UiTheme::kBackgroundDark);

    auto& arp = engine().getKeyboardArp();
    const auto s = arp.getSettings();

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));

    auto title = bounds.removeFromTop(32).reduced(12, 6);
    g.drawFittedText(juce::String("Arpeggiator: ")
                         + (arp.isActive() ? "Active" : "Off")
                         + (s.latch ? "   latch" : ""),
                     title, juce::Justification::centredLeft, 1);

    const int idx = arpRateIndex(s.rateBeats);
    juce::String summary = juce::String(arpModeName(s.mode))
                         + "   Rate " + kArpRates[idx].label
                         + "   Oct "  + juce::String(s.octaves)
                         + "   Gate " + juce::String((int)(s.gate * 100)) + "%";

    g.setColour(juce::Colours::white.withAlpha(0.7f));
    auto detail = bounds.removeFromTop(22).reduced(12, 2);
    g.drawFittedText(summary, detail, juce::Justification::centredLeft, 1);

    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawFittedText(juce::String("Held: ") + juce::String(arp.getHeldCount()),
                     bounds, juce::Justification::centred, 1);
}
