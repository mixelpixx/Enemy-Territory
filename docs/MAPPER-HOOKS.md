# Mapper hooks — authoring for the modern renderer

The modern renderer (`cl_renderer gl2`, see [`MODERN-EFFECTS.md`](MODERN-EFFECTS.md))
can light authored map content in ways the original 2003 data never exercised:
per-pixel **materials** (normal / specular / parallax), a directional **sun** that
casts real-time shadows, and **deluxe** (per-pixel directional) lightmaps. None of
these change anything on stock maps — they only appear when a map author opts in.
This document is the contract for doing that.

The reference map is **`rm_showcase`** (ships in `rm_showcase.pk3`, built from
[`rm_showcase/`](../rm_showcase)). Launch it under the modern renderer to see every
hook at once:

```
+set cl_renderer gl2 +set r_normalMapping 1 +set cg_shadows 5 +set r_softShadows 4 +devmap rm_showcase
```

> **These are gl2-only features.** The original renderer (`gl1`, the default) rejects
> any shader containing keywords it doesn't recognize, so a surface using the material
> keywords below renders as the engine default texture under gl1. Author material
> content for gl2; keep gl1-critical surfaces on plain shaders. Stock content and the
> rest of a mixed map render identically under both renderers.

## Materials (normal / specular / parallax)

Define a shader with the renderer2 material keywords. A material needs a **diffuse**,
a tangent-space **normal** map, and a **specular** map; `parallax` enables
view-dependent depth from a height field.

```
textures/mymap/crate
{
	qer_editorimage textures/mymap/crate_d.tga
	diffuseMap  textures/mymap/crate_d.tga
	bumpMap     textures/mymap/crate_n.tga   // tangent-space normal; HEIGHT in alpha
	specularMap textures/mymap/crate_s.tga
	parallax                                  // optional: relief from the normal's alpha
}
```

Texture conventions (the engine reads them this way):

- **Normal map** is tangent-space: RGB = `(normal * 0.5 + 0.5)`, so flat areas are the
  familiar `(128,128,255)` blue.
- **Parallax height** lives in the normal map's **alpha** channel (`reliefMapping`
  samples `u_NormalMap.a`). No height → no alpha → `parallax` is a no-op.
- **Specular** is a grayscale intensity map (bright = shiny).

Toggle at runtime: `r_normalMapping 1` (LATCH — set on the command line or
`vid_restart`), `r_parallaxMapping 1`, `r_specularScale` / `r_specularExponent` to
tune. With `r_normalMapping 0` the surface falls back to plain lightmapped diffuse.

## Sun / world shadows

Real-time directional sun shadows need two things:

1. A **sky shader with `q3map_sun`**, which sets the sun colour and direction. Any
   stock ET sky works (the showcase uses `textures/skies_sd/sd_siwasky`); or author
   your own. The engine reads `q3map_sun` to aim the sun (`tr.sunDirection`):
   `q3map_sun <r> <g> <b> <intensity> <azimuth°> <elevation°>` — lower elevation =
   longer shadows.
2. A **`light` entity with `"parallel" "1"`**, which the renderer turns into the
   directional (`RL_DIRECTIONAL`) shadow-casting sun. **It must survive compilation**
   — compile with q3map2 `-keeplights` (stock maps strip light entities, which is why
   they have no real-time sun).

```
{
	"classname" "light"
	"origin"    "-300 -200 470"
	"light"     "260"
	"_color"    "1 0.95 0.85"
	"parallel"  "1"
}
```

Shadows render with a mapped `cg_shadows` mode (2–6; see below) and soften with
`r_softShadows` (2–6).

## Deluxe lightmaps

Deluxe maps store, per lightmap texel, the dominant light **direction** alongside the
baked intensity, so normal-mapped surfaces catch the baked lighting per-pixel. Enable
by compiling with q3map2 **`-deluxe`** — that's it. The engine auto-detects it (the
worldspawn carries `_q3map2_cmdline ... -deluxe`, or set `"deluxeMapping" "1"`
explicitly) and lights material surfaces with the per-texel direction. Deluxe rides
the normal-mapping path, so it shows with `r_normalMapping 1`. Inspect the raw deluxe
data with `r_showDeluxeMaps 1`.

> On flat (non-normal-mapped) surfaces deluxe slightly darkens the lighting (the N·L
> term re-applies the baked cosine); the payoff is on normal-mapped surfaces. Light
> flat areas with ordinary lightmaps and reserve deluxe's benefit for material content.

## Compiling

`rm_showcase` is built with the GPL **q3map2** (NetRadiant-custom). The committed
`.bsp` means building and playing ET-RM needs no map compiler — q3map2 is only needed
to *re-author* the map. The phases, keeping lights and emitting deluxe:

```
q3map2 -game et -fs_basepath <ET install> -meta -keeplights  mymap.map
q3map2 -game et -fs_basepath <ET install> -vis               mymap.map
q3map2 -game et -fs_basepath <ET install> -light -deluxe -fast -patchshadows  mymap.map
```

`-keeplights` belongs on the **BSP (`-meta`) phase**, not `-light`. Drop `-fast` for a
final-quality bake.

The showcase's geometry and its three material texture sets are generated
deterministically by [`rm_showcase/tools/`](../rm_showcase/tools) — run
`make_showcase_textures.py` then `make_showcase_map.py`, recompile with the commands
above, and rebuild to repack `rm_showcase.pk3`.

## Cvar reference

| Cvar | Use |
|------|-----|
| `cl_renderer gl2` | select the modern renderer (then `vid_restart`) — required for all of the above |
| `r_normalMapping 1` | enable per-pixel materials (LATCH); also gates deluxe |
| `r_parallaxMapping 1` | enable parallax relief (needs height in the normal's alpha) |
| `cg_shadows 2..6` | mapped shadow modes (the cvar is `cg_shadows`, **not** `r_shadows`) |
| `r_softShadows 2..6` | PCF soft-shadow filtering |
| `r_showDeluxeMaps 1` | debug: visualize the deluxe direction field (cheat) |

## Known limitations

- **gl1 has no materials.** Material shaders are gl2-only (see the note above).
- **`r_softShadows 1` (PCSS)** and **`cg_shadows 7` (stencil)** are broken upstream —
  use `r_softShadows` 2–6 and `cg_shadows` 2–6. (Carried over from
  [`MODERN-EFFECTS.md`](MODERN-EFFECTS.md).)
- Deluxe darkens flat surfaces, as noted above.
