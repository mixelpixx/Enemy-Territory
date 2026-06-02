# SDL2 Audio + Platform Cleanup — Design Spec

**Increment 2 of the ET-RM remaster.** Status: design approved, pending spec review.
Date: 2026-06-01. Branch (planned): `rm/sdl2-audio-cleanup` off `rm/main`.

## Goal

Finish the SDL2 platform migration started in Increment 1 by replacing the
Windows-only DirectSound audio backend with a cross-platform SDL2 backend, and
remove now-orphaned Win32 platform code. This retires the last Windows-only
*runtime* dependency in the client, directly unblocking the cross-platform
(Linux/macOS) increment that follows.

This increment touches only the engine's audio backend and dead platform code.
It does **not** touch the simulation (`bg_pmove`/`sv_fps`/antilag); the pmove
feel-harness hash must stay bit-identical.

## Scope

In scope:

- **Replace DirectSound with SDL2 audio.** Implement the existing `SNDDMA_*`
  sound-backend interface on SDL2 in a new `src/sys/sdl_snd.c`; delete
  `src/win32/win_snd.c`. Single cross-platform audio path, no DirectSound
  fallback.
- **Safe cleanup:** delete the orphaned `src/win32/win_input.c` (already removed
  from the build in Increment 1); remove the dead `GlideIsValid()` 3Dfx remnant
  from `src/sys/sdl_qgl.c`.
- Windows x64 build only (this increment does not attempt a Linux/macOS build).

Out of scope (deferred to the cross-platform increment):

- `win_wndproc.c` / `win_main.c` dead-code removal and platform-layer
  restructuring (those are Windows-only by nature and do not block Linux; the
  cross-platform increment reorganizes this layer anyway). Consequently
  `src/sys/sdl_input.c` keeps the vestigial `directInput` / `in_appactive`
  symbols that the still-compiled `win_wndproc.c` references.
- Any actual Linux/macOS bring-up, CMake cross-platform work, or QVM JIT.

## Decisions (from brainstorming)

| Decision | Choice | Rationale |
|---|---|---|
| DirectSound | Fully replace with SDL audio | One cross-platform path; retires Windows-only audio. |
| SDL audio model | Callback + ring buffer (ioquake3 `snd_sdl.c`) | Faithful to the engine's continuous-DMA + `GetDMAPos` design; proven Q3-lineage port; cross-platform. |
| Cleanup depth | Audio swap + safe cleanup only | Lowest risk to the WinMain entry; avoids overlap with the cross-platform restructuring. |
| Build targets | Windows x64 only this increment | Cross-platform is the next increment. |

## Architecture

### Component — `src/sys/sdl_snd.c` (new; replaces `win_snd.c`)

Implements the engine sound-backend contract declared in `src/client/snd_local.h`.
The engine mixer (`snd_dma.c`, `snd_mix.c`) is unchanged; only the backend that
drains the `dma_t` ring buffer changes.

Functions:

- `qboolean SNDDMA_Init( void )` — `SDL_InitSubSystem(SDL_INIT_AUDIO)`; build a
  desired `SDL_AudioSpec` from existing cvars:
  - frequency from `s_khz` (11025 / 22050 / 44100),
  - format from `s_bits` (`AUDIO_U8` for 8-bit, else `AUDIO_S16SYS`),
  - channels from `s_numchannels` (default 2),
  - `samples` = a power-of-two callback buffer sized for low latency,
  - `callback` = `sdl_audiocallback`.
  Open the device with `SDL_OpenAudioDevice` allowing the obtained spec to
  differ; populate `dma.channels/samplebits/speed/samples/submission_chunk` from
  the **obtained** spec; allocate `dma.buffer` as a power-of-two ring; set
  `dma.samplepos = 0`; `SDL_PauseAudioDevice(dev, 0)` to start playback. Return
  `qfalse` on failure (engine falls back to no sound, as today).
- `sdl_audiocallback( void *userdata, Uint8 *stream, int len )` (static; SDL
  audio thread) — copy `len` bytes from `dma.buffer` at the play cursor into
  `stream`, wrapping the ring, advancing the play cursor (the position
  `SNDDMA_GetDMAPos` reports).
- `int SNDDMA_GetDMAPos( void )` — current play cursor in samples
  (`& (dma.samples - 1)`).
- `void SNDDMA_BeginPainting( void )` — `SDL_LockAudioDevice(dev)` so the
  main-thread mixer can write `dma.buffer` without racing the callback.
- `void SNDDMA_Submit( void )` — `SDL_UnlockAudioDevice(dev)`.
- `void SNDDMA_Shutdown( void )` — `SDL_CloseAudioDevice`, free `dma.buffer`,
  `SDL_QuitSubSystem(SDL_INIT_AUDIO)`.
- `void SNDDMA_Activate( void )` — pause/unpause the device on focus change
  (mute when the window loses focus).

### Data flow

```
main thread:   S_Update -> SNDDMA_BeginPainting (lock)
                         -> mixer writes dma.buffer
                         -> SNDDMA_Submit (unlock)
audio thread:  sdl_audiocallback -> drain dma.buffer at play cursor
                                  -> advance play cursor
mixer reads:   SNDDMA_GetDMAPos  -> how far the play cursor has advanced
```

Identical model to the DirectSound backend; SDL's callback replaces the
DirectSound secondary-buffer play cursor. The lock/unlock pairing in
BeginPainting/Submit is the critical correctness detail (the callback runs on a
separate thread).

### Build — `cmake/Client.cmake`

- Remove `${ETRM_SRC}/win32/win_snd.c` from `CLIENT_PLATFORM_SOURCES`; add
  `${ETRM_SRC}/sys/sdl_snd.c`.
- Remove `dsound` and `dxguid` from `target_link_libraries` after confirming no
  other translation unit references them (DirectInput was already removed in
  Increment 1). SDL2 is already linked and provides audio.

### Cleanup

- Delete `src/win32/win_input.c` (orphaned; not in the build since Increment 1).
- Remove the dead `GlideIsValid()` function (and its now-unused 3Dfx helper
  macros/block) from `src/sys/sdl_qgl.c` — it has no callers in the SDL path.
- `win_snd.c` is deleted; its `SDL_syswm` HWND shim (`SND_GetHWND`) is removed
  with it.

## Feel safety

- No changes to `bg_pmove`, `bg_slidemove`, `sv_fps`, or antilag.
- The audio backend is entirely outside the simulation; `pmove_feel` and
  `fov_math` must stay green (the merge gate). Audio latency is perceptual, not
  part of the deterministic sim.

## Testing & verification

- Build links cleanly without `dsound`/`dxguid`; all targets build.
- `ctest` green: `pmove_feel` (unchanged golden hash) and `fov_math`.
- Traced launch (objective): `SNDDMA_Init` opens the SDL audio device and logs
  the obtained frequency/format/samples with no error; menu reached; no crash;
  clean shutdown.
- Audible acceptance (user): menu music and in-game SFX play with no
  stutter/crackle; positional audio works; alt-tab mutes/unmutes cleanly; no
  crash. This is the accept-before-merge gate (same as Increment 1).

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Callback (audio thread) races the mixer (main thread) | `SDL_LockAudioDevice`/`SDL_UnlockAudioDevice` in `SNDDMA_BeginPainting`/`SNDDMA_Submit`. |
| SDL returns a different actual audio spec than requested | Derive all `dma_t` fields from the obtained spec, not the desired one. |
| Latency / buffer underruns (crackle) | Power-of-two `samples` sized for low latency with a larger `dma.buffer` ring for headroom; add an `s_sdlSamples`-style cvar only if tuning is needed. |
| Mute/zombie audio on focus loss | `SNDDMA_Activate` pauses/unpauses the device. |
| Removing `dxguid` breaks a link | Grep for other references first; keep it only if something else needs it. |

## Sequencing

Branch `rm/sdl2-audio-cleanup` off `rm/main`. Themed commits; feel harness before
merge; tag `rm-sdl2-audio`; merge `--no-ff` to `rm/main`.

1. Add `src/sys/sdl_snd.c` (SDL audio backend) + swap `cmake/Client.cmake`
   (remove `win_snd.c`/`dsound`/`dxguid`, add `sdl_snd.c`); build + verify the
   audio device opens.
2. Delete `src/win32/win_snd.c`.
3. Delete `src/win32/win_input.c` (orphaned).
4. Remove dead `GlideIsValid()` from `src/sys/sdl_qgl.c`.
5. Verify (ctest + audible), log the `snd_sdl` port in `docs/THIRDPARTY.md`, tag.

## References (port from / study)

- **ioquake3** `code/sdl/snd_sdl.c` (GPL-2.0-or-later) — primary reference for the
  SDL audio callback + ring-buffer `SNDDMA_*` implementation. Logged in
  `docs/THIRDPARTY.md`.
