# Future work

These are intentionally deferred features that can be picked up when no
higher-priority milestone is active.

## Sequencing and performance

### S1 — Expressive pattern recording

Build on the always-available built-in instrument baseline to support sub-step
timing, adjustable quantize strength, timing nudge, and velocity captured from
pad pressure. Pressed/released events remain the note lifecycle; changing
pressure while a pad is held must not retrigger it.

### S2 — Step probability

Store and apply per-note trigger probability.

### S3 — Euclidean rhythm editor

Let the user choose pulse count and rotation instead of applying the current
fixed 50% fill.

### S4 — Launcher-based pattern playback

Use Tracktion launcher playback for atomic, quantized pattern changes without
stopping and restarting transport.

### S5 — Pattern chaining

Add FollowActions or equivalent rules for automatically selecting the next
pattern.

### S6 — Groove presets

Provide MPC-style and related named swing presets through the reserved
Shift+Swing interaction.

### S7 — Per-pattern swing

Allow a pattern to override the global swing value.

### S8 — Finer swing resolution

Support fractional percentage steps or a signed timing range.

### S9 — TE-native choke groups

Move choke-group playback entirely into Tracktion's audio path and remove the
remaining playhead polling path.

### S10 — Pattern View follow mode

Add an optional **Follow active/playing pattern** mode to Pattern View. When
enabled, opening the view and subsequent playback-driven pattern changes should
select and display the pattern the engine is actually playing. Manual pattern
selection should temporarily disengage follow mode (or provide an explicit
lock) so editing a different pattern remains possible while transport runs.

## Arranger

### A1 — Lane naming

Replace generic lane numbers with editable names.

### A2 — Per-lane mute and solo

Add independent mute and solo state to arranger lanes.

### A3 — Direct block move and resize gestures

Explore direct block manipulation while retaining the MK3-friendly knob
workflow.

### A4 — Per-block follow rules

Allow a completed arranger block to select another block or pattern.

## Mixer and routing

### M1 — Editable AUX sends

Create, remove, route, and adjust sends from the controller.

### M2 — Sidechain routing

Allow effects to receive a sidechain input.

### M3 — Loose-track grouping

Optionally collect otherwise ungrouped tracks into a logical Arranger group.

### M4 — Richer mixer UI persistence

Preserve more per-channel view and selection state between sessions.

## Projects and text entry

### P1 — Full filesystem project browser

Add folder navigation, multiple project roots, and user-selected save paths.

### P2 — Dirty-project tracking

Detect unsaved changes and offer to save the current project before loading
another. Consider auto-save-on-switch as part of the design.

### P3 — Project metadata

Support optional descriptions and tags during Save As.

### P4 — Group rename through T9

Add the remaining group rename flow using the existing T9 text entry.

### P5 — Advanced T9

Explore predictive disambiguation, international text, per-keystroke undo, and
concurrent text fields.

### P6 — Configurable T9 timeout

Allow the multi-tap timeout to be adjusted.

### P7 — Rename collision handling

Offer overwrite or alternate-name handling rather than only rejecting a
duplicate name.

## Samples

### L1 — Envelope-based sample classification

Improve kick, snare, hat, and other one-shot subtype detection using audio
analysis.

### L2 — Audio-derived BPM, pitch, and key

Derive tempo and tonal metadata from audio rather than relying only on
filenames.

### L3 — Manual sample tag editor

Store per-file corrections and overrides to automatic categorisation.

## Instruments

### I1 — Bundled sampled piano

Optionally package an SFZ/SF2 player and a redistributable piano library after
the built-in synth and expressive-recording milestones are stable. Validate
content licensing, installer size, storage paths, voice count, load time, and
small-buffer Raspberry Pi performance before choosing the library.

## Plugins

### G1 — Plugin automation curves

Create and edit Tracktion parameter automation from the controller.

### G2 — Plugin-delay-compensation validation

Verify correctness and xrun behavior with plugin latency at small Raspberry Pi
audio buffers.

## UI and architecture investigations

### U1 — Toast queue and shared toast architecture

Retain and sequence multiple notifications instead of replacing them solely by
priority.

### U2 — Dedicated transport HUD

Move BPM and swing readouts out of toast- or widget-specific rendering.

### U3 — VBlank-driven rendering

Investigate only if VBlank can be shown to work reliably in the headless
runtime.

### U4 — Direct-to-USB component peer

Consider a custom rendering peer only if the existing dirty-gated, partial
rendering path proves insufficient.

## Hardware

### H1 — MK3 USB hotplug and reconnection

Detect Maschine MK3 USB arrival and removal events while the application is
running. A controller that is powered on after application startup should
connect automatically; unplug/replug and host sleep/resume should use the same
reconnection path.

Implementation constraints:

- Use libusb hotplug events rather than continuous device polling.
- Keep USB event handling off the audio and 60 Hz UI/render paths.
- Do only minimal work in the hotplug callback; perform device setup and
  teardown through the controller lifecycle.
- Replay current displays and all LED state once after a successful reconnect.
- Clear held pad, button, and knob-touch state on disconnect.
- Preserve steady-state performance. Validate idle CPU usage, UI frame timing,
  input latency, and audio xruns against the performance baseline.

This is a separate milestone and is not part of the current performance
stabilization branch.
