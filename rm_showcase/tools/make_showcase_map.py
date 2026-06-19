#!/usr/bin/env python3
# R2-6 showcase map generator (iterated in build/, promoted to rm_showcase/ when good).
# An open, sky-lit courtyard that demonstrates the renderer2 mapper hooks:
#   - sun/world shadows  : q3map_sun (sky shader) + a `parallel 1` light -> RL_DIRECTIONAL
#   - deluxe lightmaps    : compiled with q3map2 -deluxe
#   - materials           : large display panels (textures wired in phase 2)
# Box brushes are wound from outward normals so q3map2 gets valid geometry.
import os

CAULK = "common/caulk"
SKY   = "skies_sd/sd_siwasky"        # stock sky: carries its own q3map_sun (bakes + sets tr.sunDirection)
FLOOR = "desert_sd/pavement_quad_sandy"
WALL  = "egypt_walls_sd/bigblock02"
TRIM  = "egypt_walls_sd/bankwall01"
PANEL = "egypt_walls_sd/concrete_m03"  # phase-1 stock; phase-2 swaps to an rm_showcase material

_AXES = {
    (+1, 0, 0): ((0, 1, 0), (0, 0, 1)), (-1, 0, 0): ((0, 0, 1), (0, 1, 0)),
    (0, +1, 0): ((0, 0, 1), (1, 0, 0)), (0, -1, 0): ((1, 0, 0), (0, 0, 1)),
    (0, 0, +1): ((1, 0, 0), (0, 1, 0)), (0, 0, -1): ((0, 1, 0), (1, 0, 0)),
}
_NORMALS = list(_AXES.keys())


def _center(mins, maxs, n):
    c = [(mins[i] + maxs[i]) / 2 for i in range(3)]
    for i in range(3):
        if n[i] > 0:   c[i] = maxs[i]
        elif n[i] < 0: c[i] = mins[i]
    return c


def box(mins, maxs, tex, faces=None, scale=0.5):
    ext = [maxs[i] - mins[i] for i in range(3)]
    out = ["\t{"]
    for n in _NORMALS:
        u, v = _AXES[n]
        c = _center(mins, maxs, n)
        eu = max(ext[u.index(1)], 64)
        ev = max(ext[v.index(1)], 64)
        pp1 = c
        pp0 = [c[i] + u[i] * eu for i in range(3)]
        pp2 = [c[i] + v[i] * ev for i in range(3)]
        t = (faces or {}).get(n, tex)
        pts = " ".join("( %g %g %g )" % tuple(p) for p in (pp0, pp1, pp2))
        out.append("\t\t%s %s 0 0 0 %g %g 0 0 0" % (pts, t, scale, scale))
    out.append("\t}")
    return "\n".join(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)   # rm_showcase/ (tools/ is one level down)
    out = os.path.join(root, "maps", "rm_showcase.map")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    R = 768          # courtyard inner half-extent (1536 wide)
    H = 512          # wall height
    T = 16
    brushes = []

    # floor + sky cap + 4 perimeter walls (sealed hull, open to sky above)
    brushes.append(box((-R, -R, -T), (R, R, 0), CAULK, {(0, 0, 1): FLOOR}))
    brushes.append(box((-R, -R, H), (R, R, H + T), CAULK, {(0, 0, -1): SKY}))      # sky ceiling
    brushes.append(box((-R - T, -R - T, -T), (-R, R + T, H + T), CAULK, {(+1, 0, 0): WALL}))  # W
    brushes.append(box((R, -R - T, -T), (R + T, R + T, H + T), CAULK, {(-1, 0, 0): WALL}))    # E
    brushes.append(box((-R, -R - T, -T), (R, -R, H + T), CAULK, {(0, +1, 0): WALL}))          # S
    brushes.append(box((-R, R, -T), (R, R + T, H + T), CAULK, {(0, -1, 0): WALL}))            # N

    # shadow-casting pillars (tall, scattered) — the sun throws long shadows off these
    pillars = ((-360, 260), (-80, -300), (300, 180), (420, -380))
    for (px, py) in pillars:
        brushes.append(box((px - 48, py - 48, 0), (px + 48, py + 48, 360), WALL,
                           {(0, 0, 1): TRIM}))

    # a low stepped platform (more shadow interest + a place to stand)
    for i, z in enumerate((0, 64, 128)):
        s = 220 - i * 64
        brushes.append(box((-s - 120, -s - 120, z), (-120 + s, -120 + s, z + 64),
                           TRIM, {(0, 0, 1): FLOOR}))

    # covered alcove in the +x/+y corner (3 walls + roof, opening toward center):
    # its interior is lit by a point light, so its deluxe direction differs from the
    # sun-lit courtyard — good deluxe contrast.
    ax0, ay0, ax1, ay1, ah = 380, 380, R, R, 256
    brushes.append(box((ax0, ay0, ah), (ax1 + T, ay1 + T, ah + T), CAULK, {(0, 0, -1): TRIM}))  # roof
    brushes.append(box((ax0 - T, ay0, ah - 256 if False else 0), (ax0, ay1 + T, ah), CAULK, {(+1, 0, 0): TRIM}))  # inner W wall of alcove
    brushes.append(box((ax0 - T, ay0 - T, 0), (ax1 + T, ay0, ah), CAULK, {(0, +1, 0): TRIM}))   # inner S wall of alcove

    # three large display panels in a row facing south (toward the spawn/camera),
    # each showing one authored material on its -Y face.
    panels = ((-430, 120, "rm_showcase/rivet"),
              (0,    120, "rm_showcase/block"),
              (430,  120, "rm_showcase/stud"))
    for (px, py, mat) in panels:
        brushes.append(box((px - 150, py - 10, 24), (px + 150, py + 10, 312), CAULK,
                           {(0, -1, 0): mat, (0, +1, 0): mat}, scale=0.25))

    ents = []
    ents.append(
        "{\n"
        '"classname" "worldspawn"\n'
        '"_color" "0.5 0.55 0.7"\n'
        '"ambient" "25"\n'
        '"gridsize" "128 128 256"\n'
        '"message" "ET-RM Renderer Showcase"\n'
        + "\n".join(brushes) + "\n}"
    )
    # realtime directional sun (RL_DIRECTIONAL); direction comes from q3map_sun in the
    # sky shader (tr.sunDirection). q3map2 also bakes it; -keeplights preserves it.
    ents.append(
        "{\n"
        '"classname" "light"\n'
        '"origin" "-300 -200 470"\n'
        '"light" "260"\n'
        '"_color" "1 0.95 0.85"\n'
        '"parallel" "1"\n'
        "}"
    )
    # alcove point light
    ents.append(
        "{\n"
        '"classname" "light"\n'
        '"origin" "%d %d 150"\n'
        '"light" "300"\n'
        '"_color" "1 0.85 0.6"\n'
        "}" % ((ax0 + ax1) // 2, (ay0 + ay1) // 2)
    )
    # spawns (ET conventions) + intermission camera
    spawns = (("team_CTF_redspawn", -560, -560, 45),
              ("team_CTF_bluespawn", 560, -560, 135),
              ("team_CTF_redspawn", -220, -600, 90),
              ("team_CTF_bluespawn", 220, -600, 90))
    for (cls, sx, sy, ang) in spawns:
        ents.append('{\n"classname" "%s"\n"origin" "%d %d 40"\n"angle" "%d"\n}' % (cls, sx, sy, ang))
    ents.append('{\n"classname" "info_player_intermission"\n"origin" "0 -600 400"\n"angle" "90"\n}')

    with open(out, "w") as f:
        f.write("\n".join(ents) + "\n")
    print("wrote", out, "(%d entities, %d brushes)" % (len(ents), len(brushes)))


if __name__ == "__main__":
    main()
