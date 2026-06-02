# Third-Party Code & License Provenance

This file tracks every piece of code ported into the **Enemy Territory RM**
("RM") tree from an external project, and the license implications of each.

## Base license

The RM tree descends from the **Wolfenstein: Enemy Territory GPL Source Release**
(id Software, 20 August 2010), licensed under the **GNU GPL v3** ("version 3 of
the License, or (at your option) any later version" — see [`COPYING.txt`](../COPYING.txt)
and the header in every source file). Unlike the 1999 Quake 3 release (GPLv2), the
2010 RTCW/ET releases are **GPLv3**. The whole RM work is therefore **GPLv3**.

## License compatibility policy

A GPLv3 base is the *permissive* end for our purposes: GPLv3 code is directly
usable, and "GPLv2 **or later**" code (which the Q3-lineage forks carry) upgrades
cleanly into a GPLv3 combined work.

| Source project | License | How we may use it |
|----------------|---------|-------------------|
| **ET: Legacy** | GPL-3.0 | **Port verbatim.** Same license as our base, same game — the *preferred* port source. |
| **ioquake3**   | GPL-2.0-or-later | Port verbatim; combines into our GPLv3 work. |
| **Quake3e**    | GPL-2.0-or-later | Port verbatim; combines into GPLv3. |
| **ETe**        | GPL-2.0-or-later | Port verbatim; combines into GPLv3. |

> ⚠️ Guard against **GPLv2-*only*** code (no "or later" clause) — that is *not*
> combinable into a GPLv3 work. Verify the upstream license grant before copying
> any third-party snippet and log it below.

**Rule:** prefer ET:Legacy (same game, same license) and the Q3-lineage forks for
verbatim ports. Permissive deps (mimalloc MIT, zlib-ng zlib, zstd BSD,
libjpeg-turbo BSD/IJG, Recast/Detour zlib, SDL2/3 zlib) are all GPLv3-compatible.

## Port log

Record each port here as it happens: what was copied/adapted, from where, into
which RM files, and the license consequence.

| Date | RM file(s) | Ported from | Source license | Verbatim / adapted / reference | Notes |
|------|-----------|-------------|----------------|--------------------------------|-------|
| 2026-06-01 | src/sys/sdl_glimp.c | ioquake3 code/sdl/sdl_glimp.c | GPL-2.0-or-later | adapted | SDL2 window + GL context; adapted to ET GLimp_* interface (replaces win_glimp.c) |
| 2026-06-01 | src/sys/sdl_input.c | ioquake3 code/sdl/sdl_input.c | GPL-2.0-or-later | adapted | SDL2 input + raw relative mouse; ET keymap + Sys_QueEvent contract (replaces win_input.c/DirectInput) |
| 2026-06-01 | src/sys/sdl_qgl.c | id win_qgl.c (GPLv3 base) + SDL loader | GPLv3 | adapted | qgl proc table sourced via SDL_GL_GetProcAddress (replaces win_qgl.c WGL loader) |
| 2026-06-01 | src/game/bg_fov.c | original (hor+ formula; technique ref ET:Legacy/ioquake3) | GPLv3 | original | hor+ widescreen FOV math; clean-room from the standard square-pixel formula |
| 2026-06-01 | src/sys/sdl_snd.c | ioquake3 code/sdl/snd_sdl.c | GPL-2.0-or-later | adapted | SDL2 audio backend (callback + DMA ring); ET SNDDMA_* contract (replaces win_snd.c DirectSound) |

**SDL2** is now fetched via CMake `FetchContent` (pinned to release-2.30.9) as the
platform layer for window/GL context/input — its **zlib** license is GPLv3-compatible.

## Bundled libraries (Phase 4 will modernize)

| Library | Bundled version | Location | Upstream license | Plan |
|---------|-----------------|----------|------------------|------|
| libcurl | 7.12.2 (2004) | `src/curl-7.12.2/` | curl (MIT-like) | → system libcurl behind `USE_INTERNAL_CURL` |
| libjpeg | 6b (1998) | `src/jpeg-6/` | IJG | → libjpeg-turbo behind `USE_INTERNAL_JPEG` |
| FreeType 2 | legacy | `src/ft2/` | FTL / GPLv2 | → system FreeType2 behind `USE_INTERNAL_FREETYPE` |
| zlib (unzip) | legacy | `src/qcommon/unzip.c` | zlib | → system zlib behind `USE_INTERNAL_ZLIB` |
