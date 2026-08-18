#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace HardwareConstants
{

constexpr int kPadCount = 16;
// MK3 pad reports encode pressure in the low 12 bits of the HID entry.
constexpr int kPadPressureMax = 0x0fff;

// ── Mono LED brightness ──────────────────────────────────────────────────
// Used for transport, screen buttons, and other monochromatic LEDs.
constexpr uint8_t kLedOff     = 0;
// Physically validated MK3 mono-LED range. libmk3 passes this raw byte through;
// values 4, 6, and 7 provide useful dim, medium, and bright tiers on hardware.
constexpr uint8_t kLedDim     = 4;   // inactive/available button
constexpr uint8_t kLedMedium  = 6;   // armed/waiting state
constexpr uint8_t kLedBright  = 7;   // active/pressed/recording

// ── Indexed LED colors ───────────────────────────────────────────────────
// Used for pad LEDs (p1-p16), group LEDs (g1-g8), nav LEDs, touchstrip.
//
// Values 4-67 form 16 hue groups of four brightness tiers. Application color
// choices use the two middle tiers so inactive and active states remain
// readable without clipping through the MK3's diffusers.
//
// Indexed LED types on MK3:
//   p1-p16          pad LEDs (RGB)
//   g1-g8           group buttons (RGB)
//   navUp/Down/L/R  navigation arrows (RGB)
//   ts1-ts25        touchstrip segments (RGB)
//   sampling        sampling button (RGB, special case)
struct IndexedColorPair
{
    uint8_t dim;
    uint8_t bright;
    std::string_view name;
};

inline constexpr std::array<IndexedColorPair, 16> kIndexedColorPairs {{
    { 5,  6, "Red" },
    { 9, 10, "Orange" },
    { 13, 14, "Amber" },
    { 17, 18, "Yellow" },
    { 21, 22, "Lime" },
    { 25, 26, "Light Green" },
    { 29, 30, "Green" },
    { 33, 34, "Mint" },
    { 37, 38, "Turquoise" },
    { 41, 42, "Light Blue" },
    { 45, 46, "Blue" },
    { 49, 50, "Purple" },
    { 53, 54, "Light Purple" },
    { 57, 58, "Magenta" },
    { 61, 62, "Pink" },
    { 65, 66, "Rose" },
}};

constexpr uint8_t kColorOff          = 0;
constexpr uint8_t kColorWhiteDim     = 76;
constexpr uint8_t kColorWhiteMedium  = 77;
constexpr uint8_t kColorWhite        = 78;
constexpr uint8_t kColorRedBright    = 6;
constexpr uint8_t kColorGreenLowest = 28;
constexpr uint8_t kColorGreenBright  = 30;
constexpr uint8_t kColorGreenHighest = 31;
constexpr uint8_t kColorCyanDim      = 37;
constexpr uint8_t kColorCyanBright   = 38;
constexpr uint8_t kColorGoldDim      = 13;
constexpr uint8_t kColorGoldBright   = 14;
// Legacy semantic name: the application intentionally exposes two gold tiers.
constexpr uint8_t kColorGoldMedium   = kColorGoldBright;
constexpr uint8_t kColorYellowBright = 18;
constexpr uint8_t kColorBlueBright   = 46;
constexpr uint8_t kColorPurpleBright = 50;

constexpr uint8_t brightestIndexedVariant(uint8_t value) noexcept
{
    if (value == kColorWhiteDim || value == kColorWhiteMedium || value == kColorWhite)
        return kColorWhite;
    for (const auto& pair : kIndexedColorPairs)
        if (value == pair.dim || value == pair.bright)
            return pair.bright;
    return kColorOff;
}

constexpr uint8_t dimmestIndexedVariant(uint8_t value) noexcept
{
    if (value == kColorWhiteDim || value == kColorWhiteMedium || value == kColorWhite)
        return kColorWhiteDim;
    for (const auto& pair : kIndexedColorPairs)
        if (value == pair.dim || value == pair.bright)
            return pair.dim;
    return kColorOff;
}

constexpr int indexedColorPairIndex(uint8_t value) noexcept
{
    for (std::size_t i = 0; i < kIndexedColorPairs.size(); ++i)
        if (value == kIndexedColorPairs[i].dim || value == kIndexedColorPairs[i].bright)
            return static_cast<int>(i);
    return -1;
}

// MK3 display button names (d1-d8 map to the 8 buttons below the two screens)
inline const std::array<std::string, 4> kLeftButtons  { "d1", "d2", "d3", "d4" };
inline const std::array<std::string, 4> kRightButtons { "d5", "d6", "d7", "d8" };

// MK3 knob names (k1-k8 map to the 8 rotary encoders above the two screens)
inline const std::array<std::string, 4> kLeftKnobs  { "k1", "k2", "k3", "k4" };
inline const std::array<std::string, 4> kRightKnobs { "k5", "k6", "k7", "k8" };

// Pad LED names (p1-p16). Held as std::string so callers can pass them
// straight into the IController API without constructing a temporary
// per call — LED refresh loops hit these 16× on every selection change
// and per-tick update.
inline const std::array<std::string, kPadCount> kPadNames {
    "p1",  "p2",  "p3",  "p4",  "p5",  "p6",  "p7",  "p8",
    "p9",  "p10", "p11", "p12", "p13", "p14", "p15", "p16"
};

} // namespace HardwareConstants
