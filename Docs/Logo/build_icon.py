# -*- coding: utf-8 -*-
"""Build the Windows application icon from the master logo image.

    python Docs/Logo/build_icon.py Docs/Logo/Logo_Final.png res/Haruka.ico

Needs Pillow.  The master is the source of truth for the artwork: Logo.pptx
still holds the vector shapes, but the white interior disc that makes the logo
readable on a dark taskbar only exists in the exported PNG, so regenerating from
the .pptx would silently bring back an older look.  Re-export the master from
PowerPoint at the largest size you can, then run this script.

The master may be off by a pixel from square (PowerPoint crops that way); it is
padded, never stretched, so the circle stays round.  Resampling happens on
premultiplied alpha, otherwise transparent black bleeds into the outer edge.

Output entries are 16/24/32/48/64/128/256 px.  Small sizes are stored as DIBs
for the widest compatibility and the largest two as PNG so the file stays small.
The .ico format keeps each side in a single byte, so 256 is the format ceiling:
a 512 px logo can only live as a PNG master next to this script.
"""
import io, struct, sys
from PIL import Image

BMP_SIZES = [16, 24, 32, 48, 64]
PNG_SIZES = [128, 256]

def load_master(path):
    im = Image.open(path).convert("RGBA")
    side = max(im.size)
    if im.size != (side, side):
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        square.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
        im = square
    return im

def down(master, n):
    return master.convert("RGBa").resize((n, n), Image.LANCZOS).convert("RGBA")

def split_ico(blob):
    """Pull the per-image payloads back out of an .ico Pillow just wrote."""
    count = struct.unpack_from("<H", blob, 4)[0]
    out = []
    for i in range(count):
        w, h, _, _, _, _, size, off = struct.unpack_from("<BBBBHHII", blob, 6 + i * 16)
        out.append((w or 256, h or 256, blob[off:off + size]))
    return out

def write_ico(path, frames):
    header = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    directory, payloads = b"", b""
    for w, h, data in frames:
        directory += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32,
                                 len(data), offset)
        offset += len(data)
        payloads += data
    with open(path, "wb") as f:
        f.write(header + directory + payloads)

def main(master_path, out_ico):
    master = load_master(master_path)
    print(f"master {master_path} -> {master.size[0]}x{master.size[1]}")

    frames = []
    buf = io.BytesIO()
    down(master, BMP_SIZES[-1]).save(
        buf, format="ICO", bitmap_format="bmp",
        sizes=[(s, s) for s in BMP_SIZES],
        append_images=[down(master, s) for s in BMP_SIZES[:-1]])
    frames.extend(split_ico(buf.getvalue()))

    for s in PNG_SIZES:
        b = io.BytesIO()
        down(master, s).save(b, format="PNG", optimize=True)
        frames.append((s, s, b.getvalue()))

    frames.sort(key=lambda f: f[0])
    write_ico(out_ico, frames)

    print("wrote", out_ico)
    for w, h, data in frames:
        kind = "PNG" if data[:4] == b"\x89PNG" else "BMP"
        print(f"  {w:3d}x{h:<3d} {kind} {len(data):>7,d} bytes")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
