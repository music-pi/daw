#include "SampleIndex.h"

#include <algorithm>
#include <unordered_set>

namespace
{
juce::var tagsToJson(const SampleTags& tags)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("category", sampleCategoryId(tags.category));
    object->setProperty("instrumentType", sampleInstrumentTypeId(tags.instrumentType));
    object->setProperty("subType", tags.subType);
    object->setProperty("kit", tags.kit);
    if (tags.bpm.has_value()) object->setProperty("bpm", *tags.bpm);
    if (tags.key.isNotEmpty()) object->setProperty("key", tags.key);
    object->setProperty("source", sampleTagSourceId(tags.source));
    object->setProperty("confidence", tags.confidence);
    return juce::var(object.release());
}

SampleTags tagsFromJson(const juce::var& value)
{
    SampleTags tags;
    if (auto* object = value.getDynamicObject())
    {
        tags.category = sampleCategoryFromId(object->getProperty("category").toString());
        tags.instrumentType = sampleInstrumentTypeFromId(
            object->getProperty("instrumentType").toString());
        tags.subType = object->getProperty("subType").toString();
        tags.kit = object->getProperty("kit").toString();
        if (object->hasProperty("bpm"))
            tags.bpm = static_cast<int>(object->getProperty("bpm"));
        tags.key = object->getProperty("key").toString();
        tags.source = sampleTagSourceFromId(object->getProperty("source").toString());
        tags.confidence = static_cast<double>(object->getProperty("confidence"));
    }
    return tags;
}

SampleCategoryNode& childFolder(SampleCategoryNode& parent,
                                const juce::String& label,
                                const juce::String& idPart)
{
    const auto existing = std::find_if(parent.children.begin(), parent.children.end(),
        [&](const SampleCategoryNode& node) { return !node.isFile && node.label == label; });
    if (existing != parent.children.end())
        return *existing;

    SampleCategoryNode node;
    node.id = parent.id + "/" + idPart;
    node.label = label;
    node.breadcrumb = parent.breadcrumb.isEmpty() ? label : parent.breadcrumb + " / " + label;
    parent.children.push_back(std::move(node));
    return parent.children.back();
}

juce::String bpmBucket(int bpm)
{
    const int lower = (bpm / 10) * 10;
    return juce::String(lower) + "-" + juce::String(lower + 9) + " BPM";
}

void sortTree(SampleCategoryNode& node)
{
    std::sort(node.children.begin(), node.children.end(),
        [](const SampleCategoryNode& left, const SampleCategoryNode& right)
        {
            if (left.isFile != right.isFile)
                return !left.isFile;
            return left.label.compareNatural(right.label) < 0;
        });
    for (auto& child : node.children)
        sortTree(child);
}
} // namespace

const SampleIndexEntry* SampleIndex::find(const juce::File& file) const
{
    const auto found = entries_.find(file.getFullPathName());
    return found == entries_.end() ? nullptr : &found->second;
}

void SampleIndex::set(SampleIndexEntry entry)
{
    entries_[entry.file.getFullPathName()] = std::move(entry);
}

void SampleIndex::removeMissing(const std::vector<juce::String>& retainedPaths)
{
    std::unordered_set<std::string> retained;
    retained.reserve(retainedPaths.size());
    for (const auto& path : retainedPaths)
        retained.insert(path.toStdString());
    for (auto iterator = entries_.begin(); iterator != entries_.end();)
    {
        if (!retained.contains(iterator->first.toStdString()))
            iterator = entries_.erase(iterator);
        else
            ++iterator;
    }
}

void SampleIndex::clear()
{
    entries_.clear();
}

std::vector<SampleCategoryNode> SampleIndex::buildCategoryTree() const
{
    SampleCategoryNode root { "category", {}, {}, {}, false, {} };
    for (const auto& [path, entry] : entries_)
    {
        auto& category = childFolder(root, sampleCategoryLabel(entry.tags.category),
                                     sampleCategoryId(entry.tags.category));
        auto& instrument = childFolder(category,
                                       sampleInstrumentTypeLabel(entry.tags.instrumentType),
                                       sampleInstrumentTypeId(entry.tags.instrumentType));
        SampleCategoryNode* destination = &instrument;

        if (entry.tags.category == SampleCategory::OneShot && entry.tags.subType.isNotEmpty())
            destination = &childFolder(*destination,
                                       entry.tags.subType.substring(0, 1).toUpperCase()
                                           + entry.tags.subType.substring(1),
                                       "subtype-" + entry.tags.subType);
        if (entry.tags.kit.isNotEmpty())
            destination = &childFolder(*destination, entry.tags.kit,
                                       "kit-" + entry.tags.kit.toLowerCase().replace(" ", "-"));
        if (entry.tags.category == SampleCategory::Loop && entry.tags.bpm.has_value())
            destination = &childFolder(*destination, bpmBucket(*entry.tags.bpm),
                                       "bpm-" + juce::String((*entry.tags.bpm / 10) * 10));
        if (entry.tags.key.isNotEmpty())
            destination = &childFolder(*destination, entry.tags.key,
                                       "key-" + entry.tags.key.toLowerCase().replace(" ", "-"));

        SampleCategoryNode leaf;
        leaf.id = path;
        leaf.label = entry.file.getFileNameWithoutExtension();
        leaf.breadcrumb = destination->breadcrumb;
        leaf.file = entry.file;
        leaf.isFile = true;
        destination->children.push_back(std::move(leaf));
    }

    sortTree(root);
    return root.children;
}

juce::var SampleIndex::toJson(const juce::File& root) const
{
    auto document = std::make_unique<juce::DynamicObject>();
    document->setProperty("schemaVersion", 1);
    document->setProperty("root", root.getFullPathName());
    auto entriesObject = std::make_unique<juce::DynamicObject>();
    for (const auto& [path, entry] : entries_)
    {
        auto record = std::make_unique<juce::DynamicObject>();
        record->setProperty("mtime", entry.modificationTimeMs);
        record->setProperty("size", entry.size);
        if (entry.durationSeconds.has_value())
            record->setProperty("duration", *entry.durationSeconds);
        record->setProperty("tags", tagsToJson(entry.tags));
        entriesObject->setProperty(path, juce::var(record.release()));
    }
    document->setProperty("entries", juce::var(entriesObject.release()));
    return juce::var(document.release());
}

std::optional<SampleIndex> SampleIndex::fromJson(const juce::var& document,
                                                const juce::File& expectedRoot)
{
    auto* root = document.getDynamicObject();
    if (root == nullptr || static_cast<int>(root->getProperty("schemaVersion")) != 1
        || root->getProperty("root").toString() != expectedRoot.getFullPathName())
        return std::nullopt;

    auto* entries = root->getProperty("entries").getDynamicObject();
    if (entries == nullptr)
        return SampleIndex {};

    SampleIndex index;
    for (const auto& property : entries->getProperties())
    {
        auto* record = property.value.getDynamicObject();
        if (record == nullptr)
            continue;
        SampleIndexEntry entry;
        entry.file = juce::File(property.name.toString());
        entry.modificationTimeMs = static_cast<juce::int64>(record->getProperty("mtime"));
        entry.size = static_cast<juce::int64>(record->getProperty("size"));
        if (record->hasProperty("duration"))
            entry.durationSeconds = static_cast<double>(record->getProperty("duration"));
        entry.tags = tagsFromJson(record->getProperty("tags"));
        index.set(std::move(entry));
    }
    return index;
}
