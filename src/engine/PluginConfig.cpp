#include "PluginConfig.h"

namespace
{
juce::String getString(const juce::var& obj, const juce::Identifier& key)
{
    if (auto* dyn = obj.getDynamicObject())
        return dyn->getProperty(key).toString();
    return {};
}

PluginConfig::PresetSource parseSource(const juce::String& s)
{
    const auto lower = s.toLowerCase();
    if (lower == "file_scan" || lower == "filescan") return PluginConfig::PresetSource::FileScan;
    if (lower == "none")                             return PluginConfig::PresetSource::None;
    return PluginConfig::PresetSource::VstPrograms;
}

juce::String expandPath(juce::String path)
{
    if (path.startsWithChar('~'))
    {
        const auto home = juce::File::getSpecialLocation(
            juce::File::userHomeDirectory).getFullPathName();
        path = home + path.substring(1);
    }
    return path;
}
}

bool PluginConfig::matches(const juce::PluginDescription& desc) const
{
    if (matcher.format.isNotEmpty()
        && ! desc.pluginFormatName.equalsIgnoreCase(matcher.format))
        return false;

    if (matcher.manufacturer.isNotEmpty()
        && ! desc.manufacturerName.containsIgnoreCase(matcher.manufacturer))
        return false;

    if (matcher.nameGlob.isNotEmpty())
    {
        const auto name = desc.name.toLowerCase();
        const auto glob = matcher.nameGlob.toLowerCase();
        if (! name.matchesWildcard(glob, true))
            return false;
    }
    return true;
}

std::optional<PluginConfig> PluginConfig::fromJson(const juce::var& doc)
{
    auto* root = doc.getDynamicObject();
    if (root == nullptr) return std::nullopt;

    PluginConfig cfg;

    if (auto matchVar = root->getProperty("match"); matchVar.isObject())
    {
        cfg.matcher.format       = getString(matchVar, "format");
        cfg.matcher.nameGlob     = getString(matchVar, "name");
        cfg.matcher.manufacturer = getString(matchVar, "manufacturer");
    }

    if (auto presetVar = root->getProperty("presets"); presetVar.isObject())
    {
        cfg.presets.source   = parseSource(getString(presetVar, "source"));
        cfg.presets.glob     = expandPath(getString(presetVar, "glob"));
        cfg.presets.fileType = getString(presetVar, "file_type");
    }

    if (auto pinnedVar = root->getProperty("pinned"); pinnedVar.isArray())
    {
        auto* arr = pinnedVar.getArray();
        cfg.pinned.reserve(static_cast<size_t>(arr->size()));
        for (auto& entry : *arr)
        {
            if (auto* obj = entry.getDynamicObject())
            {
                PinnedParam p;
                p.id        = obj->getProperty("id").toString();
                p.nameMatch = obj->getProperty("name_match").toString();
                p.label     = obj->getProperty("label").toString();
                if (p.id.isNotEmpty() || p.nameMatch.isNotEmpty())
                    cfg.pinned.push_back(std::move(p));
            }
        }
    }

    return cfg;
}

void PluginConfigRegistry::loadDefaults()
{
    configs_.clear();

    // Project-shipped defaults: in dev, either the CWD is the repo root
    // (./mpi run) or the binary is at build/bin/ (two levels below). Try
    // both so the configs load regardless of how the app was launched.
    std::vector<juce::File> candidateProjectDirs;
    candidateProjectDirs.push_back(juce::File::getCurrentWorkingDirectory()
                                       .getChildFile("config/plugins"));

    auto exeParent = juce::File::getSpecialLocation(
                          juce::File::currentExecutableFile).getParentDirectory();
    for (int up = 0; up < 4 && exeParent.exists(); ++up)
    {
        candidateProjectDirs.push_back(exeParent.getChildFile("config/plugins"));
        exeParent = exeParent.getParentDirectory();
    }

    // User dir: ~/maschinepi/plugins, alongside projects/samples.
    const auto userDir = juce::File::getSpecialLocation(
                             juce::File::userHomeDirectory)
                                .getChildFile("maschinepi/plugins");

    // Load shipped defaults first; user dir loaded after so its entries
    // precede shipped ones when find() scans from the end.
    for (const auto& d : candidateProjectDirs)
        if (d.isDirectory()) { loadDir(d); break; }   // first one wins
    loadDir(userDir);
}

void PluginConfigRegistry::loadDir(const juce::File& dir)
{
    if (! dir.isDirectory()) return;
    for (const auto& entry : juce::RangedDirectoryIterator(
             dir, false, "*.json", juce::File::findFiles))
    {
        if (auto cfg = loadFile(entry.getFile()))
            configs_.push_back(std::move(*cfg));
    }
}

bool PluginConfig::splitGlob(const juce::String& glob, GlobSplit& out)
{
    if (glob.isEmpty()) return false;

    const int firstStar = glob.indexOfChar('*');
    out.recursive = glob.contains("**");

    // No wildcard: treat the whole string as the base, no pattern.
    if (firstStar < 0)
    {
        out.baseDir = glob;
        out.pattern = {};
        out.recursive = false;
        return true;
    }

    const int lastSlashBeforeStar = glob.substring(0, firstStar).lastIndexOfChar('/');
    if (lastSlashBeforeStar < 0) return false;
    out.baseDir = glob.substring(0, lastSlashBeforeStar);

    auto tail = glob.substring(lastSlashBeforeStar + 1);
    if (tail.startsWith("**/"))      tail = tail.substring(3);
    else if (tail == "**")           tail = "*";
    out.pattern = tail;
    return true;
}

std::optional<PluginConfig> PluginConfigRegistry::loadFile(const juce::File& f)
{
    if (! f.existsAsFile()) return std::nullopt;
    const auto text = f.loadFileAsString();
    juce::var doc = juce::JSON::parse(text);
    if (doc.isVoid())
    {
        // Empty file is "nothing to load" — silent. A non-empty but
        // malformed JSON document is a real user-authored error and
        // worth a log line.
        if (text.trim().isNotEmpty())
            juce::Logger::writeToLog(
                "[PluginConfig] JSON parse failed for '"
                + f.getFullPathName() + "' — file ignored");
        return std::nullopt;
    }
    return PluginConfig::fromJson(doc);
}

const PluginConfig*
PluginConfigRegistry::find(const juce::PluginDescription& desc) const
{
    // Iterate in reverse so user-dir entries (loaded last) take precedence
    // over project-shipped defaults.
    for (auto it = configs_.rbegin(); it != configs_.rend(); ++it)
        if (it->matches(desc))
            return &*it;
    return nullptr;
}
