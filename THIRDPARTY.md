# Third-Party Code & License Provenance

This file tracks every piece of code ported into the **Enemy Territory RM**
("RM") tree from an external project, and the license implications of each.

## Base license

The RM tree descends from the **Wolfenstein: Enemy Territory GPL Source Release**
(id Software, 20 August 2010), licensed under the **GNU GPL v2** (see
[`COPYING.txt`](COPYING.txt)). Unless noted below, all original engine code
remains GPLv2.

## License compatibility policy

| Source project | License | How we may use it |
|----------------|---------|-------------------|
| **ioquake3**   | GPL-2.0 | Port verbatim. Compatible — no relicensing needed. |
| **Quake3e**    | GPL-2.0 | Port verbatim. Compatible. |
| **ETe**        | GPL-2.0 | Port verbatim. Compatible. |
| **ET: Legacy** | GPL-3.0 | **Technique reference only.** Prefer rewriting from scratch. Copying ET:Legacy code makes the *combined distributable* GPLv3. If copied, isolate in an optional module (e.g. `renderer2`) and log it below. |

**Rule:** prefer a GPLv2 source for any code copied verbatim. ET:Legacy is used
to understand *how* a problem was solved on this exact game, not as a copy source,
unless no GPLv2 equivalent exists.

## Port log

Record each port here as it happens: what was copied/adapted, from where, into
which RM files, and the license consequence.

| Date | RM file(s) | Ported from | Source license | Verbatim / adapted / reference | Notes |
|------|-----------|-------------|----------------|--------------------------------|-------|
| _(none yet)_ | | | | | |

## Bundled libraries (Phase 4 will modernize)

| Library | Bundled version | Location | Upstream license | Plan |
|---------|-----------------|----------|------------------|------|
| libcurl | 7.12.2 (2004) | `src/curl-7.12.2/` | curl (MIT-like) | → system libcurl behind `USE_INTERNAL_CURL` |
| libjpeg | 6b (1998) | `src/jpeg-6/` | IJG | → libjpeg-turbo behind `USE_INTERNAL_JPEG` |
| FreeType 2 | legacy | `src/ft2/` | FTL / GPLv2 | → system FreeType2 behind `USE_INTERNAL_FREETYPE` |
| zlib (unzip) | legacy | `src/qcommon/unzip.c` | zlib | → system zlib behind `USE_INTERNAL_ZLIB` |
