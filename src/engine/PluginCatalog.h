#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <thread>

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class PluginCatalog
{
public:
    struct ScanProgress { int scanned, total; juce::String current; };

    explicit PluginCatalog (te::Engine& engine);
    ~PluginCatalog();

    bool isScanning() const;

    void scan (std::function<void (ScanProgress)> progressCb,
               std::function<void (int added, int failed)> doneCb);
    void cancelScan();

    juce::Array<juce::PluginDescription> getInstruments() const;
    juce::Array<juce::PluginDescription> getEffects() const;

    /** True when the scanner-backed known-plugin list contains at least one
        plugin of the requested kind. Built-in instruments are intentionally
        excluded so their presence does not suppress first-run discovery. */
    bool hasDiscoveredPlugins(bool instruments) const;

    std::optional<juce::PluginDescription>
        findByIdentifier (const juce::String& identifier) const;

private:
    te::Engine& engine_;
    std::atomic<bool> scanning_ { false };
    std::atomic<bool> abort_ { false };
    std::thread scanThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCatalog)
};
