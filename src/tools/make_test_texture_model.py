"""텍스처 압축 시험 모델 생성기 (2026-11-21, Docs/Texture_Compression.md D13).

샘플 모델에는 이미지 텍스처가 없어서, 압축 품질 · 메모리를 재려고 절차 텍스처를 가진 판 모델 하나를 glb 로 씁니다.
  albedo   512² sRGB: 색 기울기 · 바둑판 · 동심원 · 가는 줄무늬(블록 압축이 힘들어하는 경계 · 고주파)
  normal   512²     : 높이장(둥근 돌기 · 물결 · 홈)에서 유한 차분으로 만든 접공간 노멀(RGB)
  metalRough 256²   : G = 거칠기 줄무늬, B = 금속 바둑판, R = 차폐(occlusionTexture 도 같은 이미지)
  emissive 256² sRGB: 어두운 바탕에 빛나는 선
판은 1 m x 1 m(XY 평면, +Z 를 봄)이고 4x4 칸으로 나눠 탄젠트를 함께 넣습니다. 최종 아트가 아니라 측정용 자리 표시입니다.

사용: python -I src/tools/make_test_texture_model.py [출력 glb]
      (기본: 이 스크립트 기준 ../../Projects/Samples/Content/models/TexProbe.glb)
"""
import json
import os
import struct
import sys
import zlib

import numpy as np


def png_bytes(rgba):
    """RGBA8 (h, w, 4) 배열을 PNG 로(필터 없음, zlib 9)."""
    h, w, _ = rgba.shape
    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def to_u8(a):
    return np.clip(np.round(a * 255.0), 0, 255).astype(np.uint8)


def albedo(n=512):
    y, x = np.mgrid[0:n, 0:n] / (n - 1.0)
    r = 0.15 + 0.75 * x
    g = 0.20 + 0.60 * y
    b = 0.55 - 0.35 * x * y
    checker = ((np.floor(x * 8) + np.floor(y * 8)) % 2) * 0.18
    d = np.hypot(x - 0.62, y - 0.38)
    rings = (np.sin(d * 90.0) > 0.6) * 0.35
    stripes = (np.sin(x * 400.0) > 0.0) * (y > 0.8) * 0.4
    rgb = np.stack([r + checker - rings, g + checker * 0.5 + rings * 0.3, b + rings - stripes * 0.5], -1)
    rgb[y > 0.8] += stripes[y > 0.8][:, None] * np.array([0.5, 0.4, 0.0])
    rgb = np.clip(rgb, 0.0, 1.0) ** (1 / 2.2)   # 값을 sRGB 바이트로 저장(보기 좋은 분포)
    a = np.ones((n, n, 1))
    return to_u8(np.concatenate([rgb, a], -1))


def normal(n=512):
    y, x = np.mgrid[0:n, 0:n] / (n - 1.0)
    hgt = np.zeros((n, n))
    for cx, cy, rad in [(0.25, 0.25, 0.12), (0.7, 0.3, 0.08), (0.4, 0.7, 0.15), (0.8, 0.75, 0.06)]:
        d = np.hypot(x - cx, y - cy) / rad
        hgt += np.where(d < 1.0, np.sqrt(np.clip(1.0 - d * d, 0, 1)), 0.0) * rad
    hgt += 0.004 * np.sin(x * 120.0) * np.sin(y * 70.0)
    hgt -= 0.01 * (np.abs(((x * 6.0) % 1.0) - 0.5) < 0.03)
    dx = (np.roll(hgt, -1, 1) - np.roll(hgt, 1, 1)) * (n / 2.0)
    dy = (np.roll(hgt, -1, 0) - np.roll(hgt, 1, 0)) * (n / 2.0)
    v = np.stack([-dx, -dy, np.ones_like(hgt)], -1)
    v /= np.linalg.norm(v, axis=-1, keepdims=True)
    rgb = v * 0.5 + 0.5
    return to_u8(np.concatenate([rgb, np.ones((n, n, 1))], -1))


def metal_rough(n=256):
    y, x = np.mgrid[0:n, 0:n] / (n - 1.0)
    occ = 0.6 + 0.4 * np.clip(np.hypot(x - 0.5, y - 0.5) * 2.0, 0, 1)
    rough = 0.25 + 0.6 * (np.sin(y * 40.0) * 0.5 + 0.5)
    metal = ((np.floor(x * 4) + np.floor(y * 4)) % 2).astype(float)
    return to_u8(np.stack([occ, rough, metal, np.ones_like(x)], -1))


def emissive(n=256):
    y, x = np.mgrid[0:n, 0:n] / (n - 1.0)
    line = np.exp(-((y - 0.5 - 0.2 * np.sin(x * 12.0)) ** 2) / 0.0004)
    rgb = np.stack([line * 1.0, line * 0.55, line * 0.15], -1) ** (1 / 2.2)
    return to_u8(np.concatenate([rgb, np.ones((n, n, 1))], -1))


def plate(div=4):
    pos, nrm, uv, tan, idx = [], [], [], [], []
    for j in range(div + 1):
        for i in range(div + 1):
            u, v = i / div, j / div
            pos.append((u - 0.5, 0.5 - v, 0.0))
            nrm.append((0.0, 0.0, 1.0))
            uv.append((u, v))
            tan.append((1.0, 0.0, 0.0, 1.0))
    for j in range(div):
        for i in range(div):
            a = j * (div + 1) + i
            b, c, d = a + 1, a + div + 1, a + div + 2
            idx += [a, c, b, b, c, d]
    return (np.array(pos, np.float32), np.array(nrm, np.float32), np.array(uv, np.float32),
            np.array(tan, np.float32), np.array(idx, np.uint16))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "Projects", "Samples", "Content", "models", "TexProbe.glb")
    pos, nrm, uv, tan, idx = plate()
    images = [png_bytes(albedo()), png_bytes(normal()), png_bytes(metal_rough()), png_bytes(emissive())]

    blob = bytearray()
    views = []

    def add_view(data, target=None):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)})
        if target:
            views[-1]["target"] = target
        blob.extend(data)
        return len(views) - 1

    accessors = []

    def add_acc(arr, typ, comp, target, minmax=False):
        v = add_view(arr.tobytes(), target)
        a = {"bufferView": v, "componentType": comp, "count": int(arr.shape[0]), "type": typ}
        if minmax:
            a["min"] = arr.min(0).tolist()
            a["max"] = arr.max(0).tolist()
        accessors.append(a)
        return len(accessors) - 1

    p_acc = add_acc(pos, "VEC3", 5126, 34962, True)
    n_acc = add_acc(nrm, "VEC3", 5126, 34962)
    t_acc = add_acc(uv, "VEC2", 5126, 34962)
    g_acc = add_acc(tan, "VEC4", 5126, 34962)
    i_acc = add_acc(idx, "SCALAR", 5123, 34963)
    img_views = [add_view(b) for b in images]

    gltf = {
        "asset": {"version": "2.0", "generator": "Haruka make_test_texture_model.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "TexProbe"}],
        "meshes": [{"name": "TexProbe", "primitives": [{
            "attributes": {"POSITION": p_acc, "NORMAL": n_acc, "TEXCOORD_0": t_acc, "TANGENT": g_acc},
            "indices": i_acc, "material": 0}]}],
        "materials": [{
            "name": "TexProbe",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 2}},
            "normalTexture": {"index": 1},
            "occlusionTexture": {"index": 2},
            "emissiveTexture": {"index": 3},
            "emissiveFactor": [1.0, 1.0, 1.0]}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "textures": [{"sampler": 0, "source": i} for i in range(4)],
        "images": [{"bufferView": v, "mimeType": "image/png"} for v in img_views],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(js) % 4:
        js += b" "
    while len(blob) % 4:
        blob.append(0)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(out, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + bytes(blob))
    print(f"wrote {out} ({total} bytes)")


if __name__ == "__main__":
    main()
