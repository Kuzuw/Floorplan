# Third-party notices

Floorplan's own source code (everything outside `external/`) is licensed under the
GNU General Public License, version 3 or (at your option) any later version. See
[LICENSE](LICENSE).

Floorplan is built on third-party software. Its sources are **not** copied into
this repository's own code. JUCE is pulled in as a git submodule at
`external/JUCE`, and the components below ship inside it. This file lists what
ends up in a Floorplan binary and under which terms.

## JUCE

- **Project:** JUCE 8.0.16 (tag `8.0.16-security`), https://juce.com, https://github.com/juce-framework/JUCE
- **Copyright:** Raw Material Software Limited
- **License:** dual-licensed under the [GNU Affero General Public License v3](https://www.gnu.org/licenses/agpl-3.0.en.html) or the commercial [JUCE 8 licence](https://juce.com/legal/juce-8-licence/). Floorplan uses JUCE under the **AGPLv3**.
- **Full text:** `external/JUCE/LICENSE.md`

### What this means for Floorplan binaries

GPLv3 section 13 lets you combine GPLv3 code with AGPLv3 code. Each part keeps
its own license, but the combined work must also satisfy AGPLv3 section 13
(remote network interaction). Floorplan has no network features, so in practice
this adds nothing for a desktop plugin. Anyone who redistributes a modified
binary still has to provide the complete corresponding source code, including
the JUCE version used, under these terms.

If you hold a commercial JUCE licence, you may build Floorplan against JUCE
under that licence instead. Floorplan's own code stays GPL-3.0-or-later either
way.

## Components bundled with JUCE that are compiled into Floorplan

The Floorplan build enables only the modules it needs (`juce_audio_utils`,
`juce_audio_formats` and their dependencies), so only the following JUCE
dependencies are compiled in. Each license text is in the listed path inside
`external/JUCE/modules/`.

| Component | Used for | License | License file |
| --- | --- | --- | --- |
| VST3 SDK (Steinberg Media Technologies GmbH) | VST3 plugin format | MIT | `juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt` |
| pslextensions (PreSonus) | Host context extensions in the VST3 wrapper | Public domain | `juce_audio_processors_headless/format_types/pslextensions/ipslcontextinfo.h` |
| FLAC (Xiph.Org Foundation) | Loading FLAC samples | BSD 3-clause | `juce_audio_formats/codecs/flac/Flac Licence.txt` |
| Ogg Vorbis (Xiph.Org Foundation) | Loading Ogg samples | BSD 3-clause | `juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt` |
| zlib (Jean-loup Gailly, Mark Adler) | Compression used by JUCE core | zlib | `juce_core/zip/zlib/README` |
| libpng | PNG decoding (GUI) | zlib/libpng | `juce_graphics/image_formats/pnglib/LICENSE` |
| libjpeg (Independent JPEG Group) | JPEG decoding (GUI) | IJG License | `juce_graphics/image_formats/jpglib/README` |
| HarfBuzz | Text shaping (GUI) | "Old MIT" | `juce_graphics/fonts/harfbuzz/COPYING` |
| SheenBidi (Muhammad Tayyab Akram) | Bidirectional text (GUI) | Apache 2.0 | `juce_graphics/unicode/sheenbidi/LICENSE` |

Not compiled in: AAX SDK, ASIO SDK, AudioUnit SDK, LV2 SDK, Oboe (Android),
CHOC/QuickJS (`juce_javascript`), Box2D and GLEW (`juce_opengl`). If you enable
another format or module in your own build, check that component's license
first. The ASIO and AAX SDKs in particular carry proprietary or GPLv3-only terms.

This software is based in part on the work of the Independent JPEG Group.

## Fonts

Two typefaces are embedded in the plugin for its user interface. They are
kept in `Resources/fonts/` as downloaded from Google Fonts and are not modified.

| Font | Files | Copyright | License |
| --- | --- | --- | --- |
| Geist | `Geist-Regular.ttf`, `Geist-Medium.ttf`, `Geist-SemiBold.ttf` | 2024 The Geist Project Authors (https://github.com/vercel/geist-font) | SIL Open Font License 1.1, `Resources/fonts/OFL-Geist.txt` |
| Space Grotesk | `SpaceGrotesk-Medium.ttf`, `SpaceGrotesk-Bold.ttf` | 2020 The Space Grotesk Project Authors (https://github.com/floriankarsten/space-grotesk) | SIL Open Font License 1.1, `Resources/fonts/OFL-SpaceGrotesk.txt` |

The OFL allows the fonts to be bundled and embedded in software under any
license, as long as the fonts are not sold on their own and their license
travels with them.

## Trademarks

VST is a registered trademark of Steinberg Media Technologies GmbH. The VST3
logo is not used by this project. Other product names mentioned in the
documentation (host applications in particular) are trademarks of their
respective owners. They are mentioned only to describe compatibility testing.

## Factory content

All factory patterns, kits and synthesized sounds in Floorplan were created for
this project and are covered by the project license. Floorplan contains no
third-party audio samples.
