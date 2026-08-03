# Third-party material

Everything bundled in or derived from another project, and the terms it arrives
under. 240-MP itself is GPL-3.0 (see [LICENSE](LICENSE)); the licences below are
compatible with it, and their notices are preserved here as those licences
require.

Fonts are also credited in the [README](README.md); this file additionally
records the licence texts and, for ported code, which files derive from what.

---

## NostalgiaBox

- **Project:** https://github.com/landonbtw/NostalgiaBox
- **Author:** [landonbtw](https://github.com/landonbtw)
- **Licence:** MIT

TV MODE's behaviour is modelled on NostalgiaBox. The files below carry a
`Ported from NostalgiaBox's ... (MIT — see THIRD-PARTY.md)` notice in their
headers and point here.

### Ported

| This project | From NostalgiaBox | Notes |
|---|---|---|
| `src/modules/tv_mode/Channel.h` / `.cpp` | `nostalgiabox/channel.py` | Channel model, episode ordering, and the broadcast timeline |
| `src/modules/tv_mode/TvOverlay.h` / `.cpp` | `nostalgiabox/overlay.py` | ASS on-screen display. Geometry re-derived for composite: NostalgiaBox lays out a 960×720 frame inside a 1280×720 canvas for HDMI, whereas here the whole 640×480 canvas is the picture, with every coordinate and font size scaled by 2/3 |
| `src/modules/tv_mode/FillerAssets.h` / `.cpp` | `nostalgiabox/static_gen.py` | Static, glitch and colour-bars generation. Rendered at the composite framebuffer's own resolution, and generated asynchronously rather than from an installer step |
| `src/modules/tv_mode/ShuffleBag.h` | `nostalgiabox/playlist.py` (`ShuffleBag`) | Every item once before any repeats |
| `src/modules/tv_mode/CecInput.h` / `.cpp` | `nostalgiabox/input/cec.py` | Ported in spirit — HDMI-CEC input via `cec-client` |

### Design informed by, but not ported

These reference NostalgiaBox in comments to explain *why* the code is shaped as
it is. No code was carried across.

| This project | Relationship |
|---|---|
| `src/player/MpvController.h` | The EOF-suppression guard mirrors the one in `player.py`, which exists because replacing a file also produces end-of-file signals for the outgoing one |
| `src/modules/tv_mode/DurationCache.h` | Deliberately diverges: NostalgiaBox probes durations synchronously on first tune-in, which stalls on a large library, so this probes in the background and caches to disk |
| `src/modules/tv_mode/TvOverlay.h` | Style defaults are NostalgiaBox's values rescaled; its green bloom is available via `ui_bloom` but is not the default, because bloom is low-contrast mush over composite |
| `src/modules/tv_mode/TvBackend.h` | Channel-change transition defaults to `static` where NostalgiaBox defaults to none |

### Licence text

```
MIT License

Copyright (c) 2026 NostalgiaBox contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Fonts

Both ship in `assets/fonts/` and are inherited from upstream 240-MP.

### VCR OSD Mono

`assets/fonts/VCR_OSD_MONO_1.001.ttf` — created by Riciery Santos Leal
(a.k.a. mrmanet), distributed by its author as a free font via
https://www.dafont.com/vcr-osd-mono.font

Full terms: [assets/fonts/LICENSE-vcr-osd-mono.txt](assets/fonts/LICENSE-vcr-osd-mono.txt)

### GNU Unifont

`assets/fonts/unifont.otf` — used as a fallback for characters VCR OSD Mono
does not cover. Copyright © 1998–2023 Roman Czyborra, Paul Hardy, Qianqian Fang,
Andrew et al. Licensed under the SIL Open Font License v1.1.
https://unifoundry.com/unifont/

Full terms: [assets/fonts/LICENSE-unifont.txt](assets/fonts/LICENSE-unifont.txt)
