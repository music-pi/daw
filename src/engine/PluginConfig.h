#pragma once

#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

/** Per-plugin configuration loaded from JSON. Maps plugin-matcher rules to a
    preset source and an ordered list of pinned parameters the editor widget
    should surface on its first page. */
struct PluginConfig
{
    struct Matcher
    {
        juce::String format;        // "VST3" | "LV2" | "AU" | ""  (empty = any)
        juce::String nameGlob;      // wildcard against plugin name (case-insensitive)
        juce::String manufacturer;  // optional manufacturer substring
    };

    enum class PresetSource
    {
        VstPrograms,    // default: use AudioProcessor::get/setCurrentProgram
        FileScan,       // walk a glob, inject each file (or frame) as sysex
        None
    };

    struct PresetConfig
    {
        PresetSource source { PresetSource::VstPrograms };
        juce::String glob;          // expanded via ~ on load
        juce::String fileType;      // reserved; "sysex" is the only supported value today
    };

    struct PinnedParam
    {
        juce::String id;            // prefer exact AudioProcessorParameter::paramID
        juce::String nameMatch;     // fallback: substring match against param display name
        juce::String label;         // optional friendly label (defaults to plugin's name)
    };

    Matcher matcher;
    PresetConfig presets;
    std::vector<PinnedParam> pinned;

    /** Split a preset glob (e.g. a path ending in *.syx, optionally with a
        double-star recursive segment) into a real base directory, a
        filename wildcard, and a recursive flag. Used by the editor's
        file-scan preset source; exposed here so it can be unit-tested
        without a real filesystem. Returns false when the glob is empty
        or has no slash before the first wildcard. */
    struct GlobSplit
    {
        juce::String baseDir;
        juce::String pattern;
        bool recursive { false };
    };
    static bool splitGlob(const juce::String& glob, GlobSplit& out);

    /** Does this config match the given plugin? Matching rules: format (if
        specified) must equal; name (case-insensitive, `*` wildcard) must
        match; manufacturer (if specified) must be a substring. */
    bool matches(const juce::PluginDescription& desc) const;

    /** Parse a JSON document (already loaded). Invalid or missing fields
        fall back to defaults; the returned optional is empty only when the
        document itself is malformed. */
    static std::optional<PluginConfig> fromJson(const juce::var& doc);
};

/** Central registry of all PluginConfigs the app knows about.

    On construction the registry scans both the project-shipped config
    directory and the user's application-data dir; user files take
    precedence over shipped defaults when both match a plugin. If nothing
    matches, `find()` returns nullptr and the host falls back to its
    generic behaviour. */
class PluginConfigRegistry
{
public:
    PluginConfigRegistry() = default;

    /** Load configs from the two well-known directories. Repeated calls
        reload — cheap to invoke on app start or via a "reload configs"
        gesture. */
    void loadDefaults();

    /** Add a config programmatically — mainly used in tests, but also by
        any future "import plugin config" UI. */
    void add(PluginConfig config) { configs_.push_back(std::move(config)); }

    /** First config whose matcher accepts `desc`, or nullptr when none do. */
    [[nodiscard]] const PluginConfig* find(const juce::PluginDescription& desc) const;

    [[nodiscard]] std::size_t size() const noexcept { return configs_.size(); }

    /** Visible for testing — parses a single JSON file path. */
    static std::optional<PluginConfig> loadFile(const juce::File& f);

private:
    void loadDir(const juce::File& dir);

    std::vector<PluginConfig> configs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginConfigRegistry)
};
