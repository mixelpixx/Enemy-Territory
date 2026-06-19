# Building the ET-RM installer

`installer/etrm.iss` is an [Inno Setup](https://jrsoftware.org/) 6 script that
produces a single, self-contained Windows installer — the player runs `setup.exe`
and plays. There is no separate runtime download, no map data to fetch, and no admin
required (it installs per-user).

## What it bundles

| Group | Files |
|-------|-------|
| Release engine | `etrm.exe`, `etrmded.exe`, `etrm_renderer2.dll`, `SDL2.dll` |
| Game modules | `qagame_mp_x86_64.dll` (server) + loose `cgame`/`ui` (client loads these at startup) |
| VC++ runtime (app-local) | `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` |
| RM content | `etmain/rm_bin.pk3`, `etmain/zz_rm_ui.pk3`, `etmain/rm_showcase.pk3` |
| Retail data | `etmain/pak0.pk3`, `pak1.pk3`, `pak2.pk3`, `mp_bin.pk3` |

The app-local VC++ runtime means no Visual C++ Redistributable step — the UCRT
(`api-ms-win-crt-*`) ships with Windows 10/11, and the two VC runtime DLLs sit next to
`etrm.exe`. Installed size is ~230 MB (almost entirely the retail data).

> **Retail data.** The installer bundles the original Wolfenstein: Enemy Territory
> paks for offline play. ET was released as a free download; redistributing the data
> is the operator's call (see the project's stance in the README). The `.iss` sources
> the paks from a local retail install (`SrcRetail`); they are **not** committed to
> this repository.

## Prerequisites (build host)

1. **A Release build** of the engine in `build-rel\bin\` — the Debug build pulls in
   the debug CRT, which isn't on end-user machines:
   ```
   cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build-rel
   ```
2. **Retail ET paks** present at the `SrcRetail` path in the `.iss`
   (`pak0-2.pk3`, `mp_bin.pk3`).
3. **Inno Setup 6** (the `ISCC.exe` compiler) from jrsoftware.org.

The three `#define` paths at the top of `etrm.iss` (`SrcBin`, `SrcRetail`, `SrcCRT`)
point at the local build output, the retail data, and the VS x64 VC-runtime redist
folder; adjust them to your machine if they differ.

## Building

```
ISCC.exe installer\etrm.iss
```

The installer lands in `dist\etrm-setup-<version>.exe` (the `dist\` tree is
gitignored). `setup.exe` installs per-user (no admin), creates Start-menu shortcuts
(including a "modern renderer" launch that turns on gl2 + materials + sun shadows),
an optional desktop icon, and an uninstaller.

## Verifying

A clean-machine check (no dev tools / runtime on `PATH`):

```
setup.exe /VERYSILENT /CURRENTUSER /DIR=<tempdir>
```

then launch `<tempdir>\etrm.exe` with `PATH` stripped to `System32` only and confirm
the game boots and renders — e.g. drive the showcase map headlessly:

```
etrm.exe +set cl_renderer gl2 +set r_normalMapping 1 +set cg_shadows 5 +devmap rm_showcase +screenshot rel_verify +quit
```

A rendered screenshot with no `.dmp` confirms the bundle is self-contained (engine,
app-local runtime, RM content, and retail data all present).
