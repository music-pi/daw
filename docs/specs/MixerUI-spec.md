# 🎛 MusicPI MixerComponent Specification (JUCE 8 + Tracktion Engine)

> **Historical specification:** the current mixer is implemented by
> `MixerWidget`, `ChannelStripsComponent`, and components under
> `src/ui/components/mixer/`. Treat this file as the original visual brief,
> not as current implementation instructions. See [../ui-system.md](../ui-system.md).

> **Note:**  
> This document provides additional information and context for an **in-progress Codex implementation task**.  
> It refines and extends the MixerComponent design within the JUCE 8 / Tracktion Engine-based MusicPI system.

---

## 🧠 Overview

We are building the new **MusicPI UI** in **C++** using **JUCE 8** for graphics and **Tracktion Engine** for audio control.

This specification defines the **MixerComponent** and its layout, connecting the existing `OptionsBarComponent` (top) and `KnobBarComponent` (bottom) with a new middle section showing 4 channel strips.

---

## 🧩 Component Hierarchy

```
Main View Layout:
 ├── OptionsBarComponent     (top, existing)
 ├── MixerComponent          (middle, new)
 └── KnobBarComponent        (bottom, existing)
```

All three span the full display width.

---

## ⚙️ Layout Requirements

### OptionsBarComponent
- Already implemented.
- Displays **channel names**, each taking up **25 %** of total width.
- Four visible channel names aligned horizontally.

---

### MixerComponent
- Full-width container with **4 × MixerChannelStripComponent** children.
- Each channel strip occupies **25 % of total width**.
- Each channel strip contains **two columns**:

#### Left column (≈ 70 %)
Subdivided into **three vertical sub-columns**:
1. **Gain Axis** — static scale from **+12 dB → –∞ dB**  
   - Draw tick marks and numeric labels.
2. **Graphical Meter** — animated gradient showing live input level  
   - Smoothly animated fill, with **green/yellow/red** color zones.  
   - Includes **peak-hold** indicator line.
3. **dBFS Axis** — static scale from **0 → –60 dBFS**, right-aligned labels.

#### Right column (≈ 30 %)
Vertical flex column with **five indicator items**:
1. **MUTE / SOLO** toggle buttons  
2. **PLUGIN COUNT** indicator (shows active insert count)  
3. **EQ preview** (small curve visual or placeholder)  
4. **AUX COUNT** indicator (shows number of active sends)  
5. **PAN indicator** (circular arc showing pan position –1.0 → +1.0)

---

### KnobBarComponent
- Already implemented.
- Displays **Gain numeric readout** for each channel.
- Each occupies **25 % width**, aligned with the corresponding channel.

---

## 🎨 Visual Design Hints

- Use **JUCE 8 FlexBox** or manual positioning in `resized()`.
- Channel strips are separated by thin vertical dividers.
- **Gradient meter colors**:
  - Green = –60 → –12 dBFS  
  - Yellow = –12 → –3 dBFS  
  - Red = –3 → 0 dBFS (clipping)
- Use helper mapping for dB to normalized range:
  ```cpp
  static float mapDbToNorm(float db, float min, float max);
  ```
- Pan indicator can be drawn using `juce::Graphics::drawArc()` or a custom `Path`.

---

## 🔩 Technical Targets

- **Framework:** JUCE 8  
- **Audio Backend:** Tracktion Engine  
- **Data Binding:** atomics updated from audio thread (e.g., `ChannelState`)  
- **GUI Update Rate:** ~60 Hz timer  
- **Display Layout:** exactly four 25 %-width channel strips across the mixer view  
- Must compile and render within the existing MusicPI host window.

---

## 📄 ASCII Reference Layout

```
+--------------------------------------------------------------------------------------+
|                               OptionsBarComponent                                    |
|--------------------------------------------------------------------------------------|
|  [ Ch. 1 Name ]   [ Ch. 2 Name ]   [ Ch. 3 Name ]   [ Ch. 4 Name ]                  |
|     (25%)             (25%)             (25%)             (25%)                      |
+--------------------------------------------------------------------------------------+
|                                 MixerComponent                                       |
|--------------------------------------------------------------------------------------|
|  ┌─────────────── Channel (25%) ───────────────┐  ... four horizontally ...          |
|  | +----------------------------------------+ +------------------------------+ |     |
|  | | Gain Axis | Meter | dBFS+ Scale        | | | Indicators (vertical):     | |     |
|  | | +12→-∞    | ████  | 0→-60             | | | [MUTE/SOLO]                | |     |
|  | |           | ████  |                   | | | [PLUGINS: 2]              | |     |
|  | |           | ████  |                   | | | [EQ curve]                 | |     |
|  | |           | ████  |                   | | | [AUX: 1]                   | |     |
|  | |           | ████  |                   | | | [PAN ◉]                    | |     |
|  | +----------------------------------------+ +------------------------------+ |     |
+--------------------------------------------------------------------------------------+
|                               KnobBarComponent                                       |
|--------------------------------------------------------------------------------------|
|  [ Gain: 0.0 dB ] [ Gain: -2.5 dB ] [ Gain: 5.9 dB ] [ Gain: 0.0 dB ]               |
|     (25%)             (25%)             (25%)             (25%)                      |
+--------------------------------------------------------------------------------------+
```

---

**End of Specification**

# **Channel Details Specification**

Channels should be "expandable", in "compact" form, title in mixer component optionsbar we shouldnt show the gain and +dBFS axis with the meter, just the graphical ui meter, a line to indicate gain level (gain value shown in mixer component knob bar)
next to the meter we should have the vertical indicators still

## Expanding
The mixercomponent option bar should be a toggleable option, label is the channel name, when toggled on, channel component expands, when toggled off channel is compact version
