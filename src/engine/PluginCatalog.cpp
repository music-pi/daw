#include "PluginCatalog.h"

#include <juce_core/juce_core.h>

namespace
{
    void sortPlugins(juce::Array<juce::PluginDescription>& plugins)
    {
        std::sort (plugins.begin(), plugins.end(),
                   [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
                   {
                       if (a.manufacturerName != b.manufacturerName)
                           return a.manufacturerName < b.manufacturerName;
                       return a.name < b.name;
                   });
    }

    juce::Array<juce::PluginDescription> filtered (const juce::KnownPluginList& list,
                                                    bool wantInstruments)
    {
        juce::Array<juce::PluginDescription> out;
        for (auto& d : list.getTypes())
            if (d.isInstrument == wantInstruments)
                out.add (d);

        sortPlugins(out);
        return out;
    }
}

PluginCatalog::~PluginCatalog()
{
    // relaxed — pure cancellation flag; join() + TE's abort callback provide
    // the actual synchronization.
    abort_.store(true, std::memory_order_relaxed);
    if (auto& abort = engine_.getPluginManager().abortCurrentPluginScan)
        abort();
    if (scanThread_.joinable())
        scanThread_.join();
}

PluginCatalog::PluginCatalog (te::Engine& e) : engine_(e)
{
    // Diagnostic: list the registered plugin formats so we can tell whether
    // VST3 / LV2 compiled in. Printed once per construction.
    auto& pm = engine_.getPluginManager();
    juce::String formats;
    for (int i = 0; i < pm.pluginFormatManager.getNumFormats(); ++i)
    {
        if (formats.isNotEmpty()) formats << ", ";
        formats << pm.pluginFormatManager.getFormat(i)->getName();
    }
    juce::Logger::writeToLog("[PluginCatalog] formats registered: " + formats);
}

bool PluginCatalog::isScanning() const { return scanning_.load(std::memory_order_relaxed); }

juce::Array<juce::PluginDescription> PluginCatalog::getInstruments() const
{
    auto instruments = filtered (engine_.getPluginManager().knownPluginList, true);

    // Tracktion's FourOsc ships with the engine, so it is the dependable
    // instrument baseline on a clean MusicPI installation. It enters the
    // same PluginDescription-driven browser flow as scanned VST3 and LV2
    // instruments, but is instantiated directly as a built-in plugin.
    instruments.add(
        te::PluginManager::createBuiltInPluginDescription<te::FourOscPlugin>(true));
    sortPlugins(instruments);
    return instruments;
}

juce::Array<juce::PluginDescription> PluginCatalog::getEffects() const
{
    return filtered (engine_.getPluginManager().knownPluginList, false);
}

bool PluginCatalog::hasDiscoveredPlugins(bool instruments) const
{
    for (const auto& plugin : engine_.getPluginManager().knownPluginList.getTypes())
        if (plugin.isInstrument == instruments)
            return true;
    return false;
}

std::optional<juce::PluginDescription>
PluginCatalog::findByIdentifier (const juce::String& identifier) const
{
    auto& known = engine_.getPluginManager().knownPluginList;
    if (auto p = known.getTypeForIdentifierString (identifier))
        return *p;
    return std::nullopt;
}

void PluginCatalog::scan (std::function<void (ScanProgress)> progressCb,
                          std::function<void (int, int)> doneCb)
{
    // relaxed — scanning_ is a non-reentrancy gate, not a publication barrier.
    if (scanning_.exchange (true, std::memory_order_relaxed))
        return;

    // Reap any previous scan thread so we can reuse the slot.
    if (scanThread_.joinable())
        scanThread_.join();

    abort_.store(false, std::memory_order_relaxed);
    auto& pm = engine_.getPluginManager();

    // Background scan on a joinable std::thread. TE's pre-installed
    // CustomScanner (set in PluginManager::initialise) handles out-of-process
    // + dead-man's-pedal semantics for formats that support it. The
    // destructor aborts + joins to keep shutdown from racing.
    scanThread_ = std::thread([this, &pm, progressCb, doneCb]() mutable
    {
        int added = 0, failed = 0;

        for (int i = 0; i < pm.pluginFormatManager.getNumFormats(); ++i)
        {
            auto* fmt = pm.pluginFormatManager.getFormat (i);
            const auto formatName = fmt->getName();

            if (! fmt->canScanForPlugins())
            {
                juce::Logger::writeToLog(
                    "[PluginCatalog] skipping format (cannot scan): " + formatName);
                continue;
            }

            const auto paths = fmt->getDefaultLocationsToSearch();
            juce::Logger::writeToLog(
                "[PluginCatalog] scanning " + formatName
                + " in paths: " + paths.toString());

            juce::File deadMansPedal {
                juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("maschinepi-plugin-scan.deadmanspedal") };

            juce::PluginDirectoryScanner scanner (
                pm.knownPluginList, *fmt, paths,
                /*recursive*/ true, deadMansPedal, /*allowAsync*/ false);

            juce::String failureReason;
            int scanned = 0;
            int formatAdded = 0, formatFailed = 0;
            while (scanner.scanNextFile (/*dontRescan*/ true, failureReason))
            {
                if (abort_.load(std::memory_order_relaxed)) break;
                ++scanned;
                if (progressCb)
                {
                    const float p = scanner.getProgress();
                    const int total = (p > 0.0f) ? static_cast<int> (scanned / p + 0.5f) : scanned;
                    progressCb ({ scanned, total, scanner.getNextPluginFileThatWillBeScanned() });
                }
                if (failureReason.isNotEmpty())
                    ++formatFailed;
                else
                    ++formatAdded;
            }

            juce::Logger::writeToLog(
                "[PluginCatalog] " + formatName
                + " done: +" + juce::String(formatAdded)
                + " / " + juce::String(formatFailed) + " failed"
                + " / " + juce::String(scanned) + " files");

            added  += formatAdded;
            failed += formatFailed;
        }

        juce::Logger::writeToLog(
            "[PluginCatalog] scan total: +" + juce::String(added)
            + " added, " + juce::String(failed) + " failed");

        scanning_.store (false, std::memory_order_relaxed);
        // Skip the done-callback if we were aborted during shutdown — the
        // widget that owns the callback may already be gone.
        if (! abort_.load(std::memory_order_relaxed) && doneCb)
            doneCb (added, failed);
    });
}

void PluginCatalog::cancelScan()
{
    abort_.store(true, std::memory_order_relaxed);
    if (auto& abort = engine_.getPluginManager().abortCurrentPluginScan)
        abort();
    if (scanThread_.joinable())
        scanThread_.join();
    scanning_.store (false, std::memory_order_relaxed);
}
