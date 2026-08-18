# Releasing MusicPI DAW

This document describes the source and binary release gates for the public
`music-pi/daw` repository. The DAW repository is released independently from
the `music-pi/mpi-station` image integrator.

## Before tagging

Use a clean checkout and initialize every nested submodule:

```bash
git clone --recursive https://github.com/music-pi/daw.git
cd daw
git submodule status --recursive
```

Confirm that the recorded pins are intentional. In particular, `external/mk3`
must point at the released `music-pi/libmk3` commit and Tracktion Engine/JUCE
must remain at the reviewed pins in `.gitmodules`.

Run the public release gates:

```bash
./mpi build headless
./mpi test
```

The test command must report zero failures. Tests requiring a connected MK3
may be skipped when hardware is unavailable; record those skips in the release
notes. A release should also include a real-MK3 smoke check when hardware is
available.

Review the staged tree for accidental private material, credentials, sample
files, and generated build output before committing or tagging:

```bash
git status --short
git ls-files '*.wav' '*.aiff' '*.flac' '*.mp3' '*.ogg'
git submodule status --recursive
```

Do not add third-party sample packs to this repository unless their license
explicitly permits redistribution.

## Tagging a source release

1. Update the user-visible version and release notes.
2. Commit the release changes on `main`.
3. Create an annotated tag, for example `v0.1.0`.
4. Push the commit and tag to `music-pi/daw`.
5. Let the Linux CI workflow complete successfully for the tag/commit.

The tag must identify the exact source commit and therefore the exact
submodule pins. Do not retarget submodules to branches as part of a release.

## Optional headless artifact

If a release distributes the Linux headless executable, build it from the
tagged checkout and publish it as a GitHub Release asset rather than committing
it to the repository:

```bash
./mpi build headless
tar -C build/maschinepi_artefacts/Release \
  -czf maschinepi-headless-<version>-linux-arm64.tar.gz maschinepi
sha256sum maschinepi-headless-<version>-linux-arm64.tar.gz
```

Publish the archive and its SHA-256 value together with the tag and submodule
pin list. The Raspberry Pi image is produced and released by
`music-pi/mpi-station`, not by this repository.

## Release notes should state

- the source tag and commit;
- the recursive submodule pins;
- the platforms/build mode covered;
- test totals and any hardware skips;
- whether a headless artifact was published and its SHA-256;
- known limitations and the next planned milestone.
