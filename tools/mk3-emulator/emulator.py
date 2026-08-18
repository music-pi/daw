#!/usr/bin/env python3
"""
MK3 Hardware Emulator

Emulates the Native Instruments Maschine MK3 controller.
Connects to MusicPI app via TCP socket on port 9999.
HTTP automation API on port 9998.

Usage:
  python3 emulator.py [--port PORT] [--api-port API_PORT]
"""

import pygame
import socket
import struct
import json
import sys
import os
import time
import threading
import io
import argparse
import colorsys
from http.server import HTTPServer, BaseHTTPRequestHandler

try:
    import mido
    HAS_MIDO = True
except ImportError:
    HAS_MIDO = False

# ── Protocol constants ────────────────────────────────────────────────

MSG_BUTTON  = 0x01
MSG_PAD     = 0x02
MSG_KNOB    = 0x03
MSG_STEPPER = 0x04
MSG_TOUCHSTRIP = 0x05
MSG_DISPLAY     = 0x10
MSG_LED_MONO    = 0x11
MSG_LED_INDEXED = 0x12

DISPLAY_W, DISPLAY_H = 480, 272
PAD_MIN_PRESSURE = 0x0101
PAD_MAX_PRESSURE = 0x0FFF
TOUCHSTRIP_MAX_POSITION = 0x03FF

# ── LED color mapping (matches UiTheme.h ledIndexToColour) ───────────

def led_index_to_rgb(index):
    if 4 <= index <= 67:
        offset = index - 4
        hue = (offset // 4) / 16.0
        brightness = 0.55 + (offset % 4) * 0.15
        red, green, blue = colorsys.hsv_to_rgb(hue, 0.75, brightness)
        return (int(red * 255), int(green * 255), int(blue * 255))
    return {
        76: (92, 86, 76),
        77: (170, 160, 145),
        78: (255, 244, 220),
    }.get(index, PAD_EMPTY)

# ── MK3 physical layout ──────────────────────────────────────────────

D_BUTTONS = ["d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8"]

LEFT_COL = [
    ("channelMidi", "plugin"),
    ("arranger", "mixer"),
    ("browserPlugin", "sampling"),
    ("arrowLeft", "arrowRight"),
    ("fileSave", "settings"),
    ("auto", "macroSet"),
]

MODE_ROW = ["fixedVel", "padMode", "keyboard", "chords", "step"]

ENCODER_MODE_BUTTONS = ["volume", "swing", "tempo"]
PERFORMANCE_BUTTONS = ["noteRepeatArp", "lock"]

BELOW_NAV = ["pitch", "mod", "performFxSelect", "notes"]

GROUPS = [["g1", "g2", "g3", "g4"], ["g5", "g6", "g7", "g8"]]

PAD_SIDE_COL = ["scene", "pattern", "events", "variationNavigate",
                "duplicateDouble", "select", "solo", "muteChoke"]

TRANSPORT = [
    ["restartLoop", "eraseReplace", "tapMetro", "followGrid"],
    ["play", "recCountIn", "stop", "shift"],
]

KNOB_NAMES = ["k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8"]
UTILITY_KNOB_NAMES = ["micInGain", "headphoneVolume", "masterVolume"]
ALL_KNOB_NAMES = KNOB_NAMES + UTILITY_KNOB_NAMES
KNOB_MAX_VALUES = {
    **{name: 999 for name in KNOB_NAMES},
    "micInGain": 4095,
    "headphoneVolume": 4095,
    "masterVolume": 4095,
}
REAR_PANEL_KNOB_LAYOUT = ["headphoneVolume", "masterVolume", "micInGain"]

LABELS = {
    "channelMidi": "CHANNEL", "plugin": "PLUG-IN",
    "arranger": "ARRANGER", "mixer": "MIXER",
    "browserPlugin": "BROWSER", "sampling": "SAMPLE",
    "arrowLeft": "◀", "arrowRight": "▶",
    "fileSave": "FILE", "settings": "SETTINGS",
    "auto": "AUTO", "macroSet": "MACRO",
    "fixedVel": "FIX VEL", "padMode": "PAD MODE",
    "keyboard": "KEYS", "chords": "CHORDS", "step": "STEP",
    "volume": "VOLUME", "noteRepeatArp": "NOTE RP",
    "swing": "SWING", "lock": "LOCK", "tempo": "TEMPO",
    "pitch": "PITCH", "mod": "MOD",
    "performFxSelect": "PERFORM", "notes": "NOTES",
    "scene": "SCENE", "pattern": "PATTERN", "events": "EVENTS",
    "variationNavigate": "VAR/NAV", "duplicateDouble": "DUP",
    "select": "SELECT", "solo": "SOLO", "muteChoke": "MUTE",
    "restartLoop": "RESTART", "eraseReplace": "ERASE",
    "tapMetro": "TAP", "followGrid": "FOLLOW",
    "play": "▶ PLAY", "recCountIn": "● REC",
    "stop": "■ STOP", "shift": "SHIFT",
    "g1": "A", "g2": "B", "g3": "C", "g4": "D",
    "g5": "E", "g6": "F", "g7": "G", "g8": "H",
    "micInGain": "MIC GAIN", "headphoneVolume": "PHONES", "masterVolume": "MASTER",
    "microphoneConnected": "MIC IN", "pedalConnected": "PEDAL TIP",
    "pedalSwitch": "PEDAL RING",
}

KEY_TO_BUTTON = {
    pygame.K_SPACE: "play", pygame.K_s: "stop", pygame.K_r: "recCountIn",
    pygame.K_LSHIFT: "shift", pygame.K_RSHIFT: "shift",
    pygame.K_p: "pattern", pygame.K_m: "mixer",
    pygame.K_a: "arranger", pygame.K_b: "browserPlugin",
    pygame.K_RETURN: "navPush", pygame.K_CAPSLOCK: "select",
    pygame.K_F1: "d1", pygame.K_F2: "d2", pygame.K_F3: "d3", pygame.K_F4: "d4",
    pygame.K_F5: "d5", pygame.K_F6: "d6", pygame.K_F7: "d7", pygame.K_F8: "d8",
}

KEY_TO_PAD = {
    pygame.K_1: 0, pygame.K_2: 1, pygame.K_3: 2, pygame.K_4: 3,
    pygame.K_5: 4, pygame.K_6: 5, pygame.K_7: 6, pygame.K_8: 7,
    pygame.K_q: 8, pygame.K_w: 9, pygame.K_e: 10, pygame.K_t: 11,
    pygame.K_y: 12, pygame.K_u: 13, pygame.K_i: 14, pygame.K_o: 15,
}

HARDWARE_BUTTON_NAMES = set(D_BUTTONS + MODE_ROW + PAD_SIDE_COL + BELOW_NAV)
HARDWARE_BUTTON_NAMES.update(name for pair in LEFT_COL for name in pair if name)
HARDWARE_BUTTON_NAMES.update(ENCODER_MODE_BUTTONS)
HARDWARE_BUTTON_NAMES.update(PERFORMANCE_BUTTONS)
HARDWARE_BUTTON_NAMES.update(name for row in GROUPS for name in row)
HARDWARE_BUTTON_NAMES.update(name for row in TRANSPORT for name in row)
HARDWARE_BUTTON_NAMES.update({
    "navPush", "navTouch", "navUp", "navRight", "navDown", "navLeft",
    "pedalConnected", "pedalSwitch", "microphoneConnected",
})
HARDWARE_BUTTON_NAMES.update(f"knobTouch{i}" for i in range(1, 9))

# ── Colors ────────────────────────────────────────────────────────────

BG_COLOR = (20, 22, 26)
BTN_OFF = (50, 55, 62)
BTN_PRESSED = (180, 190, 210)
PAD_EMPTY = (45, 55, 63)

# MK3 pad events and LED names (p1-p16) use the same bottom-to-top numbering:
#   p13 p14 p15 p16   (top row)
#   p9  p10 p11 p12
#   p5  p6  p7  p8
#   p1  p2  p3  p4    (bottom row)
TEXT_COLOR = (200, 200, 200)
TEXT_DIM = (130, 135, 145)
KNOB_BG = (40, 44, 50)
KNOB_FG = (120, 130, 145)
SCREEN_BORDER = (60, 65, 72)
GROUP_EMPTY = (35, 42, 50)

# ── Emulator state ───────────────────────────────────────────────────

class EmulatorState:
    def __init__(self):
        self.lock = threading.Lock()
        self.client_socket = None
        self.connected = False

        # Dual screens (0=left, 1=right)
        self.display_surfaces = [
            pygame.Surface((DISPLAY_W, DISPLAY_H)),
            pygame.Surface((DISPLAY_W, DISPLAY_H)),
        ]
        for s in self.display_surfaces:
            s.fill((0, 0, 0))
        self.display_dirty = True

        self.led_brightness = {}
        self.led_indexed = {}
        self.pad_colors = [PAD_EMPTY] * 16
        self.group_colors = [GROUP_EMPTY] * 8
        self.knob_positions = {
            name: (KNOB_MAX_VALUES[name] + 1) // 2 for name in ALL_KNOB_NAMES
        }
        self.stepper_position = 0
        self.button_inputs = {name: False for name in HARDWARE_BUTTON_NAMES}
        self.pad_pressures = [0] * 16
        self.pad_pressed = [False] * 16
        self.touchstrip_positions = [0]

        self.recording = False
        self.record_dir = None
        self.record_frame_count = 0
        self.record_start_time = 0

    def reset_led_state(self):
        """Clear all LED state — called on new connection so stale state doesn't persist."""
        with self.lock:
            self.led_brightness.clear()
            self.led_indexed.clear()
            self.pad_colors = [PAD_EMPTY] * 16
            self.group_colors = [GROUP_EMPTY] * 8

    def get_button_brightness(self, name):
        with self.lock:
            return self.led_brightness.get(name, 0)

    def get_pad_color(self, index):
        with self.lock:
            return self.pad_colors[index] if 0 <= index < 16 else PAD_EMPTY

    def get_group_color(self, index):
        with self.lock:
            return self.group_colors[index] if 0 <= index < 8 else GROUP_EMPTY

    def get_indexed_color(self, name):
        with self.lock:
            return led_index_to_rgb(self.led_indexed.get(name, 0))

    def get_button_input(self, name):
        with self.lock:
            return self.button_inputs.get(name, False)

    def get_pad_pressure(self, index):
        with self.lock:
            return self.pad_pressures[index] if 0 <= index < 16 else 0

    def get_pad_pressed(self, index):
        with self.lock:
            return self.pad_pressed[index] if 0 <= index < 16 else False

    def set_led_brightness(self, name, brightness):
        with self.lock:
            self.led_brightness[name] = brightness

    def set_led_indexed(self, name, color_index):
        with self.lock:
            self.led_indexed[name] = color_index
            rgb = led_index_to_rgb(color_index)
            for i in range(16):
                if name == f"p{i+1}":
                    self.pad_colors[i] = rgb
            for i in range(8):
                if name == f"g{i+1}":
                    self.group_colors[i] = GROUP_EMPTY if color_index < 4 else rgb

    def apply_knob_input(self, name, requested_delta):
        """Apply a virtual movement and return the HID callback delta/value."""
        with self.lock:
            previous = self.knob_positions.get(
                name, (KNOB_MAX_VALUES[name] + 1) // 2)
            if name in KNOB_NAMES:
                pos = (previous + requested_delta) % 1000
            else:
                pos = max(0, min(KNOB_MAX_VALUES[name], previous + requested_delta))
            self.knob_positions[name] = pos
            actual_delta = pos - previous
            if name in KNOB_NAMES and actual_delta > 500:
                actual_delta -= 1000
            elif name in KNOB_NAMES and actual_delta < -500:
                actual_delta += 1000
            return actual_delta, pos

    def apply_knob_delta(self, name, delta):
        """Update a knob and return its raw hardware-report position."""
        return self.apply_knob_input(name, delta)[1]

    def apply_stepper_delta(self, direction):
        """Advance the MK3's 4-bit navigation encoder position."""
        with self.lock:
            self.stepper_position = (self.stepper_position + (1 if direction > 0 else -1)) % 16
            return self.stepper_position

    def record_input_packet(self, packet):
        """Track virtual hardware state even while the application is offline."""
        if len(packet) < 5:
            return
        msg_type, payload_length = struct.unpack(">BI", packet[:5])
        payload = packet[5:]
        if len(payload) != payload_length:
            return

        with self.lock:
            if msg_type == MSG_BUTTON and len(payload) >= 2:
                name_length = payload[0]
                if len(payload) == name_length + 2:
                    name = payload[1:1 + name_length].decode("ascii", errors="ignore")
                    if name in self.button_inputs and payload[-1] in (0, 1):
                        self.button_inputs[name] = payload[-1] != 0
            elif msg_type == MSG_PAD and len(payload) == 4:
                pad, pressed, pressure = struct.unpack(">BBH", payload)
                if (1 <= pad <= 16 and pressed in (0, 1)
                        and pressure <= PAD_MAX_PRESSURE
                        and (not pressed or pressure >= PAD_MIN_PRESSURE)):
                    self.pad_pressed[pad - 1] = bool(pressed)
                    self.pad_pressures[pad - 1] = pressure
            elif msg_type == MSG_KNOB and len(payload) >= 5:
                name_length = payload[0]
                if len(payload) == name_length + 5:
                    name = payload[1:1 + name_length].decode("ascii", errors="ignore")
                    _, absolute = struct.unpack(">hH", payload[1 + name_length:])
                    if (name in self.knob_positions
                            and absolute <= KNOB_MAX_VALUES[name]):
                        self.knob_positions[name] = absolute
            elif msg_type == MSG_STEPPER and len(payload) == 2:
                _, position = struct.unpack(">bB", payload)
                if position <= 15:
                    self.stepper_position = position
            elif msg_type == MSG_TOUCHSTRIP and len(payload) == 4:
                finger, touching, position = struct.unpack(">BBH", payload)
                if (finger == 1 and touching in (0, 1)
                        and bool(touching) == (position != 0)):
                    self.touchstrip_positions[finger - 1] = position

    def input_snapshot_packets(self):
        """Build the initial report state seen when hardware is connected."""
        with self.lock:
            pressed_buttons = sorted(
                name for name, pressed in self.button_inputs.items() if pressed)
            pad_pressures = list(self.pad_pressures)
            pad_pressed = list(self.pad_pressed)
            knob_positions = dict(self.knob_positions)
            stepper_position = self.stepper_position
            touchstrip_positions = list(self.touchstrip_positions)

        packets = [pack_button(name, True) for name in pressed_buttons]
        packets.extend(
            pack_pad(index + 1, pressure, True)
            for index, pressure in enumerate(pad_pressures) if pad_pressed[index])
        packets.extend(
            pack_knob(name, 0, knob_positions[name]) for name in ALL_KNOB_NAMES)
        packets.append(pack_stepper(1, stepper_position))
        packets.extend(
            pack_touchstrip(finger + 1, position != 0, position)
            for finger, position in enumerate(touchstrip_positions))
        return packets

    def update_display(self, screen_index, rgb565_data):
        if screen_index not in (0, 1):
            return
        with self.lock:
            surface = self.display_surfaces[screen_index]
            pixels = pygame.PixelArray(surface)
            offset = 0
            for y in range(DISPLAY_H):
                for x in range(DISPLAY_W):
                    if offset + 1 < len(rgb565_data):
                        # Match the MK3 bulk display stream's big-endian RGB565 pixels.
                        val = (rgb565_data[offset] << 8) | rgb565_data[offset + 1]
                        r = ((val >> 11) & 0x1F) << 3
                        g = ((val >> 5) & 0x3F) << 2
                        b = (val & 0x1F) << 3
                        pixels[x, y] = (r, g, b)
                    offset += 2
            del pixels
            self.display_dirty = True

            if self.recording and self.record_dir:
                combined = pygame.Surface((DISPLAY_W * 2, DISPLAY_H))
                combined.blit(self.display_surfaces[0], (0, 0))
                combined.blit(self.display_surfaces[1], (DISPLAY_W, 0))
                path = os.path.join(self.record_dir, f"frame_{self.record_frame_count:06d}.png")
                pygame.image.save(combined, path)
                self.record_frame_count += 1

    def get_screenshot_png(self):
        with self.lock:
            combined = pygame.Surface((DISPLAY_W * 2, DISPLAY_H))
            combined.blit(self.display_surfaces[0], (0, 0))
            combined.blit(self.display_surfaces[1], (DISPLAY_W, 0))
            buf = io.BytesIO()
            pygame.image.save(combined, buf, "screenshot.png")
            return buf.getvalue()

    def get_state_json(self):
        with self.lock:
            return {
                "connected": self.connected,
                "led_brightness": dict(self.led_brightness),
                "led_indexed": dict(self.led_indexed),
                "pad_colors": [list(c) for c in self.pad_colors],
                "recording": self.recording,
            }

# ── Protocol helpers ─────────────────────────────────────────────────

def pack_message(msg_type, payload):
    return struct.pack(">BI", msg_type, len(payload)) + payload

def pack_button(name, pressed):
    if name not in HARDWARE_BUTTON_NAMES:
        raise ValueError(f"unknown MK3 button: {name}")
    if not isinstance(pressed, bool):
        raise ValueError("MK3 button state must be boolean")
    nb = name.encode("ascii")
    return pack_message(MSG_BUTTON, struct.pack(">B", len(nb)) + nb + struct.pack(">B", 1 if pressed else 0))

def pack_pad(index, pressure, pressed=None):
    if not isinstance(index, int) or isinstance(index, bool) or not 1 <= index <= 16:
        raise ValueError(f"invalid MK3 pad: {index}")
    if not isinstance(pressure, int) or isinstance(pressure, bool):
        raise ValueError("MK3 pad pressure must be an integer")
    if not 0 <= pressure <= PAD_MAX_PRESSURE:
        raise ValueError("MK3 pad pressure exceeds the active report range")
    if pressed is None:
        pressed = pressure != 0
    if not isinstance(pressed, bool):
        raise ValueError("MK3 pad state must be boolean")
    if pressed and pressure < PAD_MIN_PRESSURE:
        raise ValueError("MK3 pad press is below the physical pressure gate")
    return pack_message(
        MSG_PAD, struct.pack(">BBH", index, 1 if pressed else 0, pressure))

def pack_knob(name, delta, absolute=0):
    if name not in ALL_KNOB_NAMES:
        raise ValueError(f"unknown MK3 knob: {name}")
    delta_limit = 2048 if name in KNOB_NAMES else 32768
    if (not isinstance(delta, int) or isinstance(delta, bool)
            or not isinstance(absolute, int) or isinstance(absolute, bool)
            or not -delta_limit <= delta <= min(delta_limit, 32767)
            or not 0 <= absolute <= KNOB_MAX_VALUES[name]):
        raise ValueError("MK3 knob values exceed the hardware report range")
    nb = name.encode("ascii")
    return pack_message(MSG_KNOB, struct.pack(">B", len(nb)) + nb + struct.pack(">hH", delta, absolute))

def pack_stepper(direction, position):
    if (not isinstance(direction, int) or isinstance(direction, bool)
            or not isinstance(position, int) or isinstance(position, bool)
            or direction not in (-1, 1) or not 0 <= position <= 15):
        raise ValueError("invalid MK3 stepper event")
    return pack_message(MSG_STEPPER, struct.pack(">bB", direction, position & 0x0F))

def pack_touchstrip(finger, touching, position):
    if (not isinstance(finger, int) or isinstance(finger, bool)
            or not isinstance(touching, bool)
            or not isinstance(position, int) or isinstance(position, bool)
            or finger != 1 or not 0 <= position <= TOUCHSTRIP_MAX_POSITION):
        raise ValueError("invalid MK3 touchstrip event")
    if bool(touching) != (position != 0):
        raise ValueError("touchstrip position zero is the release state")
    return pack_message(
        MSG_TOUCHSTRIP,
        struct.pack(">BBH", finger, 1 if touching else 0, position),
    )

# ── TCP server ───────────────────────────────────────────────────────

class TcpServer:
    def __init__(self, state, port=9999):
        self.state = state
        self.port = port
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(("0.0.0.0", port))
        self.server_socket.listen(1)
        self.server_socket.setblocking(False)
        self.read_buf = b""
        print(f"[TCP] Listening on port {port}")

    def poll(self):
        if not self.state.connected:
            try:
                client, addr = self.server_socket.accept()
                client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                client.setblocking(False)
                with self.state.lock:
                    self.state.client_socket = client
                    self.state.connected = True
                self.state.reset_led_state()
                self.read_buf = b""
                # A real MK3 produces a complete input report immediately on
                # connection. Send the same state snapshot so the application
                # can baseline absolute controls without inventing a turn.
                client.sendall(b"".join(self.state.input_snapshot_packets()))
                print(f"[TCP] App connected from {addr}")
            except BlockingIOError:
                pass

        if not self.state.connected:
            return

        try:
            data = self.state.client_socket.recv(262144)
            if not data:
                self._disconnect()
                return
            self.read_buf += data
        except BlockingIOError:
            pass
        except (ConnectionResetError, BrokenPipeError, OSError):
            self._disconnect()
            return

        while len(self.read_buf) >= 5:
            msg_type = self.read_buf[0]
            payload_len = struct.unpack(">I", self.read_buf[1:5])[0]
            total = 5 + payload_len
            if payload_len > 300000:
                self.read_buf = b""
                break
            if len(self.read_buf) < total:
                break
            payload = self.read_buf[5:total]
            self.read_buf = self.read_buf[total:]
            self._process_message(msg_type, payload)

    def send(self, data):
        self.state.record_input_packet(data)
        if not self.state.connected:
            return
        try:
            self.state.client_socket.sendall(data)
        except (BrokenPipeError, ConnectionResetError, OSError):
            self._disconnect()

    def _disconnect(self):
        print("[TCP] App disconnected")
        with self.state.lock:
            if self.state.client_socket:
                try: self.state.client_socket.close()
                except OSError: pass
            self.state.client_socket = None
            self.state.connected = False

    def _process_message(self, msg_type, payload):
        if msg_type == MSG_DISPLAY:
            if len(payload) < 1: return
            screen_index = payload[0]
            pixel_data = payload[1:]
            if len(pixel_data) >= DISPLAY_W * DISPLAY_H * 2:
                self.state.update_display(screen_index, pixel_data)
        elif msg_type == MSG_LED_MONO:
            if len(payload) < 2: return
            nl = payload[0]
            if len(payload) < 1 + nl + 1: return
            name = payload[1:1+nl].decode("ascii", errors="replace")
            self.state.set_led_brightness(name, payload[1+nl])
        elif msg_type == MSG_LED_INDEXED:
            if len(payload) < 2: return
            nl = payload[0]
            if len(payload) < 1 + nl + 1: return
            name = payload[1:1+nl].decode("ascii", errors="replace")
            self.state.set_led_indexed(name, payload[1+nl])

    def close(self):
        self._disconnect()
        self.server_socket.close()

# ── HTTP automation API ──────────────────────────────────────────────

def make_api_handler(state, tcp_server):
    class ApiHandler(BaseHTTPRequestHandler):
        def log_message(self, format, *args): pass

        def _json(self, data, status=200):
            body = json.dumps(data).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _read(self):
            n = int(self.headers.get("Content-Length", 0))
            return json.loads(self.rfile.read(n)) if n else {}

        def do_GET(self):
            if self.path == "/screenshot":
                png = state.get_screenshot_png()
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(png)))
                self.end_headers()
                self.wfile.write(png)
            elif self.path == "/state":
                self._json(state.get_state_json())
            elif self.path == "/health":
                self._json({"status": "ok", "connected": state.connected})
            else:
                self._json({"error": "not found"}, 404)

        def do_POST(self):
            try: data = self._read()
            except: self._json({"error": "invalid JSON"}, 400); return

            if self.path == "/button":
                name = data.get("name", "")
                pressed = data.get("pressed", True)
                if name not in HARDWARE_BUTTON_NAMES or not isinstance(pressed, bool):
                    self._json({"error": "unknown MK3 button"}, 400); return
                tcp_server.send(pack_button(name, pressed))
                self._json({"ok": True})
            elif self.path == "/pad":
                idx = data.get("index", 0)
                pressed = data.get("pressed", True)
                pressure = data.get("pressure", 4000 if pressed else 0)
                if (not isinstance(idx, int) or isinstance(idx, bool)
                        or not 1 <= idx <= 16):
                    self._json({"error": "pad index must be 1-16"}, 400); return
                if (not isinstance(pressure, int) or isinstance(pressure, bool)
                        or not 0 <= pressure <= PAD_MAX_PRESSURE):
                    self._json({"error": "pad pressure must be 0-4095"}, 400); return
                if not isinstance(pressed, bool):
                    self._json({"error": "pad pressed must be boolean"}, 400); return
                if pressed and pressure < PAD_MIN_PRESSURE:
                    self._json({"error": "pressed pad pressure must be 257-4095"}, 400); return
                tcp_server.send(pack_pad(idx, pressure, pressed))
                self._json({"ok": True})
            elif self.path == "/knob":
                name = data.get("name", "k1")
                delta = data.get("delta", 1)
                delta_limit = 2048 if name in KNOB_NAMES else 32768
                if (name not in ALL_KNOB_NAMES or not isinstance(delta, int)
                        or isinstance(delta, bool)
                        or not -delta_limit <= delta <= min(delta_limit, 32767)):
                    self._json({"error": "invalid MK3 knob event"}, 400); return
                actual_delta, absolute = state.apply_knob_input(name, delta)
                if actual_delta != 0:
                    tcp_server.send(pack_knob(name, actual_delta, absolute))
                self._json({"ok": True})
            elif self.path == "/stepper":
                direction = data.get("direction", 1)
                if not isinstance(direction, int) or isinstance(direction, bool) or direction not in (-1, 1):
                    self._json({"error": "stepper direction must be -1 or 1"}, 400); return
                position = state.apply_stepper_delta(direction)
                tcp_server.send(pack_stepper(direction, position))
                self._json({"ok": True})
            elif self.path == "/touchstrip":
                finger = data.get("finger", 1)
                touching = data.get("touching", True)
                position = data.get("position", 512 if touching else 0)
                if (not isinstance(finger, int) or isinstance(finger, bool)
                        or finger != 1 or not isinstance(touching, bool)
                        or not isinstance(position, int) or isinstance(position, bool)
                        or not 0 <= position <= TOUCHSTRIP_MAX_POSITION):
                    self._json({"error": "invalid MK3 touchstrip event"}, 400); return
                if bool(touching) != (position != 0):
                    self._json({"error": "position zero is the release state"}, 400); return
                tcp_server.send(pack_touchstrip(finger, touching, position))
                self._json({"ok": True})
            elif self.path == "/record/start":
                with state.lock:
                    if state.recording:
                        self._json({"error": "already recording"}, 400); return
                    state.record_dir = os.path.join(os.path.dirname(__file__),
                        f"recordings/rec_{int(time.time())}")
                    os.makedirs(state.record_dir, exist_ok=True)
                    state.record_frame_count = 0
                    state.record_start_time = time.time()
                    state.recording = True
                self._json({"ok": True, "directory": state.record_dir})
            elif self.path == "/record/stop":
                with state.lock:
                    if not state.recording:
                        self._json({"error": "not recording"}, 400); return
                    state.recording = False
                    result = {"ok": True, "directory": state.record_dir,
                              "frames": state.record_frame_count,
                              "duration_seconds": time.time() - state.record_start_time}
                self._json(result)
            elif self.path == "/macro":
                actions = data.get("actions", [])
                threading.Thread(target=_run_macro, args=(state, tcp_server, actions), daemon=True).start()
                self._json({"ok": True, "actions": len(actions)})
            else:
                self._json({"error": "not found"}, 404)
    return ApiHandler

def _run_macro(state, tcp, actions):
    for a in actions:
        delay = a.get("delay", 0)
        if delay > 0: time.sleep(delay / 1000.0)
        t = a.get("type", "")
        if t == "button": tcp.send(pack_button(a["name"], a.get("pressed", True)))
        elif t == "pad": tcp.send(pack_pad(a["index"], a.get("pressure", 4000)))
        elif t == "knob":
            name = a["name"]
            delta = a.get("delta", 1)
            actual_delta, absolute = state.apply_knob_input(name, delta)
            if actual_delta != 0:
                tcp.send(pack_knob(name, actual_delta, absolute))
        elif t == "stepper":
            direction = a.get("direction", 1)
            tcp.send(pack_stepper(direction, state.apply_stepper_delta(direction)))
        elif t == "touchstrip":
            touching = a.get("touching", True)
            tcp.send(pack_touchstrip(
                a.get("finger", 1), touching,
                a.get("position", 512 if touching else 0)))
        elif t == "wait": time.sleep(a.get("ms", 100) / 1000.0)

# ── MIDI input (external controllers like AKAI MPD18) ────────────

class MidiInput:
    """Reads MIDI from an external controller and translates to MK3 pad messages."""

    # Notes 36-51 → pads 1-16 (standard GM drum mapping)
    NOTE_BASE = 36
    NOTE_COUNT = 16

    def __init__(self, tcp_server, channel=0, device_name="MPD18"):
        self.tcp = tcp_server
        self.channel = channel  # mido uses 0-indexed channels
        self.port = None

        if not HAS_MIDO:
            print("[MIDI] mido not installed — pip install mido python-rtmidi")
            return

        port_name = self._find_port(device_name)
        if port_name is None:
            print(f"[MIDI] No MIDI device matching '{device_name}' found")
            names = mido.get_input_names()
            if names:
                print(f"[MIDI] Available: {names}")
            return

        self.port = mido.open_input(port_name, callback=self._on_message)
        print(f"[MIDI] Listening on '{port_name}' channel {channel + 1}")

    def _find_port(self, device_name):
        names = mido.get_input_names()
        if not names:
            return None
        if device_name:
            for n in names:
                if device_name.lower() in n.lower():
                    return n
        return None

    def _on_message(self, msg):
        if msg.type == 'note_on' and msg.channel == self.channel and msg.velocity > 0:
            pad = msg.note - self.NOTE_BASE + 1  # 1-indexed
            if 1 <= pad <= self.NOTE_COUNT:
                pressure = PAD_MIN_PRESSURE + (
                    (msg.velocity - 1)
                    * (PAD_MAX_PRESSURE - PAD_MIN_PRESSURE) // 126)
                self.tcp.send(pack_pad(pad, pressure, True))
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            if msg.channel == self.channel:
                pad = msg.note - self.NOTE_BASE + 1
                if 1 <= pad <= self.NOTE_COUNT:
                    self.tcp.send(pack_pad(pad, 0, False))

    def close(self):
        if self.port:
            self.port.close()

# ── GUI ──────────────────────────────────────────────────────────────

class EmulatorGui:
    def __init__(self, state, tcp_server):
        self.state = state
        self.tcp = tcp_server

        self.font = pygame.font.SysFont("monospace", 10)
        self.font_btn = pygame.font.SysFont("monospace", 9)
        self.font_med = pygame.font.SysFont("monospace", 12, bold=True)
        self.font_pad = pygame.font.SysFont("monospace", 15, bold=True)

        M = 8
        btn_w, btn_h, btn_gap = 56, 24, 3
        status_h = 18  # reserved top bar for connection/help hints

        # ── Upper section: left column + dual screens ──
        lcol_x = M
        lcol_w = btn_w * 2 + btn_gap  # 115

        scr_x = lcol_x + lcol_w + M  # 131
        scr_gap = 4

        # D-button strip above screens
        d_y = M + status_h
        d_total_w = DISPLAY_W * 2 + scr_gap
        d_btn_w = d_total_w // 8 - btn_gap
        d_btn_h = 18
        self.d_rects = {}
        for i, name in enumerate(D_BUTTONS):
            self.d_rects[name] = pygame.Rect(scr_x + i * (d_btn_w + btn_gap), d_y, d_btn_w, d_btn_h)

        # Screens
        scr_top = d_y + d_btn_h + 4
        self.left_scr = pygame.Rect(scr_x, scr_top, DISPLAY_W, DISPLAY_H)
        self.right_scr = pygame.Rect(scr_x + DISPLAY_W + scr_gap, scr_top, DISPLAY_W, DISPLAY_H)

        # Left column buttons (alongside screens)
        self.button_rects = {}
        ly = scr_top + 8
        for left_name, right_name in LEFT_COL:
            self.button_rects[left_name] = pygame.Rect(lcol_x, ly, btn_w, btn_h)
            self.button_rects[right_name] = pygame.Rect(lcol_x + btn_w + btn_gap, ly, btn_w, btn_h)
            ly += btn_h + btn_gap

        # Window width
        self.win_w = scr_x + DISPLAY_W * 2 + scr_gap + M

        # ── Knobs row (below screens) ──
        knob_top = scr_top + DISPLAY_H + M
        knob_sz = 44
        knob_spacing = d_total_w / 8
        self.knob_rects = {}
        for i, name in enumerate(KNOB_NAMES):
            kx = scr_x + int(i * knob_spacing + knob_spacing / 2) - knob_sz // 2
            self.knob_rects[name] = pygame.Rect(kx, knob_top, knob_sz, knob_sz)

        # ── Lower section ──
        lower_top = knob_top + knob_sz + 18 + M

        # Right side: mode row + pad column + pad grid
        pad_sz, pad_gap = 70, 6
        pad_grid_w = 4 * pad_sz + 3 * pad_gap
        pad_grid_x = self.win_w - pad_grid_w - M

        rcol_w = 60
        rcol_x = pad_grid_x - rcol_w - M

        # Mode row above pads
        mode_total = pad_grid_w + rcol_w + M
        mode_btn_w = mode_total // len(MODE_ROW) - btn_gap
        for i, name in enumerate(MODE_ROW):
            self.button_rects[name] = pygame.Rect(
                rcol_x + i * (mode_btn_w + btn_gap), lower_top, mode_btn_w, btn_h)

        # Pad grid (13-16 top, 1-4 bottom — matches physical MK3)
        pad_top = lower_top + btn_h + M
        self.pad_rects = {}
        for row in range(4):
            for col in range(4):
                pad_num = (3 - row) * 4 + col + 1
                self.pad_rects[pad_num] = pygame.Rect(
                    pad_grid_x + col * (pad_sz + pad_gap),
                    pad_top + row * (pad_sz + pad_gap),
                    pad_sz, pad_sz)

        # Right button column alongside pads
        pad_grid_h = 4 * (pad_sz + pad_gap) - pad_gap
        rcol_btn_h = pad_grid_h // 8 - 2
        ry = pad_top
        for name in PAD_SIDE_COL:
            self.button_rects[name] = pygame.Rect(rcol_x, ry, rcol_w, rcol_btn_h)
            ry += rcol_btn_h + 2

        # ── Lower left section ──
        small_btn_w = 48

        # 4-D encoder, with its mode-button column and performance buttons to
        # the right, matching the MK3 Edit and Performance zones.
        nav_cx = M + 42
        nav_cy = lower_top + 39
        self.nav_radius = 33
        self.nav_center = (nav_cx, nav_cy)
        self.stepper_rect = pygame.Rect(nav_cx - 35, nav_cy - 35, 70, 70)

        encoder_mode_x = nav_cx + self.nav_radius + M
        for i, name in enumerate(ENCODER_MODE_BUTTONS):
            self.button_rects[name] = pygame.Rect(
                encoder_mode_x, lower_top + i * (btn_h + btn_gap), btn_w, btn_h)

        performance_x = encoder_mode_x + btn_w + btn_gap
        performance_h = (3 * btn_h + 2 * btn_gap - btn_gap) // 2
        for i, name in enumerate(PERFORMANCE_BUTTONS):
            self.button_rects[name] = pygame.Rect(
                performance_x, lower_top + i * (performance_h + btn_gap),
                btn_w + 12, performance_h)

        # Below nav: PITCH MOD PERFORM NOTES
        edit_zone_bottom = lower_top + 3 * btn_h + 2 * btn_gap
        bn_y = edit_zone_bottom + M
        for i, name in enumerate(BELOW_NAV):
            self.button_rects[name] = pygame.Rect(
                M + i * (small_btn_w + btn_gap), bn_y, small_btn_w, btn_h)

        # Touchstrip (25 individually addressable indexed LEDs)
        touch_y = bn_y + btn_h + M
        touch_w = 8
        touch_gap = 1
        self.touchstrip_rects = {}
        for i in range(25):
            self.touchstrip_rects[f"ts{i + 1}"] = pygame.Rect(
                M + i * (touch_w + touch_gap), touch_y, touch_w, 12)
        self.touchstrip_bounds = pygame.Rect(
            self.touchstrip_rects["ts1"].left,
            touch_y,
            self.touchstrip_rects["ts25"].right - self.touchstrip_rects["ts1"].left,
            12)

        # Groups
        grp_y = touch_y + 12 + M
        grp_sz, grp_gap = 40, 4
        self.group_rects = {}
        for ri, row in enumerate(GROUPS):
            for ci, name in enumerate(row):
                gx = M + ci * (grp_sz + grp_gap)
                gy = grp_y + ri * (grp_sz + grp_gap)
                self.group_rects[name] = pygame.Rect(gx, gy, grp_sz, grp_sz)
                self.button_rects[name] = pygame.Rect(gx, gy, grp_sz, grp_sz)

        # Transport rows
        tr_y = grp_y + 2 * (grp_sz + grp_gap) + M
        for ri, row in enumerate(TRANSPORT):
            for ci, name in enumerate(row):
                self.button_rects[name] = pygame.Rect(
                    M + ci * (btn_w + btn_gap),
                    tr_y + ri * (btn_h + btn_gap),
                    btn_w, btn_h)

        # Rear-panel analogue controls and connection sensors are shown in a
        # separate strip below the top-panel controls. Their left-to-right
        # order follows the hardware as viewed facing the rear panel.
        utility_top = tr_y + 2 * (btn_h + btn_gap) + M
        utility_size = 34
        utility_gap = 10
        for i, name in enumerate(REAR_PANEL_KNOB_LAYOUT):
            self.knob_rects[name] = pygame.Rect(
                M + i * (utility_size + utility_gap), utility_top,
                utility_size, utility_size)

        microphone_x = self.knob_rects["micInGain"].right + M
        pedal_x = microphone_x + 72
        self.sensor_rects = {
            "microphoneConnected": pygame.Rect(microphone_x, utility_top + 9, 56, 15),
            "pedalConnected": pygame.Rect(pedal_x, utility_top + 1, 64, 15),
            "pedalSwitch": pygame.Rect(pedal_x, utility_top + 18, 64, 15),
        }
        # Window height
        self.win_h = max(
            pad_top + pad_grid_h + M,
            utility_top + utility_size + M
        )

        self.status_h = status_h
        self.held_button = None
        self.held_pad = -1
        self.held_touchstrip_fingers = set()
        self.last_touchstrip_positions = {}
        # knobTouch tracking: map knob name → timestamp of last scroll event
        self.knob_touch_active = {}
        self.knob_touch_timeout = 0.2  # seconds before auto-releasing knobTouch
        self.show_help = False
        self.font_help_title = pygame.font.SysFont("monospace", 16, bold=True)
        self.font_help = pygame.font.SysFont("monospace", 12)

    def _btn_color(self, name):
        if name == "sampling":
            return self.state.get_indexed_color(name)
        brightness = self.state.get_button_brightness(name)
        if self.state.get_button_input(name):
            return BTN_PRESSED
        if brightness > 0:
            # Physical brightness steps top out at firmware value 7.
            t = min(brightness, 7) / 7.0
            return tuple(int(BTN_OFF[i] + (BTN_PRESSED[i] - BTN_OFF[i]) * t) for i in range(3))
        return BTN_OFF

    def release_expired_knob_touches(self):
        """Release knobTouch buttons that haven't been scrolled recently."""
        now = time.time()
        expired = [name for name, t in self.knob_touch_active.items()
                   if now - t > self.knob_touch_timeout]
        for name in expired:
            knob_num = name[1]
            self.tcp.send(pack_button(f"knobTouch{knob_num}", False))
            del self.knob_touch_active[name]

    def _touchstrip_position(self, point):
        if not self.touchstrip_bounds.collidepoint(point):
            return None
        usable_width = max(1, self.touchstrip_bounds.width - 1)
        relative_x = point[0] - self.touchstrip_bounds.left
        return 1 + round(relative_x * (TOUCHSTRIP_MAX_POSITION - 1) / usable_width)

    def draw(self, screen):
        screen.fill(BG_COLOR)

        # Dual screens
        with self.state.lock:
            screen.blit(self.state.display_surfaces[0], self.left_scr)
            screen.blit(self.state.display_surfaces[1], self.right_scr)
        pygame.draw.rect(screen, SCREEN_BORDER, self.left_scr, 1)
        pygame.draw.rect(screen, SCREEN_BORDER, self.right_scr, 1)

        # Status bar (top row, above everything)
        status_y = (self.status_h - self.font.get_height()) // 2
        c = (0, 200, 80) if self.state.connected else (200, 60, 60)
        conn_txt = self.font.render("CONNECTED" if self.state.connected else "WAITING...", True, c)
        hint_txt = self.font.render("F10 = Help", True, TEXT_DIM)
        screen.blit(conn_txt, (8, status_y))
        if self.state.recording:
            rt = self.font.render("● REC", True, (255, 40, 40))
            screen.blit(rt, (8 + conn_txt.get_width() + 12, status_y))
        screen.blit(hint_txt, (self.win_w - hint_txt.get_width() - 8, status_y))

        # D-buttons
        for name, rect in self.d_rects.items():
            pygame.draw.rect(screen, self._btn_color(name), rect, border_radius=2)

        # Buttons (excluding groups, drawn separately)
        for name, rect in self.button_rects.items():
            if name.startswith("g"):
                continue
            pygame.draw.rect(screen, self._btn_color(name), rect, border_radius=3)
            label = LABELS.get(name, name[:7])
            txt = self.font_btn.render(label, True, TEXT_COLOR)
            screen.blit(txt, (rect.x + 3, rect.centery - txt.get_height() // 2))

        # Knobs
        for name, rect in self.knob_rects.items():
            pygame.draw.circle(screen, KNOB_BG, rect.center, rect.width // 2)
            pygame.draw.circle(screen, KNOB_FG, rect.center, rect.width // 2, 2)
            txt = self.font.render(name.upper(), True, TEXT_DIM)
            if name in LABELS:
                txt = self.font.render(LABELS[name], True, TEXT_DIM)
            screen.blit(txt, (rect.centerx - txt.get_width() // 2, rect.bottom + 2))

        # Connection sensors are latched report bits, not momentary buttons.
        for name, rect in self.sensor_rects.items():
            color = BTN_PRESSED if self.state.get_button_input(name) else BTN_OFF
            pygame.draw.rect(screen, color, rect, border_radius=2)
            txt = self.font_btn.render(LABELS[name], True, TEXT_COLOR)
            screen.blit(txt, (rect.centerx - txt.get_width() // 2,
                              rect.centery - txt.get_height() // 2))

        # Groups (colored)
        for name, rect in self.group_rects.items():
            idx = int(name[1]) - 1
            color = self.state.get_group_color(idx)
            if self.state.get_button_input(name):
                color = tuple(min(255, c + 60) for c in color)
            pygame.draw.rect(screen, color, rect, border_radius=4)
            pygame.draw.rect(screen, (70, 75, 82), rect, 1, border_radius=4)
            txt = self.font_med.render(LABELS.get(name, name), True, TEXT_COLOR)
            screen.blit(txt, (rect.centerx - txt.get_width() // 2,
                              rect.centery - txt.get_height() // 2))

        # Touchstrip
        for name, rect in self.touchstrip_rects.items():
            pygame.draw.rect(screen, self.state.get_indexed_color(name), rect, border_radius=1)

        # Pads
        for pad_num, rect in self.pad_rects.items():
            color = self.state.get_pad_color(pad_num - 1)
            if self.state.get_pad_pressed(pad_num - 1):
                color = tuple(min(255, c + 60) for c in color)
            pygame.draw.rect(screen, color, rect, border_radius=6)
            pygame.draw.rect(screen, (80, 88, 96), rect, 1, border_radius=6)
            txt = self.font_pad.render(str(pad_num), True, TEXT_COLOR)
            screen.blit(txt, (rect.centerx - txt.get_width() // 2,
                              rect.centery - txt.get_height() // 2))

        # Nav encoder
        pygame.draw.circle(screen, KNOB_BG, self.nav_center, self.nav_radius)
        pygame.draw.circle(screen, KNOB_FG, self.nav_center, self.nav_radius, 2)
        pygame.draw.circle(screen, (55, 60, 68), self.nav_center, 14)
        txt = self.font.render("NAV", True, TEXT_DIM)
        screen.blit(txt, (self.nav_center[0] - txt.get_width() // 2,
                          self.nav_center[1] - txt.get_height() // 2))

        # Four indexed nav LEDs around the encoder ring
        nav_leds = {
            "navUp": (self.nav_center[0], self.nav_center[1] - 23, "▲"),
            "navRight": (self.nav_center[0] + 23, self.nav_center[1], "▶"),
            "navDown": (self.nav_center[0], self.nav_center[1] + 23, "▼"),
            "navLeft": (self.nav_center[0] - 23, self.nav_center[1], "◀"),
        }
        for name, (x, y, glyph) in nav_leds.items():
            nav_text = self.font_med.render(glyph, True, self.state.get_indexed_color(name))
            screen.blit(nav_text, (x - nav_text.get_width() // 2, y - nav_text.get_height() // 2))

        # Help overlay
        if self.show_help:
            self._draw_help_overlay(screen)

    def _draw_help_overlay(self, screen):
        overlay = pygame.Surface((self.win_w, self.win_h), pygame.SRCALPHA)
        overlay.fill((0, 0, 0, 200))
        screen.blit(overlay, (0, 0))

        lines = [
            ("KEYBOARD SHORTCUTS", True),
            ("", False),
            ("Buttons", True),
            ("Space        Play", False),
            ("S            Stop", False),
            ("R            Record", False),
            ("L Shift      Shift", False),
            ("R Shift      Shift", False),
            ("Caps Lock    Select", False),
            ("P            Pattern", False),
            ("M            Mixer", False),
            ("A            Arranger", False),
            ("B            Browser", False),
            ("Enter        Nav Push", False),
            ("F1-F4        Display L (D1-D4)", False),
            ("F5-F8        Display R (D5-D8)", False),
            ("", False),
            ("Pads", True),
            ("1-4          Pads 1-4", False),
            ("5-8          Pads 5-8", False),
            ("Q W E T      Pads 9-12", False),
            ("Y U I O      Pads 13-16", False),
            ("", False),
            ("Navigation", True),
            ("Up/Down      Stepper +/-", False),
            ("Scroll       Knob (hover over knob)", False),
            ("Scroll Nav   Stepper", False),
            ("", False),
            ("Press any key to close", True),
        ]

        x, y = 40, 30
        for text, bold in lines:
            if not text:
                y += 10
                continue
            font = self.font_help_title if bold else self.font_help
            color = (255, 255, 255) if bold else (200, 200, 200)
            txt = font.render(text, True, color)
            screen.blit(txt, (x, y))
            y += txt.get_height() + 4

    def handle_event(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button in (1, 3):
            pos = event.pos

            touchstrip_position = self._touchstrip_position(pos)
            if touchstrip_position is not None:
                if event.button == 1:
                    self.held_touchstrip_fingers.add(1)
                    self.last_touchstrip_positions[1] = touchstrip_position
                    self.tcp.send(pack_touchstrip(1, True, touchstrip_position))
                return

            if event.button == 1:
                for name, rect in self.sensor_rects.items():
                    if rect.collidepoint(pos):
                        active = not self.state.get_button_input(name)
                        self.tcp.send(pack_button(name, active))
                        return

                for name in KNOB_NAMES:
                    if self.knob_rects[name].collidepoint(pos):
                        self.held_button = f"knobTouch{name[1]}"
                        self.tcp.send(pack_button(self.held_button, True))
                        return

            if event.button != 1:
                # Right click outside the touchstrip only has meaning on NAV.
                dx = pos[0] - self.nav_center[0]
                dy = pos[1] - self.nav_center[1]
                if dx*dx + dy*dy <= self.nav_radius * self.nav_radius:
                    self.held_button = "navTouch"
                    self.tcp.send(pack_button("navTouch", True))
                return

            for name, rect in self.d_rects.items():
                if rect.collidepoint(pos):
                    self.held_button = name
                    self.tcp.send(pack_button(name, True))
                    return

            for name, rect in self.button_rects.items():
                if rect.collidepoint(pos):
                    self.held_button = name
                    self.tcp.send(pack_button(name, True))
                    return

            for pad_num, rect in self.pad_rects.items():
                if rect.collidepoint(pos):
                    self.held_pad = pad_num
                    self.tcp.send(pack_pad(pad_num, 4000))
                    return

            # The MK3 encoder is also a four-way switch. Use its centre for
            # navPush and the outer ring for the directional buttons.
            dx = pos[0] - self.nav_center[0]
            dy = pos[1] - self.nav_center[1]
            if dx*dx + dy*dy <= self.nav_radius * self.nav_radius:
                if dx*dx + dy*dy <= 14 * 14:
                    name = "navPush"
                elif abs(dx) > abs(dy):
                    name = "navRight" if dx > 0 else "navLeft"
                else:
                    name = "navDown" if dy > 0 else "navUp"
                self.held_button = name
                self.tcp.send(pack_button(name, True))
                return

        elif event.type == pygame.MOUSEBUTTONUP and event.button in (1, 3):
            if event.button == 1 and 1 in self.held_touchstrip_fingers:
                self.tcp.send(pack_touchstrip(1, False, 0))
                self.held_touchstrip_fingers.remove(1)
                self.last_touchstrip_positions.pop(1, None)
            if self.held_button:
                self.tcp.send(pack_button(self.held_button, False))
                self.held_button = None
            if self.held_pad > 0:
                self.tcp.send(pack_pad(self.held_pad, 0))
                self.held_pad = -1

        elif event.type == pygame.MOUSEWHEEL:
            pos = pygame.mouse.get_pos()
            for name, rect in self.knob_rects.items():
                if rect.collidepoint(pos):
                    # Only the eight display encoders have capacitive touch.
                    if name in KNOB_NAMES:
                        knob_num = name[1]
                        touch_name = f"knobTouch{knob_num}"
                        if name not in self.knob_touch_active:
                            self.tcp.send(pack_button(touch_name, True))
                        self.knob_touch_active[name] = time.time()
                    actual_delta, absolute = self.state.apply_knob_input(name, event.y)
                    if actual_delta != 0:
                        self.tcp.send(pack_knob(name, actual_delta, absolute))
                    return
            # Scroll on nav encoder = stepper
            dx = pos[0] - self.nav_center[0]
            dy = pos[1] - self.nav_center[1]
            if dx*dx + dy*dy <= (self.nav_radius + 20) ** 2:
                if event.y == 0:
                    return
                # Wheel-up is the counter-clockwise hardware direction, which
                # the MK3 driver reports as -1 (navigation up).
                direction = -1 if event.y > 0 else 1
                position = self.state.apply_stepper_delta(direction)
                self.tcp.send(pack_stepper(direction, position))
                return

        elif event.type == pygame.MOUSEMOTION and self.held_touchstrip_fingers:
            pos = event.pos
            position = self._touchstrip_position(pos)
            if position is not None:
                for finger in tuple(self.held_touchstrip_fingers):
                    if position != self.last_touchstrip_positions.get(finger):
                        self.last_touchstrip_positions[finger] = position
                        self.tcp.send(pack_touchstrip(finger, True, position))
                return

        elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_F10:
                self.show_help = not self.show_help
                return
            if self.show_help:
                self.show_help = False
                return
            if event.key in KEY_TO_BUTTON:
                self.tcp.send(pack_button(KEY_TO_BUTTON[event.key], True))
                return
            if event.key in KEY_TO_PAD:
                self.tcp.send(pack_pad(KEY_TO_PAD[event.key] + 1, 4000))
                return
            if event.key == pygame.K_UP:
                self.tcp.send(pack_stepper(-1, self.state.apply_stepper_delta(-1)))
            elif event.key == pygame.K_DOWN:
                self.tcp.send(pack_stepper(1, self.state.apply_stepper_delta(1)))

        elif event.type == pygame.KEYUP:
            if event.key in KEY_TO_BUTTON:
                self.tcp.send(pack_button(KEY_TO_BUTTON[event.key], False))
            elif event.key in KEY_TO_PAD:
                self.tcp.send(pack_pad(KEY_TO_PAD[event.key] + 1, 0))

# ── Main ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="MK3 Hardware Emulator")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--api-port", type=int, default=9998)
    parser.add_argument("--headless", action="store_true",
                        help="Run without GUI (for CI/automated testing)")
    parser.add_argument("--midi-channel", type=int, default=0,
                        help="MIDI channel to listen on (0-15, default: 0)")
    parser.add_argument("--midi-device", type=str, default="MPD18",
                        help="MIDI device name substring (default: MPD18)")
    parser.add_argument("--no-midi", action="store_true",
                        help="Disable MIDI input")
    args = parser.parse_args()

    if args.headless:
        os.environ["SDL_VIDEODRIVER"] = "dummy"

    pygame.init()

    state = EmulatorState()
    tcp = TcpServer(state, port=args.port)

    midi = None
    if not args.no_midi:
        midi = MidiInput(tcp, channel=args.midi_channel, device_name=args.midi_device)

    if not args.headless:
        pygame.display.set_caption("MK3 Emulator")
        gui = EmulatorGui(state, tcp)
        screen = pygame.display.set_mode((gui.win_w, gui.win_h))
    else:
        gui = None
        screen = None
        print(f"[headless] TCP :{args.port} | HTTP :{args.api_port} | no GUI")

    handler = make_api_handler(state, tcp)
    http = HTTPServer(("0.0.0.0", args.api_port), handler)
    threading.Thread(target=http.serve_forever, daemon=True).start()
    if not args.headless:
        print(f"[HTTP] Automation API on port {args.api_port}")

    clock = pygame.time.Clock()
    running = True

    if args.headless:
        try:
            while running:
                tcp.poll()
                clock.tick(60)
        except KeyboardInterrupt:
            running = False
    else:
        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                else:
                    gui.handle_event(event)
            tcp.poll()
            gui.release_expired_knob_touches()
            gui.draw(screen)
            pygame.display.flip()
            clock.tick(60)

    if midi:
        midi.close()
    tcp.close()
    http.shutdown()
    pygame.quit()

if __name__ == "__main__":
    main()
