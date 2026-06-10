# Settings UX Modernization — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the first-run settings page modern: a resolution picker that lists the modes the monitor actually reports (true per-system detection), and connection settings that reflect 2026 broadband instead of dialup/ISDN.

**Architecture:** The renderer/SDL layer enumerates the display's modes and publishes them as a space-delimited `r_availableModes` cvar. A new `FEEDER_RESOLUTIONS` listbox in the `ui` module reads that cvar and lets the player pick one (writing `r_customwidth/height` + `r_mode -1`). Connection presets are modernized via the existing `ui_rate → ui_setRate` path and a higher default `rate`. All UI changes ship as menu-script overrides in the existing `zz_rm_ui.pk3`. Presentation only — the sim (`bg_pmove`/`sv_fps`/antilag) and wire protocol 84 are untouched.

**Tech Stack:** C, CMake + Ninja + MSVC x64, SDL2, id Tech 3 data-driven UI (`.menu` scripts + `ui` module DLL), the `zz_rm_ui.pk3` override mechanism.

**Approved design:** `C:\Users\chris\.claude\plans\we-need-to-take-lucky-hedgehog.md`

**Build/run (use the PowerShell tool, NOT Bash):**
- Build: `cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build'`
- Reconfigure: prepend `&& cmake -S C:\repo\et-rm -B C:\repo\et-rm\build -G Ninja -DCMAKE_BUILD_TYPE=Debug`.
- Tests: `... && ctest --test-dir C:\repo\et-rm\build --output-on-failure`
- Editor clang "SDL.h not found"/UI-type diagnostics are FALSE POSITIVES (LSP lacks include paths); the MSVC build is authoritative.

**Git:** branch `rm/05-settings-ux` off `rm/main`. Capture BASE sha at each task start (`git -C C:\repo\et-rm rev-parse HEAD`). Commit trailer (exact last line):
```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

**Feel gate:** `ctest -R pmove_feel` must stay green (sim untouched). Most verification is build + manual (visual), since this is UI; the **user does the visual acceptance** before merge.

---

## File Structure

- `src/sys/sdl_glimp.c` (modify) — enumerate display-0 modes, publish `r_availableModes`.
- `src/renderer/tr_init.c` (modify) — register `r_availableModes` (CVAR_ROM).
- `src/ui/menudef.h` (modify) — add `#define FEEDER_RESOLUTIONS 0x1e` (C side).
- `src/ui/ui_local.h` (modify) — add a cached resolution list to `uiInfo_t`.
- `src/ui/ui_main.c` (modify) — `UI_BuildResolutionList()`; wire `FEEDER_RESOLUTIONS` into `UI_FeederCount`/`UI_FeederItemText`/`UI_FeederSelection`; extend `ui_setRate`.
- `src/client/cl_main.c` (modify) — default `rate` 5000 → 25000.
- `rm/ui/options_system.menu`, `rm/ui/profile_create.menu`, `rm/ui/profile_create_initial.menu` (new overrides) — resolution listbox + modern connection presets.
- `cmake/RmAssets.cmake` (modify) — add the 3 new menu paths to `RM_PAK_ENTRIES`.

---

## Task 1: Engine — detect modes & publish `r_availableModes`

**Files:** `src/sys/sdl_glimp.c`, `src/renderer/tr_init.c`

- [ ] **Step 1: Register the cvar** in `src/renderer/tr_init.c` `R_Register()` (near the other `r_*` `ri.Cvar_Get` calls, e.g. by `r_mode`):
```c
ri.Cvar_Get( "r_availableModes", "", CVAR_ROM );
```

- [ ] **Step 2: Enumerate + publish** in `src/sys/sdl_glimp.c` `GLimp_Init`, right AFTER the `SDL_GetDesktopDisplayMode` block (after the `desktop`/`r_mode` resolution selection, before `SDL_CreateWindow`). Add a helper above `GLimp_Init` and call it:
```c
/*
** Build a space-delimited "WxH" list of the display's unique modes and publish
** it as r_availableModes for the UI resolution picker. Detected dynamically so
** the picker reflects what THIS monitor supports. Display 0 (matches the
** desktop-mode query above); deduped; sorted descending by area.
*/
static void GLimp_PublishAvailableModes( void ) {
	char	buf[1024];
	int		nmodes, i, n;
	struct { int w, h; } list[96];
	int		count = 0;

	nmodes = SDL_GetNumDisplayModes( 0 );
	for ( i = 0; i < nmodes && count < (int)( sizeof( list ) / sizeof( list[0] ) ); i++ ) {
		SDL_DisplayMode m;
		int j, dup = 0;
		if ( SDL_GetDisplayMode( 0, i, &m ) != 0 ) {
			continue;
		}
		if ( m.w < 640 || m.h < 480 ) {        // skip ancient tiny modes
			continue;
		}
		for ( j = 0; j < count; j++ ) {
			if ( list[j].w == m.w && list[j].h == m.h ) { dup = 1; break; }
		}
		if ( !dup ) {
			list[count].w = m.w;
			list[count].h = m.h;
			count++;
		}
	}
	// insertion-sort descending by pixel area (SDL lists largest-first already,
	// but don't rely on it)
	for ( i = 1; i < count; i++ ) {
		int kw = list[i].w, kh = list[i].h, j = i - 1;
		while ( j >= 0 && ( list[j].w * list[j].h ) < ( kw * kh ) ) {
			list[j + 1] = list[j];
			j--;
		}
		list[j + 1].w = kw; list[j + 1].h = kh;
	}
	buf[0] = '\0';
	for ( i = 0, n = 0; i < count; i++ ) {
		char entry[32];
		Com_sprintf( entry, sizeof( entry ), "%s%dx%d", ( n ? " " : "" ), list[i].w, list[i].h );
		if ( strlen( buf ) + strlen( entry ) >= sizeof( buf ) - 1 ) {
			break;                              // cap to cvar buffer
		}
		Q_strcat( buf, sizeof( buf ), entry );
		n++;
	}
	ri.Cvar_Set( "r_availableModes", buf );
	Com_RMTrace( "GLimp: r_availableModes (%d modes) = %s", n, buf );
}
```
Call `GLimp_PublishAvailableModes();` once the window/SDL video is initialized (place the call right after the desktop-mode selection block, where SDL video is already inited). Use the engine string helpers already used in this file (`Com_sprintf`, `Q_strcat`) — confirm they're declared via the file's includes; if `Q_strcat` isn't visible, use `Com_sprintf` into the running buffer.

- [ ] **Step 3: Build**
```
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake -S C:\repo\et-rm -B C:\repo\et-rm\build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build C:\repo\et-rm\build'
```
Expected: clean build.

- [ ] **Step 4: Verify the cvar is populated** (objective). Launch traced + windowed, dump the cvar:
```
$env:ETRM_TRACE="C:\repo\et-rm\build\modes_trace.log"
$h="C:\repo\et-rm\build\modeshome"; Remove-Item -Recurse -Force $h -ErrorAction SilentlyContinue
$a='+set fs_basepath "C:\repo\enemy-territory-RM" +set fs_homepath "'+$h+'" +set sv_pure 0 +set r_fullscreen 0 +set developer 1 +set logfile 2 +echo RMODES= +cvar_restart +wait 30 +echo done'
$p=Start-Process -FilePath C:\repo\et-rm\build\bin\etrm.exe -ArgumentList $a -PassThru
Start-Sleep -Seconds 9; if(!$p.HasExited){ Stop-Process -Id $p.Id -Force }
Select-String -Path $env:ETRM_TRACE -Pattern "r_availableModes" | ForEach-Object { $_.Line }
```
Expected: the trace shows `GLimp: r_availableModes (N modes) = 1920x1200 1600x1200 ...` with the dev machine's real modes. (Also check `etconsole.log` for the value via `/r_availableModes`.)

- [ ] **Step 5: Commit**
```
git -C C:\repo\et-rm add src/sys/sdl_glimp.c src/renderer/tr_init.c
git -C C:\repo\et-rm commit -m "renderer: detect + publish r_availableModes from SDL display modes

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: UI module — `FEEDER_RESOLUTIONS` listbox

**Files:** `src/ui/menudef.h`, `src/ui/ui_local.h`, `src/ui/ui_main.c`

The `ui` module reads the cvar, lists modes, and on select writes the chosen WxH. Pattern modeled on `FEEDER_GLINFO` (read path) + `FEEDER_HEADS` (cvar-write-on-select). The feeder callbacks switch on `feederID` in `UI_FeederCount` (~6243), `UI_FeederItemText` (~6389), `UI_FeederSelection` (~6660).

- [ ] **Step 1: Add the feeder id** in `src/ui/menudef.h` after `FEEDER_GLINFO 0x0000001d`:
```c
#define FEEDER_RESOLUTIONS  0x0000001e   // RM: SDL-detected resolutions (r_availableModes)
```

- [ ] **Step 2: Add a cached list to `uiInfo_t`** in `src/ui/ui_local.h` (near the other feeder counts like `numGlInfoLines`):
```c
#define UI_MAX_RESOLUTIONS 96
	int  resolutionCount;
	char resolutionList[UI_MAX_RESOLUTIONS][20];   // "WxH" strings
```

- [ ] **Step 3: Add the builder + wire the feeder** in `src/ui/ui_main.c`. Add the builder above `UI_FeederCount`:
```c
/*
==================
UI_BuildResolutionList

Parse the space-delimited r_availableModes cvar (published by the renderer from
the SDL-detected display modes) into uiInfo.resolutionList. Cheap; called from
the feeder count path.
==================
*/
static void UI_BuildResolutionList( void ) {
	char	buf[1024];
	char	*p, *tok;

	uiInfo.resolutionCount = 0;
	trap_Cvar_VariableStringBuffer( "r_availableModes", buf, sizeof( buf ) );
	p = buf;
	while ( uiInfo.resolutionCount < UI_MAX_RESOLUTIONS ) {
		tok = COM_ParseExt( &p, qfalse );
		if ( !tok[0] ) {
			break;
		}
		Q_strncpyz( uiInfo.resolutionList[uiInfo.resolutionCount], tok,
					sizeof( uiInfo.resolutionList[0] ) );
		uiInfo.resolutionCount++;
	}
}
```
In `UI_FeederCount`, add a branch (e.g. after the `FEEDER_GLINFO` branch):
```c
	} else if ( feederID == FEEDER_RESOLUTIONS ) {
		UI_BuildResolutionList();
		return uiInfo.resolutionCount;
```
In `UI_FeederItemText`, add:
```c
	} else if ( feederID == FEEDER_RESOLUTIONS ) {
		if ( index >= 0 && index < uiInfo.resolutionCount ) {
			return uiInfo.resolutionList[index];
		}
		return "";
```
In `UI_FeederSelection` (find the function ~6660; it sets `feederID`-specific selection + often `trap_Cvar_Set`), add a branch that parses "WxH" and applies it as a custom mode via the proxy:
```c
	} else if ( feederID == FEEDER_RESOLUTIONS ) {
		if ( index >= 0 && index < uiInfo.resolutionCount ) {
			int w = 0, h = 0;
			if ( sscanf( uiInfo.resolutionList[index], "%dx%d", &w, &h ) == 2 && w > 0 && h > 0 ) {
				trap_Cvar_Set( "r_customwidth",  va( "%d", w ) );
				trap_Cvar_Set( "r_customheight", va( "%d", h ) );
				trap_Cvar_Set( "ui_r_mode", "-1" );   // custom; applied via systemCvarsApply
			}
		}
```
Notes: `ui_r_mode` is the proxy synced to `r_mode` by `systemCvarsApply` (preserved). `r_customwidth/height` are set directly (they're CVAR_ARCHIVE|CVAR_LATCH and not part of the systemCvars proxy set) — acceptable; they take effect on the next `vid_restart` along with `r_mode -1`. Confirm `COM_ParseExt`, `Q_strncpyz`, `va`, `sscanf` are available in ui_main.c (they are used widely in this file / q_shared); if `sscanf` is undesirable, split on 'x' manually with `atoi`.

- [ ] **Step 4: Build the ui module**
```
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build'
```
Expected: `ui_mp_x86_64.dll` recompiles/links clean. (Feeder behavior is verified visually in Task 4/5; this step just confirms it compiles.)

- [ ] **Step 5: Commit**
```
git -C C:\repo\et-rm add src/ui/menudef.h src/ui/ui_local.h src/ui/ui_main.c
git -C C:\repo\et-rm commit -m "ui: add FEEDER_RESOLUTIONS listbox backed by r_availableModes

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Connection/rate modernization (engine + ui_setRate)

**Files:** `src/ui/ui_main.c` (`ui_setRate` ~4294), `src/client/cl_main.c` (~3427)

- [ ] **Step 1: Extend `ui_setRate`** in `src/ui/ui_main.c`. Current block maps `ui_rate` → maxpackets/packetdup with branches `>=5000`, `>=4000`, else. Add a high-rate branch FIRST so broadband presets map cleanly:
```c
	} else if ( Q_stricmp( name, "ui_setRate" ) == 0 ) {
		float rate = trap_Cvar_VariableValue( "ui_rate" );
		if ( rate >= 25000 ) {
			trap_Cvar_Set( "ui_cl_maxpackets", "30" );
			trap_Cvar_Set( "ui_cl_packetdup", "1" );
		} else if ( rate >= 5000 ) {
			trap_Cvar_Set( "ui_cl_maxpackets", "30" );
			trap_Cvar_Set( "ui_cl_packetdup", "1" );
		} else if ( rate >= 4000 ) {
			trap_Cvar_Set( "ui_cl_maxpackets", "15" );
			trap_Cvar_Set( "ui_cl_packetdup", "2" );
		} else {
			trap_Cvar_Set( "ui_cl_maxpackets", "15" );
			trap_Cvar_Set( "ui_cl_packetdup", "1" );
		}
	}
```
(The `>=25000` and `>=5000` arms are identical today but keeping both documents intent and leaves room to tune the broadband arm later. Preserve everything else in the handler chain.)

- [ ] **Step 2: Raise the default `rate`** in `src/client/cl_main.c` (~line 3427). Change:
```c
	Cvar_Get( "rate", "5000", CVAR_USERINFO | CVAR_ARCHIVE );
```
to:
```c
	Cvar_Get( "rate", "25000", CVAR_USERINFO | CVAR_ARCHIVE );   // RM: modern broadband default (was 5000)
```
Leave `cl_maxpackets` "30" and `cl_packetdup` "1" unchanged. The server clamp `[1000,90000]` (sv_client.c:1376) is unchanged and remains the ceiling.

- [ ] **Step 3: Build**
```
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build'
```
Expected: clean.

- [ ] **Step 4: Verify default on a fresh config** (objective):
```
$h="C:\repo\et-rm\build\ratehome"; Remove-Item -Recurse -Force $h -ErrorAction SilentlyContinue
$a='+set fs_basepath "C:\repo\enemy-territory-RM" +set fs_homepath "'+$h+'" +set sv_pure 0 +set r_fullscreen 0 +set logfile 2 +cvarlist rate +wait 40 +quit'
$p=Start-Process -FilePath C:\repo\et-rm\build\bin\etrm.exe -ArgumentList $a -PassThru
Start-Sleep -Seconds 10; if(!$p.HasExited){ Stop-Process -Id $p.Id -Force }
Select-String -Path "$h\etmain\etconsole.log" -Pattern "(^| )rate " | ForEach-Object { $_.Line }
```
Expected: `rate` default shows `25000` on the fresh config.

- [ ] **Step 5: Commit**
```
git -C C:\repo\et-rm add src/ui/ui_main.c src/client/cl_main.c
git -C C:\repo\et-rm commit -m "net: modern broadband rate default (25000) + ui_setRate high-rate arm

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Menu overrides (resolution listbox + modern connection presets) + packaging

**Files:** new `rm/ui/options_system.menu`, `rm/ui/profile_create.menu`, `rm/ui/profile_create_initial.menu`; modify `cmake/RmAssets.cmake`

Stock menus (in retail `pak0.pk3`) hold both selectors; all three duplicate them. Override all three so the first-run wizard matches.

- [ ] **Step 1: Extract the three stock menus** to copy/modify (PowerShell):
```
$tmp="$env:TEMP\pak0menus"; Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Copy-Item "C:\repo\enemy-territory-RM\etmain\pak0.pk3" "$env:TEMP\pak0menus.zip" -Force
Expand-Archive "$env:TEMP\pak0menus.zip" $tmp -Force
Copy-Item "$tmp\ui\options_system.menu","$tmp\ui\profile_create.menu","$tmp\ui\profile_create_initial.menu" "C:\repo\et-rm\rm\ui\" -Force
```
(These become the override sources; keep their `menuDef name`, `#include "ui/menudef.h"`, and `#include "ui/menumacros.h"` lines intact so they still resolve against stock pak0.)

- [ ] **Step 2: Replace the resolution selector** in each menu. The stock widget is:
```
MULTIACTION( 8, 118, (SUBWINDOW_WIDTH)-4, 10, "Resolution:", .2, 8, "ui_r_mode", cvarFloatList { "640*480" 3 ... "856*480 Wide Screen" 11 }, uiScript glCustom, "" )
```
Replace it with a listbox fed by `FEEDER_RESOLUTIONS` (raw id `30` = 0x1e, so no `menudef.h` override needed), plus explicit Desktop-native and Borderless controls. Use the stock listbox itemDef shape (model on the server-browser/glinfo listboxes elsewhere in the stock menus for exact keyword syntax — `type ITEM_TYPE_LISTBOX`, `feeder 30`, `elementtype LISTBOX_TEXT`, `elementwidth`, `elementheight`, `columns ...`, a `rect` sized to fit several rows). Reclaim vertical space from the row layout below it. Add:
  - A "Desktop (native)" action button → `setcvar ui_r_mode "-2"`.
  - A "Borderless fullscreen" toggle bound to `r_fullscreen` (already borderless under SDL) — optional if space allows.
  Keep the menu's existing `onOpen systemCvarsGet` / APPLY-button `systemCvarsApply` / `onESC systemCvarsReset` wiring so the `ui_r_mode` proxy still applies. (Exact rects/labels: adapt to the stock menu's coordinate system — match the surrounding items.)

- [ ] **Step 3: Replace the connection selector** in each menu. Stock:
```
MULTIACTION( 8, 332, (SUBWINDOW_WIDTH)-4, 10, "Connection:", .2, 8, "ui_rate", cvarFloatList { "Modem" 4000 "ISDN" 5000 "LAN/CABLE/xDSL" 25000 } cvarListUndefined "Unselected", uiScript update "ui_setRate", "" )
```
Replace the list with modern presets (keep the same macro, cvar, and `uiScript update "ui_setRate"`):
```
MULTIACTION( 8, 332, (SUBWINDOW_WIDTH)-4, 10, "Connection:", .2, 8, "ui_rate", cvarFloatList { "Broadband" 25000 "Fiber / LAN" 45000 "Custom (advanced)" 5000 } cvarListUndefined "Unselected", uiScript update "ui_setRate", "" )
```
(Values stay inside the server clamp ≤90000. The `ui_rate→ui_setRate` path and `systemCvarsApply` `ui_rate→rate` sync are preserved.)

- [ ] **Step 4: Register the overrides for packaging** in `cmake/RmAssets.cmake` — add to `RM_PAK_ENTRIES` (currently just `ui/options_customise_game.menu`):
```cmake
    ui/options_system.menu
    ui/profile_create.menu
    ui/profile_create_initial.menu
```
(The `GLOB_RECURSE` DEPENDS already triggers a repack; the explicit entry list is what gets stored in the pk3.)

- [ ] **Step 5: Build + confirm the pk3 contents**
```
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake --build C:\repo\et-rm\build && cmake -E tar tf C:\repo\et-rm\build\bin\etmain\zz_rm_ui.pk3'
```
Expected: `tar tf` lists `ui/options_customise_game.menu`, `ui/options_system.menu`, `ui/profile_create.menu`, `ui/profile_create_initial.menu`.

- [ ] **Step 6: Objective parse check** (the override must load without a menu parse error). Launch traced (developer 1, logfile 2) as in Task 1 Step 4; read `etconsole.log` and confirm `Parsing menu file: ui/options_system.menu` appears with NO `menu` parse error for it, and no crash. (Trick used in a prior increment: if unsure the override wins, temporarily inject a bogus keyword and confirm the engine errors on YOUR file, then remove it.) Visual confirmation of the listbox/presets is the user's job (Task 5).

- [ ] **Step 7: Commit**
```
git -C C:\repo\et-rm add rm/ui/options_system.menu rm/ui/profile_create.menu rm/ui/profile_create_initial.menu cmake/RmAssets.cmake
git -C C:\repo\et-rm commit -m "ui: resolution listbox (detected modes) + modern connection presets via menu override

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Final verification + hand off for acceptance

**Files:** none (verification); possibly `docs/THIRDPARTY.md` if any code was copied (none expected — all RM-original/SDL).

- [ ] **Step 1: Clean from-scratch build**
```
Remove-Item -Recurse -Force C:\repo\et-rm\build -ErrorAction SilentlyContinue
cmd /c '"...vcvars64.bat" >nul 2>&1 && cmake -S C:\repo\et-rm -B C:\repo\et-rm\build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build C:\repo\et-rm\build'
```
Expected: all targets + `zz_rm_ui.pk3` (now with 4 menus). 

- [ ] **Step 2: Feel gate + tests**
```
cmd /c '"...vcvars64.bat" >nul 2>&1 && ctest --test-dir C:\repo\et-rm\build --output-on-failure'
```
Expected: `pmove_feel` + `fov_math` PASS (sim untouched).

- [ ] **Step 3: Hand off for user visual acceptance** (do NOT merge/tag yet). The user runs `scripts\play.bat` and confirms: the Options/System page resolution picker lists the monitor's real modes; selecting one + applying + `vid_restart` switches to it; "Desktop (native)" works; the Connection presets read Broadband/Fiber-LAN/Custom and set rate/maxpackets/packetdup; the first-run profile wizard shows the same modern widgets; fresh-config `rate` is 25000. Report status + commit SHAs; controller merges `--no-ff` → `rm/main` and tags `rm-settings-ux` after the user signs off.

---

## Self-Review

**Spec coverage:**
- TRUE per-system resolution detection: SDL enumeration + `r_availableModes` (Task 1) → `FEEDER_RESOLUTIONS` listbox (Task 2) → menu override listbox (Task 4) ✓
- Desktop-native + Borderless entries (Task 4 Step 2) ✓
- Preserve `ui_r_mode` proxy + `systemCvarsGet/Apply` (Tasks 2 & 4) ✓
- Connection modernization: `ui_setRate` high-rate arm (Task 3) + modern presets (Task 4 Step 3) ✓
- Default `rate` 25000, maxpackets/packetdup unchanged, server clamp unchanged (Task 3) ✓
- Override 3 menus incl. first-run wizard; add to RM_PAK_ENTRIES (Task 4) ✓
- Feel gate green; sim/protocol untouched (Task 5) ✓

**Placeholder scan:** The `.menu` listbox rect/columns (Task 4 Step 2) are described by "match the stock menu's coordinate system / model on existing listboxes" rather than literal numbers, because exact coordinates depend on the extracted stock file — the feeder id (`30`), cvar bindings, and connection `cvarFloatList` values are all exact. This mirrors how the prior FOV-slider increment adapted to the stock menu.

**Type/contract consistency:** `r_availableModes` is registered (T1), populated (T1), read (T2 `UI_BuildResolutionList`), consumed by the menu feeder id `0x1e`/`30` (T2 + T4). `FEEDER_RESOLUTIONS` = `0x1e` in `menudef.h` (C) and raw `30` in the `.menu` (script) — consistent. `uiInfo.resolutionList`/`resolutionCount` defined (T2 ui_local.h) and used (T2 ui_main.c). `ui_rate`/`ui_r_mode` proxy + `systemCvarsApply` contract preserved throughout. `r_customwidth/height` + `r_mode -1` is the documented selection mapping.
