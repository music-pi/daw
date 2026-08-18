#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <unordered_map>
#include <vector>

class T9Dictionary
{
public:
    explicit T9Dictionary(juce::ValueTree persistNode);
    ~T9Dictionary();

    /** Add values for a scope if not already present. Idempotent. */
    void seed(const juce::String& scope, juce::StringArray values);

    /** Record a committed string — increments use count and stamps lastUsedMs. */
    void record(const juce::String& scope, const juce::String& text);

    /** Return up to `limit` entries matching the (case-insensitive) prefix, ranked by score. */
    [[nodiscard]] std::vector<juce::String> completionsFor(const juce::String& scope,
                                             const juce::String& prefix,
                                             int limit = 5) const;

    /** Number of entries for a scope (testing + diagnostics). */
    [[nodiscard]] int entryCount(const juce::String& scope) const;

private:
    juce::ValueTree root_;

    juce::ValueTree findEntry(const juce::String& scope, const juce::String& text) const;
    juce::ValueTree insertEntry(const juce::String& scope, const juce::String& text,
                                int uses, juce::int64 lastUsedMs);
    static double scoreOf(int uses, juce::int64 lastUsedMs, juce::int64 nowMs);

    // Lazy per-scope cache: entries grouped by scope name for O(1) scope
    // lookup instead of a linear scan over every child of root_. The cache
    // holds ValueTree handles (ref-counted, cheap to copy) so readers get
    // live references to the backing nodes. Invalidated by a ValueTree
    // listener whenever anything under root_ mutates — including external
    // edits made through the underlying ValueTree directly.
    // Only structural changes (add/remove/reorder/reparent) or a change to
    // an entry's scope property affect which scope-bucket an entry lives
    // in. Other property changes (uses, lastUsedMs, text) are handled
    // through the cached ValueTree handles, so they don't invalidate.
    struct CacheInvalidator : public juce::ValueTree::Listener
    {
        explicit CacheInvalidator(T9Dictionary& owner) : owner_(owner) {}
        void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& id) override
        {
            if (id == kScope)
                owner_.invalidateCache();
        }
        void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { owner_.invalidateCache(); }
        void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { owner_.invalidateCache(); }
        void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override { owner_.invalidateCache(); }
        void valueTreeParentChanged(juce::ValueTree&) override { owner_.invalidateCache(); }
        T9Dictionary& owner_;
    };

    mutable std::unordered_map<juce::String, std::vector<juce::ValueTree>> cache_;
    mutable bool cacheDirty_ { true };
    CacheInvalidator invalidator_;

    void invalidateCache() const { cacheDirty_ = true; }
    void rebuildCacheIfNeeded() const;
    const std::vector<juce::ValueTree>& scopeEntries(const juce::String& scope) const;

    static const juce::Identifier kEntry;
    static const juce::Identifier kScope;
    static const juce::Identifier kText;
    static const juce::Identifier kUses;
    static const juce::Identifier kLastUsedMs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T9Dictionary)
};
