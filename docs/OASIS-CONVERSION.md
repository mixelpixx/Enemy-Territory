# Converting stock Oasis to full modern lighting (`oasis_rm`)

This documents the in-progress conversion of the stock ET map **oasis** to the modern
renderer's full lighting set (per-pixel materials + realtime directional sun shadows +
deluxe lightmaps), as a demonstration of taking a real, beloved map "all the way."
It is the worked example behind [`MAPPER-HOOKS.md`](MAPPER-HOOKS.md).

> **Status: experimental / in progress.** The map recompiles, lights, and renders
> under `cl_renderer gl2` with 146 kept point lights + a directional sun + deluxe +
> HD materials. Outstanding: restoring the baked-in `misc_model` set dressing (palms,
> vases, …) and a full-quality light bake. See **Current state** below.
>
> All inputs and outputs are **Splash Damage–derived** (the original map source, the
> retail textures, the mapobject models) and therefore are **not committed to this
> repo** — they live in `build/oasis_rm/` (gitignored), assembled locally from your
> own retail data + the downloaded source, the same posture as the bundled retail data
> in the installer. This document + the generator scripts are the committed, reusable
> parts.

## Pipeline

```
oasis_final.map (SD source)                          ── O1
   └─ + parallel sun light, rename oasis_rm.map      ── O2
   └─ + terrain alphamap (placeholder)               ── O2
   └─ q3map2 -meta -keeplights / -vis / -light       ── O3
        -wolf -patchshadows -deluxe -deluxemode 1 -external -lightmapsize 512
   └─ + HD materials (auto-bump from retail diffuse) ── O4
   └─ pack maps/ + materials -> oasis_rm.pk3         ── O5
```

The engine support (deluxe, sun `RL_DIRECTIONAL`, materials) is the R2-6 work already
in `main`; this is **content only** — no engine changes.

## Resources

| Need | Source | Notes |
|------|--------|-------|
| `oasis_final.map` | wolffiles.de filebase backup (GitHub `wolffiles/FileBaseIndex` → `wolf_et_map_source.zip`, Google-Drive id `1vYm7gtYYRxSpuXjQRdD4YUNv9_-7cUTL`) | 6 stock map sources; **`.map` files only — no models/terrain assets** |
| q3map2 | NetRadiant-custom `C:\repo\et-deps\netradiant\q3map2.exe` (`-game et`) | |
| Textures / shaders | retail `pak0.pk3` via `-fs_basepath C:\repo\enemy-territory-RM` | sky `sd_siwasky` carries `q3map_sun 0.75 0.70 0.6 135 199 49` |
| Terrain alphamap `mp_siwa_thomasc.pcx` | **source-only, missing** → placeholder generated | see Workarounds |
| 24 `misc_model` `.md3`s (palms, vases, …) | **source-only, missing from paks** → being sourced | see Models |

## Compile commands (run from `build/oasis_rm/maps/`)

```
Q=C:\repo\et-deps\netradiant\q3map2.exe
BASE=C:\repo\enemy-territory-RM
HP=C:\repo\et-rm\build\oasis_rm

%Q% -game et -fs_basepath %BASE% -fs_homepath %HP% -meta -keeplights  oasis_rm.map
%Q% -game et -fs_basepath %BASE% -fs_homepath %HP% -vis              oasis_rm.map   (optional; skipped for screenshot demo)
%Q% -game et -fs_basepath %BASE% -fs_homepath %HP% -light -wolf -patchshadows -samples 3 \
            -deluxe -deluxemode 1 -external -lightmapsize 512  oasis_rm.map
```

`-keeplights` (BSP phase) preserves Oasis's 146 point lights + the appended sun.
`-deluxemode 1` = tangent-space deluxemaps (what renderer2 wants). `-external` writes
`maps/oasis_rm/lm_*.tga` (lightmaps + interleaved deluxemaps). Drop `-fast` for the
final bake (the demo used `-fast`).

## Workarounds for missing source assets

- **Terrain alphamap.** Oasis's dunes are an old-style `"terrain" "1"` entity (8
  layers, `shader mp_siwa/lmterrain`) whose blend is driven by `mp_siwa_thomasc.pcx` —
  a *source-only* file never shipped. We generate a **placeholder PCX** (256×256, all
  index 0 → layer 0 = `desert_sd/sand_wave_desert`, the base sand). q3map2 inherits
  id's classic `LoadPCX` cap (`xmax≥640 || ymax≥480` → "Bad pcx file"), and reads the
  alphamap from its **VFS**, so the PCX must be q3map2-valid (v5 / 8-bit / RLE /
  256-color trailer) and placed under `-fs_homepath …/etmain/`. Result: correct dune
  *geometry* + uniform base-sand texture (the original blend added grass patches /
  variation we can't faithfully recreate without the source).
- **Missing models** — see below.

## Models: the set-dressing gap

Oasis references **56 `misc_model` `.md3`s + 6 `.ase`** prefab/tunnel meshes. ~32 of
the `.md3`s ship in `pak0` (tanks, guns, radios, the pump objective — gameplay models).
The rest are **set dressing that was baked into the shipped `oasis.bsp`** at the
original compile, so their **source models were never distributed in the paks** (only
their textures were). q3map2 needs the source models to re-insert them; missing ones
are omitted (non-fatal).

Sourced so far (**24 of 34 models in**):
- **GtkRadiant 1.6.20120520 ETPack** (the ET map editor bundles the SD mapobjects at
  the exact paths, incl. SD's `miltary_trim` typo) → 22 models: all 6 palms, vases,
  plants/bushes, furniture, baskets, `fuel_can`, `dragon_teeth`, `wagon_tilt`,
  `siwa_cushiona1`. `https://www.wolffiles.eu/files/gtkradiant-16-20120520/download`
- **hkf1 map** → `toolshed/generator.md3`, `toolshed/tools1.md3`.
  `https://www.wolffiles.eu/files/hkf1/download`

Still missing (**10**, only in the gated ~4.5 GB Teuthis "Mapping sources" SD pack;
all non-fatal — they're detail on top of brushwork that's already present):
- `.ase`: `prefabs_sd/prefabs_egyptian_{obelisk,pillar,pillar_half,pillar_with_bottom}`
  and `siwa_tunnels_sd/{blocks,tunnel_new2}` (obelisk/pillar/tunnel detail meshes).
- `.md3`: `siwa_props_sd/{siwa_book2,siwa_vessel1,siwa_vessel2,siwa_vessel3}` (small
  interior pottery).

## Current state

- ✅ `oasis_rm.bsp` compiles (no leak), 11–12 MB.
- ✅ Loads under gl2: **146 omni lights + 1 directional sun**, 8 external lightmaps
  (4 + 4 deluxe), no crash.
- ✅ Sun shadows + deluxe + HD materials visible; A/B vs stock oasis = ~77% of frame
  changed at the old-city viewpoints, brightness preserved.
- ✅ **24 of 34 set-dressing models** re-inserted (palms, vases, plants, furniture,
  baskets, toolshed, …) from GtkRadiant + hkf1; 10 detail meshes omitted (see above).
- ✅ Full-quality light bake (`-bounce 4 -samples 2 -deluxe`, no `-fast`).
- Demo staged loose in `build/bin/etmain/` (`devmap oasis_rm`, `cl_renderer gl2`).

## Caveats

- **gl2 only** — material shaders are rejected by the original renderer (see
  MAPPER-HOOKS.md). View with `cl_renderer gl2`.
- **Visual demo** — objective scripts (`oasis.script`) aren't wired for `oasis_rm`;
  it's a `devmap` / noclip walkthrough, not competitive play.
- **Licensing** — SD-derived; treated like the bundled retail data (not committed).

## Reproduce

1. Download `wolf_et_map_source.zip` (above), extract `oasis_final.map` to
   `build/oasis_rm/maps/oasis_rm.map`.
2. Append the parallel sun light entity (see git history / the snippet in this dir's
   tooling); generate the placeholder alphamap into `build/oasis_rm/etmain/`.
3. Run the three q3map2 phases above.
4. Generate HD materials (the `make_hd_pack.py` generator pointed at Oasis's texture
   set) and the override shader.
5. Pack `maps/oasis_rm.bsp` + `maps/oasis_rm/*` + materials into `oasis_rm.pk3` in
   `etmain`; `devmap oasis_rm` under gl2.
