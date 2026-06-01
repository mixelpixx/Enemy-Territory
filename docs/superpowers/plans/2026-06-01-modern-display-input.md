# Modern Display & Input — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the Windows client's video + input to an SDL2 platform layer, and correct widescreen field-of-view and HUD aspect in the game modules, so ET presents and plays correctly on a modern 16:10 display — without touching the simulation.

**Architecture:** Two independent layers. (A) Engine: a new `src/sys/` SDL2 layer replaces `win_glimp.c`/`win_qgl.c`/`win_gamma.c` (video) and `win_input.c` (input); DirectSound (`win_snd.c`) and the dedicated console (`win_syscon.c`) stay. (B) Modules: `cgame` gets hor+ FOV math + 2D-aspect correction; `ui` gets a FOV slider via a shipped menu-script override. The pmove feel-harness hash must stay bit-identical throughout.

**Tech Stack:** C (C11-ish, 2005-era idioms), CMake + Ninja + MSVC x64, SDL2 (via CMake FetchContent, pinned), OpenGL 1.x fixed-function (`renderer1`), ET native game-module DLLs.

**Design spec:** `docs/superpowers/specs/2026-06-01-modern-display-input-design.md`

**Porting note (TDD adaptation):** This is a platform-layer port of well-trodden upstream code (ioquake3 / ETe), not greenfield logic. Most verification is *build + run + manual checklist + the feel harness* (the automated gate). The one piece of genuinely new pure logic — the hor+ FOV math — is factored into a testable helper and unit-tested (Task E1/E2). For verbatim ports, each task gives the exact ET integration contract (function signatures, event types, cvars) the ported file must satisfy, plus the ET-specific edits in full.

**License:** ioquake3 and ETe are GPL-2.0-or-later → combine cleanly into this GPLv3 tree. Every ported file is logged in `docs/THIRDPARTY.md` (Task H2).

---

## File Structure

**Engine (new — `src/sys/`):**
- `src/sys/sdl_glimp.c` — SDL window + GL context; implements `GLimp_Init/EndFrame/Shutdown/SetGamma/LogComment` and the render-thread stubs the renderer expects.
- `src/sys/sdl_qgl.c` — `QGL_Init`/`QGL_Shutdown` populating ET's `qgl*` pointer table via `SDL_GL_GetProcAddress` (replaces `win_qgl.c`'s WGL/`GetProcAddress` loader; keeps `renderer1` byte-faithful).
- `src/sys/sdl_input.c` — `IN_Init/Shutdown/Frame/Activate`, SDL→ET keymap, raw mouse via `SDL_SetRelativeMouseMode`, event pump via `SDL_PollEvent` (replaces `win_input.c` + the Win32 message pump's input role).

**Engine (modified):**
- `CMakeLists.txt` — SDL2 FetchContent.
- `cmake/Client.cmake` — swap platform sources, link `SDL2::SDL2`, copy `SDL2.dll` to `bin/`.
- `src/win32/win_main.c` — message pump no longer drives input/GL; SDL owns events (small edits, see Task C3).
- `src/win32/win_snd.c` — get `HWND` from SDL for DirectSound cooperative level (Task C4).

**Modules (modified):**
- `src/cgame/cg_local.h` / `cg_main.c` — `cg_fov` default; new `cg_fixedAspect` cvar.
- `src/cgame/cg_view.c` — hor+ FOV in `CG_CalcFov` via new helper.
- `src/cgame/cg_drawtools.c` — `CG_AdjustFrom640` pillarbox correction.
- `src/game/bg_local.h` or a new `src/game/bg_fov.c` — pure hor+ helper (shared, testable).

**Modules (new asset):**
- `rm/ui/rm_options.menu` (or minimal override) — FOV slider; shipped in an `rm` data dir loaded ahead of `pak0`.

**Tests:**
- `tests/fov_test.c` — unit test for the hor+ helper (built like the existing feel harness).
- `cmake/FeelHarness.cmake` — add the `fov_math` test target.

---

## Phase A — Branch & SDL2 build plumbing

### Task A0: Create the working branch

- [ ] **Step 1: Branch off `rm/main`**

```bash
cd /c/repo/et-rm
git checkout rm/main
git pull --ff-only
git checkout -b rm/sdl2-display-input
```

- [ ] **Step 2: Confirm clean baseline builds**

Run (from an x64 MSVC dev shell, repo root):
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
Expected: `build/bin/etrm.exe`, `etrmded.exe`, `cgame_mp_x86_64.dll`, `ui_mp_x86_64.dll`, `qagame_mp_x86_64.dll`, `etrm_feeltest.exe` all produced. This is the known-good starting point.

### Task A1: Add SDL2 via FetchContent (pinned)

**Files:**
- Modify: `CMakeLists.txt` (the SDL2 block, lines ~60-73)

- [ ] **Step 1: Replace the optional-`find_package` SDL2 block**

In `CMakeLists.txt`, replace the current `if(BUILD_CLIENT) find_package(SDL2 QUIET) ... endif()` block with a FetchContent-based one:

```cmake
# ----------------------------------------------------------------------------
#  SDL2 — fetched and built from a pinned release tag (reproducible, no system
#  install). Provides the SDL2::SDL2 import target used by the src/sys layer.
# ----------------------------------------------------------------------------
if(BUILD_CLIENT)
    include(FetchContent)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.9   # pinned; bump deliberately
        GIT_SHALLOW    TRUE)
    message(STATUS "ET-RM: fetching SDL2 (release-2.30.9) ...")
    FetchContent_MakeAvailable(SDL2)
endif()
```

- [ ] **Step 2: Configure and confirm SDL2 builds**

Run:
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```
Expected: first configure downloads SDL2 and configures it; `SDL2::SDL2` target now exists. No client wiring yet, so `etrm` is unchanged. Build to confirm nothing broke:
```
cmake --build build
```
Expected: all existing binaries still build; `build/_deps/sdl2-*` present.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: fetch SDL2 (pinned release-2.30.9) via FetchContent"
```

---

## Phase B — SDL2 video (window + GL context)

### Task B1: Port the SDL GL proc-loader (`sdl_qgl.c`)

ET's `renderer1` calls GL through the `qgl*` function-pointer table that `win_qgl.c` fills with `GetProcAddress`/`wglGetProcAddress`. SDL provides one portable loader, `SDL_GL_GetProcAddress`. We keep the table (renderer untouched) and only change where the addresses come from.

**Files:**
- Create: `src/sys/sdl_qgl.c`
- Reference: `src/win32/win_qgl.c` (copy the qgl table; replace the loader)

- [ ] **Step 1: Create `src/sys/sdl_qgl.c`**

Copy `src/win32/win_qgl.c` to `src/sys/sdl_qgl.c`, then make exactly these changes:
1. At top, replace `#include <windows.h>`/WGL includes with `#include <SDL.h>` (keep `#include "../renderer/qgl.h"` and `tr_local`/`q_shared` includes as in the original).
2. Replace every proc-address fetch of the form `qgl<Name> = dll<Name> = (…)GetProcAddress( glw_state.hinstOpenGL, "gl<Name>" );` and the `wglGetProcAddress` variants with:
   `qgl<Name> = SDL_GL_GetProcAddress( "gl<Name>" );`
   A search-and-replace on the existing macro the file uses (`GPA(a)` if present) is sufficient — redefine it as:
   ```c
   #define GPA( a ) SDL_GL_GetProcAddress( a )
   ```
3. Delete the WGL-extension section (everything that resolves `qwgl*` / `wglGetProcAddress`). SDL owns context/buffer-swap; the renderer's swap goes through `GLimp_EndFrame` (Task B2). Stub any `qwgl*` symbols the renderer still references to `NULL` if the linker complains; resolve referenced ones in Task B3 troubleshooting.
4. Keep `QGL_Init( const char *dllname )` and `QGL_Shutdown( void )` signatures unchanged (the renderer calls them). In `QGL_Init`, drop the `LoadLibrary` of the GL driver — SDL has already created the context — and just populate the table via `GPA`.

- [ ] **Step 2: Confirm it compiles in isolation (deferred to B2 build)**

No standalone build; verified when linked in Task B2.

### Task B2: Port the SDL window/context layer (`sdl_glimp.c`)

**Files:**
- Create: `src/sys/sdl_glimp.c`
- Reference: ioquake3 `code/sdl/sdl_glimp.c`; ET interface from `src/win32/win_glimp.c`

The renderer (`src/renderer/tr_init.c`) requires these symbols with these exact signatures (from `win_glimp.c`):
```c
void GLimp_Init( void );                 // create window+context at cgs/r_mode size, fill glconfig
void GLimp_EndFrame( void );             // SDL_GL_SwapWindow
void GLimp_Shutdown( void );             // destroy context+window
void GLimp_SetGamma( unsigned char r[256], unsigned char g[256], unsigned char b[256] );
void GLimp_LogComment( char *comment );
void GLimp_RenderThreadWrapper( void );  // single-threaded: stub
void GLimp_FrontEndSleep( void );        // stub returns
void *GLimp_WakeRenderer( void *data );  // stub (match current return type in win_glimp.c)
```

- [ ] **Step 1: Create `src/sys/sdl_glimp.c` with the ET contract**

Port ioquake3's `sdl_glimp.c`, adapting to the signatures above. Concretely it must:
1. `GLimp_Init`: read `r_mode`, `r_customwidth`, `r_customheight`, `r_fullscreen`, `r_swapInterval` cvars (already registered by the renderer). Compute the target resolution (Task B3 adds `r_mode -2`). Create the window:
   ```c
   Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
   if ( r_fullscreen->integer )
       flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;   // borderless desktop fullscreen
   SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
   SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
   SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
   SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
   SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
   s_window = SDL_CreateWindow( CLIENT_WINDOW_TITLE, SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, width, height, flags );
   s_context = SDL_GL_CreateContext( s_window );
   SDL_GL_MakeCurrent( s_window, s_context );
   SDL_GL_SetSwapInterval( r_swapInterval->integer );
   QGL_Init( NULL );                              // load GL via SDL (Task B1)
   ```
   Then fill `glConfig` (the renderer's `glconfig_t`): `vidWidth`/`vidHeight` from the actual drawable (`SDL_GL_GetDrawableSize`), `windowAspect = (float)vidWidth/vidHeight`, `isFullscreen`, and the GL strings (`qglGetString( GL_RENDERER/VENDOR/VERSION/EXTENSIONS )`). Match how `win_glimp.c` populates `glConfig` so the renderer sees identical fields.
2. `GLimp_EndFrame`: `SDL_GL_SwapWindow( s_window );` (preserve the existing `r_swapInterval->modified` re-apply logic from `win_glimp.c:1264` if present).
3. `GLimp_Shutdown`: `QGL_Shutdown(); SDL_GL_DeleteContext; SDL_GL_DestroyWindow; SDL_QuitSubSystem(SDL_INIT_VIDEO);` and clear `glConfig`.
4. `GLimp_SetGamma`: build an SDL gamma ramp from the three 256-byte tables and call `SDL_SetWindowGammaRamp( s_window, rRamp, gRamp, bRamp )`. If it fails (common on modern Windows), log once and return — the renderer's software `r_gamma`/overbright path still applies (this is the gamma risk in the spec).
5. The three render-thread functions: stub them exactly as `win_glimp.c` does in single-threaded mode (`r_smp 0`).
6. Expose the window handle for DirectSound: add `SDL_Window *Sys_GetSDLWindow( void )` returning `s_window` (used in Task C4). Declare it in a small new header `src/sys/sdl_local.h`.

- [ ] **Step 2: Wire the SDL video sources into the client build**

In `cmake/Client.cmake`, change `CLIENT_PLATFORM_SOURCES`: remove `win_glimp.c`, `win_qgl.c`, `win_gamma.c`; add the SDL layer. Add the include dir and link SDL2:

```cmake
set(CLIENT_PLATFORM_SOURCES
    ${ETRM_SRC}/win32/win_main.c
    ${ETRM_SRC}/win32/win_net.c
    ${ETRM_SRC}/win32/win_shared.c
    ${ETRM_SRC}/win32/win_wndproc.c
    ${ETRM_SRC}/win32/win_syscon.c
    ${ETRM_SRC}/win32/win_input.c    # still DirectInput for now (swapped in Phase C)
    ${ETRM_SRC}/sys/sdl_glimp.c      # SDL window + GL context
    ${ETRM_SRC}/sys/sdl_qgl.c        # GL proc table via SDL
    ${ETRM_SRC}/win32/win_snd.c
    ${ETRM_SRC}/qcommon/dl_main_stubs.c)

add_executable(etrm ${ETRM_CORE_SOURCES} ${CLIENT_PLATFORM_SOURCES})
set_target_properties(etrm PROPERTIES WIN32_EXECUTABLE ON)
target_include_directories(etrm PRIVATE
    ${ETRM_CORE_INCLUDE_DIRS}
    ${ETRM_SRC}/win32
    ${ETRM_SRC}/sys)
target_link_libraries(etrm PRIVATE
    etrm_renderer etrm_client etrm_jpeg etrm_botlib etrm_splines
    SDL2::SDL2
    opengl32 gdi32 user32 winmm wsock32 ws2_32 iphlpapi
    ole32 advapi32 dinput8 dsound dxguid comctl32)
etrm_apply_common_definitions(etrm)

# Copy the SDL2 runtime DLL next to the client so it runs from build/bin.
add_custom_command(TARGET etrm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:etrm>)
```

- [ ] **Step 3: Build**

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
Expected: `etrm.exe` links; `SDL2.dll` copied into `build/bin`. Fix any unresolved `qwgl*`/`glw_state` symbols by stubbing them in `sdl_glimp.c`/`sdl_qgl.c` (these were WGL-only). The renderer's `*.c` must remain unmodified.

- [ ] **Step 4: Run — menu must render in an SDL window**

```
scripts\play.bat
```
Expected: client boots to the main menu in an SDL-created window. Input is still DirectInput (unchanged) so the mouse works. If it renders, the GL context handoff is correct.

- [ ] **Step 5: Commit**

```bash
git add src/sys/sdl_glimp.c src/sys/sdl_qgl.c src/sys/sdl_local.h cmake/Client.cmake
git commit -m "client: create window + GL context via SDL2 (sdl_glimp/sdl_qgl); menu renders"
```

### Task B3: Native resolution (`r_mode -2`), borderless fullscreen, vsync

**Files:**
- Modify: `src/sys/sdl_glimp.c` (resolution selection in `GLimp_Init`)

- [ ] **Step 1: Add the desktop-resolution mode**

In `GLimp_Init`, before creating the window, select resolution:
```c
SDL_DisplayMode desktop;
SDL_GetDesktopDisplayMode( 0, &desktop );

int width, height;
if ( r_mode->integer == -2 ) {            // ioquake3 convention: desktop resolution
    width  = desktop.w;
    height = desktop.h;
} else if ( r_mode->integer == -1 ) {     // custom
    width  = r_customwidth->integer;
    height = r_customheight->integer;
} else {
    // fall back to ET's R_GetModeInfo() table for >=0 modes (call the existing
    // renderer helper exactly as win_glimp.c did)
    if ( !R_GetModeInfo( &width, &height, &windowAspectUnused, r_mode->integer ) ) {
        width = desktop.w; height = desktop.h;
    }
}
```
Keep the `glConfig.vidWidth/vidHeight` assignment based on the *actual* drawable size after window creation.

- [ ] **Step 2: Default `r_mode` to `-2`**

Find where the renderer registers `r_mode` (`src/renderer/tr_init.c`, `Cvar_Get( "r_mode", "3", CVAR_ARCHIVE | CVAR_LATCH )` or similar). Change the default string from its current value to `"-2"`. This affects only fresh configs (CVAR_ARCHIVE).

- [ ] **Step 3: Build, then run and verify native resolution + vsync**

```
cmake --build build
build\bin\etrm.exe +set fs_basepath "C:\repo\enemy-territory-RM" +set sv_pure 0
```
(Note: do not pass `+set r_fullscreen 0` here — we want to see the new default. The committed `play.bat` forces windowed; run the exe directly for this check, or temporarily edit it.)
Expected: the game opens at 1920×1200 (desktop res) borderless fullscreen. `/r_swapInterval 1; /vid_restart` enables vsync; `/r_swapInterval 0; /vid_restart` disables it.

- [ ] **Step 4: Commit**

```bash
git add src/sys/sdl_glimp.c src/renderer/tr_init.c
git commit -m "client: r_mode -2 desktop resolution default + borderless fullscreen + vsync via SDL"
```

---

## Phase C — SDL2 input (raw mouse + keyboard)

### Task C1: Port the SDL input layer (`sdl_input.c`)

**Files:**
- Create: `src/sys/sdl_input.c`
- Reference: ioquake3 `code/sdl/sdl_input.c`; ET contract from `src/win32/win_input.c` + `win_local.h`

ET's client/event system requires these symbols and behaviors:
```c
void IN_Init( void );        // SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER optional); register in_* cvars
void IN_Shutdown( void );
void IN_Frame( void );       // poll SDL events -> Sys_QueEvent; manage mouse grab/relative mode
void IN_Activate( qboolean active );
```
Events are delivered via the existing queue API (used by `win_input.c`):
```c
Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );
// keyboard:  type=SE_KEY,   value=<ET keynum>, value2=<down?1:0>
// chars:     type=SE_CHAR,  value=<ascii>
// mouse move:type=SE_MOUSE, value=dx, value2=dy   (relative deltas)
// wheel:     SE_KEY K_MWHEELUP / K_MWHEELDOWN down+up
```

- [ ] **Step 1: Create `src/sys/sdl_input.c`**

Port ioquake3's `sdl_input.c`, adapting to the above. It must:
1. Build an SDL-scancode → ET-keynum table (`K_*` from `keycodes.h`). Port ioquake3's `IN_TranslateSDLToQ3Key`; verify ET-specific keys (`K_CONSOLE` on `~`, `K_KP_*`, mouse buttons `K_MOUSE1..K_MOUSE5`) map correctly.
2. In `IN_Frame`, pump `SDL_PollEvent`:
   - `SDL_KEYDOWN`/`SDL_KEYUP` → `Sys_QueEvent( 0, SE_KEY, key, down, 0, NULL )`.
   - `SDL_TEXTINPUT` → one `SE_CHAR` per char.
   - `SDL_MOUSEMOTION` → accumulate `event.motion.xrel`/`yrel`; emit one `SE_MOUSE` per frame with summed deltas.
   - `SDL_MOUSEBUTTONDOWN/UP` → `SE_KEY` for `K_MOUSE1..`.
   - `SDL_MOUSEWHEEL` → `SE_KEY` `K_MWHEELUP/DOWN` (down+up).
   - `SDL_QUIT` → `Com_Quit_f()`.
   - `SDL_WINDOWEVENT` focus gained/lost → call `IN_Activate` and gate mouse grab.
3. **Raw mouse:** when the game wants mouse look (in-game, not console/menu), enable `SDL_SetRelativeMouseMode( SDL_TRUE )` — this gives raw relative deltas and hides/locks the cursor, bypassing Windows pointer acceleration. Disable it (`SDL_FALSE`) when the console or a menu is up so the cursor is free. Mirror `win_input.c`'s activate/deactivate logic (`IN_Activate`, the `Key_GetCatcher()` checks) for *when* to grab.
4. Register `in_mouse` (default `"1"`) and keep `m_filter`/sensitivity handling out of here — sensitivity scaling stays in the shared `cl_input.c` exactly as today, so the feel is unchanged.

- [ ] **Step 2: Compile (verified at C2 link)**

No standalone build.

### Task C2: Swap input sources in the build

**Files:**
- Modify: `cmake/Client.cmake`

- [ ] **Step 1: Replace `win_input.c` with `sdl_input.c` and drop DirectInput libs**

```cmake
    ${ETRM_SRC}/sys/sdl_input.c      # SDL input (raw mouse) — replaces win_input.c
```
Remove `${ETRM_SRC}/win32/win_input.c`. In `target_link_libraries`, remove `dinput8` and `dxguid` (no longer used; `dsound` stays for audio).

- [ ] **Step 2: Build**

```
cmake --build build
```
Expected: links without DirectInput. Resolve any references to `win_input.c`-only symbols (e.g. MIDI/joystick stubs) by providing minimal stubs in `sdl_input.c` or deleting the call sites if they are platform-internal.

### Task C3: Reconcile the Win32 message pump

`win_main.c` historically pumped Win32 messages that drove input. With SDL owning events, the pump must not fight SDL.

**Files:**
- Modify: `src/win32/win_main.c`

- [ ] **Step 1: Neutralize the input role of the Win32 pump**

Find `Sys_SendKeyEvents` (and any `GetMessage`/`PeekMessage`/`DispatchMessage` loop) in `win_main.c`. Replace its body with a call into the SDL pump so events still flow:
```c
void Sys_SendKeyEvents( void ) {
    extern void IN_Frame( void );   // SDL pump (sdl_input.c)
    IN_Frame();
}
```
Leave non-input Win32 plumbing (timing, paths, `WinMain` entry, the dedicated `win_syscon` console) untouched. If `WinMain` registers a `WNDCLASS`/`WndProc` used only for the old GL window, leave it inert — SDL creates its own window.

- [ ] **Step 2: Build, run, verify input end-to-end**

```
cmake --build build
build\bin\etrm.exe +set fs_basepath "C:\repo\enemy-territory-RM" +set sv_pure 0 +devmap oasis
```
Expected: keyboard + mouse work in menu and in-game; in-game mouse-look is raw (cursor locked); `~` opens console and frees the cursor; mouse wheel cycles weapons; `Esc` menus work.

- [ ] **Step 3: Commit**

```bash
git add src/sys/sdl_input.c cmake/Client.cmake src/win32/win_main.c
git commit -m "client: SDL2 input with raw relative mouse; retire DirectInput"
```

### Task C4: Hand SDL's HWND to DirectSound

DirectSound's `SetCooperativeLevel` needs the window handle; it previously used the Win32 GL window.

**Files:**
- Modify: `src/win32/win_snd.c`

- [ ] **Step 1: Source the HWND from SDL**

Find the `IDirectSound_SetCooperativeLevel( ..., hWnd, ... )` call in `win_snd.c`. Replace the `hWnd` source with SDL's:
```c
#include <SDL.h>
#include <SDL_syswm.h>
#include "../sys/sdl_local.h"   // Sys_GetSDLWindow

static HWND SND_GetHWND( void ) {
    SDL_SysWMinfo info;
    SDL_Window *win = Sys_GetSDLWindow();
    SDL_VERSION( &info.version );
    if ( win && SDL_GetWindowWMInfo( win, &info ) )
        return info.info.win.window;
    return GetDesktopWindow();   // safe fallback
}
```
Use `SND_GetHWND()` where the old window handle was passed. Add `${ETRM_SRC}/sys` to the include dirs if not already (done in Task B2).

- [ ] **Step 2: Build, run, verify audio still works**

```
cmake --build build
build\bin\etrm.exe +set fs_basepath "C:\repo\enemy-territory-RM" +set sv_pure 0 +devmap oasis
```
Expected: sound effects/music play as before. No DirectSound init error in the console.

- [ ] **Step 3: Commit**

```bash
git add src/win32/win_snd.c
git commit -m "sound: source DirectSound cooperative-level HWND from the SDL window"
```

---

## Phase D — Modern input/display defaults

### Task D1: Set the modern default cvar values

**Files:**
- Modify: `src/client/cl_input.c` (or wherever `cl_mouseAccel`/`m_filter` are registered) and `src/sys/sdl_input.c` (`in_mouse` default)

- [ ] **Step 1: Default mouse acceleration off and filtering off**

Locate the registrations of `cl_mouseAccel` and `m_filter` (grep `Cvar_Get( "cl_mouseAccel"` and `"m_filter"`). Set their default strings to `"0"`. These are `CVAR_ARCHIVE`, so only fresh configs change. Leave the *logic* untouched — only the default value changes.

- [ ] **Step 2: Confirm the display defaults are already set**

`r_mode "-2"` (Task B3 Step 2) and `r_swapInterval` (renderer default `"0"` — verify; set to `"0"` if not) are the display defaults. Borderless fullscreen is the behavior of `r_fullscreen 1` under SDL (Task B2). No further change if those are confirmed.

- [ ] **Step 3: Build, run with a fresh config, verify defaults**

```
cmake --build build
build\bin\etrm.exe +set fs_basepath "C:\repo\enemy-territory-RM" +set fs_homepath "%TEMP%\etrm_fresh" +set sv_pure 0
```
(`fs_homepath` to a fresh dir → no inherited config.) Expected console checks: `/cl_mouseAccel` = 0, `/m_filter` = 0, `/r_mode` = -2, `/r_swapInterval` = 0. Aim feels 1:1 with no acceleration.

- [ ] **Step 4: Commit**

```bash
git add src/client/cl_input.c src/sys/sdl_input.c
git commit -m "defaults: mouse accel/filter off for raw 1:1 aim on fresh configs"
```

---

## Phase E — Widescreen hor+ FOV (cgame)

### Task E1: Testable hor+ FOV helper (TDD)

**Files:**
- Create: `src/game/bg_fov.c`, declaration in `src/game/bg_public.h`
- Create test: `tests/fov_test.c`
- Modify: `cmake/FeelHarness.cmake` (add `fov_math` test)

The helper is pure math (no engine state), so we unit-test it.

- [ ] **Step 1: Write the failing test**

`tests/fov_test.c`:
```c
#include <math.h>
#include <stdio.h>
#include "../src/game/bg_public.h"

static int approx( float a, float b ) { return fabs( a - b ) < 0.01f; }

int main( void ) {
    float fx, fy;
    int fails = 0;

    /* 4:3 must reproduce vanilla: fov_x stays the reference, fov_y derived. */
    BG_CalcFovHorPlus( 90.0f, 640, 480, &fx, &fy );
    if ( !approx( fx, 90.0f ) ) { printf("FAIL 4:3 fov_x=%f want 90\n", fx); fails++; }
    /* vanilla fov_y for 90@4:3 is ~73.74 deg */
    if ( !approx( fy, 73.7398f ) ) { printf("FAIL 4:3 fov_y=%f want 73.74\n", fy); fails++; }

    /* 16:10 hor+: vertical FOV must MATCH the 4:3 vertical (anchor), */
    /* and horizontal must be WIDER than the reference. */
    float fx10, fy10;
    BG_CalcFovHorPlus( 90.0f, 1920, 1200, &fx10, &fy10 );
    if ( !approx( fy10, 73.7398f ) ) { printf("FAIL 16:10 fov_y=%f want 73.74 (anchored)\n", fy10); fails++; }
    if ( !( fx10 > 90.0f + 0.5f ) ) { printf("FAIL 16:10 fov_x=%f want >90\n", fx10); fails++; }

    printf( fails ? "FOV TEST FAILED (%d)\n" : "FOV TEST PASSED\n", fails );
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Declare the helper and add the test target**

In `src/game/bg_public.h`, add:
```c
void BG_CalcFovHorPlus( float fov43ref, int width, int height, float *fov_x, float *fov_y );
```
In `cmake/FeelHarness.cmake`, add (mirroring the existing `etrm_feeltest` target):
```cmake
add_executable(etrm_fovtest ${CMAKE_SOURCE_DIR}/tests/fov_test.c
                            ${ETRM_SRC}/game/bg_fov.c)
target_include_directories(etrm_fovtest PRIVATE ${ETRM_SRC}/game ${ETRM_SRC}/qcommon)
add_test(NAME fov_math COMMAND etrm_fovtest)
```

- [ ] **Step 3: Run the test — expect FAIL (no implementation)**

```
cmake -S . -B build -G Ninja && cmake --build build --target etrm_fovtest
ctest --test-dir build -R fov_math --output-on-failure
```
Expected: build fails to link (`BG_CalcFovHorPlus` undefined) — that is the red state.

- [ ] **Step 4: Implement the helper**

`src/game/bg_fov.c`:
```c
#include <math.h>
#include "q_shared.h"
#include "bg_public.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/*
 * Hor+ widescreen FOV.
 *   fov43ref : the user's cg_fov, interpreted as the horizontal FOV at 4:3.
 * Anchors the VERTICAL fov to what 4:3 would give at fov43ref, then derives
 * the HORIZONTAL fov for the actual aspect, so wide screens see MORE sideways
 * and the same vertically (never zoomed/cropped).
 */
void BG_CalcFovHorPlus( float fov43ref, int width, int height, float *fov_x, float *fov_y ) {
    float fovy, fovx;
    /* vertical fov that a 4:3 frame would produce for the reference horizontal fov */
    float x43 = 640.0f / tan( fov43ref / 360.0f * M_PI );
    fovy = atan2( 480.0f, x43 ) * 360.0f / M_PI;
    /* horizontal fov for the actual aspect, anchored on that vertical fov */
    {
        float x = height / tan( fovy / 360.0f * M_PI );
        fovx = atan2( (float)width, x ) * 360.0f / M_PI;
    }
    *fov_x = fovx;
    *fov_y = fovy;
}
```

- [ ] **Step 5: Run the test — expect PASS**

```
cmake --build build --target etrm_fovtest
ctest --test-dir build -R fov_math --output-on-failure
```
Expected: `FOV TEST PASSED`, ctest reports `fov_math` passing.

- [ ] **Step 6: Commit**

```bash
git add src/game/bg_fov.c src/game/bg_public.h tests/fov_test.c cmake/FeelHarness.cmake
git commit -m "cgame: add tested hor+ FOV helper (BG_CalcFovHorPlus)"
```

### Task E2: Use hor+ in `CG_CalcFov` and raise the default

**Files:**
- Modify: `src/cgame/cg_view.c` (`CG_CalcFov`, ~line 914-1022)
- Modify: `src/cgame/cg_main.c` (`cg_fov` default, line 340)
- Add `bg_fov.c` to the cgame build (`cmake/ClientModules.cmake`)

- [ ] **Step 1: Add `bg_fov.c` to the cgame shared sources**

In `cmake/ClientModules.cmake`, add `${GM_DIR}/bg_fov.c` to `CGAME_SHARED_SOURCES`.

- [ ] **Step 2: Replace the fov_y derivation with the hor+ helper**

In `cg_view.c` `CG_CalcFov`, the block at line ~998-1001:
```c
	// Arnout: this is weird... (but ensures square pixel ratio!)
	x = cg.refdef_current->width / tan( fov_x / 360 * M_PI );
	fov_y = atan2( cg.refdef_current->height, x );
	fov_y = fov_y * 360 / M_PI;
```
becomes:
```c
	// Hor+ widescreen: treat fov_x (from cg_fov / zoom) as the 4:3-reference
	// horizontal fov; anchor vertical to 4:3 and widen horizontal for the
	// actual aspect. Presentation only — does not affect the simulation.
	BG_CalcFovHorPlus( fov_x, cg.refdef_current->width, cg.refdef_current->height, &fov_x, &fov_y );
```
This routes the normal view *and* every zoom/binoc/MG42 branch (they all set `fov_x` above this point) through hor+, so zoomed views stay aspect-correct too. Leave the intermission fixed-90 and `showGameView` (`fov_x=fov_y=60`) special cases as they are — the `showGameView` branch sets `fov_y` directly and returns its own path; ensure the helper call does not override the `showGameView` case (guard with `if ( !cg.showGameView )` around the call, mirroring the existing structure).

- [ ] **Step 3: Raise the `cg_fov` default**

In `cg_main.c` line 340, change:
```c
	{ &cg_fov, "cg_fov", "90", CVAR_ARCHIVE },
```
to:
```c
	{ &cg_fov, "cg_fov", "100", CVAR_ARCHIVE },   // RM: wider modern default (4:3-reference; hor+ widens for widescreen)
```

- [ ] **Step 4: Build cgame, run the feel harness (must be unchanged), manual check**

```
cmake --build build
ctest --test-dir build -R pmove_feel --output-on-failure
```
Expected: `pmove_feel` PASSES with the existing golden hash (FOV does not touch the sim). Then:
```
build\bin\etrm.exe +set fs_basepath "C:\repo\enemy-territory-RM" +set sv_pure 0 +devmap oasis
```
Expected on 1920×1200: noticeably wider horizontal view than vanilla; vertical framing matches 4:3 (not zoomed). `/cg_fov 110; ` updates live; `/cg_fov 90` ≈ classic-but-widened.

- [ ] **Step 5: Commit**

```bash
git add src/cgame/cg_view.c src/cgame/cg_main.c cmake/ClientModules.cmake
git commit -m "cgame: hor+ widescreen FOV in CG_CalcFov; default cg_fov 100"
```

---

## Phase F — 2D HUD aspect correction (cgame)

### Task F1: Pillarbox `CG_AdjustFrom640` behind `cg_fixedAspect`

The current `CG_AdjustFrom640` (`cg_drawtools.c:39`) scales X and Y independently (`*x *= screenXScale; *y *= screenYScale`), which stretches round HUD elements horizontally on widescreen. Pillarbox: scale uniformly by the Y factor and center horizontally.

**Files:**
- Modify: `src/cgame/cg_drawtools.c` (`CG_AdjustFrom640`)
- Modify: `src/cgame/cg_main.c` (register `cg_fixedAspect`); `src/cgame/cg_local.h` (declare `vmCvar_t cg_fixedAspect`)

- [ ] **Step 1: Declare and register the cvar**

In `cg_local.h`, near the other `extern vmCvar_t cg_*;` declarations, add `extern vmCvar_t cg_fixedAspect;`. In `cg_main.c`: add the definition `vmCvar_t cg_fixedAspect;` near line 164 (with the other defs) and register it in the cvar table near line 340:
```c
	{ &cg_fixedAspect, "cg_fixedAspect", "1", CVAR_ARCHIVE },   // RM: 1 = pillarbox 2D so round elements stay round on widescreen
```

- [ ] **Step 2: Pillarbox in `CG_AdjustFrom640`**

Replace the body's scaling section (lines ~57-61) with:
```c
	if ( cg_fixedAspect.integer && cgs.glconfig.vidWidth * 480 > cgs.glconfig.vidHeight * 640 ) {
		// widescreen: uniform scale by the vertical factor, center horizontally (pillarbox)
		float yscale = cgs.glconfig.vidHeight / 480.0f;
		float wide   = cgs.glconfig.vidWidth - ( cgs.glconfig.vidHeight * 640.0f / 480.0f );
		*x = ( *x * yscale ) + ( wide * 0.5f );
		*y =   *y * yscale;
		*w =   *w * yscale;
		*h =   *h * yscale;
	} else {
		// original behavior (4:3, taller-than-wide, or correction disabled)
		*x *= cgs.screenXScale;
		*y *= cgs.screenYScale;
		*w *= cgs.screenXScale;
		*h *= cgs.screenYScale;
	}
```
Note: this centers HUD/menu 2D in a 4:3-proportioned region. Full-screen backgrounds that intentionally fill the width (e.g. some menu backdrops) may now pillarbox; if any look wrong during testing, that is the expected trade-off the `cg_fixedAspect` toggle exists for.

- [ ] **Step 3: Build, run, verify round elements**

```
cmake --build build
build\bin\etrm.exe +set fs_basepath "C:\repo\enemy-territory-RM" +set sv_pure 0 +devmap oasis
```
Expected: crosshair is round (not a horizontal oval); HUD icons/compass are correctly proportioned. `/cg_fixedAspect 0; ` reverts to the stretched look (sanity check the toggle).

- [ ] **Step 4: Commit**

```bash
git add src/cgame/cg_drawtools.c src/cgame/cg_main.c src/cgame/cg_local.h
git commit -m "cgame: pillarbox 2D HUD on widescreen (cg_fixedAspect, default on)"
```

---

## Phase G — UI FOV slider (menu-script override)

ET's menus are `.menu` scripts parsed by `ui_shared.c` from the mounted paks. We ship a tiny override that adds a FOV slider bound to `cg_fov`, loaded ahead of `pak0`.

**Files:**
- Create: `rm/ui/` override menu file(s)
- Verify load order via `fs_basepath`/`fs_game` (no engine code change expected)

- [ ] **Step 1: Identify the options menu to extend**

From the retail `pak0.pk3`, locate the controls/system options menu script (commonly `ui/options.menu` or `ui/controls.menu` with a slider pattern). Copy the smallest menu that owns a logical "FOV belongs here" section. Confirm the slider widget syntax by copying an existing `cvarFloat` slider (e.g. mouse sensitivity) as the template.

- [ ] **Step 2: Add the FOV slider control**

In the copied menu, add a slider bound to `cg_fov` with the engine clamp range:
```
itemDef {
    name        fovslider
    text        "Field Of View:"
    type        ITEM_TYPE_SLIDER
    cvarFloat   "cg_fov" 100 90 140
    rect        ...        // match the neighbouring controls' rect/spacing
    textscale   .25
    ...
}
```
(`cvarFloat name default min max`; min/max stay inside ET's 90-160 clamp.)

- [ ] **Step 3: Ship it ahead of `pak0`**

Place the override so the engine mounts it with higher priority than `pak0.pk3`. Simplest: an `rm/` mod dir launched via `+set fs_game rm` containing `ui/<menu>.menu`, OR a `zz_rm_ui.pk3` in `etmain` (lexically after `pak*` so it wins). Update `scripts/play.bat` to add `+set fs_game rm` if using the mod-dir approach. Document the chosen mechanism in the spec's "first asset override" note.

- [ ] **Step 4: Build (if pk3 packed by build) / run, verify the slider**

```
build\bin\etrm.exe +set fs_basepath "C:\repo\enemy-territory-RM" +set sv_pure 0
```
Expected: the Options menu shows a FOV slider; dragging it changes `cg_fov` (verify `/cg_fov` in console) and the in-game view width updates after closing the menu.

- [ ] **Step 5: Commit**

```bash
git add rm/ scripts/play.bat
git commit -m "ui: add FOV slider to options via RM menu override (bound to cg_fov)"
```

---

## Phase H — Final verification, provenance, tag

### Task H1: Full verification pass

- [ ] **Step 1: Clean build + all tests**

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: every target builds; `pmove_feel` PASSES (unchanged golden hash — the sacred gate) and `fov_math` PASSES.

- [ ] **Step 2: Manual acceptance checklist on 1920×1200**

Run `scripts\play.bat +devmap oasis` and confirm:
- Boots at native 1920×1200, borderless fullscreen.
- Horizontal view is wider than vanilla; vertical framing matches 4:3 (hor+, not zoomed).
- Crosshair round (not oval); HUD correctly proportioned.
- FOV slider in Options changes the view; `/cg_fov` reflects it.
- Raw mouse: aim 1:1, no acceleration; cursor locked in-game, free in console/menu.
- `~` console works; mouse wheel changes weapons; `Esc` menus work.
- `/r_swapInterval 1; /vid_restart` toggles vsync; windowed↔fullscreen via `/r_fullscreen` + `/vid_restart` is clean.
- Sound plays (DirectSound intact).

- [ ] **Step 3: Server/demo sanity**

```
scripts\server.bat +map oasis
```
then connect a second client with `+connect 127.0.0.1`. Expected: connects and plays (protocol 84 unchanged). Play back any stock `.dm_84` demo if available — expected: plays without desync (FOV/HUD/input changes don't touch recorded `playerState`).

### Task H2: Log third-party provenance

**Files:**
- Modify: `docs/THIRDPARTY.md` (the Port log table)

- [ ] **Step 1: Record the SDL ports**

Add rows to the Port log table in `docs/THIRDPARTY.md`:

```markdown
| 2026-06-01 | src/sys/sdl_glimp.c | ioquake3 code/sdl/sdl_glimp.c | GPL-2.0-or-later | adapted | SDL2 window+GL context; adapted to ET GLimp_* interface |
| 2026-06-01 | src/sys/sdl_input.c | ioquake3 code/sdl/sdl_input.c | GPL-2.0-or-later | adapted | SDL2 input + raw relative mouse; ET keymap/Sys_QueEvent |
| 2026-06-01 | src/sys/sdl_qgl.c   | src/win32/win_qgl.c (id) + SDL loader | GPLv3 base | adapted | qgl table sourced via SDL_GL_GetProcAddress |
```
(Adjust "verbatim/adapted" to what actually landed. If any ET:Legacy hor+/aspect code was copied rather than reimplemented from the formula, add a GPLv3 row for it.)

- [ ] **Step 2: Commit**

```bash
git add docs/THIRDPARTY.md
git commit -m "docs: log SDL2 platform-layer ports in THIRDPARTY"
```

### Task H3: Tag the milestone

- [ ] **Step 1: Merge to `rm/main` and tag**

```bash
git checkout rm/main
git merge --no-ff rm/sdl2-display-input -m "Merge: Modern Display & Input (SDL2 video+input, hor+ FOV, defaults)"
git tag -a rm-sdl2-display -m "SDL2 video+input platform layer; widescreen hor+ FOV + slider; modern defaults"
git push origin rm/main --tags
```

---

## Self-Review

**Spec coverage:**
- SDL2 video+input migration → Phases B, C ✓
- Audio deferred (DirectSound kept) → Task C4 keeps DS, no SDL audio ✓
- Hor+ FOV, default on, raised default, console-adjustable → Phase E ✓
- FOV menu slider → Phase G ✓
- 2D HUD aspect correction → Phase F ✓
- Modern defaults (native res, accel-off+raw, vsync off, fullscreen handling) → Tasks B3, C1, D1 ✓
- SDL2 via FetchContent pinned, SDL2 not SDL3 → Task A1 ✓
- Feel-harness hash unchanged (gate) → Tasks E4 (E2 Step 4), H1 ✓
- 4:3-unaffected regression → Task E1 test ✓
- THIRDPARTY provenance → Task H2 ✓
- Branch `rm/sdl2-display-input`, tag `rm-sdl2-display` → A0, H3 ✓

**Placeholder scan:** Verbatim-port tasks (B1, B2, C1) intentionally instruct "port ioquake3's file then make these exact ET edits" with the ET contract and ET-specific code given in full — this is the honest unit of work for a large upstream port, not a placeholder. The `.menu` slider rect (Task G2) is left as "match neighbouring controls" because exact coordinates depend on the retail menu being extended (discovered in G1) — the binding (`cvarFloat cg_fov 100 90 140`) is exact.

**Type consistency:** `BG_CalcFovHorPlus( float, int, int, float*, float* )` is declared (E1 Step 2), implemented (E1 Step 4), tested (E1 Step 1), and called (E2 Step 2) with matching signature. `Sys_GetSDLWindow()` defined in `sdl_glimp.c` (B2), declared in `sdl_local.h`, used in `win_snd.c` (C4). `cg_fixedAspect` declared/defined/registered/used consistently (F1). `IN_Frame` is the SDL pump used by `Sys_SendKeyEvents` (C3).

**Risk note:** The highest-uncertainty integration point is the GL proc-address handoff (Task B1/B2) and any residual WGL symbols in `renderer1`/`win_qgl.c`; Task B2 Step 3 calls out stubbing them. If `renderer1` proves to reference WGL directly (not just via `qgl`), that surfaces at B2 link time and is resolved by routing those through SDL in `sdl_glimp.c`.
