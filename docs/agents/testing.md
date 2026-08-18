# Testing

Run from the repository root:

```bash
./mpi build headless
./mpi test
```

Filter GoogleTest suites by invoking the generated binary directly:

```bash
build/maschinepi_tests_artefacts/Release/maschinepi_tests \
  --gtest_filter='InputManagerTest.*'
```

Avoid exporting `GTEST_FILTER` around `./mpi test`: CTest's PRE_TEST discovery
inherits it and can leave the generated registry narrowed until the test
binary is relinked.

Tests live under `tests/` and use GoogleTest. Follow arrange/act/assert and use
fixtures for shared JUCE, Tracktion, filesystem, or controller state.

## Harnesses

- `JuceHarness` initialises headless JUCE and can mount a component at the
  960x272 hardware surface size.
- `MockControllerHost` disables hardware, records button brightness/display
  frame writes, and can inject named button events.
- `EngineHarness` owns a lightweight Tracktion Engine/AudioEngine pair and
  creates temporary WAV samples.

See [the harness inventory](../../tests/harness/README.md) for exact APIs.

Use `WindowManager` with these harnesses for Widget routing tests. Assertions
should cover the physical resource path (d/k/p IDs and LED state) when the
behaviour is intended for MK3 hardware.

For emulator and physical-map verification:

```bash
./mpi emulator test
./mpi emulator qa
```

Coverage is available through `./mpi test --coverage` (with `gcovr`
installed for the report).

The physical acceptance checklist is in
[mk3-parity-acceptance.md](../mk3-parity-acceptance.md).
