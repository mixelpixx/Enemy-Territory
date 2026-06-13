#!/usr/bin/env python3
# R2-5 Task 5b: generate PROJECT-ORIGINAL neutral camera-effect textures for the
# renderer2 (gl2) cameraEffects post-FX (r_cameraPostFX). These replace the
# ET:Legacy assets that retail ET does not ship, so the vignette/grain shader has
# something sane to sample instead of NULL (which crushed the frame to black).
#
# This art is generated procedurally here and committed under the project's GPLv3;
# nothing is copied from ET:Legacy. The RNG is seeded so the committed PNGs are
# byte-reproducible from this script.
#
# Outputs (written next to rm/gfx/2d/camera/ by default):
#   grain.png    512x512 8-bit RGB. Seamlessly-tileable grayscale film-grain noise.
#                Per-pixel independent Gaussian noise centered at 0.5 (std ~0.25,
#                clamped 0..1) => tiles seamlessly under WT_REPEAT because pixels
#                are independent. R=G=B so the shader's per-channel blue tint
#                (0.035,0.065,0.09) does the coloring. Subtle, not snow.
#   vignette.png 256x256 8-bit RGB. Subtle radial vignette: white (255) at center,
#                smoothly falling to ~0.82*255 (~209) at the corners via
#                1 - k*r^2 (r normalized so corner r=1). Barely-there edge darkening.
#
# Contract (verified against src/renderer2/glsl/cameraEffects_fp.glsl and
# src/renderer2/etl/common/tr_image.c): grain WT_REPEAT, vignette WT_EDGE_CLAMP;
# loader accepts 8-bit non-interlaced RGB/RGBA. PIL writes exactly that by default.
#
# Dependency: numpy (required) + PIL/Pillow (required, used as the PNG writer).
# Both verified present in the project toolchain.
#
# Usage: python make_camera_textures.py [out_dir]
#   default out_dir = <repo>/rm/gfx/2d/camera

import os
import sys
import numpy as np
from PIL import Image

SEED = 20260612  # deterministic -> reproducible committed PNGs


def make_grain(size=512):
    rng = np.random.default_rng(SEED)
    # per-pixel Gaussian noise centered at 0.5, std 0.25, clamped to [0,1].
    noise = rng.normal(0.5, 0.25, size=(size, size))
    noise = np.clip(noise, 0.0, 1.0)
    g = (noise * 255.0 + 0.5).astype(np.uint8)
    rgb = np.stack([g, g, g], axis=-1)  # grayscale R=G=B
    return Image.fromarray(rgb)  # uint8 HxWx3 -> RGB


def make_vignette(size=256, corner=0.82):
    # normalized coords centered at 0; corner of the square is r=1.
    ax = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    xx, yy = np.meshgrid(ax, ax)
    r2 = (xx * xx + yy * yy) / 2.0  # /2 so the corner (1,1) gives r2=1
    k = 1.0 - corner               # corners land at exactly `corner`
    v = 1.0 - k * r2
    v = np.clip(v, 0.0, 1.0)
    g = (v * 255.0 + 0.5).astype(np.uint8)
    rgb = np.stack([g, g, g], axis=-1)
    return Image.fromarray(rgb)  # uint8 HxWx3 -> RGB


def main():
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, "rm", "gfx", "2d", "camera")
    os.makedirs(out_dir, exist_ok=True)

    grain = make_grain()
    vig = make_vignette()
    gp = os.path.join(out_dir, "grain.png")
    vp = os.path.join(out_dir, "vignette.png")
    # optimize=False, default zlib; non-interlaced 8-bit RGB.
    grain.save(gp, format="PNG")
    vig.save(vp, format="PNG")

    ga = np.asarray(grain)
    va = np.asarray(vig)
    print(f"wrote {gp}  {ga.shape}  mean={ga.mean():.2f} min={ga.min()} max={ga.max()}")
    print(f"wrote {vp}  {va.shape}  center={va[va.shape[0]//2, va.shape[1]//2, 0]} "
          f"corner={va[0,0,0]} mean={va.mean():.2f}")


if __name__ == "__main__":
    main()
