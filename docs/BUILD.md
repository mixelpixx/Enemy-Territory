# Building and running

## Prerequisites (Windows)

- Windows 10 or 11, 64-bit.
- Visual Studio 2022 (or the standalone **Build Tools for Visual Studio 2022**)
  with the **Desktop development with C++** workload, which provides the MSVC
  x64 toolset and a Windows 10/11 SDK.
- **CMake** 3.20 or newer.
- **Ninja** (recommended generator). The Visual Studio generator also works.

The build uses the MSVC toolchain. A clang/LLVM-only setup is not currently
supported for the C++ portions.

## Configure and build

From a developer shell that has the x64 MSVC environment loaded (the
"x64 Native Tools Command Prompt for VS 2022", or a shell after running
`vcvars64.bat`):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Outputs are written to `build/bin/`:

| File | Description |
|------|-------------|
| `etrm.exe` | Client |
| `etrmded.exe` | Dedicated server |
| `qagame_mp_x86_64.dll` | Server game module |
| `cgame_mp_x86_64.dll` | Client game module |
| `ui_mp_x86_64.dll` | User interface module |
| `etrm_feeltest.exe` | Movement-determinism test harness |

### Build options

| Option | Default | Effect |
|--------|---------|--------|
| `BUILD_CLIENT` | ON | Build `etrm` and the client/ui modules |
| `BUILD_SERVER` | ON | Build `etrmded` and the game module |
| `BUILD_FEEL_HARNESS` | ON | Build the `etrm_feeltest` determinism test |

SDL2 is detected if present but is not required (the Windows client uses the
native platform layer for now). Set `-DSDL2_DIR=<dir with sdl2-config.cmake>`
if you want it located for future work.

## Game data

The repository ships no game assets. Obtain a retail *Wolfenstein: Enemy
Territory* **2.60b** installation and locate its `etmain` directory, which
contains `pak0.pk3`, `pak1.pk3`, `pak2.pk3`, and `mp_bin.pk3`.

The engine loads the game/cgame/ui modules as native DLLs. The compiled
`*_mp_x86_64.dll` files in `build/bin/` are found there automatically when the
executable runs with `build/bin` as its working directory.

## Running

Client, loading a map locally (point `fs_basepath` at the directory that
*contains* `etmain`):

```
build\bin\etrm.exe +set fs_basepath "C:\path\to\ET" +set sv_pure 0 +devmap oasis
```

Dedicated server:

```
build\bin\etrmded.exe +set dedicated 1 +set fs_basepath "C:\path\to\ET" +set sv_pure 0 +map oasis
```

Then connect a client to it:

```
build\bin\etrm.exe +set fs_basepath "C:\path\to\ET" +set sv_pure 0 +connect 127.0.0.1
```

## Tests

The movement-determinism harness verifies that the shared player-movement code
(`bg_pmove`) produces a bit-identical result for a fixed input sequence:

```
ctest --test-dir build -R pmove_feel --output-on-failure
```

It fails if the movement physics change. The golden hash is baselined per
toolchain/architecture; re-baseline deliberately via `-DETRM_PMOVE_GOLDEN=<hash>`
only when intentionally changing the simulation or moving to a new toolchain.

## Debug tracing

A lightweight, flush-on-write trace logger is built in and disabled by default.
Set the environment variable `ETRM_TRACE` to a file path to enable it:

```
set ETRM_TRACE=C:\path\to\trace.log
```

Trace points exist around engine and module initialization and the render path.
It is invaluable for locating early-startup or pre-console crashes.
