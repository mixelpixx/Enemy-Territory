import sys, numpy as np
sys.path.insert(0, r"C:\repo\et-rm\docs\superpowers\evidence-scripts")
from tga_diff import read_tga
from PIL import Image
a = read_tga(sys.argv[1])[::-1, :, ::-1].astype(np.uint8)  # flip vertical (TGA bottom-up), BGR->RGB
img = Image.fromarray(a)
if len(sys.argv) > 3:
    img = img.resize((int(sys.argv[3]), int(sys.argv[3]) * a.shape[0] // a.shape[1]))
img.save(sys.argv[2])
print("wrote", sys.argv[2], a.shape)
