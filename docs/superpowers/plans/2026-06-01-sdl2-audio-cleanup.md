# SDL2 Audio + Platform Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Windows-only DirectSound audio backend with a cross-platform SDL2 backend, and delete now-orphaned Win32 platform code — completing the SDL2 platform migration and unblocking the cross-platform increment.

**Architecture:** Implement the engine's existing `SNDDMA_*` sound-backend contract (snd_local.h) on SDL2 in a new `src/sys/sdl_snd.c`, using an SDL audio callback that drains the `dma.buffer` ring the engine mixer already writes. The mixer (`snd_dma.c`/`snd_mix.c`) is unchanged. Delete `win_snd.c` (DirectSound), the orphaned `win_input.c`, and the dead `GlideIsValid()` 3Dfx remnant. Windows x64 only this increment.

**Tech Stack:** C, CMake + Ninja + MSVC x64, SDL2 (already fetched via FetchContent), ET id Tech 3 sound mixer.

**Design spec:** `docs/superpowers/specs/2026-06-01-sdl2-audio-cleanup-design.md`

**Build/run environment (use the PowerShell tool, NOT Bash — the `cmd /c` one-liner misbehaves under Bash on this host):**
- Build: `cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build'`
- Reconfigure (after CMake edits): same line with `&& cmake -S C:\repo\et-rm -B C:\repo\et-rm\build -G Ninja -DCMAKE_BUILD_TYPE=Debug` before the build.
- Tests: `cmd /c '"...vcvars64.bat" >nul 2>&1 && ctest --test-dir C:\repo\et-rm\build --output-on-failure'`
- Editor clang diagnostics like "SDL.h not found" are FALSE POSITIVES (the clang LSP lacks the FetchContent include path); the MSVC build is authoritative.

**Git:** branch `rm/sdl2-audio-cleanup` is already created off `rm/main` (the design spec is committed on it). Capture the BASE sha at task start with `git -C C:\repo\et-rm rev-parse HEAD`. Commit-message trailer (exact last line of every commit):
```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

**Feel gate:** `pmove_feel` must stay green (unchanged golden hash) — audio is outside the sim. Run `ctest` before merge.

---

## File Structure

- **Create** `src/sys/sdl_snd.c` — SDL2 audio backend implementing `SNDDMA_*`; one responsibility (drain the DMA ring to an SDL device via callback). Also exposes `Sys_SndPause(qboolean)` for focus muting.
- **Modify** `src/sys/sdl_local.h` — declare `void Sys_SndPause( qboolean pause )`.
- **Modify** `src/sys/sdl_input.c` — in `IN_Activate`, call `Sys_SndPause` so audio mutes on focus loss.
- **Modify** `cmake/Client.cmake` — drop `win_snd.c` + `dsound`/`dxguid`; add `sys/sdl_snd.c`.
- **Delete** `src/win32/win_snd.c` (DirectSound backend, replaced).
- **Delete** `src/win32/win_input.c` (orphaned since Increment 1).
- **Modify** `src/sys/sdl_qgl.c` — remove dead `GlideIsValid()` + its 3Dfx helper block.
- **Modify** `docs/THIRDPARTY.md` — log the `snd_sdl` port.

---

## Task 1: SDL2 audio backend (`sdl_snd.c`) + build swap

**Files:**
- Create: `src/sys/sdl_snd.c`
- Modify: `src/sys/sdl_local.h`, `src/sys/sdl_input.c`, `cmake/Client.cmake`
- Reference: `src/win32/win_snd.c` (the backend being replaced), `src/client/snd_local.h` (the `dma_t` + `SNDDMA_*` contract), ioquake3 `code/sdl/snd_sdl.c`

- [ ] **Step 1: Verify ET's mixer ring-size assumption (informs the ring sizing below)**

Read `src/client/snd_mix.c` and `src/client/snd_dma.c` for how `dma.samples` and `dma.samplepos`/`SNDDMA_GetDMAPos()` are used. Confirm the mixer masks the sample position with `(dma.samples - 1)` (Q3-lineage mixers do), which means **`dma.samples` MUST be a power of two**. The implementation below sizes the ring to a power of two for exactly this reason. If (unexpectedly) the mixer does not mask and instead uses modulo, the pow2 ring is still correct, so proceed either way — just note what you found.

- [ ] **Step 2: Create `src/sys/sdl_snd.c`**

```c
/*
===========================================================================

Wolfenstein: Enemy Territory GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Wolfenstein: Enemy Territory GPL Source Code.
It is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
See <http://www.gnu.org/licenses/>.

===========================================================================
*/

/*
** SDL_SND.C
**
** SDL2 audio backend for the ET RM remaster. Implements the engine's
** SNDDMA_* contract (src/client/snd_local.h) using an SDL audio callback
** that drains the dma.buffer ring the mixer (snd_dma.c / snd_mix.c) writes.
** Replaces the DirectSound backend (win_snd.c). The engine mixer is unchanged.
**
** Adapted from ioquake3's code/sdl/snd_sdl.c (GPL-2.0-or-later).
*/

#include <SDL.h>

#include "../client/snd_local.h"
#include "sdl_local.h"

static int      dmapos     = 0;        // play cursor, in (channel-)samples
static int      dmasize    = 0;        // ring size in BYTES
static qboolean snd_inited = qfalse;

cvar_t *s_sdlSamples;                   // device callback buffer (0 = auto)

/*
===============
SNDDMA_AudioCallback   (runs on SDL's audio thread)

Copy len bytes from the dma ring at the play cursor into SDL's stream,
wrapping the ring. Mirrors ioquake3's callback.
===============
*/
static void SNDDMA_AudioCallback( void *userdata, Uint8 *stream, int len ) {
	int pos = ( dmapos * ( dma.samplebits / 8 ) );
	if ( pos >= dmasize ) {
		dmapos = pos = 0;
	}

	if ( !snd_inited || !dma.buffer ) {
		memset( stream, '\0', len );
		return;
	} else {
		int tobufend = dmasize - pos;     // bytes to end of ring
		int len1 = len;
		int len2 = 0;

		if ( len1 > tobufend ) {
			len1 = tobufend;
			len2 = len - len1;
		}

		memcpy( stream, dma.buffer + pos, len1 );
		if ( len2 <= 0 ) {
			dmapos += ( len1 / ( dma.samplebits / 8 ) );
		} else {
			// wraparound
			memcpy( stream + len1, dma.buffer, len2 );
			dmapos = ( len2 / ( dma.samplebits / 8 ) );
		}
	}

	if ( dmapos >= dma.samples ) {
		dmapos = 0;
	}
}

static int SND_RoundUpPow2( int v ) {
	int p = 1;
	while ( p < v ) {
		p <<= 1;
	}
	return p;
}

/*
===============
SNDDMA_Init
===============
*/
qboolean SNDDMA_Init( void ) {
	SDL_AudioSpec desired, obtained;
	int freq, bits, channels, devSamples, ringSamples;

	if ( snd_inited ) {
		return qtrue;
	}

	if ( !s_sdlSamples ) {
		s_sdlSamples = Cvar_Get( "s_sdlSamples", "0", CVAR_ARCHIVE | CVAR_LATCH );
	}

	if ( !SDL_WasInit( SDL_INIT_AUDIO ) ) {
		if ( SDL_InitSubSystem( SDL_INIT_AUDIO ) < 0 ) {
			Com_Printf( "SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError() );
			return qfalse;
		}
	}

	// desired format from the existing ET cvars
	switch ( (int)s_khz->value ) {
	case 11: freq = 11025; break;
	case 44: freq = 44100; break;
	default: freq = 22050; break;
	}
	bits     = ( (int)s_bits->value == 8 ) ? 8 : 16;
	channels = ( (int)s_numchannels->value == 1 ) ? 1 : 2;

	// device callback buffer (power of two). auto-scale with frequency for low latency.
	if ( s_sdlSamples->integer > 0 ) {
		devSamples = SND_RoundUpPow2( s_sdlSamples->integer );
	} else {
		devSamples = ( freq <= 11025 ) ? 256 : ( freq <= 22050 ) ? 512 : 1024;
	}

	memset( &desired, 0, sizeof( desired ) );
	desired.freq     = freq;
	desired.format   = ( bits == 8 ) ? AUDIO_U8 : AUDIO_S16SYS;
	desired.channels = channels;
	desired.samples  = devSamples;
	desired.callback = SNDDMA_AudioCallback;

	if ( SDL_OpenAudio( &desired, &obtained ) < 0 ) {
		Com_Printf( "SDL_OpenAudio failed: %s\n", SDL_GetError() );
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		return qfalse;
	}

	// fill dma_t from the OBTAINED spec (SDL may have changed it)
	dma.channels         = obtained.channels;
	dma.samplebits       = SDL_AUDIO_BITSIZE( obtained.format );
	dma.speed            = obtained.freq;
	dma.submission_chunk = 1;

	// Ring buffer: power-of-two (channel-)samples, with headroom over the
	// device buffer. ET's mixer masks the sample position with (dma.samples-1),
	// so this MUST be a power of two (see Step 1).
	ringSamples = SND_RoundUpPow2( obtained.samples * obtained.channels * 8 );
	if ( ringSamples < 0x8000 ) {
		ringSamples = 0x8000;
	}
	dma.samples = ringSamples;
	dmasize     = dma.samples * ( dma.samplebits / 8 );
	dma.buffer  = (byte *)calloc( 1, dmasize );
	dmapos      = 0;

	Com_Printf( "SDL audio: %d Hz %d-bit %dch; dev buffer %d samples; ring %d samples\n",
				dma.speed, dma.samplebits, dma.channels, obtained.samples, dma.samples );

	SDL_PauseAudio( 0 );
	snd_inited = qtrue;
	return qtrue;
}

/*
===============
SNDDMA_GetDMAPos
===============
*/
int SNDDMA_GetDMAPos( void ) {
	return dmapos;
}

/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown( void ) {
	if ( !snd_inited ) {
		return;
	}
	SDL_PauseAudio( 1 );
	SDL_CloseAudio();
	SDL_QuitSubSystem( SDL_INIT_AUDIO );
	if ( dma.buffer ) {
		free( dma.buffer );
	}
	memset( (void *)&dma, 0, sizeof( dma ) );
	dmapos = dmasize = 0;
	snd_inited = qfalse;
}

/*
===============
SNDDMA_BeginPainting / SNDDMA_Submit

Serialize the main-thread mixer against the audio-thread callback.
===============
*/
void SNDDMA_BeginPainting( void ) {
	if ( snd_inited ) {
		SDL_LockAudio();
	}
}

void SNDDMA_Submit( void ) {
	if ( snd_inited ) {
		SDL_UnlockAudio();
	}
}

/*
===============
SNDDMA_Activate

Kept for the (still-compiled) win_wndproc.c contract. Focus-based muting is
driven through Sys_SndPause() from sdl_input.c's IN_Activate instead.
===============
*/
void SNDDMA_Activate( void ) {
}

/*
===============
Sys_SndPause

Pause/unpause the audio device (mute on focus loss). Declared in sdl_local.h,
called from sdl_input.c IN_Activate.
===============
*/
void Sys_SndPause( qboolean pause ) {
	if ( snd_inited ) {
		SDL_PauseAudio( pause ? 1 : 0 );
	}
}
```

- [ ] **Step 3: Declare `Sys_SndPause` in `src/sys/sdl_local.h`**

Add, alongside the existing `Sys_GetSDLWindow` declaration:
```c
void Sys_SndPause( qboolean pause );
```

- [ ] **Step 4: Mute audio on focus loss in `src/sys/sdl_input.c`**

In `IN_Activate`, add the `Sys_SndPause` call so alt-tab mutes cleanly:
```c
void IN_Activate( qboolean active ) {
	in_appactive = active;
	Sys_SndPause( !active );    // mute when the window loses focus
	if ( !active ) {
		IN_DeactivateMouse();
	}
}
```
(`sdl_local.h` is already included by `sdl_input.c`.)

- [ ] **Step 5: Swap the build in `cmake/Client.cmake`**

In `CLIENT_PLATFORM_SOURCES`, remove the `win_snd.c` line and add `sdl_snd.c`:
```cmake
    ${ETRM_SRC}/sys/sdl_snd.c        # SDL2 audio (replaces win_snd.c DirectSound)
```
(Delete the `${ETRM_SRC}/win32/win_snd.c` entry.)

In `target_link_libraries(etrm ...)`, remove `dsound` and `dxguid` (DirectInput was already removed in Increment 1; the only client TU that used DirectX GUIDs is the now-removed `win_snd.c` and the soon-deleted `win_input.c`). Keep `ole32`, `winmm`, etc. The resulting link line should read (order otherwise unchanged):
```cmake
    opengl32 gdi32 user32 winmm wsock32 ws2_32 iphlpapi
    ole32 advapi32 comctl32)
```

- [ ] **Step 6: Reconfigure + build**

```
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake -S C:\repo\et-rm -B C:\repo\et-rm\build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build C:\repo\et-rm\build'
```
Expected: `etrm.exe` links **without** `dsound`/`dxguid`. If the linker complains about a missing DirectX GUID symbol, something other than `win_snd.c`/`win_input.c` referenced it — investigate before forcing the lib back (likely a leftover `win_snd.c` reference because Step 5 wasn't applied). Note: `win_snd.c` is still on disk but no longer compiled (deleted in Task 2).

- [ ] **Step 7: Verify the SDL audio device opens (objective; audible test is the user's)**

```
$env:ETRM_TRACE="C:\repo\et-rm\build\snd_trace.log"
$home = "C:\repo\et-rm\build\sndhome"
Remove-Item -Recurse -Force $home -ErrorAction SilentlyContinue
$args = "+set fs_basepath `"C:\repo\enemy-territory-RM`" +set fs_homepath `"$home`" +set sv_pure 0 +set r_fullscreen 0 +set developer 1 +set logfile 2"
$p = Start-Process -FilePath "C:\repo\et-rm\build\bin\etrm.exe" -ArgumentList $args -PassThru
Start-Sleep -Seconds 9
if(!$p.HasExited){ Stop-Process -Id $p.Id -Force }
```
Then read `C:\repo\et-rm\build\sndhome\etmain\etconsole.log` and confirm the `SDL audio: <hz> <bits>-bit <ch>ch ...` line appears with **no** `SDL_OpenAudio failed` / `SDL_InitSubSystem(AUDIO) failed`, and the client reached the menu without crashing. (The audible test — music/SFX play, no crackle, alt-tab mutes — is the user's acceptance step in Task 5.)

- [ ] **Step 8: Commit**

```
git -C C:\repo\et-rm add src/sys/sdl_snd.c src/sys/sdl_local.h src/sys/sdl_input.c cmake/Client.cmake
git -C C:\repo\et-rm commit -m "client: SDL2 audio backend (sdl_snd) replacing DirectSound; focus mute

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Delete the DirectSound backend file

**Files:**
- Delete: `src/win32/win_snd.c`

- [ ] **Step 1: Confirm it is no longer referenced by the build**

```
git -C C:\repo\et-rm grep -n "win_snd.c" -- cmake/
```
Expected: no matches (Task 1 Step 5 removed it). If a match remains, fix `cmake/Client.cmake` first.

- [ ] **Step 2: Remove the file**

```
git -C C:\repo\et-rm rm src/win32/win_snd.c
```

- [ ] **Step 3: Rebuild to confirm nothing referenced it**

```
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build'
```
Expected: clean build (the file was already out of the source list, so this just confirms no dangling include/reference).

- [ ] **Step 4: Commit**

```
git -C C:\repo\et-rm commit -m "client: delete DirectSound backend win_snd.c (replaced by sdl_snd)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Delete the orphaned `win_input.c`

**Files:**
- Delete: `src/win32/win_input.c`

- [ ] **Step 1: Confirm it is not in any build target**

```
git -C C:\repo\et-rm grep -n "win_input.c" -- cmake/
```
Expected: no matches (removed from the build in Increment 1; SDL input lives in `src/sys/sdl_input.c`).

- [ ] **Step 2: Remove the file**

```
git -C C:\repo\et-rm rm src/win32/win_input.c
```

- [ ] **Step 3: Rebuild**

```
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build'
```
Expected: clean build. (`directInput` / `in_appactive` are defined in `sdl_input.c`, so removing `win_input.c` changes nothing for the link.)

- [ ] **Step 4: Commit**

```
git -C C:\repo\et-rm commit -m "cleanup: delete orphaned win_input.c (DirectInput; superseded by sdl_input)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Remove dead `GlideIsValid()` from `sdl_qgl.c`

**Files:**
- Modify: `src/sys/sdl_qgl.c`

- [ ] **Step 1: Confirm `GlideIsValid` has no callers**

```
git -C C:\repo\et-rm grep -n "GlideIsValid\|GR_NUM_BOARDS" -- src/
```
Expected: matches only inside `src/sys/sdl_qgl.c` (the definition + its helper macro). No call sites elsewhere — it was a 3Dfx-era probe whose only callers lived in the retired `win_glimp.c`.

- [ ] **Step 2: Remove the dead function and its 3Dfx helper block**

In `src/sys/sdl_qgl.c`, delete the `GlideIsValid()` function (around line 2849) and any now-unused 3Dfx-only helper it relied on (e.g. a `GR_NUM_BOARDS` macro and the `#if 0`/Glide `LoadLibrary` probe block it sits in). Remove only that dead block — do not touch the `qgl*` proc table or `QGL_Init`/`QGL_Shutdown`. Read the surrounding 40 lines first to scope the deletion precisely.

- [ ] **Step 3: Rebuild**

```
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build'
```
Expected: `etrm.exe` builds clean (no "undefined `GlideIsValid`" — confirming it had no callers).

- [ ] **Step 4: Commit**

```
git -C C:\repo\et-rm commit -m "cleanup: remove dead GlideIsValid 3Dfx remnant from sdl_qgl.c

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Final verification + provenance

**Files:**
- Modify: `docs/THIRDPARTY.md`

- [ ] **Step 1: Clean from-scratch build (proves reproducibility)**

```
Remove-Item -Recurse -Force C:\repo\et-rm\build -ErrorAction SilentlyContinue
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake -S C:\repo\et-rm -B C:\repo\et-rm\build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build C:\repo\et-rm\build'
```
Expected: all targets build (`etrm.exe`, `etrmded.exe`, the three modules, `etrm_feeltest.exe`, `etrm_fovtest.exe`, `SDL2d.dll`, `etmain\zz_rm_ui.pk3`). SDL2 re-fetches on first configure (~1-2 min).

- [ ] **Step 2: Run the test suite (the feel gate)**

```
cmd /c '"...vcvars64.bat" >nul 2>&1 && ctest --test-dir C:\repo\et-rm\build --output-on-failure'
```
Expected: `pmove_feel` PASS (unchanged golden hash — audio doesn't touch the sim) and `fov_math` PASS.

- [ ] **Step 3: Log the port in `docs/THIRDPARTY.md`**

Add a row to the Port log table:
```markdown
| 2026-06-01 | src/sys/sdl_snd.c | ioquake3 code/sdl/snd_sdl.c | GPL-2.0-or-later | adapted | SDL2 audio backend (callback + DMA ring); ET SNDDMA_* contract (replaces win_snd.c DirectSound) |
```

- [ ] **Step 4: Commit the doc**

```
git -C C:\repo\et-rm add docs/THIRDPARTY.md
git -C C:\repo\et-rm commit -m "docs: log SDL2 audio backend port in THIRDPARTY

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 5: Hand off for audible user acceptance (do NOT merge/tag yet)**

The merge + tag (`rm-sdl2-audio`) is gated on the user's audible acceptance: run `scripts\play.bat +devmap oasis` and confirm menu music + in-game SFX play with no stutter/crackle, positional audio works, and alt-tab mutes/unmutes cleanly. Report status and the commit SHAs; the controller handles merge/tag via the finishing-a-development-branch flow after the user signs off.

---

## Self-Review

**Spec coverage:**
- Replace DirectSound with SDL2 audio (`sdl_snd.c`, callback+ring, `SNDDMA_*`) → Task 1 ✓
- Delete `win_snd.c` → Task 2 ✓
- Delete orphaned `win_input.c` → Task 3 ✓
- Remove dead `GlideIsValid` → Task 4 ✓
- Remove `dsound`/`dxguid` from link → Task 1 Step 5 ✓
- `BeginPainting`/`Submit` lock/unlock against callback → Task 1 Step 2 ✓
- Derive `dma` from obtained spec → Task 1 Step 2 ✓
- Power-of-two ring (mixer masking) → Task 1 Steps 1–2 ✓
- Alt-tab mute (`SNDDMA_Activate`/`Sys_SndPause`) → Task 1 Steps 2–4 ✓
- Feel gate green → Task 5 Step 2 ✓
- THIRDPARTY provenance → Task 5 Step 3 ✓
- Windows-only, `win_wndproc`/`win_main` left alone (so `SNDDMA_Activate` is still provided) → respected throughout ✓

**Placeholder scan:** No TBD/TODO; all code shown in full. The `GlideIsValid` deletion (Task 4 Step 2) is described by location + scope rather than a literal diff because the exact surrounding lines must be read first — the grep guard (Step 1) and "no undefined symbol" build check (Step 3) make the scope unambiguous.

**Type/contract consistency:** `SNDDMA_Init/GetDMAPos/Shutdown/BeginPainting/Submit/Activate` match snd_local.h + win_local.h declarations. `Sys_SndPause(qboolean)` is defined in `sdl_snd.c`, declared in `sdl_local.h`, called in `sdl_input.c` — consistent. `dma` fields used (`channels/samples/samplebits/speed/submission_chunk/buffer`) match the `dma_t` struct. `s_khz`/`s_bits`/`s_numchannels` are existing extern cvars from snd_local.h.
