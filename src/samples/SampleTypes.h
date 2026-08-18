#pragma once

#include <optional>

#include <juce_core/juce_core.h>

enum class SampleCategory
{
    Unknown,
    Loop,
    OneShot
};

enum class SampleInstrumentType
{
    Unknown,
    Drum,
    Bass,
    Key,
    Vocal,
    Fx
};

enum class SampleTagSource
{
    Heuristic,
    Analysis
};

struct SampleTags
{
    SampleCategory category { SampleCategory::Unknown };
    SampleInstrumentType instrumentType { SampleInstrumentType::Unknown };
    juce::String subType;
    juce::String kit;
    std::optional<int> bpm;
    juce::String key;
    SampleTagSource source { SampleTagSource::Heuristic };
    double confidence { 0.0 };

    bool operator==(const SampleTags&) const = default;
};

juce::String sampleCategoryId(SampleCategory category);
SampleCategory sampleCategoryFromId(const juce::String& id);
juce::String sampleCategoryLabel(SampleCategory category);

juce::String sampleInstrumentTypeId(SampleInstrumentType type);
SampleInstrumentType sampleInstrumentTypeFromId(const juce::String& id);
juce::String sampleInstrumentTypeLabel(SampleInstrumentType type);

juce::String sampleTagSourceId(SampleTagSource source);
SampleTagSource sampleTagSourceFromId(const juce::String& id);
