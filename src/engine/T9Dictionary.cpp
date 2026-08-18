#include "T9Dictionary.h"
#include <algorithm>
#include <cmath>

const juce::Identifier T9Dictionary::kEntry      { "entry" };
const juce::Identifier T9Dictionary::kScope      { "scope" };
const juce::Identifier T9Dictionary::kText       { "text" };
const juce::Identifier T9Dictionary::kUses       { "uses" };
const juce::Identifier T9Dictionary::kLastUsedMs { "lastUsedMs" };

T9Dictionary::T9Dictionary(juce::ValueTree persistNode)
    : root_(std::move(persistNode)), invalidator_(*this)
{
    jassert(root_.isValid());
    root_.addListener(&invalidator_);
}

T9Dictionary::~T9Dictionary()
{
    root_.removeListener(&invalidator_);
}

void T9Dictionary::rebuildCacheIfNeeded() const
{
    if (!cacheDirty_)
        return;

    cache_.clear();
    for (int i = 0; i < root_.getNumChildren(); ++i)
    {
        auto child = root_.getChild(i);
        if (!child.hasType(kEntry))
            continue;
        const auto scope = child.getProperty(kScope).toString();
        cache_[scope].push_back(child);
    }
    cacheDirty_ = false;
}

const std::vector<juce::ValueTree>& T9Dictionary::scopeEntries(const juce::String& scope) const
{
    rebuildCacheIfNeeded();
    static const std::vector<juce::ValueTree> empty;
    auto it = cache_.find(scope);
    return it == cache_.end() ? empty : it->second;
}

juce::ValueTree T9Dictionary::findEntry(const juce::String& scope, const juce::String& text) const
{
    for (const auto& entry : scopeEntries(scope))
    {
        if (entry.getProperty(kText).toString().equalsIgnoreCase(text))
            return entry;
    }
    return {};
}

juce::ValueTree T9Dictionary::insertEntry(const juce::String& scope, const juce::String& text,
                                          int uses, juce::int64 lastUsedMs)
{
    juce::ValueTree entry(kEntry);
    entry.setProperty(kScope, scope, nullptr);
    entry.setProperty(kText, text, nullptr);
    entry.setProperty(kUses, uses, nullptr);
    entry.setProperty(kLastUsedMs, lastUsedMs, nullptr);
    root_.appendChild(entry, nullptr);
    // appendChild fires valueTreeChildAdded → invalidator_ marks cache dirty.
    return entry;
}

void T9Dictionary::seed(const juce::String& scope, juce::StringArray values)
{
    const auto now = juce::Time::currentTimeMillis();
    for (const auto& v : values)
    {
        if (v.isEmpty()) continue;
        if (findEntry(scope, v).isValid()) continue;
        insertEntry(scope, v, 1, now);
    }
}

void T9Dictionary::record(const juce::String& scope, const juce::String& text)
{
    if (text.isEmpty()) return;
    const auto now = juce::Time::currentTimeMillis();
    auto existing = findEntry(scope, text);
    if (existing.isValid())
    {
        const int uses = static_cast<int>(existing.getProperty(kUses)) + 1;
        existing.setProperty(kUses, uses, nullptr);
        existing.setProperty(kLastUsedMs, now, nullptr);
    }
    else
    {
        insertEntry(scope, text, 1, now);
    }
}

double T9Dictionary::scoreOf(int uses, juce::int64 lastUsedMs, juce::int64 nowMs)
{
    constexpr double kHalfLifeDays = 30.0;
    const double ageDays = static_cast<double>(nowMs - lastUsedMs) / (1000.0 * 60.0 * 60.0 * 24.0);
    const double decay = std::exp(-ageDays / kHalfLifeDays);
    return static_cast<double>(uses) * decay;
}

std::vector<juce::String> T9Dictionary::completionsFor(const juce::String& scope,
                                                       const juce::String& prefix,
                                                       int limit) const
{
    const auto now = juce::Time::currentTimeMillis();
    struct Row { juce::String text; double score; };
    std::vector<Row> rows;

    const auto& entries = scopeEntries(scope);
    rows.reserve(entries.size());
    for (const auto& c : entries)
    {
        const auto text = c.getProperty(kText).toString();
        if (prefix.isNotEmpty() && !text.startsWithIgnoreCase(prefix)) continue;

        const int uses = static_cast<int>(c.getProperty(kUses));
        const auto lastUsedMs = static_cast<juce::int64>(c.getProperty(kLastUsedMs));
        rows.push_back({ text, scoreOf(uses, lastUsedMs, now) });
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score > b.score; });

    if (limit > 0 && static_cast<int>(rows.size()) > limit)
        rows.resize(static_cast<size_t>(limit));

    std::vector<juce::String> out;
    out.reserve(rows.size());
    for (auto& r : rows) out.push_back(std::move(r.text));
    return out;
}

int T9Dictionary::entryCount(const juce::String& scope) const
{
    return static_cast<int>(scopeEntries(scope).size());
}
