# Enemy Territory RM

A modernization of the 2010 GPL source release of *Wolfenstein: Enemy Territory*.

The objective is to build and run the original multiplayer game on current
64-bit toolchains and operating systems while preserving the original gameplay.
The work is incremental: first restore a correct, buildable 64-bit baseline, then
modernize subsystems behind regression tests, without changing how the game plays.

## Status

Current, verified state on Windows 11 (MSVC, x64):

- Builds as a native 64-bit application with CMake. No 32-bit assumptions remain
  on the active code paths.
- The client (`etrm`), dedicated server (`etrmded`), and the game, cgame, and ui
  modules (built as native 64-bit DLLs) all compile and run.
- Multiplayer is functional: a client connects to a dedicated server, downloads
  the game state, and renders the live game.
- The main menu, fixed-function renderer, and sound initialize and run.
- Gameplay movement is regression-tested for bit-exact determinism (see
  `tests/`), so future changes that would alter the feel are caught automatically.

Not yet done (planned, see `docs/MODERNIZATION.md`):

- Linux and macOS builds (an SDL2 platform layer is scaffolded but not yet wired
  in; the client currently uses the native Win32 layer).
- A modern (GLSL/Vulkan) renderer alongside the original fixed-function one.
- The performance and infrastructure modernizations catalogued in the docs.

## Game data is not included

This repository contains source code only. It does not contain, and will never
contain, game assets. To run, supply your own retail *Wolfenstein: Enemy
Territory* 2.60b data (`pak0.pk3`, `pak1.pk3`, `pak2.pk3`, `mp_bin.pk3`). See
[`docs/BUILD.md`](docs/BUILD.md).

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | Engine, renderer, and game/cgame/ui module source |
| `cmake/` | CMake build modules (per target) |
| `tests/` | Movement-determinism (feel) test harness |
| `docs/` | Build instructions, modernization catalog, third-party notes |
| `etmain/` | Stock menu/UI assets from the GPL release (no game data) |
| `COPYING.txt` | GNU GPL v3 |
| `README.txt` | Original id Software GPL release notes |

## Branches

- `master` — the pristine 2010 GPL release, unchanged, as a permanent reference.
- `rm/main` — the modernization work. Each phase is developed on a branch and
  merged here; milestones are tagged.

## Building

See [`docs/BUILD.md`](docs/BUILD.md). In brief, from an x64 MSVC environment:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## License

The 2010 *Wolfenstein: Enemy Territory* source release, and therefore this
project, is licensed under the GNU General Public License v3 (see `COPYING.txt`).
Third-party components and their licenses are listed in
[`docs/THIRDPARTY.md`](docs/THIRDPARTY.md).

This project is not affiliated with or endorsed by id Software, ZeniMax, or
Splash Damage.
