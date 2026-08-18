#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * LED type — determines which FlushTarget method to call.
 */
enum class LedType
{
    Mono,     // Monochromatic firmware brightness (0, 4-7) — transport LEDs
    Indexed,  // Sparse color index (0-78) — pad, group, nav, touch strip LEDs
    Button    // Monochromatic firmware brightness (0, 4-7) — screen buttons
};

/**
 * Resource sets — predefined bundles of hardware resource IDs
 * that widgets claim together.
 */
enum class ResourceSet
{
    PadLeds,        // p1-p16
    GroupLeds,      // g1-g8
    LeftOptions,    // d1-d4
    RightOptions,   // d5-d8
    LeftKnobs,      // k1-k4
    RightKnobs,     // k5-k8
    TransportLeds,  // play, stop, recCountIn, restartLoop
    TouchStrips,    // ts1-ts25
    NavLeds,        // navUp, navDown, navLeft, navRight
    ScreenButtons   // pattern, mixer, sampling, arranger, step
};

/**
 * Resource ID constants for each ResourceSet.
 */
namespace ResourceIds
{

inline const std::array<std::string, 16> PadLeds = {
    "p1",  "p2",  "p3",  "p4",
    "p5",  "p6",  "p7",  "p8",
    "p9",  "p10", "p11", "p12",
    "p13", "p14", "p15", "p16"
};

inline const std::array<std::string, 8> GroupLeds = {
    "g1", "g2", "g3", "g4",
    "g5", "g6", "g7", "g8"
};

inline const std::array<std::string, 4> LeftOptions  = { "d1", "d2", "d3", "d4" };
inline const std::array<std::string, 4> RightOptions = { "d5", "d6", "d7", "d8" };
inline const std::array<std::string, 4> LeftKnobs    = { "k1", "k2", "k3", "k4" };
inline const std::array<std::string, 4> RightKnobs   = { "k5", "k6", "k7", "k8" };

inline const std::array<std::string, 4> TransportLeds = {
    "play", "stop", "recCountIn", "restartLoop"
};

inline const std::array<std::string, 25> TouchStrips = {
    "ts1",  "ts2",  "ts3",  "ts4",  "ts5",
    "ts6",  "ts7",  "ts8",  "ts9",  "ts10",
    "ts11", "ts12", "ts13", "ts14", "ts15",
    "ts16", "ts17", "ts18", "ts19", "ts20",
    "ts21", "ts22", "ts23", "ts24", "ts25"
};

inline const std::array<std::string, 4> NavLeds = {
    "navUp", "navDown", "navLeft", "navRight"
};

inline const std::array<std::string, 2> ArrowLeds = {
    "arrowLeft", "arrowRight"
};

inline const std::array<std::string, 2> ModifierLeds = {
    "solo", "muteChoke"
};

inline const std::array<std::string, 5> ScreenButtons = {
    "pattern", "mixer", "sampling", "arranger", "step"
};

} // namespace ResourceIds

/**
 * HardwareState manages ownership of hardware output resources (LEDs, display regions).
 *
 * Every resource is a claimable entity with strict ownership. Double-claiming
 * throws an error, forcing valid widget lifecycle ordering.
 *
 * The flush() method sends all dirty LED values to a FlushTarget (typically
 * ControllerHost), calling the appropriate mono/indexed method per resource.
 */
class HardwareState
{
public:
    /**
     * FlushTarget — interface for receiving LED state updates during flush.
     */
    class FlushTarget
    {
    public:
        virtual ~FlushTarget() = default;
        virtual void setMonoLed(const std::string& name, uint8_t brightness) = 0;
        virtual void setIndexedLed(const std::string& name, uint8_t colorIndex) = 0;
        virtual void setButtonBrightness(const std::string& name, uint8_t brightness) = 0;
        virtual void beginLedBatch() {}
        virtual void endLedBatch() {}
    };

    HardwareState();

    // --- Ownership ---

    /** Claim a resource for an owner. Throws if already owned by a different owner. No-op if same owner re-claims. */
    void claim(const std::string& resourceId, const std::string& ownerId);

    /** Release a resource. Throws if not owned by caller. */
    void release(const std::string& resourceId, const std::string& ownerId);

    /** Release all resources owned by the given owner. */
    void releaseAll(const std::string& ownerId);

    /** Claim all resources in a ResourceSet. */
    void claimSet(ResourceSet set, const std::string& ownerId);

    /** Release all resources in a ResourceSet. */
    void releaseSet(ResourceSet set, const std::string& ownerId);

    /** Force-release a resource regardless of owner (escape hatch). Logs a
        warning when the resource had an owner. Pass `warn=false` for expected
        hand-offs (e.g. transient overlays) where the log noise would hide
        real bugs. */
    void forceRelease(const std::string& resourceId, bool warn = true);

    // --- Queries ---

    /** Get the current owner of a resource (empty string if unclaimed). */
    std::string getOwner(const std::string& resourceId) const;

    /** Check if a resource is currently claimed. */
    bool isClaimed(const std::string& resourceId) const;

    // --- LED values ---

    /** Set LED value. Throws if not owned by caller. Marks dirty. */
    void setLed(const std::string& resourceId, uint8_t value, const std::string& ownerId);

    /** Get current LED value. */
    uint8_t getLed(const std::string& resourceId) const;

    // --- Dirty tracking ---

    /** Returns resources that changed since last clearDirty(). */
    std::vector<std::pair<std::string, uint8_t>> getDirtyResources() const;

    /** Reset all dirty flags. */
    void clearDirty();

    /** Mark all resources in a set as dirty (forces re-flush). */
    void markSetDirty(ResourceSet set);

    // --- Flush ---

    /** Send all dirty LED values to the target, then clear dirty flags. */
    void flush(FlushTarget& target);

private:
    struct ResourceEntry
    {
        std::string owner;
        uint8_t value = 0;
        bool dirty = false;
        LedType ledType = LedType::Mono;
    };

    std::unordered_map<std::string, ResourceEntry> resources_;

    void registerResource(const std::string& id, LedType type = LedType::Mono);
    const ResourceEntry& getEntry(const std::string& id) const;
    ResourceEntry& getEntry(const std::string& id);

    /** Get the list of resource IDs for a given ResourceSet. */
    static std::vector<std::string> getResourceIds(ResourceSet set);
};
