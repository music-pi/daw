# Test harness inventory

## JuceHarness

`JuceHarness.h/.cpp` provides:

- `JuceFrameworkContext`, which owns `juce::ScopedJuceInitialiser_GUI`;
- `ComponentHost`, which owns one root component at 960x272 by default and
  supports `mount<T>()`, `setBounds()`, and `getRoot()`.

The current `flushMessageQueue()` hook is intentionally a no-op. Tests that
need a running asynchronous JUCE dispatch loop must provide one explicitly,
as the audio-routing and emulator-protocol suites do.

## MockControllerHost

`MockControllerHost.h/.cpp` constructs `ControllerHost` with hardware disabled.
It:

- injects named button events with `triggerButton()`;
- records button brightness writes;
- counts display frame pushes.

Use `WindowManager::getHardwareState()` to inspect indexed pad/LED ownership
and values that are not recorded directly by the mock.

## EngineHarness

`EngineHarness.h/.cpp` owns:

- a lightweight `tracktion::engine::Engine`;
- MusicPI `AudioEngine` and `SamplerInstrument` accessors;
- `createEmptyEdit()`;
- temporary WAV generation through `createTemporarySampleFile()`.

The harness removes its temporary root during teardown and does not require
real audio hardware.

## Typical Widget fixture

```cpp
class MyWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        engine.createEmptyEdit();
        windows.setAudioEngine(&engine.audio());
        windows.setControllerHost(&controller);
    }

    testharness::JuceFrameworkContext juce;
    testharness::EngineHarness engine;
    testharness::MockControllerHost controller;
    WindowManager windows;
};
```

Prefer driving hardware-visible behaviour through `WindowManager` rather than
calling Widget callbacks directly.
