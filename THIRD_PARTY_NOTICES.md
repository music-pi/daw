# Third-party notices

MusicPI includes third-party projects as pinned Git submodules. Those projects
retain their own copyright and license terms; the MusicPI GPL-3.0-only license
does not replace them.

The commit identifiers below are the versions referenced by this repository.
Clone with `--recursive` to obtain their complete source and bundled license
files.

## Tracktion Engine

- Source: <https://github.com/Tracktion/tracktion_engine>
- Pinned commit: `2877b621f2fbee564d0696a616b86bf8ba8c8ab0`
- License: GPL-3.0-or-later or a commercial Tracktion Engine license
- Local license: [`external/tracktion_engine/LICENSE.md`](external/tracktion_engine/LICENSE.md)

Tracktion Engine contains additional third-party components under their own
licenses. Its README and source tree provide the applicable notices and license
texts.

## JUCE

JUCE is nested inside the pinned Tracktion Engine submodule.

- Source: <https://github.com/juce-framework/JUCE>
- Pinned commit: `7c89e11f6b7316c369f3d3f22227c60e816e738b`
- License: AGPL-3.0 or a commercial JUCE license
- Local license:
  [`external/tracktion_engine/modules/juce/LICENSE.md`](external/tracktion_engine/modules/juce/LICENSE.md)

JUCE contains additional third-party components under their own licenses. Its
license file identifies those components and their corresponding license texts.

## libmk3

- Source: <https://github.com/music-pi/libmk3>
- Pinned commit: `ca3627a2ab4a84f41d0f7e5a0fd7fe26e737d025`
- License: MIT
- Local license: [`external/mk3/LICENSE`](external/mk3/LICENSE)
- Local notices:
  [`external/mk3/THIRD_PARTY_NOTICES.md`](external/mk3/THIRD_PARTY_NOTICES.md)

libmk3 is an original C implementation whose Maschine MK3 protocol mappings
were informed by the permissively licensed `ni-controllers-lib` project and
the CABL Maschine MK3 work. Its own third-party notice preserves the relevant
ISC and MIT attributions.
