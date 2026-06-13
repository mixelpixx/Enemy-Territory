# Modern renderer effects

The optional modern renderer (`cl_renderer gl2`, vendored from ET:Legacy's
"renderer2") exposes a set of modern rendering features. Every one of them is
**off by default**; the default boot of the modern renderer is intended to
reproduce the original renderer's look, and is regression-tested to do so
(see "Off by default" below).

This document is the switch reference: what each cvar does, how to set it, what
it actually looks like on the stock retail maps, its approximate cost, and its
known limitations. The numbers and verdicts here come from the R2-5
bring-up measurements (Debug build, integrated GPU; treat fps figures as
relative, not absolute).

All of these cvars require the modern renderer. Select it first:

```
cl_renderer gl2
vid_restart
```

If the modern renderer fails to load for any reason, the game falls back to the
original renderer and these cvars do nothing.

## How to read the tables

- **Apply** — "live" means the change takes effect on the next frame. "latched"
  means you must run `vid_restart` (or set it on the command line before the
  renderer starts) for the change to take effect; the console will tell you the
  cvar "will be changed upon restarting."
- **fps cost** — measured with the `t4demo` timedemo (220 frames) in a Debug
  build on a low-end integrated GPU. The relative cost is what matters. A modern
  discrete GPU will pay far less; an absolute Release-build number will be much
  higher.
- **On stock content** — what you actually see on the shipped 2003 retail maps
  (oasis, goldrush, etc.), which were built before any of this technology
  existed. Several features are technically working but visually inert on stock
  content because they need authored material/lighting data that the retail
  assets do not contain. Where that is the case it is stated plainly.

## High dynamic range and bloom

| Cvar | Values | Apply | Default |
|---|---|---|---|
| `r_hdrRendering` | 0/1 | latched (`vid_restart`) | 0 |
| `r_bloom` | 0/1 | live | 0 |
| `r_bloomBlur` | float | live | 1.0 |

**`r_hdrRendering`** renders the scene to a floating-point framebuffer and tone-
maps it back to the display, with automatic exposure adaptation. On stock maps
this is clearly visible and works as intended: bright outdoor sand on oasis is
compressed and the auto-exposure pulls the dark goldrush night map up toward
mid-grey, so both move toward a balanced exposure. Keep `r_gamma` at 1.0 — gl2
applies gamma in-shader, and HDR does not double-apply it.

**`r_bloom`** adds a glow around bright pixels. It is independent of HDR (no
restart needed) and is most visible on bright scenes — oasis sky/sand glow
clearly; on the dark goldrush map almost nothing exceeds the bloom threshold so
it is nearly a no-op there (correct behavior, not a failure). Under HDR, bloom
is subtler because tone mapping has already compressed the highlights before the
bloom pass sees them. `r_bloomBlur` widens the blur kernel.

**Cost:** HDR + bloom together measured roughly **−55%** fps (about 2.2x frame
cost) — the most expensive feature pair, due to the full-resolution float
framebuffer round-trip plus the tone-map and bloom passes.

## Shadow mapping

> The shadow-mode cvar is named **`cg_shadows`**, not `r_shadows`. There is no
> engine cvar called `r_shadows`; the renderer's internal `r_shadows` variable
> is registered under the name `cg_shadows`.

| Cvar | Values | Apply | Default |
|---|---|---|---|
| `cg_shadows` | 0–7 | latched (`vid_restart`) | 1 |
| `r_softShadows` | 0–6 | latched (`vid_restart`) | 0 |
| `r_shadowMapQuality` | 0–4 | latched | 3 |
| `r_dynamicLightShadows` | 0/1 | live | 1 |

`cg_shadows` modes:

| Value | Mode |
|---|---|
| 0 | none |
| 1 | planar blob decal (the baseline default — a dark blob under players) |
| 2 | ESM 16-bit shadow map |
| 3 | ESM 32-bit |
| 4 | VSM 16-bit |
| 5 | VSM 32-bit |
| 6 | EVSM 32-bit |
| 7 | stencil volumes — **known-off, see below** |

Modes **2–6 are shadow mapping** and they work — for **dynamic lights**. The
path that stock content can actually exercise is dynamic-light shadows: a
grenade, muzzle flash, or flamethrower fireball is an omni light, and with a
mapping mode selected (e.g. `cg_shadows 5`) plus `r_dynamicLightShadows 1`,
world geometry correctly occludes that moving light — e.g. a cart blocks an
explosion's light pool and casts a moving shadow on the wall behind it, with no
flicker or lag. All of modes 2–6 render this correctly (ESM modes show slightly
softer/lighter shadow cores, cosmetic only).

**`r_softShadows`** values 2–6 apply PCF softening (sample count = value + 1) to
the shadow edges; this works. Edge-quality differences between PCF tap counts
are subtle at high resolution with the default quality-3 maps.

**Cost:** `cg_shadows 5` + `r_softShadows 4` measured roughly **−28%** fps —
much cheaper than HDR+bloom.

### Sun / world shadows: known-off on stock content

Directional sun shadows and static world-geometry shadow casting do **not**
appear on the retail maps, and this is structural, not a bug. The stock 2003
BSPs ship with **zero** renderer2 light entities — the map compiler baked all
static lighting into lightmaps and stripped the light entities. A cast sun
shadow requires a directional light entity (`parallel 1`) that simply is not
present in the retail content, and renderer2 has no "force a sun" override. With
no directional light in the scene, the sun-cascade machinery
(`r_parallelShadowSplits` etc.) has nothing to drive, so it produces nothing to
look at. The full world-surface shadow path itself is wired and verified (via a
synthetic test material during bring-up); it needs authored map content to show,
which is future work.

## Per-pixel material lighting (normal / specular / parallax / reflection)

| Cvar | Values | Apply | Default |
|---|---|---|---|
| `r_normalMapping` | 0/1 | latched | 0 |
| `r_specularScale` | float | latched | 0.2 |
| `r_specularExponent` | float | latched | 512.0 |
| `r_parallaxMapping` | 0/1 | latched | 0 |
| `r_reflectionMapping` | 0/1 | latched | 0 |

These are the "PBR-ish" material features. They all work, but they are
**nearly invisible on stock content**, because the retail textures are diffuse-
only — they carry no normal (`_nm`), specular (`_s`), or height maps for these
features to act on.

**`r_normalMapping`** switches world surfaces from the simple vertex-lit path to
a per-pixel diffuse/bump/specular path. With no normal map present, the surface
gets a flat normal plus a small specular term, so the change is sub-perceptual
on stock maps. It does work — when given an authored normal-mapped material, the
surface shows clear per-pixel relief. `r_specularScale` / `r_specularExponent`
tune the specular highlight intensity / tightness on that path.

**`r_parallaxMapping`** adds view-dependent depth to surfaces that have a height
map and the `parallax` material keyword. On stock content this is a true no-op
(the keyword is absent from retail shaders). It works on authored material — the
parallax shift is strongest at grazing angles — and required one one-line fix to
a vendored GLSL shader to compile (a missing uniform declaration; see
`docs/THIRDPARTY.md`). The fix only affects the parallax-enabled shader
permutation, so default rendering is untouched.

**`r_reflectionMapping`** (with `r_normalMapping 1`) builds environment cube
probes from the BSP and applies cube-map reflections. The probe infrastructure
is live and reachable on stock maps (it builds hundreds of probes from the world
geometry), but the reflection effect itself is gated through the normal-mapping
material path, so it is a near-no-op on diffuse-only textures. Visible payoff
needs authored reflective materials.

In short: these four are correct and ready, but they are content features —
they light up when the maps carry modern materials, which is future work.

## Screen-space and post-process effects

| Cvar | Values | Apply | Default |
|---|---|---|---|
| `r_screenSpaceAmbientOcclusion` | 0/1/2 | live | 0 |
| `r_depthOfField` | 0/1 | live | 0 |
| `r_rotoscope` | 0/1 | live | 0 |
| `r_rotoscopeBlur` | float | live | 5.0 |
| `r_cameraPostFX` | 0/1 | live | 0 |
| `r_cameraVignette` | 0/1 | live | 1 |
| `r_cameraFilmGrainScale` | float | live | 3 |

**`r_screenSpaceAmbientOcclusion`** (SSAO) darkens crevices and contact points
from depth. It works on stock content and is visible (modes 1 and 2 differ in
quality/strength). Live-toggleable.

**`r_depthOfField`** blurs by distance from the focal plane. Works, live.

**`r_rotoscope`** is a novelty cel-shading / edge-ink filter (with
`r_rotoscopeBlur` controlling the blur). Works, live — it is a stylistic toggle,
not a realism feature.

**`r_cameraPostFX`** applies film grain plus a subtle vignette. This requires
the grain and vignette textures, which the original ET:Legacy ships but the
retail game does not. ET-RM supplies **project-original neutral textures**
(`rm/gfx/2d/camera/grain.png`, `vignette.png`, generated deterministically and
packaged in `zz_rm_ui.pk3`) so the effect works out of the box. `r_cameraVignette`
(default on) and `r_cameraFilmGrainScale` (default 3) tune the look; the effect
is intentionally subtle and seamless (no visible tiling). Live-toggleable.

## Known-off / limited

- **`r_softShadows 1` (PCSS)** — avoid. The upstream blocker-search for
  percentage-closer soft shadows is unfinished; it produces inverted/garbage
  occlusion (the lit area is wrongly shadowed and walls behind occluders catch
  light they should not). Use `r_softShadows` 2–6 (PCF) instead.
- **`cg_shadows 7` (stencil)** — avoid. The upstream stencil shadow-volume path
  is incomplete; it renders without errors but produces no real occlusion. Use
  `cg_shadows` 2–6 for working shadow mapping.
- **Sun / static world shadows** — not visible on retail maps; the stock BSPs
  contain no directional light entities for them to derive from (see Shadow
  mapping above). Needs authored map content.
- **Normal / specular / parallax / reflection mapping** — correct but visually
  inert on stock diffuse-only textures; needs authored materials to show.

## Off by default

A fresh boot of the modern renderer with all defaults is regression-tested to
match the original-renderer-parity baseline within the measured pixel noise
floor (mean absolute pixel difference ≤ 0.73 across the four canonical test
views). None of the features above is on at default: HDR/bloom/SSAO/DoF/
rotoscope/cameraPostFX/normal/parallax/reflection are 0, `cg_shadows` is 1 (the
original blob shadow), `r_softShadows` is 0. The non-zero defaults
(`r_dynamicLightShadows 1`, `r_cameraVignette 1`, `r_specularScale 0.2`, etc.)
only take effect when their parent feature is enabled.

## Example configs

A tasteful "modern lite" — readable, moderate cost:

```
cl_renderer gl2
seta r_hdrRendering 1
seta r_bloom 1
seta r_screenSpaceAmbientOcclusion 1
vid_restart
```

The full "showcase" — everything that works on stock content, highest cost:

```
cl_renderer gl2
seta r_hdrRendering 1
seta r_bloom 1
seta cg_shadows 5
seta r_softShadows 4
seta r_dynamicLightShadows 1
seta r_screenSpaceAmbientOcclusion 1
vid_restart
```

The showcase config was run for a sustained 5-minute session on oasis with no
crash; it costs roughly 5x the frame time of the all-default boot on a low-end
integrated GPU but remains smoothly playable. To return to the original look,
set all of these back to their defaults (`vid_restart` for the latched ones) or
switch back to the original renderer with `cl_renderer gl1; vid_restart`.
