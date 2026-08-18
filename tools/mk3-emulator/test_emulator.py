#!/usr/bin/env python3

import os
import pathlib
import re
import struct
import unittest
from types import SimpleNamespace

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")

import pygame

import emulator


class RecordingTcp:
    def __init__(self):
        self.messages = []

    def send(self, message):
        self.messages.append(message)


def unpack_message(message):
    msg_type, payload_length = struct.unpack(">BI", message[:5])
    payload = message[5:]
    if len(payload) != payload_length:
        raise AssertionError("protocol payload length mismatch")
    return msg_type, payload


class EmulatorProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        pygame.init()

    @classmethod
    def tearDownClass(cls):
        pygame.quit()

    def test_touchstrip_packet_matches_cpp_protocol(self):
        msg_type, payload = unpack_message(
            emulator.pack_touchstrip(1, True, 0x03EF))

        self.assertEqual(msg_type, emulator.MSG_TOUCHSTRIP)
        self.assertEqual(payload, bytes((1, 1, 0x03, 0xEF)))

    def test_connection_snapshot_matches_complete_hardware_report_state(self):
        state = emulator.EmulatorState()
        offline_tcp = emulator.TcpServer.__new__(emulator.TcpServer)
        offline_tcp.state = state
        offline_tcp.send(emulator.pack_button("play", True))
        offline_tcp.send(emulator.pack_pad(
            13, emulator.PAD_MIN_PRESSURE, True))
        offline_tcp.send(emulator.pack_knob("k1", 7, 507))
        offline_tcp.send(emulator.pack_stepper(-1, 15))
        offline_tcp.send(emulator.pack_touchstrip(1, True, 0x03EF))

        decoded = [unpack_message(packet)
                   for packet in state.input_snapshot_packets()]
        payloads_by_type = {}
        for msg_type, payload in decoded:
            payloads_by_type.setdefault(msg_type, []).append(payload)

        self.assertEqual(
            payloads_by_type[emulator.MSG_BUTTON],
            [bytes((4,)) + b"play" + bytes((1,))])
        self.assertEqual(
            payloads_by_type[emulator.MSG_PAD],
            [bytes((13, 1, 0x01, 0x01))])
        self.assertEqual(len(payloads_by_type[emulator.MSG_KNOB]), 11)
        k1_payload = next(
            payload for payload in payloads_by_type[emulator.MSG_KNOB]
            if payload[1:3] == b"k1")
        self.assertEqual(k1_payload, bytes((2,)) + b"k1" + bytes((0, 0, 1, 251)))
        self.assertEqual(
            payloads_by_type[emulator.MSG_STEPPER], [bytes((1, 15))])
        self.assertEqual(
            payloads_by_type[emulator.MSG_TOUCHSTRIP],
            [bytes((1, 1, 0x03, 0xEF))])

    def test_display_decoder_reconstructs_rgb565_frames(self):
        state = emulator.EmulatorState()
        red_pixel_big_endian = bytes((0xF8, 0x00))
        state.update_display(
            1, red_pixel_big_endian * (emulator.DISPLAY_W * emulator.DISPLAY_H))

        self.assertEqual(state.display_surfaces[1].get_at((0, 0))[:3], (248, 0, 0))
        self.assertEqual(state.display_surfaces[1].get_at((479, 271))[:3], (248, 0, 0))

    def test_hardware_knobs_use_their_documented_raw_ranges(self):
        state = emulator.EmulatorState()
        expected = set(emulator.KNOB_NAMES + emulator.UTILITY_KNOB_NAMES)

        self.assertEqual(set(state.knob_positions), expected)
        self.assertEqual(state.apply_knob_delta("masterVolume", 99999), 4095)
        self.assertEqual(state.apply_knob_delta("masterVolume", -99999), 0)
        actual_delta, absolute = state.apply_knob_input("masterVolume", -1)
        self.assertEqual((actual_delta, absolute), (0, 0))
        state.knob_positions["k1"] = 999
        self.assertEqual(state.apply_knob_delta("k1", 1), 0)
        state.knob_positions["k1"] = 500
        actual_delta, absolute = state.apply_knob_input("k1", 499)
        self.assertEqual((actual_delta, absolute), (499, 999))
        _, utility_payload = unpack_message(
            emulator.pack_knob("headphoneVolume", 1, 4095))
        self.assertEqual(struct.unpack(">hH", utility_payload[-4:]), (1, 4095))

    def test_pad_pressure_and_stepper_position_match_hardware_reports(self):
        _, pad_payload = unpack_message(
            emulator.pack_pad(1, emulator.PAD_MAX_PRESSURE))
        self.assertEqual(pad_payload, bytes((1, 1, 0x0F, 0xFF)))

        with self.assertRaises(ValueError):
            emulator.pack_pad(1, 0, True)

        state = emulator.EmulatorState()
        state.stepper_position = 15
        position = state.apply_stepper_delta(1)
        _, stepper_payload = unpack_message(emulator.pack_stepper(1, position))
        self.assertEqual(stepper_payload, bytes((1, 0)))

        position = state.apply_stepper_delta(-1)
        _, stepper_payload = unpack_message(emulator.pack_stepper(-1, position))
        self.assertEqual(stepper_payload, bytes((0xFF, 15)))

    def test_midi_velocity_range_always_emits_hardware_pad_presses(self):
        tcp = RecordingTcp()
        midi = emulator.MidiInput.__new__(emulator.MidiInput)
        midi.tcp = tcp
        midi.channel = 0

        midi._on_message(SimpleNamespace(
            type="note_on", channel=0, note=36, velocity=1))
        _, low_payload = unpack_message(tcp.messages[-1])
        low_pad, low_pressed, low_pressure = struct.unpack(">BBH", low_payload)
        self.assertEqual(low_pad, 1)
        self.assertEqual(low_pressed, 1)
        self.assertEqual(low_pressure, emulator.PAD_MIN_PRESSURE)

        midi._on_message(SimpleNamespace(
            type="note_on", channel=0, note=51, velocity=127))
        _, high_payload = unpack_message(tcp.messages[-1])
        high_pad, high_pressed, high_pressure = struct.unpack(">BBH", high_payload)
        self.assertEqual(high_pad, 16)
        self.assertEqual(high_pressed, 1)
        self.assertEqual(high_pressure, emulator.PAD_MAX_PRESSURE)

    def test_gui_exposes_utility_knobs_pedal_bits_and_single_touchstrip(self):
        state = emulator.EmulatorState()
        tcp = RecordingTcp()
        gui = emulator.EmulatorGui(state, tcp)
        surface = pygame.Surface((gui.win_w, gui.win_h))
        gui.draw(surface)

        self.assertTrue(set(emulator.UTILITY_KNOB_NAMES).issubset(gui.knob_rects))
        self.assertEqual(
            set(gui.sensor_rects),
            {"microphoneConnected", "pedalConnected", "pedalSwitch"})
        self.assertEqual(len(gui.touchstrip_rects), 25)

        first_segment = next(iter(gui.touchstrip_rects.values())).center
        gui.handle_event(pygame.event.Event(
            pygame.MOUSEBUTTONDOWN, {"button": 1, "pos": first_segment}))
        self.assertEqual(gui.held_touchstrip_fingers, {1})

        gui.handle_event(pygame.event.Event(
            pygame.MOUSEBUTTONUP, {"button": 1, "pos": first_segment}))
        self.assertEqual(gui.held_touchstrip_fingers, set())

        decoded = [unpack_message(message) for message in tcp.messages]
        self.assertEqual([entry[0] for entry in decoded], [
            emulator.MSG_TOUCHSTRIP,
            emulator.MSG_TOUCHSTRIP,
        ])
        self.assertEqual(decoded[-1][1], bytes((1, 0, 0, 0)))

    def test_touchstrip_input_has_full_continuous_hardware_range(self):
        state = emulator.EmulatorState()
        tcp = RecordingTcp()
        gui = emulator.EmulatorGui(state, tcp)

        left = (gui.touchstrip_bounds.left, gui.touchstrip_bounds.centery)
        right = (gui.touchstrip_bounds.right - 1, gui.touchstrip_bounds.centery)
        self.assertEqual(gui._touchstrip_position(left), 1)
        self.assertEqual(
            gui._touchstrip_position(right), emulator.TOUCHSTRIP_MAX_POSITION)

        first = (gui.touchstrip_bounds.left + 2, gui.touchstrip_bounds.centery)
        second = (gui.touchstrip_bounds.left + 3, gui.touchstrip_bounds.centery)
        self.assertNotEqual(
            gui._touchstrip_position(first),
            gui._touchstrip_position(second))

    def test_edit_zone_and_rear_knobs_follow_physical_layout(self):
        gui = emulator.EmulatorGui(emulator.EmulatorState(), RecordingTcp())

        volume = gui.button_rects["volume"]
        swing = gui.button_rects["swing"]
        tempo = gui.button_rects["tempo"]
        note_repeat = gui.button_rects["noteRepeatArp"]
        lock = gui.button_rects["lock"]

        self.assertLess(gui.nav_center[0], volume.left)
        self.assertEqual(volume.left, swing.left)
        self.assertEqual(swing.left, tempo.left)
        self.assertLess(volume.top, swing.top)
        self.assertLess(swing.top, tempo.top)
        self.assertGreater(note_repeat.left, volume.right)
        self.assertEqual(note_repeat.left, lock.left)
        self.assertLess(note_repeat.top, lock.top)

        rear_knob_centers = [
            gui.knob_rects[name].centerx
            for name in emulator.REAR_PANEL_KNOB_LAYOUT
        ]
        self.assertEqual(rear_knob_centers, sorted(rear_knob_centers))
        self.assertGreater(
            gui.sensor_rects["microphoneConnected"].left,
            gui.knob_rects["micInGain"].right)
        self.assertGreater(
            gui.sensor_rects["pedalConnected"].left,
            gui.sensor_rects["microphoneConnected"].right)
        self.assertEqual(
            gui.sensor_rects["pedalConnected"].left,
            gui.sensor_rects["pedalSwitch"].left)

    def test_pad_grid_uses_hardware_bottom_to_top_numbering(self):
        state = emulator.EmulatorState()
        tcp = RecordingTcp()
        gui = emulator.EmulatorGui(state, tcp)

        pads_in_visual_order = [
            pad_num for pad_num, _ in sorted(
                gui.pad_rects.items(), key=lambda entry: (
                    entry[1].top, entry[1].left))
        ]
        self.assertEqual(pads_in_visual_order, [
            13, 14, 15, 16,
             9, 10, 11, 12,
             5,  6,  7,  8,
             1,  2,  3,  4,
        ])

        gui.handle_event(pygame.event.Event(
            pygame.MOUSEBUTTONDOWN,
            {"button": 1, "pos": gui.pad_rects[13].center}))
        msg_type, payload = unpack_message(tcp.messages[-1])
        self.assertEqual(msg_type, emulator.MSG_PAD)
        self.assertEqual(payload, bytes((13, 1, 0x0F, 0xA0)))

        gui.handle_event(pygame.event.Event(
            pygame.MOUSEBUTTONUP,
            {"button": 1, "pos": gui.pad_rects[13].center}))
        gui.handle_event(pygame.event.Event(
            pygame.MOUSEBUTTONDOWN,
            {"button": 1, "pos": gui.pad_rects[1].center}))
        _, payload = unpack_message(tcp.messages[-1])
        self.assertEqual(payload, bytes((1, 1, 0x0F, 0xA0)))

    def test_pad_leds_use_the_same_physical_index_as_pad_events(self):
        state = emulator.EmulatorState()
        color_index = 78
        expected_color = emulator.led_index_to_rgb(color_index)

        state.set_led_indexed("p1", color_index)
        self.assertEqual(state.get_pad_color(0), expected_color)
        self.assertEqual(state.get_pad_color(12), emulator.PAD_EMPTY)

        state.set_led_indexed("p16", color_index)
        self.assertEqual(state.get_pad_color(15), expected_color)

    def test_indexed_white_tiers_and_group_off_state_are_visually_distinct(self):
        whites = [emulator.led_index_to_rgb(index) for index in range(76, 79)]
        self.assertEqual(len(set(whites)), 3)
        self.assertEqual(
            [sum(color) for color in whites],
            sorted(sum(color) for color in whites))

        state = emulator.EmulatorState()
        self.assertEqual(state.get_group_color(0), emulator.GROUP_EMPTY)
        state.set_led_indexed("g1", 78)
        self.assertNotEqual(state.get_group_color(0), emulator.GROUP_EMPTY)
        state.set_led_indexed("g1", 0)
        self.assertEqual(state.get_group_color(0), emulator.GROUP_EMPTY)

    def test_indexed_palette_uses_physical_sixteen_hue_four_tier_layout(self):
        mid_tier = [emulator.led_index_to_rgb(4 + hue * 4 + 2)
                    for hue in range(16)]
        self.assertEqual(len(set(mid_tier)), 16)

        for hue in range(16):
            tiers = [emulator.led_index_to_rgb(4 + hue * 4 + tier)
                     for tier in range(4)]
            self.assertEqual(
                [max(color) for color in tiers],
                sorted(max(color) for color in tiers))

        green_brightest = emulator.led_index_to_rgb(31)
        mint_mid = emulator.led_index_to_rgb(34)
        turquoise_mid = emulator.led_index_to_rgb(38)
        self.assertGreater(green_brightest[1], green_brightest[2])
        self.assertGreater(mint_mid[1], mint_mid[2])
        self.assertEqual(turquoise_mid[1], turquoise_mid[2])

    def test_keyboard_navigation_matches_hardware_stepper_direction(self):
        state = emulator.EmulatorState()
        tcp = RecordingTcp()
        gui = emulator.EmulatorGui(state, tcp)

        gui.handle_event(pygame.event.Event(
            pygame.KEYDOWN, {"key": pygame.K_UP}))
        msg_type, payload = unpack_message(tcp.messages[-1])
        self.assertEqual(msg_type, emulator.MSG_STEPPER)
        self.assertEqual(payload, bytes((0xFF, 15)))

        gui.handle_event(pygame.event.Event(
            pygame.KEYDOWN, {"key": pygame.K_DOWN}))
        _, payload = unpack_message(tcp.messages[-1])
        self.assertEqual(payload, bytes((1, 0)))

    def test_gui_surface_covers_every_hardware_input_and_led(self):
        repo = pathlib.Path(__file__).resolve().parents[2]
        input_map = (repo / "external/mk3/mk3_input_map.c").read_text()
        input_driver = (repo / "external/mk3/mk3_input.c").read_text()
        output_map = (repo / "external/mk3/mk3_output_map.c").read_text()

        button_entries = re.findall(
            r'\{"([^"]+)",\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\}',
            input_map)
        hardware_buttons = {name for name, _, _ in button_entries}
        self.assertEqual(len(hardware_buttons), len(button_entries))
        self.assertEqual(
            len({(address, mask) for _, address, mask in button_entries}),
            len(button_entries),
            "two MK3 inputs share the same report bit")
        knob_block = input_driver.split(
            "static const mk3_knob_descriptor_t knob_descriptors[] = {", 1
        )[1].split("};", 1)[0]
        hardware_knobs = set(re.findall(r'\{"([^"]+)"', knob_block))
        exported_knob_block = input_map.split(
            "const char* const mk3_knob_names[] = {", 1
        )[1].split("};", 1)[0]
        exported_knobs = set(re.findall(r'"([^"]+)"', exported_knob_block))

        state = emulator.EmulatorState()
        gui = emulator.EmulatorGui(state, RecordingTcp())
        gui_buttons = set(gui.button_rects) | set(gui.d_rects) | set(gui.sensor_rects)
        gui_buttons |= {"navPush", "navTouch", "navUp", "navRight", "navDown", "navLeft"}
        gui_buttons |= {f"knobTouch{i}" for i in range(1, 9)}

        self.assertEqual(gui_buttons, hardware_buttons)
        self.assertEqual(emulator.HARDWARE_BUTTON_NAMES, hardware_buttons)
        self.assertEqual(set(gui.knob_rects), hardware_knobs)
        self.assertEqual(exported_knobs, hardware_knobs)

        led_entries = re.findall(
            r'\{"([^"]+)",\s*(0x[0-9A-Fa-f]+),\s*(\d+),\s*'
            r'(MK3_LED_TYPE_MONO|MK3_LED_TYPE_INDEXED)\}',
            output_map)
        hardware_mono = {name for name, _, _, kind in led_entries if kind.endswith("MONO")}
        hardware_indexed = {name for name, _, _, kind in led_entries if kind.endswith("INDEXED")}
        self.assertEqual(
            len({(report, address) for _, report, address, _ in led_entries}),
            len(led_entries),
            "two MK3 LEDs share the same output-report byte")

        rendered_mono = (set(gui.button_rects) | set(gui.d_rects)) - {
            "sampling", *gui.group_rects.keys()
        }
        rendered_indexed = (
            {f"p{i}" for i in range(1, 17)}
            | set(gui.group_rects)
            | set(gui.touchstrip_rects)
            | {"sampling", "navUp", "navRight", "navDown", "navLeft"}
        )
        self.assertEqual(rendered_mono, hardware_mono)
        self.assertEqual(rendered_indexed, hardware_indexed)

    def test_pad_input_output_maps_share_hardware_coordinates(self):
        repo = pathlib.Path(__file__).resolve().parents[2]
        input_map = (repo / "external/mk3/mk3_input_map.c").read_text()
        output_map = (repo / "external/mk3/mk3_output_map.c").read_text()

        map_block = input_map.split(
            "const uint8_t mk3_pad_hw_to_physical_map[16] = {", 1
        )[1].split("};", 1)[0]
        # Strip the hardware-index ranges and physical-pad values from comments
        # by parsing only the source portion before each // marker.
        hardware_to_pad = []
        for line in map_block.splitlines():
            source = line.split("//", 1)[0]
            hardware_to_pad.extend(int(value) for value in re.findall(r"\d+", source))
        self.assertEqual(hardware_to_pad, [
            13, 14, 15, 16,
             9, 10, 11, 12,
             5,  6,  7,  8,
             1,  2,  3,  4,
        ])

        pad_led_addresses = {
            int(pad): int(address)
            for pad, address in re.findall(
                r'\{"p(\d+)",\s*0x81,\s*(\d+),\s*MK3_LED_TYPE_INDEXED\}',
                output_map)
        }
        self.assertEqual(len(pad_led_addresses), 16)
        for hardware_index, physical_pad in enumerate(hardware_to_pad):
            self.assertEqual(
                pad_led_addresses[physical_pad],
                26 + hardware_index,
                f"pad {physical_pad} input and LED coordinates diverged")

    def test_protocol_rejects_controls_the_hardware_cannot_emit(self):
        with self.assertRaises(ValueError):
            emulator.pack_button("imaginaryButton", True)
        with self.assertRaises(ValueError):
            emulator.pack_button("play", 1)
        with self.assertRaises(ValueError):
            emulator.pack_pad(17, 4000)
        with self.assertRaises(ValueError):
            emulator.pack_pad(1, "loud")
        with self.assertRaises(ValueError):
            emulator.pack_pad(1, emulator.PAD_MAX_PRESSURE + 1)
        with self.assertRaises(ValueError):
            emulator.pack_pad(1, emulator.PAD_MIN_PRESSURE - 1, True)
        with self.assertRaises(ValueError):
            emulator.pack_touchstrip(1, True, emulator.TOUCHSTRIP_MAX_POSITION + 1)
        with self.assertRaises(ValueError):
            emulator.pack_knob("imaginaryKnob", 1, 1)
        with self.assertRaises(ValueError):
            emulator.pack_stepper(0, 0)
        with self.assertRaises(ValueError):
            emulator.pack_touchstrip(2, True, 100)
        with self.assertRaises(ValueError):
            emulator.pack_touchstrip(1, False, 100)
        with self.assertRaises(ValueError):
            emulator.pack_touchstrip(1, 1, 100)


if __name__ == "__main__":
    unittest.main()
