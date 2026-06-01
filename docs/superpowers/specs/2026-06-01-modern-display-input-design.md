# Modern Display & Input — Design Spec

**Increment 1 of the ET-RM remaster.** Status: design approved, pending spec review.
Date: 2026-06-01. Branch (planned): `rm/sdl2-display-input` off `rm/main`.

## Goal

Make Wolfenstein: Enemy Territory present correctly and feel right on a modern
Windows display, by (a) migrating the engine's video + input platform layer to
SDL2, and (b) correcting widescreen field-of-view and HUD aspect in the game
modules. The reference target is the developer's display: **1920×1200 @ 60 Hz
(16:10)**, standard DPI.

This is the first increment that modifies the game modules (`cgame`/`ui`). It
changes **presentation only** — it does not touch the simulation
(`bg_pmove`/`sv_fps`/antilag), and the pmove feel-harness hash must stay
bit-identical.

## Scope

In scope:

- Migrate the engine platform layer to **SDL2 — video + input only**.
- **Widescreen hor+ FOV** correction, default on; raised default `cg_fov`
  (4:3-reference value); adjustable via the `cg_fov` console cvar **and** a new
  Options-menu slider.
- **2D HUD/menu aspect correction** so round elements stop being horizontally
  stretched on widescreen.
- **Modern defaults** (applied to a *fresh* config only): native-resolution
  boot, mouse acceleration off + raw input, vsync exposed and off by default,
  correct fullscreen/windowed handling.
- SDL2 acquired via **CMake FetchContent**, pinned to a specific SDL2 release
  tag. Target **SDL2** (not SDL3) so proven fork code ports verbatim.

Out of scope (explicitly deferred):

- **Audio** — the existing DirectSound path stays untouched; SDL audio is its
  own later increment.
- **High-FPS uncap** — not pursued now (low value at 60 Hz; would require
  separate feel analysis of frame-rate-dependent movement).
- **Linux/macOS bring-up** — this lays the SDL2 groundwork but the increment
  builds and runs on Windows x64 only.
- HiDPI scaling — not applicable at 1920×1200 standard DPI.

## Decisions (from brainstorming)

| Decision | Choice | Rationale |
|---|---|---|
| FOV behavior | Hor+ on by default | Preserves the 4:3 vertical view, widens horizontal on 16:10 — the geometrically correct widescreen fix. |
| Default `cg_fov` | Raised (~100–110, finalized during testing) | A more modern wide look out of the box; still clamped to a sane range. |
| FOV adjustability | `cg_fov` console cvar **+** new Options-menu slider | Live, no-console adjustment requested by the user. |
| Modern defaults | Native res, accel-off+raw, vsync toggle (off), fullscreen handling — all four | Full "feels modern" baseline for fresh configs. |
| Platform approach | **SDL2 migration now (video+input)** | "We're headed there anyway" — do it on the layer we'll keep, not legacy Win32 we'll delete. |
| Audio | Defer (keep DirectSound) | Keeps the increment bounded and lower risk. |
| SDL2 acquisition | CMake FetchContent, pinned tag | Reproducible, cross-platform, nothing installed system-wide. |
| Default window mode | Borderless fullscreen at desktop resolution | Modern default; `play.bat`/cvars can override to windowed. |

## Architecture

Two independent layers.

### Layer A — Engine: SDL2 platform layer (new `src/sys/`)

Infrastructure-aggressive zone: fully replaceable engine code. Add an
SDL2-backed platform layer; retire the matching Win32 pieces.

| New (`src/sys/`) | Replaces | Responsibility |
|---|---|---|
| `sdl_glimp.c` | `win_glimp.c` | SDL window + GL context creation; mode-setting (desktop resolution via `SDL_GetDesktopDisplayMode`, borderless fullscreen via `SDL_WINDOW_FULLSCREEN_DESKTOP`); `GLimp_EndFrame` → `SDL_GL_SwapWindow`; gamma; vsync via `SDL_GL_SetSwapInterval`. |
| `sdl_input.c` | `win_input.c` (DirectInput) | Keyboard + mouse via SDL events; raw mouse via `SDL_SetRelativeMouseMode`; mouse wheel; SDL key → ET keynum translation; window focus / mouse-grab handling. |
| event pump (in `sys_main.c` / existing `win_main.c`) | DirectInput / Win32 message loop | `SDL_PollEvent` feeds the existing engine event queue. |

Porting source: **ioquake3** (verbatim where possible), **ET-adapted from ETe**.
Both are GPL-2.0-or-later → combine cleanly into the GPLv3 tree. Every ported
file/snippet is logged in `THIRDPARTY.md`.

Key invariants:

- `renderer1`'s GL calls are unchanged — only the *source* of the GL context
  changes (WGL → SDL).
- **DirectSound stays.** Integration point: pass SDL's `HWND`
  (`SDL_GetWindowWMInfo`) to DirectSound's `SetCooperativeLevel`.
- `win_syscon.c` (dedicated-server console) stays; the server path is
  unaffected by this increment.
- Old `win_glimp.c` / `win_input.c` are removed from the build (left in-tree for
  reference until the cross-platform increment cleans them up).

### Layer B — Game modules: widescreen presentation (`cgame` + `ui`)

Presentation only; does not touch the simulation.

1. **3D FOV (hor+)** — `cgame/cg_view.c` `CG_CalcFov`. Vanilla treats `cg_fov`
   as the horizontal FOV and derives vertical from the *actual* screen aspect
   (line ~999: `x = width / tan(fov_x...); fov_y = atan2(height, x)`), so 16:10
   users get a narrower vertical view than the 4:3 the game was tuned for. The
   fix: a helper that takes a **4:3-reference FOV** and returns the correct
   `fov_x`/`fov_y` for the actual aspect — anchoring the vertical view to 4:3 and
   widening horizontal on wide screens. Applied consistently to the normal view
   and the zoom/binoc/MG42 paths (which set `fov_x` directly) so zoom feel stays
   consistent. `cg_fov` becomes the 4:3-reference value with the existing sane
   clamp; default raised.

2. **2D HUD/menu aspect** — ET draws 2D in a 640×480 virtual space scaled to the
   full screen, so round elements (crosshair, icons) are horizontally squished on
   widescreen. Correct this through the single chokepoint (`CG_AdjustFrom640`)
   rather than editing every draw call. Guarded by a cvar (default on), since it
   is the most visually opinionated piece and the default may need tuning.

3. **FOV slider** — `ui` module. ET menus are data-driven `.menu` scripts shipped
   in the pk3s, so the slider ships as a **small RM menu-script override** loaded
   ahead of `pak0`, bound to `cg_fov` with a sane min/max. This is the project's
   first shipped client-side asset override; it is main-menu/options UI, which is
   client-local and **not** subject to `sv_pure` server checks, so it is safe for
   multiplayer.

### Modern defaults

Set as the registered cvar defaults so only a *fresh* config changes; existing
user configs keep their values.

- `r_mode -2` (use desktop resolution; ioquake3 pattern) + borderless-fullscreen
  default.
- `cl_mouseAccel 0`, `m_filter 0`, raw relative-mouse on.
- `r_swapInterval 0` (vsync off), exposed cleanly.

## Feel safety

- No changes to `bg_pmove`, `bg_slidemove`, `sv_fps`, or antilag.
- Raw mouse changes only *how* deltas are gathered, not the view-angle math;
  `m_filter 0` / `cl_mouseAccel 0` keep aim 1:1 (no added smoothing/accel).
- The pmove feel-harness hash must remain bit-identical — this is the merge gate.

## Testing & verification

- Builds on MSVC x64: `etrm.exe`, `etrmded.exe`, `qagame`/`cgame`/`ui` modules.
- `ctest -R pmove_feel` green (the gate).
- Added regression check: `CG_CalcFov` at 4:3 produces the vanilla `fov_y` for
  `cg_fov 90` — proving 4:3 players are unaffected and hor+ only widens wide
  aspects.
- Manual on 1920×1200: native-res borderless boot; round (not oval) crosshair;
  FOV slider moves the view live; raw mouse 1:1 with no acceleration; vsync
  toggle works; alt-tab / focus / mouse-grab behave; windowed↔fullscreen switch
  is clean.
- Server unaffected: protocol 84 unchanged; demo playback unaffected (demos store
  `playerState`, not refdef FOV).

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| SDL window + DirectSound coexistence | Hand SDL `HWND` to DS `SetCooperativeLevel`; verify no audio regression. |
| Gamma/brightness differs under SDL | Validate `r_gamma`; fall back to shader-based gamma if hardware gamma is unavailable. |
| HUD aspect correction is opinionated | Single chokepoint + cvar toggle; tune default during testing. |
| Menu-script override load order / missing-element edge cases | Load RM override ahead of `pak0`; verify menu still loads if an element is absent. |
| SDL2 FetchContent needs network on first configure | Document it; pin the tag; allow a local cache/offline override later if needed. |

## Sequencing

Branch `rm/sdl2-display-input` off `rm/main`. Themed commits in order; run the
feel harness before merge; tag `rm-sdl2-display` at completion.

1. SDL2 via FetchContent + bare SDL window/GL context (prove it boots and
   renders the menu).
2. SDL input + raw mouse.
3. Resolution / fullscreen / vsync defaults.
4. `cgame` hor+ FOV correction.
5. HUD aspect chokepoint correction.
6. `ui` FOV slider menu-script override.

## References (port from / study)

- **ioquake3** (GPL-2.0-or-later) — primary verbatim source for the SDL2
  platform layer (`sdl_glimp`, `sdl_input`), `r_mode -2` desktop-resolution
  pattern.
- **ETe** (GPL-2.0-or-later) — ET-specific SDL glue.
- **ET: Legacy** (GPL-3.0) — technique reference for hor+ FOV and 2D aspect
  correction on this exact game.

All ports logged in `docs/THIRDPARTY.md`.
