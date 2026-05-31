# ET-RM Modernization Catalog

> Bringing 2000-era id Tech 3 implementations up to 2026 best practice.
> **Policy: "infrastructure aggressive, sim sacred."** Rewrite freely outside the
> gameplay simulation; inside it, only changes *proven bit-identical* by the
> demo-replay determinism harness (Phase 3) may land.

This catalog is the menu the build draws from. It does not replace the phased
plan — it feeds it. Each item is rated:

- **Effort** low / med / high
- **Perf** upside low / med / high
- **Feel-risk**: `none` · `engine-safe` (presentation/infra, no sim impact) ·
  ⚠️ `SIM` (touches or feeds the simulation → **harness-gated, sim TUs pinned to strict FP**)

## The dividing line (verified in this tree)

**Sacred — bit-identical or it isn't ET:**
`game/bg_pmove.c`, `game/bg_slidemove.c` (shared movement, runs identically on
client prediction + server), `server/sv_main.c` `SV_Frame` 20 Hz tick
(`frameMsec = 1000 / sv_fps`), antilag/unlagged reconciliation, and the on-wire
protocol (`qcommon/msg.c` deltas, `qcommon/huffman.c`).

**Fair game:** everything in `qcommon` / `server` / `client` / `renderer`
infrastructure, the platform layer, build, and bundled libs.

**The classic trap:** the x87→SSE2 FP-model change and swapping `Q_rsqrt`/`myftol`
silently alter `pmove` rounding. They're listed as ⚠️ SIM for exactly this reason.
`rsqrtss` is *not* bit-identical to the `0x5f3759df` hack; keep the literal
original in any sim TU and only swap it in renderer/engine code.

---

## A. Compiler / build-era wins (the free perf the old asm chased)

| # | 2000-era (in tree) | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| A1 | `wolf.vcproj`/SCons, x87 FP | CMake + clang/MSVC2022, SSE2 FP, `-march=x86-64-v2` | low–med | med | ⚠️ SIM (FP-model for sim TUs only; infra none) |
| A2 | per-TU, no cross-module inlining | LTO (`-flto` / `/GL /LTCG`) | low | low–med | engine-safe (strict-FP/exclude sim TUs) |
| A3 | no PGO | PGO from a demo/bot-match profile | med | low–med | engine-safe |

> Modern autovectorization + SSE2-by-default match or beat the hand-asm, portably.

## B. SIMD / math (retire the hand-asm)

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| B1 | `myftol` `fld/fistp` asm (`q_math.c:596`) | `(long)f` → `cvttss2si` / intrinsic | low | low | ⚠️ SIM |
| B2 | `Q_rsqrt` `0x5f3759df` (`q_math.c:574`) | hw `rsqrtss`+Newton (engine) / exact `1/sqrtf` | low | low | ⚠️ SIM (feeds `VectorNormalize`) |
| B3 | MMX `Com_Memcpy`/`Com_Memset` (`common.c:3296+`) | CRT `memcpy`/`memset` (AVX, dispatched) | low | low–med | engine-safe (bit-exact) |
| B4 | AoS `tess.xyz[i]` "padded for SIMD" (`tr_shade.c`) | SoA / aligned SIMD batch transforms | med | med | engine-safe (renderer) |

## C. Multithreading / job system — **the single biggest win**

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| C1 | single-threaded immediate-mode render | SMP: frontend/backend split + job pool (Quake3e/ioq3 `r_smp`) | high | **high** | engine-safe (sim stays on main thread @20 Hz) |
| C2 | synchronous map load (`files.c`/`tr_image.c`) | threaded pk3 inflate + image/model decode; main thread only uploads | med–high | med (load) | engine-safe |
| C3 | single-threaded `bspc`/light/AAS compile | thread-pool per-cluster/per-luxel | med | med (tooling) | engine-safe |

> Consumer multicore didn't exist in 2000; the engine still uses one core.

## D. Memory management

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| D1 | `mainzone`+Hunk (`common.c:816`) | mimalloc under `Z_Malloc`/Hunk, or Quake3e Zone rework (2–6× server mem) | low–med | med (servers) | engine-safe |
| D2 | manual Hunk marks | arena/pool for frame-scoped data | med | low–med | engine-safe |

## E. Asset & file I/O

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| E1 | libjpeg **6b (1998)** `jpeg-6/` | libjpeg-turbo (2–6× decode) | low | med | engine-safe |
| E2 | zlib **1.1.3** in `unzip.c` | zlib-ng (~4×, drop-in) / zstd for new packs | low | med | engine-safe |
| E3 | JPEG/TGA → raw upload | DDS/BCn + KTX2/Basis GPU-ready textures | med–high | med | engine-safe (renderer phase) |
| E4 | `fread` (`files.c`) | mmap / `MapViewOfFile` zero-copy | low–med | low–med | engine-safe |
| E5 | bundled `ft2/` FreeType | system/current FreeType | low | low | engine-safe |
| E6 | curl **7.12.2 (2004)** | current curl | low | n/a (security) | engine-safe |

## F. Renderer *(summary — see Phase 7 plan)*

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| F1 | `qglBegin`/`qglVertex3fv` immediate mode, ARB asm shaders (`tr_shade.c`) | VBO/VAO + GLSL (GL3.3, ET:Legacy renderer2) → optional Vulkan (Quake3e) | high | **high** | engine-safe |

## G. Networking transport *(keep on-wire behavior identical)*

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| G1 | IPv4 only (`AF_INET`) | dual-stack IPv6 via `getaddrinfo` (additive) | med | n/a (reach) | engine-safe |
| G2 | adaptive Huffman per packet (`huffman.c`), delta snapshots (`msg.c`) | faster impl, **byte-identical bitstream** | med | low | ⚠️ SIM (protocol compat — golden-packet + demo gated) |

## H. Data structures & hygiene

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| H1 | linear scans (`cvar.c`/`cmd.c`/pk3 index in `files.c`) | open-addressing hash maps | med | low–med | engine-safe (preserve sim-visible iteration order) |
| H2 | raw `strcpy`/`sprintf` scattered | `Q_strncpyz`/`Com_sprintf` sweep + ASan/`/sdl` in CI | med | n/a (safety) | engine-safe |
| H3 | `vm_x86.c` x86-only JIT + LCC QVM | x86_64/ARM JIT + native module option; keep QVM for mod compat | high | med | ⚠️ SIM (game logic runs here) |

## I. Bot AI / navigation

| # | 2000-era | 2026 | Effort | Perf | Feel-risk |
|---|---|---|---|---|---|
| I1 | AAS (`botlib/be_aas_*`) + `bspc` | Recast/Detour navmesh (keep AAS fallback) | high | med | ⚠️ SIM-adjacent (bot pathing changes; bots aren't in demo determinism) |

---

## Prioritized shortlist

### Quick wins — do first (high value, low risk, engine-safe)

0. **Build the determinism harness first** (Phase 3) — the gate that unblocks every ⚠️ SIM item.
1. **libjpeg 6b → libjpeg-turbo** (E1)
2. **zlib 1.1.3 → zlib-ng** (E2)
3. **mimalloc under Z_Malloc/Hunk** (D1)
4. **CMake + modern compiler, LTO, ASan/warnings in CI** (A1 infra + A2) — sim TUs pinned strict-FP
5. **MMX `Com_Memcpy`/`Com_Memset` → CRT** (B3)
6. **IPv6 / getaddrinfo** (G1)
7. **curl 7.12.2 → current** (E6, security)
8. **Hash maps for pk3/cvar/cmd lookup** (H1)
9. **Modern FreeType + bounds-safe string sweep** (E5, H2)

### Big bets — high effort, high payoff

1. **Threaded/SMP renderer backend + job system** (C1) — largest single perf win; port from Quake3e/ioq3
2. **Renderer: immediate-mode → VBO/VAO + GLSL → Vulkan** (F1) — Phase 7
3. **Parallel/async asset streaming** (C2) — kills load hitches; pairs with E1/E2
4. **Whole-engine SSE2 FP migration, sim TUs strict-FP-pinned** (A1) — unlocks B1/B2; riskiest, harness-only
5. **AAS → Recast/Detour** (I1) — accepts bot-behavior divergence; keep AAS fallback

### Prerequisite (step zero of any ⚠️ SIM work)
Full call-graph audit of `Q_rsqrt`/`myftol`/x87 math reaching
`bg_pmove.c`/`bg_slidemove.c`/antilag — so we know exactly which math must stay literal.

---

*Catalog compiled from research against this tree + the reference forks
(ETe, Quake3e, ioquake3, ET:Legacy). Perf magnitudes are mostly Q3/generic
proxies; real numbers come from harness + profiling on this engine.*
