# Enemy Territory RM

A modernization of the 2010 GPL source release of *Wolfenstein: Enemy Territory*.

The goal is to bring the original multiplayer game to current 64-bit toolchains,
operating systems, and displays while preserving how it plays. The work is
incremental: each subsystem is modernized on its own branch, verified against
regression tests and hands-on play, then merged and tagged. Anything that could
change gameplay is treated as off-limits.

## The rule that governs everything: the feel is sacred

ET's movement and gameplay timing are the product of specific code paths
(player movement, server tick, lag compensation) that this project never
modifies. That guarantee is enforced, not just promised:

- A movement-determinism test harness (`tests/`) replays a recorded input
  sequence through the player-movement code and compares the result against a
  golden hash. Every change to the engine must pass it before merging. If a
  change would alter movement by even one bit, the build fails.
- The original renderer remains the default and is kept byte-faithful. New
  rendering work ships as a separate, strictly opt-in renderer.
- Modernization targets infrastructure — platform layer, display handling,
  build system, renderer — never simulation.

## What has been done

Verified on Windows 11 (MSVC, x64). Each item was accepted in hands-on play
before merging; milestones are tagged.

**64-bit baseline** (`rm-playable-64bit`, `rm-mp-hardened`, `rm-feel-harness`)
- Native 64-bit client, dedicated server, and game/cgame/ui modules built with
  CMake. Multiplayer works end to end. The feel harness gates all later work.

**Modern platform layer** (`rm-sdl2-display`, `rm-sdl2-audio`)
- SDL2 video, input, and audio replace the DirectX-7-era Win32 code. Raw mouse
  input, native-resolution borderless fullscreen, DPI awareness, modern audio
  output. The mixer and input semantics are unchanged.

**Widescreen done correctly** (`rm-sdl2-display`)
- Hor+ field of view: widescreen displays see more at the sides instead of a
  stretched or cropped image, with an in-menu FOV slider. The HUD keeps its
  original proportions.

**Settings for current hardware** (`rm-settings-ux`)
- The resolution picker lists what the connected display actually supports.
  The 2003 connection presets (modem, ISDN, DSL) are replaced with current
  ones, and the default network rate matches what competitive ET servers have
  used for two decades.

**An optional modern renderer** (`rm-r2-1` through `rm-r2-4`)
- ET:Legacy's GLSL renderer ("renderer2", GPL like this project) is integrated
  as a separate DLL, selected with `cl_renderer gl2` and a `vid_restart`. A
  full round is playable under it: player models and animations, decals,
  dynamic lights, coronas, fog, foliage, rain and snow, and demo playback have
  all been verified against the original renderer with side-by-side
  screenshots. The original renderer remains the default and is untouched; if
  the modern renderer fails to load for any reason the game falls back to the
  original.

**Optional modern effects** (`rm-r2-5`)
- On the modern renderer, each effect is its own switch and all are off by
  default — turning them all off reproduces the original look exactly. Working
  on stock content: HDR with tone mapping, bloom, shadow mapping (with soft
  shadows), screen-space ambient occlusion, depth of field, and film grain.
  Per-pixel material lighting and world shadow casting are in place but need
  authored map content to show. All switches, their costs, and limitations are
  documented in `docs/MODERN-EFFECTS.md`.

**Pure-server hosting** (`rm-pure-hosting`)
- Because RM ships its client game logic (`cgame`/`ui`) as native DLLs, the
  build packs them into `etmain/rm_bin.pk3` so servers running the default
  "pure" mode start and accept matched clients. This covers *Host Game* listen
  servers and dedicated servers for clients on the same build (LAN / same-build
  groups). A client only ever loads modules from its own install, so a server
  cannot push executable code; a build mismatch shows an "update your client"
  message and returns to the menu. This is not yet a public auto-download
  server. See `docs/HOSTING.md`.

**Denial-of-service hardening** (`sv_protect`)
- Server-side rate limiting on the connectionless `getstatus`/`getinfo`/
  `getchallenge`/`rcon` handlers, plus optional DRDoS reflection protection,
  gated behind the `sv_protect` cvar. The local client of a listen server is
  always exempt, so normal play is unaffected. Public / internet-facing servers
  should set `sv_protect 3`. See `docs/HOSTING.md`.

**Server browser** (`rm-server-browser`)
- The browser is revived: LAN discovery (IPv4 broadcast) and an internet master.
  The dead id master is gone; servers and clients point at a configurable master
  (`sv_master1` / `cl_master`, or a build-time default), with the ET:Legacy
  community master as a fallback so the existing scene stays visible. A project
  master server runs on a VPS. Wire protocol is unchanged (84), so RM can still
  browse — and join where the native-module/pure rules allow — original ET
  servers. See `docs/HOSTING.md`.

Planned next, in order: optional renderer mapper hooks, and modern (TLS) content
downloads for custom-map distribution. Linux and macOS support is deferred but
kept in mind — new code prefers SDL and standard APIs. Details in
`docs/MODERNIZATION.md`.

## Game data is not included

This repository contains source code only. It does not contain, and will never
contain, game assets. To run, supply your own retail *Wolfenstein: Enemy
Territory* 2.60b data (`pak0.pk3`, `pak1.pk3`, `pak2.pk3`, `mp_bin.pk3`). The
full game was released as a free download by Splash Damage and id Software and
is still widely available. See [`docs/BUILD.md`](docs/BUILD.md).

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | Engine, renderers, and game/cgame/ui module source |
| `src/renderer2/` | The opt-in modern renderer (vendored ET:Legacy renderer2 plus the bridge that connects it to this engine) |
| `cmake/` | CMake build modules (per target) |
| `tests/` | Movement-determinism (feel) test harness |
| `docs/` | Build instructions, modernization catalog, third-party notes |
| `etmain/` | Stock menu/UI assets from the GPL release (no game data) |
| `COPYING.txt` | GNU GPL v3 |
| `README.txt` | Original id Software GPL release notes |

## Branches

- `main` — the modernization work. Each increment is developed on a branch,
  verified, and merged here; milestones are tagged.
- `historical` — the pristine 2010 GPL release, unchanged, as a permanent
  reference. Every line of the original source remains available there and in
  this branch's history.

## Building

See [`docs/BUILD.md`](docs/BUILD.md). In brief, from an x64 MSVC environment:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

## License and attribution

The 2010 *Wolfenstein: Enemy Territory* source release, and therefore this
project, is licensed under the GNU General Public License v3 (see
`COPYING.txt`). The modern renderer is ported from
[ET:Legacy](https://github.com/etlegacy/etlegacy) (also GPL), whose work is
gratefully acknowledged. All third-party components, their versions, and every
adaptation made to them are logged in [`docs/THIRDPARTY.md`](docs/THIRDPARTY.md).

This project is not affiliated with or endorsed by id Software, ZeniMax, or
Splash Damage.
