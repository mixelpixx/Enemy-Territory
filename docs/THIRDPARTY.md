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
| 2026-06-10 | src/renderer2/etl/** | ET:Legacy v2.84.0 @ 764ffc00a953e59aaf435272d004c49a89710309 src/renderer2/*.c\|h | GPL-3.0 | verbatim | vendored into src/renderer2/ (etl/, etl/common/, glsl/, header closure) — renderer2 (GLSL, GL 3.3 core) sources |
| 2026-06-10 | src/renderer2/etl/common/** | ET:Legacy v2.84.0 @ 764ffc00a953e59aaf435272d004c49a89710309 src/renderercommon/* | GPL-3.0 | verbatim | vendored into src/renderer2/ (etl/, etl/common/, glsl/, header closure) — renderercommon (tr_common, image loaders, font); tr_common_vulkan.c/tr_image_svg.c do not exist at this pin |
| 2026-06-10 | src/renderer2/glsl/**, src/renderer2/gldef/default.gldef | ET:Legacy v2.84.0 @ 764ffc00a953e59aaf435272d004c49a89710309 src/renderer2/glsl/** + gldef/default.gldef | GPL-3.0 | verbatim | vendored into src/renderer2/ (etl/, etl/common/, glsl/, header closure) — GLSL shaders + shdr/ (shaders2h host tool) + default.gldef |
| 2026-06-10 | src/renderer2/etlhdr/** | ET:Legacy v2.84.0 @ 764ffc00a953e59aaf435272d004c49a89710309 src/qcommon/, src/game/, src/renderercommon/ headers | GPL-3.0 | verbatim | vendored into src/renderer2/ (etl/, etl/common/, glsl/, header closure) — private header closure (10 qcommon + surfaceflags.h + 3 renderercommon duplicates for `../renderercommon/` include shape); headers only, no engine .c |
| 2026-06-10 | src/renderer2/etlsrc/qcommon/q_math.c, q_shared.c | ET:Legacy v2.84.0 @ 764ffc00a953e59aaf435272d004c49a89710309 src/qcommon/q_math.c, q_shared.c | GPL-3.0 | verbatim | R2-2 Task 2 — the shared math (vec3/mat4/quat/Matrix*/color tables) and parser/string utilities (COM_Parse*, Q_str*, va, Info_*) the renderer tree links against; compiled into etrm_renderer2.dll. No source edits (C_ONLY compile-def selects the portable-C BoxOnPlaneSide path) |
| 2026-06-10 | vendor/tinydir.h | ET:Legacy v2.84.0 vendor/tinydir.h (tinydir authors, 2013-2016) | BSD-2-Clause | verbatim | R2-2 Task 2 — single-header dir-iteration helper required by the shdr2h host tool (`#include "../../../../vendor/tinydir.h"`); vendored at the upstream-relative path so the existing include resolves with no source edit. BSD-2-Clause is GPLv3-compatible |
| 2026-06-10 | (FetchContent) GLEW | nigels-com/glew glew-2.2.0 source release (SHA256 a9046a91…e0d4) | BSD-3-Clause / MIT (Modified BSD + Mesa3D + Khronos) | reference (built from source) | R2-2 Task 2 — OpenGL entry-point loader for renderer2 (GL 3.3 core). Fetched as the pre-amalgamated source release, built as static `etrm_glew` with GLEW_STATIC/GLEW_NO_GLU. BSD/MIT is GPLv3-compatible |
| 2026-06-10 | (FetchContent) zlib | madler/zlib v1.3.1 | zlib | reference (built from source) | R2-2 Task 2 — inflate/crc + headers required because ET:Legacy's tr_public.h `#include <zlib.h>` unconditionally and the refimport carries zlib_compress/zlib_crc32. Built as `zlibstatic`; examples/tests disabled (ZLIB_BUILD_EXAMPLES OFF). zlib license is GPLv3-compatible |
| 2026-06-10 | src/renderer2/etlsrc/qcommon/puff.c | ET:Legacy v2.84.0 @ 764ffc00a953e59aaf435272d004c49a89710309 src/qcommon/puff.c | zlib (Mark Adler's puff) | verbatim | R2-3 Task 2 — tiny standalone DEFLATE decompressor required by renderer2's tr_image_png.c (`#include "../qcommon/puff.h"`, calls puff()). Vendored verbatim (byte-identical) into the DLL closure so PNG decode works without pulling the engine's copy across the bridge. puff.h was already in etlhdr/. Mark Adler's puff is zlib-licensed, GPLv3-compatible. No source edits |
| 2026-06-10 | src/renderer2/bridge/tr2_jpeg_compat.c\|h | RM-original glue (standard libjpeg source-manager pattern, cf. ioquake3) | GPL-3.0 | original | R2-3 Task 1 — jpeg-6b compatibility shim: implements libjpeg-8's `jpeg_mem_src` (a jpeg_source_mgr over a memory buffer) so the vendored tr_image_jpg.c decodes from RAM against our bundled jpeg-6b (which lacks jpeg_mem_src). Also stubs the two jpeg-6b compressor symbols absent from our decode-only jpeg subset (jpeg_start_compress/jpeg_write_scanlines) as logged-once no-ops — JPEG screenshot SAVE is unavailable under gl2 (TGA used instead); DECODE is full. RM-original, GPLv3 |

**SDL2** is now fetched via CMake `FetchContent` (pinned to release-2.30.9) as the
platform layer for window/GL context/input — its **zlib** license is GPLv3-compatible.
**GLEW** (glew-2.2.0) and **zlib** (v1.3.1) are likewise fetched via `FetchContent`
(pinned) for the renderer2 DLL — see the port-log rows above.

## Bundled libraries (Phase 4 will modernize)

| Library | Bundled version | Location | Upstream license | Plan |
|---------|-----------------|----------|------------------|------|
| libcurl | 7.12.2 (2004) | `src/curl-7.12.2/` | curl (MIT-like) | → system libcurl behind `USE_INTERNAL_CURL` |
| libjpeg | 6b (1998) | `src/jpeg-6/` | IJG | → libjpeg-turbo behind `USE_INTERNAL_JPEG` |
| FreeType 2 | legacy | `src/ft2/` | FTL / GPLv2 | → system FreeType2 behind `USE_INTERNAL_FREETYPE` |
| zlib (unzip) | legacy | `src/qcommon/unzip.c` | zlib | → system zlib behind `USE_INTERNAL_ZLIB` |
