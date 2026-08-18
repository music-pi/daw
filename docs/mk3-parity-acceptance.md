# MK3 Emulator Physical Parity Acceptance

This is the final evidence gate for claiming 1:1 parity. Software-only tests
are necessary but cannot prove display orientation, real LED colors, pressure
response, capacitive sensing, jack detection, or device disconnect behavior.

## Preconditions

Run from the repository root. A Maschine MK3 must appear as USB ID
`17cc:1600`, and no Native Instruments process may hold its HID interfaces.

```sh
lsusb -d 17cc:1600
./mpi build headless
./mpi emulator test
./mpi test
```

Record the controller serial number, firmware version, commit, date, and the
complete command output with the acceptance result.

## Output acceptance

```sh
build/bin/test_mk3 parity-output
```

Confirm every item before pressing Ctrl+C:

- Left display has red/green top quadrants and blue/white bottom quadrants.
- Right display has cyan, magenta, yellow, and black vertical bands.
- Neither display is mirrored, swapped, shifted, color-channel swapped, or torn.
- Pad rows are red, amber, green, and blue from bottom to top; Pad 1 is bottom-left.
- Groups A–H are red, orange, amber, yellow, lime, green, blue, and purple.
- All 25 Smart Strip segments are turquoise.
- Sampling and all four encoder navigation LEDs are white.
- Mono buttons are dim, while Play, Record, and Stop are bright.
- Ctrl+C clears both displays and every LED.

Any discrepancy fails acceptance; capture a photo and the command output.

## Input acceptance

```sh
build/bin/test_mk3 parity-input
```

Confirm one press and one release event with the exact printed name for every
physical button, including d1–d8, groups A–H, encoder directions/push/touch,
all eight encoder-touch sensors, transport, modes, navigation, and edit-zone
buttons. Then confirm:

- Pads 1–16 print their physical number, with 1–4 on the bottom row and 13–16 on top.
- Hold Pad 1, then press Pad 13: both events are reported and neither releases early.
- Each k1–k8 clockwise movement has positive delta and wraps within `0..999`.
- Mic gain, headphone volume, and master volume each span `0..4095`.
- The navigation encoder reports positions `0..15` and correct wrap direction.
- Smart Strip moves continuously across the active area (physically measured near `1..1016`; the report field supports `1..1023`) and emits one release.
- Inserting/removing microphone and pedal plugs toggles the corresponding detection events.
- Operating both pedal contacts produces `pedalConnected` and `pedalSwitch` edges.

Press Ctrl+C only after every input has been observed. The command prints a
coverage report and exits unsuccessfully if any required edge, direction,
range, overlap, or release is missing. Use `build/bin/test_mk3 input` when a
free-form event trace is useful for diagnosing a failed item.

## Lifecycle acceptance

Run the headless application with the MK3 attached, then unplug the USB cable.
The process must remain alive and log a controller disconnect. Reconnect and
restart the application; displays, LEDs, and input must initialize without a
phantom press or first-turn encoder jump.

Repeat the equivalent emulator test by closing the emulator while the app is
running. The app must remain alive and report the emulator disconnected.

## Acceptance record

Parity is accepted only when all automated tests and every physical check pass
on the same revision. Store the evidence using this template:

```text
Commit:
Date/time:
MK3 serial:
Firmware:
Automated tests: PASS / FAIL
Displays: PASS / FAIL
LED map/colors/brightness: PASS / FAIL
Buttons/touch sensors: PASS / FAIL
Pads/multitouch/pressure: PASS / FAIL
Encoders/rear controls: PASS / FAIL
Smart Strip: PASS / FAIL
Pedal/microphone detection: PASS / FAIL
Disconnect/restart lifecycle: PASS / FAIL
Evidence paths:
Notes:
```
