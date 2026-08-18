#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <set>
#include <string>

extern "C"
{
#include "mk3_input_map.h"
#include "mk3_output_map.h"
}

namespace
{
const mk3_button_t* findButton(const char* name)
{
    for (int index = 0; index < mk3_buttons_count; ++index)
        if (std::string(mk3_buttons[index].name) == name)
            return &mk3_buttons[index];
    return nullptr;
}

const mk3_led_definition_t* findLed(const char* name)
{
    for (int index = 0; index < mk3_leds_count; ++index)
        if (std::string(mk3_leds[index].name) == name)
            return &mk3_leds[index];
    return nullptr;
}

TEST(Mk3HidMapParityTest, DigitalInputMapMatchesEveryDocumentedReportBit)
{
    using ByteBits = std::array<const char*, 8>;
    const std::array<ByteBits, 10> expected {{
        ByteBits {{ "navPush", nullptr, "navUp", "navRight", "navDown", "navLeft", "shift", "d8" }},
        ByteBits {{ "g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8" }},
        ByteBits {{ "notes", "volume", "swing", "tempo", "noteRepeatArp", "lock", "pedalConnected", "pedalSwitch" }},
        ByteBits {{ "padMode", "keyboard", "chords", "step", "fixedVel", "scene", "pattern", "events" }},
        ByteBits {{ "microphoneConnected", "variationNavigate", "duplicateDouble", "select", "solo", "muteChoke", "pitch", "mod" }},
        ByteBits {{ "performFxSelect", "restartLoop", "eraseReplace", "tapMetro", "followGrid", "play", "recCountIn", "stop" }},
        ByteBits {{ "macroSet", "settings", "arrowRight", "sampling", "mixer", "plugin", nullptr, nullptr }},
        ByteBits {{ "channelMidi", "arranger", "browserPlugin", "arrowLeft", "fileSave", "auto", nullptr, nullptr }},
        ByteBits {{ "d1", "d2", "d3", "d4", "d5", "d6", "d7", "navTouch" }},
        ByteBits {{ "knobTouch8", "knobTouch7", "knobTouch6", "knobTouch5", "knobTouch4", "knobTouch3", "knobTouch2", "knobTouch1" }},
    }};

    int expectedCount = 0;
    std::set<std::pair<uint8_t, uint8_t>> occupiedBits;
    for (std::size_t byte = 0; byte < expected.size(); ++byte)
    {
        for (std::size_t bit = 0; bit < expected[byte].size(); ++bit)
        {
            const char* name = expected[byte][bit];
            if (name == nullptr)
                continue;
            ++expectedCount;
            const auto* definition = findButton(name);
            ASSERT_NE(definition, nullptr) << name;
            EXPECT_EQ(definition->addr, static_cast<uint8_t>(byte + 1)) << name;
            EXPECT_EQ(definition->mask, static_cast<uint8_t>(1u << bit)) << name;
            EXPECT_TRUE(occupiedBits.emplace(definition->addr, definition->mask).second)
                << name;
        }
    }
    EXPECT_EQ(mk3_buttons_count, expectedCount);
}

TEST(Mk3HidMapParityTest, Report80LedMapMatchesEveryDocumentedByte)
{
    const std::array<const char*, 62> expected {{
        "channelMidi", "plugin", "arranger", "mixer", "browserPlugin", "sampling",
        "arrowLeft", "arrowRight", "fileSave", "settings", "auto", "macroSet",
        "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8",
        "volume", "swing", "noteRepeatArp", "tempo", "lock", "pitch", "mod",
        "performFxSelect", "notes",
        "g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8",
        "restartLoop", "eraseReplace", "tapMetro", "followGrid", "play", "recCountIn",
        "stop", "shift", "fixedVel", "padMode", "keyboard", "chords", "step", "scene",
        "pattern", "events", "variationNavigate", "duplicateDouble", "select", "solo",
        "muteChoke", "navUp", "navLeft", "navRight", "navDown"
    }};

    for (std::size_t address = 0; address < expected.size(); ++address)
    {
        const auto* definition = findLed(expected[address]);
        ASSERT_NE(definition, nullptr) << expected[address];
        EXPECT_EQ(definition->report_id, 0x80) << expected[address];
        EXPECT_EQ(definition->addr, static_cast<uint8_t>(address + 1)) << expected[address];

        const bool indexed = address == 5 || (address >= 29 && address <= 36)
            || address >= 58;
        EXPECT_EQ(definition->type,
                  indexed ? MK3_LED_TYPE_INDEXED : MK3_LED_TYPE_MONO)
            << expected[address];
    }
}

TEST(Mk3HidMapParityTest, Report81LedMapMatchesTouchstripAndPhysicalPadOrder)
{
    for (int segment = 1; segment <= 25; ++segment)
    {
        const std::string name = "ts" + std::to_string(segment);
        const auto* definition = findLed(name.c_str());
        ASSERT_NE(definition, nullptr) << name;
        EXPECT_EQ(definition->report_id, 0x81) << name;
        EXPECT_EQ(definition->addr, segment) << name;
        EXPECT_EQ(definition->type, MK3_LED_TYPE_INDEXED) << name;
    }

    const std::array<int, 16> padsInReportOrder {
        13, 14, 15, 16, 9, 10, 11, 12, 5, 6, 7, 8, 1, 2, 3, 4
    };
    for (std::size_t index = 0; index < padsInReportOrder.size(); ++index)
    {
        const std::string name = "p" + std::to_string(padsInReportOrder[index]);
        const auto* definition = findLed(name.c_str());
        ASSERT_NE(definition, nullptr) << name;
        EXPECT_EQ(definition->report_id, 0x81) << name;
        EXPECT_EQ(definition->addr, static_cast<uint8_t>(26 + index)) << name;
        EXPECT_EQ(definition->type, MK3_LED_TYPE_INDEXED) << name;
    }

    EXPECT_EQ(mk3_leds_count, 62 + 25 + 16);
}
} // namespace
