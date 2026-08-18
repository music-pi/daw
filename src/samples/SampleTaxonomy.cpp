#include "SampleTaxonomy.h"

#include "../app/DataPaths.h"

#include <regex>

namespace
{
std::vector<juce::String> stringArray(const juce::var& value)
{
    std::vector<juce::String> result;
    if (auto* array = value.getArray())
    {
        result.reserve(static_cast<size_t>(array->size()));
        for (const auto& item : *array)
        {
            auto text = item.toString().trim().toLowerCase();
            if (text.isNotEmpty())
                result.push_back(std::move(text));
        }
    }
    return result;
}

SampleTaxonomy::KeywordMap keywordMap(const juce::var& value)
{
    SampleTaxonomy::KeywordMap result;
    if (auto* object = value.getDynamicObject())
    {
        for (const auto& property : object->getProperties())
        {
            auto values = stringArray(property.value);
            if (!values.empty())
                result.emplace(property.name.toString().toLowerCase(), std::move(values));
        }
    }
    return result;
}

bool validRegex(const juce::String& expression)
{
    try
    {
        const std::regex compiled(expression.toStdString(), std::regex::icase);
        juce::ignoreUnused(compiled);
        return true;
    }
    catch (const std::regex_error&)
    {
        return false;
    }
}

juce::File findTaxonomyAbove(juce::File current)
{
    for (int depth = 0; depth < 6 && current.exists(); ++depth)
    {
        const auto candidate = current.getChildFile("config/sample-taxonomy.json");
        if (candidate.existsAsFile())
            return candidate;
        const auto parent = current.getParentDirectory();
        if (parent == current)
            break;
        current = parent;
    }
    return {};
}
} // namespace

SampleTaxonomy SampleTaxonomy::defaults()
{
    SampleTaxonomy taxonomy;
    taxonomy.instrumentKeywords_ = {
        { "drum", { "drum", "kit", "perc" } },
        { "bass", { "bass", "808", "sub" } },
        { "key", { "key", "keys", "piano", "synth", "pad", "lead", "chord" } },
        { "vocal", { "vox", "vocal", "acapella" } },
        { "fx", { "fx", "riser", "sweep", "impact" } }
    };
    taxonomy.subTypeKeywords_ = {
        { "kick", { "kick", "bd", "bassdrum" } },
        { "snare", { "snare", "sd", "rim" } },
        { "hat", { "hat", "hh", "hihat" } },
        { "clap", { "clap", "clp" } },
        { "tom", { "tom" } },
        { "cymbal", { "crash", "ride", "cymbal" } },
        { "perc", { "perc", "shaker", "conga", "bongo" } }
    };
    taxonomy.loopKeywords_ = { "loop", "groove", "phrase" };
    taxonomy.oneShotKeywords_ = { "one shot", "oneshot", "one-shot", "hit", "stab" };
    taxonomy.bpmRegex_ = R"((?:^|[^0-9])(\d{2,3})\s?bpm|_(\d{2,3})(?:_|$))";
    taxonomy.keyRegex_ = R"((?:^|[_\-\s])([A-Ga-g](?:#|b)?)(maj|min|m)(?:[_\-\s.]|$))";
    return taxonomy;
}

std::optional<SampleTaxonomy> SampleTaxonomy::fromJson(const juce::var& document)
{
    auto* root = document.getDynamicObject();
    if (root == nullptr || static_cast<int>(root->getProperty("schemaVersion")) != 1)
        return std::nullopt;

    auto taxonomy = defaults();
    auto* loop = root->getProperty("loop").getDynamicObject();
    if (loop == nullptr)
        return std::nullopt;
    const double minimum = static_cast<double>(loop->getProperty("minDurationSec"));
    if (minimum <= 0.0)
        return std::nullopt;
    taxonomy.minimumLoopDurationSeconds_ = minimum;
    auto loopKeywords = stringArray(loop->getProperty("keywords"));
    if (!loopKeywords.empty())
        taxonomy.loopKeywords_ = std::move(loopKeywords);
    if (auto* oneShot = root->getProperty("oneShot").getDynamicObject())
    {
        auto keywords = stringArray(oneShot->getProperty("keywords"));
        if (!keywords.empty())
            taxonomy.oneShotKeywords_ = std::move(keywords);
    }

    auto instruments = keywordMap(root->getProperty("instrumentTypes"));
    auto subTypes = keywordMap(root->getProperty("subTypes"));
    if (instruments.empty() || subTypes.empty())
        return std::nullopt;
    taxonomy.instrumentKeywords_ = std::move(instruments);
    taxonomy.subTypeKeywords_ = std::move(subTypes);

    const auto bpm = root->getProperty("bpmRegex").toString();
    const auto key = root->getProperty("keyRegex").toString();
    if (bpm.isEmpty() || key.isEmpty() || !validRegex(bpm) || !validRegex(key))
        return std::nullopt;
    taxonomy.bpmRegex_ = bpm;
    taxonomy.keyRegex_ = key;
    return taxonomy;
}

SampleTaxonomy SampleTaxonomy::load(const juce::File& file, bool* usedFallback)
{
    if (usedFallback != nullptr)
        *usedFallback = false;
    if (file.existsAsFile())
    {
        const auto parsed = juce::JSON::parse(file.loadFileAsString());
        if (auto taxonomy = fromJson(parsed))
            return *taxonomy;
        juce::Logger::writeToLog("[SampleTaxonomy] Invalid taxonomy at "
                                 + file.getFullPathName() + "; using defaults");
    }
    else
    {
        juce::Logger::writeToLog("[SampleTaxonomy] Taxonomy missing at "
                                 + file.getFullPathName() + "; using defaults");
    }
    if (usedFallback != nullptr)
        *usedFallback = true;
    return defaults();
}

juce::File SampleTaxonomy::findDefaultFile()
{
    const auto userFile = DataPaths::getRoot().getChildFile("sample-taxonomy.json");
    if (userFile.existsAsFile())
        return userFile;

    if (const auto file = findTaxonomyAbove(juce::File::getCurrentWorkingDirectory());
        file.existsAsFile())
        return file;

    const auto executableDirectory = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();
    if (const auto file = findTaxonomyAbove(executableDirectory); file.existsAsFile())
        return file;
    return userFile;
}
