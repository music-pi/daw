#include "SampleTypes.h"

juce::String sampleCategoryId(SampleCategory category)
{
    switch (category)
    {
        case SampleCategory::Loop: return "loop";
        case SampleCategory::OneShot: return "one-shot";
        case SampleCategory::Unknown: break;
    }
    return "unknown";
}

SampleCategory sampleCategoryFromId(const juce::String& id)
{
    if (id == "loop") return SampleCategory::Loop;
    if (id == "one-shot") return SampleCategory::OneShot;
    return SampleCategory::Unknown;
}

juce::String sampleCategoryLabel(SampleCategory category)
{
    switch (category)
    {
        case SampleCategory::Loop: return "Loops";
        case SampleCategory::OneShot: return "One-Shots";
        case SampleCategory::Unknown: break;
    }
    return "Unknown";
}

juce::String sampleInstrumentTypeId(SampleInstrumentType type)
{
    switch (type)
    {
        case SampleInstrumentType::Drum: return "drum";
        case SampleInstrumentType::Bass: return "bass";
        case SampleInstrumentType::Key: return "key";
        case SampleInstrumentType::Vocal: return "vocal";
        case SampleInstrumentType::Fx: return "fx";
        case SampleInstrumentType::Unknown: break;
    }
    return "unknown";
}

SampleInstrumentType sampleInstrumentTypeFromId(const juce::String& id)
{
    if (id == "drum") return SampleInstrumentType::Drum;
    if (id == "bass") return SampleInstrumentType::Bass;
    if (id == "key") return SampleInstrumentType::Key;
    if (id == "vocal") return SampleInstrumentType::Vocal;
    if (id == "fx") return SampleInstrumentType::Fx;
    return SampleInstrumentType::Unknown;
}

juce::String sampleInstrumentTypeLabel(SampleInstrumentType type)
{
    switch (type)
    {
        case SampleInstrumentType::Drum: return "Drums";
        case SampleInstrumentType::Bass: return "Bass";
        case SampleInstrumentType::Key: return "Keys";
        case SampleInstrumentType::Vocal: return "Vocals";
        case SampleInstrumentType::Fx: return "FX";
        case SampleInstrumentType::Unknown: break;
    }
    return "Unknown";
}

juce::String sampleTagSourceId(SampleTagSource source)
{
    return source == SampleTagSource::Analysis ? "analysis" : "heuristic";
}

SampleTagSource sampleTagSourceFromId(const juce::String& id)
{
    return id == "analysis" ? SampleTagSource::Analysis : SampleTagSource::Heuristic;
}
