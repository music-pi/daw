#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
extern "C"
{
#include "mk3_input.h"
#include "mk3_input_map.h"
#include "mk3_internal.h"
#include "mk3_output_map.h"
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace
{
struct RecordedPad
{
    uint8_t pad {};
    bool pressed {};
    uint16_t pressure {};
};

struct RecordedButton
{
    std::string name;
    bool pressed {};
};

struct RecordedKnob
{
    std::string name;
    int16_t delta {};
    uint16_t absolute {};
};

struct RecordedStepper
{
    int8_t direction {};
    uint8_t position {};
};

struct RecordedTouchstrip
{
    uint8_t finger {};
    bool touching {};
    uint16_t position {};
};

struct ReportRecorder
{
    std::vector<RecordedPad> pads;
    std::vector<RecordedButton> buttons;
    std::vector<RecordedKnob> knobs;
    std::vector<RecordedStepper> steppers;
    std::vector<RecordedTouchstrip> touchstrip;
};

void recordPad(uint8_t pad, bool pressed, uint16_t pressure, void* user)
{
    static_cast<ReportRecorder*>(user)->pads.push_back({ pad, pressed, pressure });
}

void recordButton(const char* name, bool pressed, void* user)
{
    static_cast<ReportRecorder*>(user)->buttons.push_back({ name, pressed });
}

void recordKnob(const char* name, int16_t delta, uint16_t absolute, void* user)
{
    static_cast<ReportRecorder*>(user)->knobs.push_back({ name, delta, absolute });
}

void recordStepper(int8_t direction, uint8_t position, void* user)
{
    static_cast<ReportRecorder*>(user)->steppers.push_back({ direction, position });
}

void recordTouchstrip(uint8_t finger, bool touching, uint16_t position, void* user)
{
    static_cast<ReportRecorder*>(user)->touchstrip.push_back({ finger, touching, position });
}

std::array<uint8_t, HID_INPUT_PACKET_SIZE> report01()
{
    std::array<uint8_t, HID_INPUT_PACKET_SIZE> report {};
    report[0] = 0x01;
    return report;
}

void write12Bit(std::array<uint8_t, HID_INPUT_PACKET_SIZE>& report,
                size_t lsbAddress,
                uint16_t value)
{
    report[lsbAddress] = static_cast<uint8_t>(value & 0xff);
    report[lsbAddress + 1] = static_cast<uint8_t>((value >> 8) & 0x0f);
}

void write16Bit(std::array<uint8_t, HID_INPUT_PACKET_SIZE>& report,
                size_t lsbAddress,
                uint16_t value)
{
    report[lsbAddress] = static_cast<uint8_t>(value & 0xff);
    report[lsbAddress + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

TEST(Mk3InputReportParityTest, EveryMappedButtonEmitsOnePressAndReleaseEdge)
{
    for (int index = 0; index < mk3_buttons_count; ++index)
    {
        mk3_t device {};
        ReportRecorder recorder;
        mk3_input_set_button_callback(&device, recordButton, &recorder);

        auto report = report01();
        ASSERT_EQ(mk3_input_process_report(&device, report.data(), report.size()), 1);

        const auto& button = mk3_buttons[index];
        report[button.addr] |= button.mask;
        ASSERT_EQ(mk3_input_process_report(&device, report.data(), report.size()), 1);
        ASSERT_EQ(mk3_input_process_report(&device, report.data(), report.size()), 1);
        report[button.addr] &= static_cast<uint8_t>(~button.mask);
        ASSERT_EQ(mk3_input_process_report(&device, report.data(), report.size()), 1);

        ASSERT_EQ(recorder.buttons.size(), 2u) << button.name;
        EXPECT_EQ(recorder.buttons[0].name, button.name);
        EXPECT_TRUE(recorder.buttons[0].pressed);
        EXPECT_EQ(recorder.buttons[1].name, button.name);
        EXPECT_FALSE(recorder.buttons[1].pressed);
    }
}

TEST(Mk3InputReportParityTest, PedalBitsUseTheDocumentedReportByte)
{
    const mk3_button_t* pedalConnected = nullptr;
    const mk3_button_t* pedalSwitch = nullptr;
    for (int index = 0; index < mk3_buttons_count; ++index)
    {
        const auto& button = mk3_buttons[index];
        if (std::string(button.name) == "pedalConnected")
            pedalConnected = &button;
        else if (std::string(button.name) == "pedalSwitch")
            pedalSwitch = &button;

        EXPECT_FALSE(button.addr == 0x01 && button.mask == 0x02)
            << "report byte 1 bit 1 is reserved on the MK3";
    }

    ASSERT_NE(pedalConnected, nullptr);
    EXPECT_EQ(pedalConnected->addr, 0x03);
    EXPECT_EQ(pedalConnected->mask, 0x40);
    ASSERT_NE(pedalSwitch, nullptr);
    EXPECT_EQ(pedalSwitch->addr, 0x03);
    EXPECT_EQ(pedalSwitch->mask, 0x80);
}

TEST(Mk3InputReportParityTest, EveryRawPadCoordinateUsesBottomUpPhysicalNumbering)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_pad_callback(&device, recordPad, &recorder);

    for (uint8_t hardwareIndex = 0; hardwareIndex < 16; ++hardwareIndex)
    {
        std::array<uint8_t, HID_INPUT_PACKET_SIZE> press {};
        press[0] = 0x02;
        press[1] = hardwareIndex;
        press[2] = 0x11;
        press[3] = 0x01; // hardware press marker with pressure 257
        ASSERT_EQ(mk3_input_process_report(&device, press.data(), press.size()), 2);

        auto release = press;
        release[2] = 0x20;
        release[3] = 0;
        ASSERT_EQ(mk3_input_process_report(&device, release.data(), release.size()), 2);
    }

    ASSERT_EQ(recorder.pads.size(), 32u);
    for (size_t hardwareIndex = 0; hardwareIndex < 16; ++hardwareIndex)
    {
        const auto physicalPad = mk3_pad_hw_to_physical_map[hardwareIndex];
        const auto& press = recorder.pads[hardwareIndex * 2];
        const auto& release = recorder.pads[hardwareIndex * 2 + 1];
        EXPECT_EQ(press.pad, physicalPad);
        EXPECT_TRUE(press.pressed);
        EXPECT_EQ(press.pressure, 257);
        EXPECT_EQ(release.pad, physicalPad);
        EXPECT_FALSE(release.pressed);
        EXPECT_EQ(release.pressure, 0);
    }
}

TEST(Mk3InputReportParityTest, RawPadZeroIsDecodedInAnyPacketSlot)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_pad_callback(&device, recordPad, &recorder);

    std::array<uint8_t, HID_INPUT_PACKET_SIZE> report {};
    report[0] = 0x02;
    report[1] = 12;   // first slot: physical Pad 1
    report[2] = 0x11;
    report[3] = 0x2c; // pressure 300
    report[4] = 0;    // second slot: physical Pad 13, not a terminator
    report[5] = 0x11;
    report[6] = 0x2d; // pressure 301
    mk3_input_process_report(&device, report.data(), report.size());

    ASSERT_EQ(recorder.pads.size(), 2u);
    EXPECT_EQ(recorder.pads[0].pad, 1);
    EXPECT_EQ(recorder.pads[1].pad, 13);
    EXPECT_TRUE(recorder.pads[1].pressed);

    report.fill(0);
    report[0] = 0x02;
    report[1] = 0;    // Pad 13 release is distinguishable from an empty slot
    report[2] = 0x20;
    report[4] = 12;
    report[5] = 0x20;
    mk3_input_process_report(&device, report.data(), report.size());

    ASSERT_EQ(recorder.pads.size(), 4u);
    EXPECT_EQ(recorder.pads[2].pad, 13);
    EXPECT_FALSE(recorder.pads[2].pressed);
    EXPECT_EQ(recorder.pads[3].pad, 1);
    EXPECT_FALSE(recorder.pads[3].pressed);
}

TEST(Mk3InputReportParityTest, HardwareMarkersAloneCreatePressAndReleaseEdges)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_pad_callback(&device, recordPad, &recorder);

    std::array<uint8_t, HID_INPUT_PACKET_SIZE> report {};
    report[0] = 0x02;
    report[1] = 12; // raw coordinate for physical Pad 1
    report[2] = 0x10;
    report[3] = 0xfd; // hardware press marker
    mk3_input_process_report(&device, report.data(), report.size());
    ASSERT_EQ(recorder.pads.size(), 1u);
    EXPECT_TRUE(recorder.pads[0].pressed);
    EXPECT_EQ(recorder.pads[0].pressure, 0xfd);

    report[2] = 0x42;
    report[3] = 0x32; // physical held slot, pressure 0x232
    mk3_input_process_report(&device, report.data(), report.size());
    ASSERT_EQ(recorder.pads.size(), 1u);

    report[2] = 0x30;
    report[3] = 0x00; // physical release slot
    mk3_input_process_report(&device, report.data(), report.size());
    ASSERT_EQ(recorder.pads.size(), 2u);
    EXPECT_FALSE(recorder.pads[1].pressed);
    EXPECT_EQ(recorder.pads[1].pressure, 0);

    report[2] = 0x40;
    report[3] = 0x00; // physical trailing idle/reset slot
    mk3_input_process_report(&device, report.data(), report.size());
    EXPECT_EQ(recorder.pads.size(), 2u);

    report[2] = 0x41;
    report[3] = 0x41; // measured Pad 1 sensor tail: pressure 321
    mk3_input_process_report(&device, report.data(), report.size());
    EXPECT_EQ(recorder.pads.size(), 2u);
}

TEST(Mk3InputReportParityTest, SaturatedPacketSamplesRemainOneContinuousPress)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_pad_callback(&device, recordPad, &recorder);

    std::array<uint8_t, HID_INPUT_PACKET_SIZE> report {};
    report[0] = 0x02;
    report[1] = 3;    // raw coordinate for physical Pad 16
    report[2] = 0x40;
    report[3] = 0xa6; // pressure sample before the edge marker
    report[4] = 3;
    report[5] = 0x1f;
    report[6] = 0xff; // hardware press marker: pressure 4095
    mk3_input_process_report(&device, report.data(), report.size());

    ASSERT_EQ(recorder.pads.size(), 1u);
    EXPECT_EQ(recorder.pads[0].pad, 16);
    EXPECT_TRUE(recorder.pads[0].pressed);
    EXPECT_EQ(recorder.pads[0].pressure, 4095);

    report.fill(0);
    report[0] = 0x02;
    report[1] = 3;
    report[2] = 0x4f;
    report[3] = 0xfe; // pressure 4094 remains held
    mk3_input_process_report(&device, report.data(), report.size());
    EXPECT_EQ(recorder.pads.size(), 1u);

    report[2] = 0x30;
    report[3] = 0x00;
    mk3_input_process_report(&device, report.data(), report.size());
    ASSERT_EQ(recorder.pads.size(), 2u);
    EXPECT_FALSE(recorder.pads[1].pressed);
}

TEST(Mk3InputReportParityTest, DisplayKnobDeltaWrapsAcrossThePhysicalThousandSteps)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_knob_callback(&device, recordKnob, &recorder);

    auto report = report01();
    write12Bit(report, 12, 999); // K1 baseline
    mk3_input_process_report(&device, report.data(), report.size());
    EXPECT_TRUE(recorder.knobs.empty());

    write12Bit(report, 12, 0);
    mk3_input_process_report(&device, report.data(), report.size());
    ASSERT_EQ(recorder.knobs.size(), 1u);
    EXPECT_EQ(recorder.knobs[0].name, "k1");
    EXPECT_EQ(recorder.knobs[0].delta, 1);
    EXPECT_EQ(recorder.knobs[0].absolute, 0);

    write12Bit(report, 12, 999);
    mk3_input_process_report(&device, report.data(), report.size());
    ASSERT_EQ(recorder.knobs.size(), 2u);
    EXPECT_EQ(recorder.knobs[1].delta, -1);
    EXPECT_EQ(recorder.knobs[1].absolute, 999);
}

TEST(Mk3InputReportParityTest, EveryHardwareKnobNameIsEmittedFromItsReportField)
{
    static constexpr std::array<std::pair<const char*, size_t>, 11> Knobs {{
        { "k1", 12 }, { "k2", 14 }, { "k3", 16 }, { "k4", 18 },
        { "k5", 20 }, { "k6", 22 }, { "k7", 24 }, { "k8", 26 },
        { "micInGain", 36 }, { "headphoneVolume", 38 }, { "masterVolume", 40 }
    }};

    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_knob_callback(&device, recordKnob, &recorder);

    auto report = report01();
    for (const auto& [name, address] : Knobs)
    {
        static_cast<void>(name);
        write12Bit(report, address, address < 28 ? 500 : 1000);
    }
    mk3_input_process_report(&device, report.data(), report.size());

    for (const auto& [name, address] : Knobs)
    {
        const uint16_t nextValue = address < 28 ? 501 : 1001;
        write12Bit(report, address, nextValue);
        mk3_input_process_report(&device, report.data(), report.size());
        ASSERT_FALSE(recorder.knobs.empty());
        const auto& event = recorder.knobs.back();
        EXPECT_EQ(event.name, name);
        EXPECT_EQ(event.delta, 1);
        EXPECT_EQ(event.absolute, nextValue);
    }

    ASSERT_EQ(recorder.knobs.size(), Knobs.size());
}

TEST(Mk3InputReportParityTest, RearControlsUsePhysicalTwelveBitPositions)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_knob_callback(&device, recordKnob, &recorder);

    auto report = report01();
    write16Bit(report, 38, 0x8000);
    write16Bit(report, 40, 0xf000);
    mk3_input_process_report(&device, report.data(), report.size());

    write16Bit(report, 38, 0x8fff);
    write16Bit(report, 40, 0xffff);
    mk3_input_process_report(&device, report.data(), report.size());

    ASSERT_EQ(recorder.knobs.size(), 2u);
    EXPECT_EQ(recorder.knobs[0].name, "headphoneVolume");
    EXPECT_EQ(recorder.knobs[0].delta, 4095);
    EXPECT_EQ(recorder.knobs[0].absolute, 4095);
    EXPECT_EQ(recorder.knobs[1].name, "masterVolume");
    EXPECT_EQ(recorder.knobs[1].delta, 4095);
    EXPECT_EQ(recorder.knobs[1].absolute, 4095);
}

TEST(Mk3InputReportParityTest, StepperUsesFourBitPositionAndWrapDirection)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_stepper_callback(&device, recordStepper, &recorder);

    auto report = report01();
    report[11] = 0x0f;
    mk3_input_process_report(&device, report.data(), report.size());
    EXPECT_TRUE(recorder.steppers.empty());

    report[11] = 0x10; // upper bits ignored; position wraps to zero
    mk3_input_process_report(&device, report.data(), report.size());
    report[11] = 0x0f;
    mk3_input_process_report(&device, report.data(), report.size());

    ASSERT_EQ(recorder.steppers.size(), 2u);
    EXPECT_EQ(recorder.steppers[0].direction, 1);
    EXPECT_EQ(recorder.steppers[0].position, 0);
    EXPECT_EQ(recorder.steppers[1].direction, -1);
    EXPECT_EQ(recorder.steppers[1].position, 15);
}

TEST(Mk3InputReportParityTest, SmartStripEmitsMovementAndSingleZeroRelease)
{
    mk3_t device {};
    ReportRecorder recorder;
    mk3_input_set_touchstrip_callback(&device, recordTouchstrip, &recorder);

    auto report = report01();
    mk3_input_process_report(&device, report.data(), report.size());
    write16Bit(report, 30, 1);
    mk3_input_process_report(&device, report.data(), report.size());
    mk3_input_process_report(&device, report.data(), report.size());
    write16Bit(report, 30, 1023);
    mk3_input_process_report(&device, report.data(), report.size());
    write16Bit(report, 30, 0xffff); // unused upper bits do not extend the 10-bit range
    mk3_input_process_report(&device, report.data(), report.size());
    write16Bit(report, 30, 0);
    mk3_input_process_report(&device, report.data(), report.size());
    mk3_input_process_report(&device, report.data(), report.size());

    ASSERT_EQ(recorder.touchstrip.size(), 3u);
    EXPECT_EQ(recorder.touchstrip[0].finger, 1);
    EXPECT_TRUE(recorder.touchstrip[0].touching);
    EXPECT_EQ(recorder.touchstrip[0].position, 1);
    EXPECT_EQ(recorder.touchstrip[1].finger, 1);
    EXPECT_EQ(recorder.touchstrip[1].position, 1023);
    EXPECT_EQ(recorder.touchstrip[2].finger, 1);
    EXPECT_FALSE(recorder.touchstrip[2].touching);
    EXPECT_EQ(recorder.touchstrip[2].position, 0);
}

TEST(Mk3InputReportParityTest, PadLedAddressesUseTheSameRawCoordinatesAsInput)
{
    for (size_t hardwareIndex = 0; hardwareIndex < 16; ++hardwareIndex)
    {
        const auto physicalPad = mk3_pad_hw_to_physical_map[hardwareIndex];
        const std::string ledName = "p" + std::to_string(physicalPad);
        const mk3_led_definition_t* found = nullptr;
        for (int ledIndex = 0; ledIndex < mk3_leds_count; ++ledIndex)
        {
            if (ledName == mk3_leds[ledIndex].name)
            {
                found = &mk3_leds[ledIndex];
                break;
            }
        }

        ASSERT_NE(found, nullptr) << ledName;
        EXPECT_EQ(found->report_id, 0x81);
        EXPECT_EQ(found->addr, 26 + hardwareIndex);
        EXPECT_EQ(found->type, MK3_LED_TYPE_INDEXED);
    }
}
} // namespace
