# MusicPI

MusicPI is a headless digital audio workstation for the Native Instruments
Maschine MK3. It runs on Raspberry Pi-class Linux systems and uses Tracktion
Engine for audio/MIDI processing and JUCE for the two-panel hardware UI.

## Current capabilities

- 16-pad sampler with one Tracktion audio track and plugin chain per pad
- Pattern sequencing, velocity editing, swing, recording, and multi-lane song arrangement
- Eight save/recall groups with hardware LED feedback
- Pad, group, master, and plugin mixer views
- Sample browsing, categorisation, preview, truncate, normalise, and slicing workflows
- Always-available FourOsc synth plus VST3/LV2 instrument and effect hosting
- On-device T9 text entry and project file dialogs
- Physical MK3 USB and TCP emulator controller backends
- Undo/redo and `.mpi` project persistence

The desktop target is deprecated. Use the headless target unless you are
explicitly working on legacy desktop behaviour.

## Running on hardware

The easiest way to run MusicPI on a Raspberry Pi + Maschine MK3 is the
prebuilt appliance image from
[music-pi/mpi-station](https://github.com/music-pi/mpi-station) — one flashable image that
boots into MusicPI or MixxxDJ, chosen at boot. Most users should start there.
The instructions below are for building and developing MusicPI from source.

**Requirements for a from-source install:** a Native Instruments Maschine MK3,
a Raspberry Pi 4-class Linux system (or an x86 Linux dev host with the emulator),
`libusb-1.0`, and the build dependencies listed under
[Development dependencies](#development-dependencies). Non-root USB access to the
MK3 requires a udev rule (see mpi-station / the MK3 udev rule for a working
example).

## Quick start

Clone recursively, build the small `mpi` development CLI, then run every
remaining command from the repository root:

```bash
git clone --recursive https://github.com/music-pi/daw.git
cd daw
./scripts/build-cli.sh
./mpi build headless
./mpi test
./mpi run
```

Useful development commands:

```bash
# Run a filtered GoogleTest suite directly
build/maschinepi_tests_artefacts/Release/maschinepi_tests \
  --gtest_filter='InputManagerTest.*'

# Start the graphical emulator and the app
./mpi emulator run --no-build

# Start the headless emulator QA control plane
./mpi emulator qa

# Run emulator protocol/parity tests
./mpi emulator test
```

The test binary is generated at
`build/maschinepi_tests_artefacts/Release/maschinepi_tests` when direct GTest
arguments are needed.

## Keyboard instruments

Press **Keyboard** on the MK3 to enter chromatic instrument mode. On an empty
instrument slot, press **D8 (Load Instr)** and choose Tracktion's built-in
FourOsc synth; it is available immediately without scanning or installing a
plugin. Installed VST3/LV2 instruments appear in the same browser after a scan.
Pads use pressed/released events for note-on/note-off, while initial pad
pressure controls velocity without retriggering a held note. **Fixed Vel**
toggles velocity 127 in Keyboard mode; **Shift + Fixed Vel** toggles 16
velocity levels, quantizing each chromatic pad's pressure curve to steps
8–127. Press **Plugin** to edit the active instrument parameters from the
hardware knobs.

Use the two-part D5/D6 navigator to change instrument slots. D7 adds another
slot, and D8 deletes the selected populated slot. Shift+Pads 13/14 move by a
semitone and Shift+Pads 15/16 move by an octave.

## Development dependencies

Ubuntu/Debian:

```bash
sudo apt install build-essential cmake pkg-config \
  ca-certificates libasound2-dev libjack-jackd2-dev \
  libusb-1.0-0-dev \
  libfreetype6-dev libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libgl1-mesa-dev
```

Initialise the Tracktion Engine submodule if it is not already present:

```bash
git submodule update --init --recursive
```

## Repository layout

```text
src/app/         JUCE application and data paths
src/engine/      Tracktion Engine wrappers, sequencing, commands, project state
src/input/       Normalised input events and priority/context dispatch
src/control/     Controller abstraction and USB/emulator coordination
src/devices/     MK3 device wrapper
src/samples/     Sample indexing, classification, and taxonomy
src/ui/widget/   Widget base, WindowManager, views, and dialogs
src/ui/hw/       Hardware resource and LED ownership
src/ui/components/ Reusable rendered controls
tests/           GoogleTest suites and headless harnesses
scripts/         Emulator, image, and deployment helpers
external/        Third-party sources and hardware libraries
```

## Documentation

- [Documentation index](docs/README.md)
- [Architecture](docs/architecture.md)
- [UI system](docs/ui-system.md)
- [Audio engine](docs/audio-engine.md)
- [Input system](docs/input-system.md)
- [MK3 physical parity acceptance](docs/mk3-parity-acceptance.md)
- [Test harnesses](tests/harness/README.md)
- [Contributor rules](AGENTS.md)

The `docs/` guides above and the current source are authoritative.

## Related projects

- [MusicPI Station](https://github.com/music-pi/mpi-station) — the integrator that fuses
  the DAW and DJ modes into a single flashable Raspberry Pi image.
- [libmk3](https://github.com/music-pi/libmk3) — the shared C driver for the MK3
  hardware, consumed here as a pinned submodule.
- **mixxx-mk3** — MK3 support for the Mixxx DJ software; the other half of the
  dual-mode rig. _Link to be added on repo publication._

## License

Copyright (C) 2026 Sebastian Hines.

MusicPI is released under the **GNU General Public License v3.0** — see
[`LICENSE`](LICENSE). Dependencies retain their own licenses: the pinned
Tracktion Engine is GPLv3-or-later/commercial, JUCE is AGPLv3/commercial, and
the pinned libmk3 driver is MIT-licensed. When distributing a combined build,
comply with the applicable terms of each dependency.

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the exact pinned
versions and links to their complete license texts.

GPLv3 permits use, modification, and redistribution, and does not prevent
accepting donations or selling copies.
