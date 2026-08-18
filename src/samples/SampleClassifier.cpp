#include "SampleClassifier.h"

#include <algorithm>
#include <regex>
#include <string>

namespace
{
juce::String normaliseWords(juce::String text)
{
    text = text.toLowerCase();
    for (int index = 0; index < text.length(); ++index)
    {
        const auto character = text[index];
        if (!juce::CharacterFunctions::isLetterOrDigit(character))
            text = text.replaceSection(index, 1, " ");
    }
    while (text.contains("  "))
        text = text.replace("  ", " ");
    return " " + text.trim() + " ";
}

std::vector<juce::String> normaliseKeywords(
    const std::vector<juce::String>& keywords)
{
    std::vector<juce::String> result;
    result.reserve(keywords.size());
    for (const auto& keyword : keywords)
        result.push_back(normaliseWords(keyword));
    return result;
}

SampleTaxonomy::KeywordMap normaliseKeywordMap(
    const SampleTaxonomy::KeywordMap& groups)
{
    SampleTaxonomy::KeywordMap result;
    for (const auto& [name, keywords] : groups)
        result.emplace(name, normaliseKeywords(keywords));
    return result;
}

int keywordScore(const std::vector<juce::String>& keywords,
                 const juce::String& fileWords,
                 const juce::String& folderWords)
{
    int score = 0;
    for (const auto& keyword : keywords)
    {
        if (keyword.length() > 2 && fileWords.contains(keyword)) score += 3;
        if (keyword.length() > 2 && folderWords.contains(keyword)) score += 2;
    }
    return score;
}

juce::String bestKeywordGroup(const SampleTaxonomy::KeywordMap& groups,
                              const juce::String& fileWords,
                              const juce::String& folderWords)
{
    juce::String best;
    int bestScore = 0;
    for (const auto& [name, keywords] : groups)
    {
        const int score = keywordScore(keywords, fileWords, folderWords);
        if (score > bestScore)
        {
            best = name;
            bestScore = score;
        }
    }
    return best;
}

bool anyKeyword(const std::vector<juce::String>& keywords,
                const juce::String& fileWords,
                const juce::String& folderWords)
{
    return keywordScore(keywords, fileWords, folderWords) > 0;
}

std::optional<int> parseBpm(const juce::String& name, const juce::String& expression)
{
    try
    {
        const std::regex regex(expression.toStdString(), std::regex::icase);
        std::smatch match;
        const auto text = name.toStdString();
        if (!std::regex_search(text, match, regex))
            return std::nullopt;
        for (size_t group = 1; group < match.size(); ++group)
        {
            if (!match[group].matched)
                continue;
            const int bpm = std::stoi(match[group].str());
            if (bpm >= 20 && bpm <= 300)
                return bpm;
        }
    }
    catch (const std::regex_error&)
    {
        juce::Logger::writeToLog("[SampleClassifier] Invalid BPM regex in taxonomy");
    }
    return std::nullopt;
}

juce::String parseKey(const juce::String& name, const juce::String& expression)
{
    try
    {
        const std::regex regex(expression.toStdString(), std::regex::icase);
        std::smatch match;
        const auto text = name.toStdString();
        if (!std::regex_search(text, match, regex) || match.size() < 3)
            return {};
        auto note = juce::String(match[1].str());
        note = note.substring(0, 1).toUpperCase() + note.substring(1);
        auto quality = juce::String(match[2].str()).toLowerCase();
        quality = quality == "maj" ? "maj" : "min";
        return note + " " + quality;
    }
    catch (const std::regex_error&)
    {
        juce::Logger::writeToLog("[SampleClassifier] Invalid key regex in taxonomy");
    }
    return {};
}

bool isGenericFolder(const juce::String& name)
{
    const auto lower = name.toLowerCase();
    static const std::vector<juce::String> generic {
        "sample", "samples", "audio", "loop", "loops", "one shot", "one shots",
        "oneshot", "oneshots", "drum", "drums", "wav", "files"
    };
    return std::find(generic.begin(), generic.end(), lower) != generic.end();
}
} // namespace

SampleClassifier::SampleClassifier(SampleTaxonomy taxonomy)
    : taxonomy_(std::move(taxonomy)),
      instrumentKeywords_(normaliseKeywordMap(taxonomy_.instrumentKeywords())),
      subTypeKeywords_(normaliseKeywordMap(taxonomy_.subTypeKeywords())),
      loopKeywords_(normaliseKeywords(taxonomy_.loopKeywords())),
      oneShotKeywords_(normaliseKeywords(taxonomy_.oneShotKeywords()))
{
}

SampleTags SampleClassifier::classify(const juce::File& file,
                                      const DurationProvider& durationProvider) const
{
    SampleTags tags;
    const auto fileName = file.getFileNameWithoutExtension();
    const auto folderName = file.getParentDirectory().getFileName();
    const auto fileWords = normaliseWords(fileName);
    const auto folderWords = normaliseWords(folderName);

    const bool namedLoop = anyKeyword(loopKeywords_, fileWords, folderWords);
    const bool namedOneShot = anyKeyword(oneShotKeywords_, fileWords, folderWords);
    if (namedLoop != namedOneShot)
        tags.category = namedLoop ? SampleCategory::Loop : SampleCategory::OneShot;

    const auto instrument = bestKeywordGroup(instrumentKeywords_,
                                             fileWords, folderWords);
    tags.instrumentType = sampleInstrumentTypeFromId(instrument);
    tags.subType = bestKeywordGroup(subTypeKeywords_, fileWords, folderWords);
    if (tags.instrumentType == SampleInstrumentType::Unknown && tags.subType.isNotEmpty())
        tags.instrumentType = SampleInstrumentType::Drum;

    tags.bpm = parseBpm(fileName, taxonomy_.bpmRegex());
    tags.key = parseKey(fileName, taxonomy_.keyRegex());

    if (tags.category == SampleCategory::Unknown && durationProvider)
    {
        tags.source = SampleTagSource::Analysis;
        if (const auto duration = durationProvider(file))
        {
            tags.category = *duration >= taxonomy_.minimumLoopDurationSeconds()
                                ? SampleCategory::Loop
                                : SampleCategory::OneShot;
        }
    }

    if (tags.category == SampleCategory::OneShot
        && tags.instrumentType == SampleInstrumentType::Drum
        && folderName.isNotEmpty() && !isGenericFolder(folderName))
    {
        tags.kit = folderName;
    }

    const bool hasHeuristic = instrument.isNotEmpty() || tags.subType.isNotEmpty()
                              || tags.bpm.has_value() || tags.key.isNotEmpty();
    if (tags.source == SampleTagSource::Analysis)
        tags.confidence = tags.category == SampleCategory::Unknown ? 0.15 : 0.65;
    else if (tags.category != SampleCategory::Unknown && hasHeuristic)
        tags.confidence = 0.95;
    else if (tags.category != SampleCategory::Unknown || hasHeuristic)
        tags.confidence = 0.75;
    else
        tags.confidence = 0.1;

    return tags;
}
