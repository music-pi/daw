#include <gtest/gtest.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../src/engine/PluginConfig.h"

namespace
{
juce::PluginDescription makeDesc(const juce::String& name,
                                  const juce::String& format = "VST3",
                                  const juce::String& mfr = "Acme")
{
    juce::PluginDescription d;
    d.name = name;
    d.pluginFormatName = format;
    d.manufacturerName = mfr;
    return d;
}

PluginConfig parseOrFail(const juce::String& json)
{
    auto doc = juce::JSON::parse(json);
    auto cfg = PluginConfig::fromJson(doc);
    [&](){ ASSERT_TRUE(cfg.has_value()); }();
    return *cfg;
}
}

TEST(PluginConfigTests, ParsesMatcherFields)
{
    auto cfg = parseOrFail(R"({
        "match": { "format": "VST3", "name": "Os*", "manufacturer": "suspects" }
    })");

    EXPECT_EQ(cfg.matcher.format, "VST3");
    EXPECT_EQ(cfg.matcher.nameGlob, "Os*");
    EXPECT_EQ(cfg.matcher.manufacturer, "suspects");
}

TEST(PluginConfigTests, MatchesWildcardNameCaseInsensitive)
{
    auto cfg = parseOrFail(R"({ "match": { "name": "OsTIRus*" } })");

    EXPECT_TRUE(cfg.matches(makeDesc("OsTIRus Synth")));
    EXPECT_TRUE(cfg.matches(makeDesc("ostirus synth")));     // case-insensitive
    EXPECT_FALSE(cfg.matches(makeDesc("Dexed")));
}

TEST(PluginConfigTests, FormatAndManufacturerMustMatchWhenSpecified)
{
    auto cfg = parseOrFail(R"({
        "match": { "format": "VST3", "name": "*", "manufacturer": "suspects" }
    })");

    EXPECT_TRUE(cfg.matches(makeDesc("Anything", "VST3", "Usual Suspects")));
    EXPECT_FALSE(cfg.matches(makeDesc("Anything", "LV2", "Usual Suspects")))
        << "wrong format should fail";
    EXPECT_FALSE(cfg.matches(makeDesc("Anything", "VST3", "Native Instruments")))
        << "wrong mfr substring should fail";
}

TEST(PluginConfigTests, EmptyMatcherFieldsMatchAnything)
{
    PluginConfig cfg;        // all defaults: empty matcher
    EXPECT_TRUE(cfg.matches(makeDesc("Anything", "LV2", "Anyone")));
}

TEST(PluginConfigTests, ParsesPresetFileScanSource)
{
    auto cfg = parseOrFail(R"({
        "presets": { "source": "file_scan", "glob": "/tmp/*.syx", "file_type": "sysex" }
    })");

    EXPECT_EQ(cfg.presets.source, PluginConfig::PresetSource::FileScan);
    EXPECT_EQ(cfg.presets.glob, "/tmp/*.syx");
    EXPECT_EQ(cfg.presets.fileType, "sysex");
}

TEST(PluginConfigTests, UnknownPresetSourceFallsBackToVstPrograms)
{
    auto cfg = parseOrFail(R"({ "presets": { "source": "magic" } })");
    EXPECT_EQ(cfg.presets.source, PluginConfig::PresetSource::VstPrograms);
}

TEST(PluginConfigTests, ParsesPinnedParams)
{
    auto cfg = parseOrFail(R"({
        "pinned": [
            { "id": "param_023", "label": "Cutoff" },
            { "name_match": "Resonance", "label": "Res" },
            { "id": "" }
        ]
    })");

    ASSERT_EQ(cfg.pinned.size(), 2u) << "entries with neither id nor name_match are skipped";
    EXPECT_EQ(cfg.pinned[0].id, "param_023");
    EXPECT_EQ(cfg.pinned[0].label, "Cutoff");
    EXPECT_EQ(cfg.pinned[1].nameMatch, "Resonance");
    EXPECT_EQ(cfg.pinned[1].label, "Res");
}

TEST(PluginConfigTests, InvalidJsonReturnsNullopt)
{
    auto doc = juce::JSON::parse("not even close to json");
    EXPECT_FALSE(PluginConfig::fromJson(doc).has_value());
}

TEST(PluginConfigTests, RegistryFindReturnsFirstMatchingConfig)
{
    PluginConfigRegistry reg;
    {
        PluginConfig a;
        a.matcher.nameGlob = "Dexed*";
        reg.add(std::move(a));
    }
    {
        PluginConfig b;
        b.matcher.nameGlob = "OsTIRus*";
        reg.add(std::move(b));
    }

    EXPECT_NE(reg.find(makeDesc("OsTIRus Synth")), nullptr);
    EXPECT_NE(reg.find(makeDesc("Dexed")), nullptr);
    EXPECT_EQ(reg.find(makeDesc("Surge XT")), nullptr);
}

TEST(PluginConfigTests, ExpandsHomeTildeInPresetGlob)
{
    auto cfg = parseOrFail(R"({ "presets": { "glob": "~/.local/share/plugins/*.syx" } })");
    EXPECT_FALSE(cfg.presets.glob.startsWithChar('~'));
    EXPECT_TRUE(cfg.presets.glob.contains(".local/share/plugins/*.syx"));
}

// ── splitGlob ──────────────────────────────────────────────────────────────

TEST(PluginConfigTests, SplitGlobSingleDirNonRecursive)
{
    PluginConfig::GlobSplit s;
    ASSERT_TRUE(PluginConfig::splitGlob("/home/user/.local/share/presets/*.syx", s));
    EXPECT_EQ(s.baseDir, "/home/user/.local/share/presets");
    EXPECT_EQ(s.pattern, "*.syx");
    EXPECT_FALSE(s.recursive);
}

TEST(PluginConfigTests, SplitGlobRecursiveDoubleStar)
{
    // Regression guard for the bug where "**" stayed in the dir path
    // and juce::File treated it as a non-existent directory, so the scan
    // silently found zero files even though .syx dumps lived in subdirs.
    PluginConfig::GlobSplit s;
    ASSERT_TRUE(PluginConfig::splitGlob("/home/user/.local/share/presets/**/*.syx", s));
    EXPECT_EQ(s.baseDir, "/home/user/.local/share/presets");
    EXPECT_EQ(s.pattern, "*.syx");
    EXPECT_TRUE(s.recursive);
}

TEST(PluginConfigTests, SplitGlobLiteralFallsBackWithoutPattern)
{
    PluginConfig::GlobSplit s;
    ASSERT_TRUE(PluginConfig::splitGlob("/tmp/one-specific-file.syx", s));
    EXPECT_EQ(s.baseDir, "/tmp/one-specific-file.syx");
    EXPECT_TRUE(s.pattern.isEmpty());
    EXPECT_FALSE(s.recursive);
}

TEST(PluginConfigTests, SplitGlobRejectsEmpty)
{
    PluginConfig::GlobSplit s;
    EXPECT_FALSE(PluginConfig::splitGlob("", s));
}

TEST(PluginConfigTests, SplitGlobRejectsPureWildcard)
{
    // No slash before the star — can't derive a base dir.
    PluginConfig::GlobSplit s;
    EXPECT_FALSE(PluginConfig::splitGlob("*.syx", s));
}
