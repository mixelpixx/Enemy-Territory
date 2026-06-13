# R2-5 evidence: TGA pixel-diff metrics (uncompressed or RLE 24/32-bit TGA)
import sys, numpy as np

def read_tga(path):
    with open(path, 'rb') as f:
        d = f.read()
    idlen, cmap, imgtype = d[0], d[1], d[2]
    w = int.from_bytes(d[12:14], 'little'); h = int.from_bytes(d[14:16], 'little')
    bpp = d[16]; ch = bpp // 8
    off = 18 + idlen
    if imgtype == 2:
        arr = np.frombuffer(d[off:off + w*h*ch], dtype=np.uint8).reshape(h, w, ch)
    elif imgtype == 10:
        out = np.empty((w*h, ch), dtype=np.uint8); i = off; p = 0
        while p < w*h:
            hdr = d[i]; i += 1; n = (hdr & 0x7F) + 1
            if hdr & 0x80:
                out[p:p+n] = np.frombuffer(d[i:i+ch], dtype=np.uint8); i += ch
            else:
                out[p:p+n] = np.frombuffer(d[i:i+n*ch], dtype=np.uint8).reshape(n, ch); i += n*ch
            p += n
        arr = out.reshape(h, w, ch)
    else:
        raise ValueError(f"unsupported TGA type {imgtype} in {path}")
    return arr[:, :, :3].astype(np.int16)  # BGR, drop alpha

if __name__ == "__main__":
    a = read_tga(sys.argv[1]); b = read_tga(sys.argv[2])
    if a.shape != b.shape:
        print(f"SHAPE MISMATCH {a.shape} vs {b.shape}"); sys.exit(1)
    diff = np.abs(a - b)
    print(f"{sys.argv[1]} vs {sys.argv[2]}: mean_abs={diff.mean():.4f} max={diff.max()} "
          f"pct_changed={(diff.sum(axis=2) > 0).mean()*100:.2f}% "
          f"mean_brightness_a={a.mean():.2f} mean_brightness_b={b.mean():.2f}")
