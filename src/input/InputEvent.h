#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Lightweight normalized representation for input events flowing through the
// dispatcher. The struct intentionally keeps JUCE types so we can reuse the
// existing helpers for encoding/decoding MIDI and key presses.
struct InputEvent
{
    enum class Type
    {
        Midi,
        Key,
        Controller,
        Pad,
        Button,
        Knob,
        Stepper,
        Touchstrip
    };

    Type type { Type::Midi };
    juce::String source;                   // Device identifier or component path
    juce::MidiMessage midiMessage;         // Valid when type == Midi
    juce::KeyPress    keyPress;            // Valid when type == Key
    juce::NamedValueSet metadata;          // Optional context (e.g. device name)
    mutable bool consumed { false };        // Set to true to prevent other bindings from handling this event

    // ── Metadata convenience accessors ───────────────────────────────────
    // The dispatcher packs event state as NamedValueSet entries so every
    // caller would otherwise duplicate the contains() + cast dance. These
    // are lossless and return the expected default when the key is absent.

    bool isPressed() const
    {
        return metadata.contains("pressed")
            && static_cast<bool>(metadata["pressed"]);
    }

    bool isShift() const
    {
        return metadata.contains("shift")
            && static_cast<bool>(metadata["shift"]);
    }

    int padIndex() const
    {
        return metadata.contains("pad")
            ? static_cast<int>(metadata["pad"]) : -1;
    }

    int stepperDirection() const
    {
        return metadata.contains("direction")
            ? static_cast<int>(metadata["direction"]) : 0;
    }

    static InputEvent makeMidi(const juce::MidiMessage& message,
                               juce::String sourceId,
                               juce::NamedValueSet extra = {});

    static InputEvent makeKey(const juce::KeyPress& key,
                              juce::String sourceId,
                              juce::NamedValueSet extra = {});

    static InputEvent makeController(juce::String controlId,
                                     juce::String sourceId,
                                     juce::NamedValueSet extra = {});

    static InputEvent makePad(uint8_t pad,
                              bool pressed,
                              uint16_t pressure,
                              bool shift,
                              juce::String sourceId,
                              juce::NamedValueSet extra = {});

    static InputEvent makeButton(juce::String buttonName,
                                 bool pressed,
                                 bool shift,
                                 juce::String sourceId,
                                 juce::NamedValueSet extra = {});

    static InputEvent makeKnob(juce::String knobName,
                                int16_t delta,
                                uint16_t absolute,
                                bool shift,
                                juce::String sourceId,
                                juce::NamedValueSet extra = {});

    static InputEvent makeStepper(int8_t direction,
                                   uint8_t position,
                                   bool shift,
                                   juce::String sourceId,
                                   juce::NamedValueSet extra = {});

    static InputEvent makeTouchstrip(uint8_t finger,
                                     bool touching,
                                     uint16_t position,
                                     bool shift,
                                     juce::String sourceId,
                                     juce::NamedValueSet extra = {});
};

inline InputEvent InputEvent::makeMidi(const juce::MidiMessage& message,
                                       juce::String sourceId,
                                       juce::NamedValueSet extra)
{
    InputEvent ev;
    ev.type        = Type::Midi;
    ev.source      = std::move(sourceId);
    ev.midiMessage = message;
    ev.metadata    = std::move(extra);
    return ev;
}

inline InputEvent InputEvent::makeKey(const juce::KeyPress& key,
                                      juce::String sourceId,
                                      juce::NamedValueSet extra)
{
    InputEvent ev;
    ev.type     = Type::Key;
    ev.source   = std::move(sourceId);
    ev.keyPress = key;
    ev.metadata = std::move(extra);
    return ev;
}

inline InputEvent InputEvent::makeController(juce::String controlId,
                                             juce::String sourceId,
                                             juce::NamedValueSet extra)
{
    extra.set("controlId", controlId);

    InputEvent ev;
    ev.type     = Type::Controller;
    ev.source   = std::move(sourceId);
    ev.metadata = std::move(extra);
    return ev;
}

inline InputEvent InputEvent::makePad(uint8_t pad,
                                      bool pressed,
                                      uint16_t pressure,
                                      bool shift,
                                      juce::String sourceId,
                                      juce::NamedValueSet extra)
{
    extra.set("pad", static_cast<int>(pad));
    extra.set("pressed", pressed);
    extra.set("pressure", static_cast<int>(pressure));
    extra.set("shift", shift);

    InputEvent ev;
    ev.type     = Type::Pad;
    ev.source   = std::move(sourceId);
    ev.metadata = std::move(extra);
    return ev;
}

inline InputEvent InputEvent::makeButton(juce::String buttonName,
                                         bool pressed,
                                         bool shift,
                                         juce::String sourceId,
                                         juce::NamedValueSet extra)
{
    extra.set("buttonName", buttonName);
    extra.set("pressed", pressed);
    extra.set("shift", shift);

    InputEvent ev;
    ev.type     = Type::Button;
    ev.source   = std::move(sourceId);
    ev.metadata = std::move(extra);
    return ev;
}

inline InputEvent InputEvent::makeKnob(juce::String knobName,
                                       int16_t delta,
                                       uint16_t absolute,
                                       bool shift,
                                       juce::String sourceId,
                                       juce::NamedValueSet extra)
{
    extra.set("knobName", knobName);
    extra.set("delta", static_cast<int>(delta));
    extra.set("absolute", static_cast<int>(absolute));
    extra.set("shift", shift);

    InputEvent ev;
    ev.type     = Type::Knob;
    ev.source   = std::move(sourceId);
    ev.metadata = std::move(extra);
    return ev;
}

inline InputEvent InputEvent::makeStepper(int8_t direction,
                                          uint8_t position,
                                          bool shift,
                                          juce::String sourceId,
                                          juce::NamedValueSet extra)
{
    extra.set("direction", static_cast<int>(direction));
    extra.set("position", static_cast<int>(position));
    extra.set("shift", shift);

    InputEvent ev;
    ev.type     = Type::Stepper;
    ev.source   = std::move(sourceId);
    ev.metadata = std::move(extra);
    return ev;
}

inline InputEvent InputEvent::makeTouchstrip(uint8_t finger,
                                             bool touching,
                                             uint16_t position,
                                             bool shift,
                                             juce::String sourceId,
                                             juce::NamedValueSet extra)
{
    extra.set("finger", static_cast<int>(finger));
    extra.set("touching", touching);
    extra.set("position", static_cast<int>(position));
    extra.set("shift", shift);

    InputEvent ev;
    ev.type     = Type::Touchstrip;
    ev.source   = std::move(sourceId);
    ev.metadata = std::move(extra);
    return ev;
}
