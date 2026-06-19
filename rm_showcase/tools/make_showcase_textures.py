#!/usr/bin/env python3
"""R2-6 showcase material generator (project-original, reproducible, deterministic).

Generates three bold material sets, each with diffuse + tangent-space normal
(height in alpha, for parallax) + specular, into <out>/textures/rm_showcase/:
    rivet_{d,n,s}.tga   riveted steel panel  (high spec, raised rivets + seams)
    block_{d,n,s}.tga   carved sandstone     (deep mortar grooves -> strong parallax)
    stud_{d,n,s}.tga    studded relief       (pyramidal studs, medium spec)

All patterns are generated from scratch (no retail art) so the showcase is fully
self-contained and unambiguously project-original. Height is baked into the normal
map's alpha channel because renderer2's reliefMapping samples u_NormalMap.a.
"""
import os
import numpy as np

SIZE = 512


def write_tga(path, rgba):
    h, w, _ = rgba.shape
    header = bytearray(18)
    header[2] = 2
    header[12] = w & 0xFF; header[13] = (w >> 8) & 0xFF
    header[14] = h & 0xFF; header[15] = (h >> 8) & 0xFF
    header[16] = 32; header[17] = 8
    bgra = rgba[::-1, :, [2, 1, 0, 3]]
    with open(path, "wb") as f:
        f.write(bytes(header)); f.write(np.ascontiguousarray(bgra).tobytes())


def normal_from_height(hgt, strength):
    gy, gx = np.gradient(hgt.astype(np.float64))
    nx, ny, nz = -gx * strength, -gy * strength, np.ones_like(hgt, dtype=np.float64)
    n = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgba = np.empty((hgt.shape[0], hgt.shape[1], 4), np.uint8)
    rgba[..., 0] = np.clip((nx / n * 0.5 + 0.5) * 255, 0, 255)
    rgba[..., 1] = np.clip((ny / n * 0.5 + 0.5) * 255, 0, 255)
    rgba[..., 2] = np.clip((nz / n * 0.5 + 0.5) * 255, 0, 255)
    rgba[..., 3] = np.clip(hgt * 255, 0, 255)  # parallax height
    return rgba


def gray(v):
    v = np.clip(v * 255, 0, 255).astype(np.uint8)
    return np.stack([v, v, v, np.full_like(v, 255)], -1)


def rgb(r, g, b):
    out = np.empty(r.shape + (4,), np.uint8)
    out[..., 0] = np.clip(r * 255, 0, 255); out[..., 1] = np.clip(g * 255, 0, 255)
    out[..., 2] = np.clip(b * 255, 0, 255); out[..., 3] = 255
    return out


def _rng(seed):
    return np.random.default_rng(seed)


def make_rivet(size=SIZE):
    """Riveted steel: 2x2 sub-panels split by recessed seams, raised rivets along them."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    h = np.full((size, size), 0.55)
    # recessed seams forming a 2x2 grid (cross through the middle + border)
    seam = (np.abs(((x % size) - size / 2)) < 7) | (np.abs(((y % size) - size / 2)) < 7)
    border = (x < 9) | (x > size - 10) | (y < 9) | (y > size - 10)
    h[seam | border] = 0.22
    # raised rivets along the seams/border at regular intervals
    rivets = np.zeros((size, size))
    step = size // 8
    for cx in range(step // 2, size, step):
        for cy in range(step // 2, size, step):
            on_line = (abs(cx - size / 2) < 10) or (abs(cy - size / 2) < 10) \
                or cx < 14 or cx > size - 14 or cy < 14 or cy > size - 14
            if on_line:
                r = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
                rivets = np.maximum(rivets, np.clip(1.0 - r / 9.0, 0, 1) ** 0.6 * 0.6)
    h = np.clip(h + rivets, 0, 1)
    nrm = _rng(1).normal(0, 0.015, (size, size))
    base = 0.42 + 0.10 * (h - 0.5) + nrm            # steel, a touch darker in seams
    diff = rgb(base * 0.95, base * 0.98, base * 1.05)  # cool steel tint
    spec = gray(np.clip(0.55 + 0.45 * rivets / 0.6, 0, 1))  # bright metal, rivets brightest
    return diff, normal_from_height(h, 7.0), spec


def make_block(size=SIZE):
    """Carved sandstone: 3x2 blocks, deep mortar grooves + beveled edges (parallax star)."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    cols, rows = 3, 2
    cw, ch = size / cols, size / rows
    fx = (x % cw) / cw
    fy = (y % ch) / ch
    # bevel: distance to nearest block edge -> raised center, recessed mortar
    edge = np.minimum(np.minimum(fx, 1 - fx), np.minimum(fy, 1 - fy))
    bevel = np.clip(edge / 0.12, 0, 1)
    mortar = edge < 0.045
    h = 0.30 + 0.55 * bevel
    h[mortar] = 0.08
    h += _rng(2).normal(0, 0.03, (size, size))      # surface grain
    h = np.clip(h, 0, 1)
    # per-block tint variation
    bid = (np.floor(x / cw) + cols * np.floor(y / ch)).astype(int)
    tint = (_rng(3).uniform(0.85, 1.08, cols * rows))[bid % (cols * rows)]
    grain = _rng(4).normal(0, 0.04, (size, size))
    base = (0.62 + 0.18 * bevel + grain) * tint
    base[mortar] *= 0.55
    diff = rgb(base * 1.05, base * 0.92, base * 0.70)   # warm sandstone
    spec = gray(np.clip(0.12 + 0.10 * (1 - bevel), 0, 1))  # matte
    return diff, normal_from_height(h, 9.0), spec


def make_stud(size=SIZE):
    """Studded relief: grid of pyramidal studs on a dark plate (bold normals)."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    step = size // 6
    fx = np.abs((x % step) - step / 2) / (step / 2)
    fy = np.abs((y % step) - step / 2) / (step / 2)
    pyr = np.clip(1.0 - np.maximum(fx, fy) / 0.78, 0, 1)   # square pyramids
    h = np.clip(0.18 + 0.82 * pyr, 0, 1)
    base = 0.20 + 0.30 * pyr + _rng(5).normal(0, 0.02, (size, size))
    diff = rgb(base * 0.8, base * 0.85, base * 0.9)        # dark painted metal
    spec = gray(np.clip(0.30 + 0.55 * pyr, 0, 1))
    return diff, normal_from_height(h, 8.0), spec


MATERIALS = {"rivet": make_rivet, "block": make_block, "stud": make_stud}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)   # rm_showcase/ (tools/ is one level down)
    out = os.path.join(root, "textures", "rm_showcase")
    os.makedirs(out, exist_ok=True)
    for name, fn in MATERIALS.items():
        d, n, s = fn()
        write_tga(os.path.join(out, name + "_d.tga"), d)
        write_tga(os.path.join(out, name + "_n.tga"), n)
        write_tga(os.path.join(out, name + "_s.tga"), s)
        print("wrote", name, "(d/n/s)")
    print("-> ", out)


if __name__ == "__main__":
    main()
