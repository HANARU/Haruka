"""시험용 우주복 장갑 모델 생성기 (2026-11-07, Docs/Hand_Grasp_Design.md 4-1).

OpenXR 26관절 뼈대(src/xr/HandSkeleton.hpp 와 같은 이름 · 부모)와 스킨 무게를 가진 단순한 장갑을 왼손 · 오른손 glb 로 씁니다.
최종 아트가 아니라 스키닝 · 손 자세를 확인하는 자리 표시용입니다. 단위는 m, 손목이 원점이고 손가락이 -Z, 손등이 +Y 입니다.
각 관절 노드의 로컬 축은 OpenXR 규약(+Z = 손가락 끝의 반대쪽, +Y = 손등 바깥, X = Y x Z)을 따릅니다.
왼손은 오른손을 X 로 거울 대칭한 위치에서 축을 다시 계산합니다(음수 배율을 쓰지 않음).

사용: Tools\run_python.bat src/tools/make_test_gloves.py [출력 폴더]   (기본: 이 스크립트 기준 ../../assets/models/hands)
"""
import json
import math
import os
import struct
import sys

import numpy as np

JOINTS = [
    "Palm", "Wrist",
    "ThumbMetacarpal", "ThumbProximal", "ThumbDistal", "ThumbTip",
    "IndexMetacarpal", "IndexProximal", "IndexIntermediate", "IndexDistal", "IndexTip",
    "MiddleMetacarpal", "MiddleProximal", "MiddleIntermediate", "MiddleDistal", "MiddleTip",
    "RingMetacarpal", "RingProximal", "RingIntermediate", "RingDistal", "RingTip",
    "LittleMetacarpal", "LittleProximal", "LittleIntermediate", "LittleDistal", "LittleTip",
]
PARENT = [1, -1, 1, 2, 3, 4, 1, 6, 7, 8, 9, 1, 11, 12, 13, 14, 1, 16, 17, 18, 19, 1, 21, 22, 23, 24]
J = {n: i for i, n in enumerate(JOINTS)}

# 오른손 기준 치수(m). 손가락: 이름, metacarpal 뿌리, 손가락 뿌리 마디(proximal), 방향, 마디 길이 3개, 반지름.
FINGERS = [
    ("Index",  (-0.018,  0.000, -0.014), (-0.023, 0.002, -0.090), (-0.07, 0.0, -1.0), (0.042, 0.025, 0.021), 0.0105),
    ("Middle", ( 0.000,  0.000, -0.014), ( 0.000, 0.003, -0.094), ( 0.00, 0.0, -1.0), (0.046, 0.029, 0.022), 0.0110),
    ("Ring",   ( 0.016,  0.000, -0.014), ( 0.0205, 0.002, -0.090), ( 0.06, 0.0, -1.0), (0.043, 0.027, 0.021), 0.0105),
    ("Little", ( 0.030, -0.002, -0.016), ( 0.038, -0.002, -0.081), ( 0.13, 0.0, -1.0), (0.034, 0.020, 0.019), 0.0092),
]
THUMB = [(-0.018, -0.008, -0.016), (-0.040, -0.013, -0.044), (-0.054, -0.015, -0.072), (-0.062, -0.016, -0.096)]
THUMB_RADIUS = (0.0150, 0.0118)        # metacarpal 쪽 · 끝 쪽
THUMB_NAIL_HINT = (-0.7, 0.7, 0.0)     # 오른손 엄지 손톱이 보는 쪽(대략)
PALM_BOX = ((-0.034, 0.046), (-0.013, 0.015), (-0.088, 0.004))
SEGMENTS = 12                          # 손가락 둘레 정점 수
RING_STEP = 0.005                      # 손가락 길이 방향 고리 간격
BLEND = 0.005                          # 관절 앞뒤로 무게를 섞는 반폭


def norm(v):
    v = np.asarray(v, dtype=np.float64)
    n = np.linalg.norm(v)
    return v / n if n > 1e-12 else v


def joint_positions(mirror):
    p = [None] * 26
    p[J["Wrist"]] = np.zeros(3)
    for name, mc, knuckle, d, lens, _r in FINGERS:
        d = norm(d)
        p[J[name + "Metacarpal"]] = np.array(mc, float)
        p[J[name + "Proximal"]] = np.array(knuckle, float)
        p[J[name + "Intermediate"]] = p[J[name + "Proximal"]] + d * lens[0]
        p[J[name + "Distal"]] = p[J[name + "Intermediate"]] + d * lens[1]
        p[J[name + "Tip"]] = p[J[name + "Distal"]] + d * lens[2]
    for k, n in enumerate(["ThumbMetacarpal", "ThumbProximal", "ThumbDistal", "ThumbTip"]):
        p[J[n]] = np.array(THUMB[k], float)
    p[J["Palm"]] = 0.5 * (p[J["MiddleMetacarpal"]] + p[J["MiddleProximal"]])
    if mirror:
        p = [q * np.array([-1.0, 1.0, 1.0]) for q in p]
    return p


def bone_child(j):
    """관절 j 의 뼈가 향하는 다음 관절(같은 손가락). 손가락 끝은 None."""
    name = JOINTS[j]
    if name == "Wrist":
        return J["MiddleMetacarpal"]
    if name == "Palm":
        return J["MiddleProximal"]
    if name.endswith("Tip"):
        return None
    for c in range(26):
        if PARENT[c] == j:
            return c
    return None


def joint_frames(p, mirror):
    """관절마다 OpenXR 규약 축(열 X, Y, Z)의 회전 행렬."""
    sx = -1.0 if mirror else 1.0
    R = [None] * 26
    for j in range(26):
        name = JOINTS[j]
        c = bone_child(j)
        if c is None:                       # 손가락 끝: 바로 앞 마디와 같은 축
            c_from, c_to = PARENT[j], j
        elif name == "Palm":
            c_from, c_to = J["MiddleMetacarpal"], J["MiddleProximal"]
        else:
            c_from, c_to = j, c
        z = norm(p[c_from] - p[c_to])       # +Z = 손가락 끝의 반대쪽
        hint = np.array([sx * THUMB_NAIL_HINT[0], THUMB_NAIL_HINT[1], THUMB_NAIL_HINT[2]]) if name.startswith("Thumb") \
            else np.array([0.0, 1.0, 0.0])
        y = norm(hint - np.dot(hint, z) * z)
        x = np.cross(y, z)
        R[j] = np.column_stack([x, y, z])
    return R


def quat_from_matrix(m):
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        w, x, y, z = 0.25 * s, (m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w, x, y, z = (m[2, 1] - m[1, 2]) / s, 0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w, x, y, z = (m[0, 2] - m[2, 0]) / s, (m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w, x, y, z = (m[1, 0] - m[0, 1]) / s, (m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s
    q = np.array([x, y, z, w])
    return q / np.linalg.norm(q)


class MeshBuilder:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.jnt, self.wgt, self.tri = [], [], [], [], [], []

    def vert(self, p, n, uv, weights):
        """weights: {관절 번호: 무게}."""
        items = sorted(((w, j) for j, w in weights.items() if w > 1e-6), reverse=True)[:4]
        tot = sum(w for w, _ in items) or 1.0
        js = [j for _, j in items] + [0] * (4 - len(items))
        ws = [w / tot for w, _ in items] + [0.0] * (4 - len(items))
        self.pos.append(np.asarray(p, float))
        self.nrm.append(norm(n))
        self.uv.append(uv)
        self.jnt.append(js)
        self.wgt.append(ws)
        return len(self.pos) - 1

    def quad(self, a, b, c, d):
        self.tri += [(a, b, c), (a, c, d)]

    def fix_winding(self):
        """면 노멀이 정점 노멀과 같은 쪽(바깥)을 보게 감김을 고칩니다."""
        P, N = np.array(self.pos), np.array(self.nrm)
        out = []
        for a, b, c in self.tri:
            fn = np.cross(P[b] - P[a], P[c] - P[a])
            if np.linalg.norm(fn) < 1e-14:
                continue
            out.append((a, b, c) if np.dot(fn, N[a] + N[b] + N[c]) >= 0 else (a, c, b))
        self.tri = out


def chain_weights(s, bounds, bones):
    """사슬 위 거리 s 에서 bones(길이 len(bounds)+1)의 무게. 경계 앞뒤 BLEND 안에서 선형으로 섞습니다."""
    w = {}
    carry = 1.0
    for k, b in enumerate(bounds):
        t = min(max((s - (b - BLEND)) / (2 * BLEND), 0.0), 1.0)
        w[bones[k]] = w.get(bones[k], 0.0) + carry * (1.0 - t)
        carry *= t
    w[bones[-1]] = w.get(bones[-1], 0.0) + carry
    return w


def polyline_at(points, s):
    """점 사슬 위 호 길이 s 의 위치와 방향(s < 0 이면 첫 마디를 뒤로 연장)."""
    segs = [points[i + 1] - points[i] for i in range(len(points) - 1)]
    lens = [np.linalg.norm(v) for v in segs]
    if s <= 0:
        d = norm(segs[0])
        return points[0] + d * s, d
    acc = 0.0
    for i, (v, L) in enumerate(zip(segs, lens)):
        if s <= acc + L or i == len(segs) - 1:
            d = norm(v)
            # 관절 근처에서는 앞뒤 마디 방향을 섞어 고리가 꺾이지 않게 합니다.
            local = s - acc
            if i > 0 and local < BLEND:
                t = 0.5 + 0.5 * local / BLEND
                d = norm((1 - t) * norm(segs[i - 1]) + t * d)
            if i < len(segs) - 1 and L - local < BLEND:
                t = 0.5 + 0.5 * (L - local) / BLEND
                d = norm(t * d + (1 - t) * norm(segs[i + 1]))
            return points[i] + norm(v) * local, d
        acc += L
    return points[-1], norm(segs[-1])


def add_tube(mb, points, bones, r0, r1, start, up_hint):
    """points(관절 위치 사슬)를 따라 끝을 둥글게 막은 관. bones = [시작 전 뼈, 마디마다 뼈...]."""
    lens = [np.linalg.norm(points[i + 1] - points[i]) for i in range(len(points) - 1)]
    total = sum(lens)
    bounds = list(np.cumsum([0.0] + lens[:-1]))   # 0, 첫 마디 끝, 둘째 마디 끝
    r_end = r1
    s_end = total - r_end
    ss = list(np.arange(start, s_end, RING_STEP)) + [s_end]
    rings = []
    for s in ss:
        c, d = polyline_at(points, s)
        r = r0 + (r1 - r0) * max(0.0, s) / total
        up = norm(up_hint - np.dot(up_hint, d) * d)
        side = np.cross(d, up)
        w = chain_weights(s, bounds, bones)
        ring = []
        for k in range(SEGMENTS):
            a = 2 * math.pi * k / SEGMENTS
            off = math.cos(a) * side + math.sin(a) * up
            ring.append(mb.vert(c + r * off, off, (k / SEGMENTS, s / total), w))
        rings.append(ring)
    # 끝 반구(마지막 뼈)
    c_end, d_end = polyline_at(points, s_end)
    up = norm(up_hint - np.dot(up_hint, d_end) * d_end)
    side = np.cross(d_end, up)
    wtip = {bones[-1]: 1.0}
    for step in (1, 2, 3):
        phi = (math.pi / 2) * step / 4
        ring = []
        for k in range(SEGMENTS):
            a = 2 * math.pi * k / SEGMENTS
            off = math.cos(a) * side + math.sin(a) * up
            n = math.cos(phi) * off + math.sin(phi) * d_end
            ring.append(mb.vert(c_end + r_end * n, n, (k / SEGMENTS, 1.0), wtip))
        rings.append(ring)
    apex = mb.vert(c_end + r_end * d_end, d_end, (0.5, 1.0), wtip)
    for a_ring, b_ring in zip(rings[:-1], rings[1:]):
        for k in range(SEGMENTS):
            k2 = (k + 1) % SEGMENTS
            mb.quad(a_ring[k], a_ring[k2], b_ring[k2], b_ring[k])
    last = rings[-1]
    for k in range(SEGMENTS):
        mb.tri.append((last[k], last[(k + 1) % SEGMENTS], apex))


def add_box(mb, lo, hi, weights):
    """면마다 따로 정점을 둔 상자(평평한 음영)."""
    lo, hi = np.asarray(lo, float), np.asarray(hi, float)
    for axis in range(3):
        for sign in (-1, 1):
            n = np.zeros(3)
            n[axis] = sign
            u_ax, v_ax = [a for a in range(3) if a != axis]
            corners = []
            for (cu, cv) in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = np.zeros(3)
                p[axis] = hi[axis] if sign > 0 else lo[axis]
                p[u_ax] = hi[u_ax] if cu else lo[u_ax]
                p[v_ax] = hi[v_ax] if cv else lo[v_ax]
                corners.append(mb.vert(p, n, (cu, cv), weights))
            mb.quad(*corners)


def add_cuff(mb, z0, z1, rx, ry, weights, cap_front, cap_back=False):
    """손목 쪽 타원 원통(+Z 방향). cap_front 면 z0 쪽을, cap_back 이면 z1 쪽(팔 쪽)을 막습니다."""
    rings = []
    for z in np.linspace(z0, z1, 3):
        ring = []
        for k in range(SEGMENTS * 2):
            a = 2 * math.pi * k / (SEGMENTS * 2)
            p = np.array([rx * math.cos(a), ry * math.sin(a), z])
            n = np.array([math.cos(a) / rx, math.sin(a) / ry, 0.0])
            ring.append(mb.vert(p, n, (k / (SEGMENTS * 2), (z - z0) / (z1 - z0)), weights))
        rings.append(ring)
    m = SEGMENTS * 2
    for a_ring, b_ring in zip(rings[:-1], rings[1:]):
        for k in range(m):
            mb.quad(a_ring[k], a_ring[(k + 1) % m], b_ring[(k + 1) % m], b_ring[k])
    for want, ring, z, nz in ((cap_front, rings[0], z0, -1.0), (cap_back, rings[-1], z1, 1.0)):
        if not want:
            continue
        ctr = mb.vert((0, 0, z), (0, 0, nz), (0.5, 0.5), weights)
        cap = [mb.vert(mb.pos[i], (0, 0, nz), (0.5, 0.5), weights) for i in ring]
        for k in range(m):
            mb.tri.append((cap[k], cap[(k + 1) % m], ctr))


def build(mirror):
    p = joint_positions(mirror)
    R = joint_frames(p, mirror)
    sx = -1.0 if mirror else 1.0
    mb = MeshBuilder()
    wrist = {J["Wrist"]: 1.0}

    # 손바닥 상자 · 손목 · 소매(모두 손목 뼈)
    (x0, x1), (y0, y1), (z0, z1) = PALM_BOX
    if mirror:
        x0, x1 = -x1, -x0
    add_box(mb, (x0, y0, z0), (x1, y1, z1), wrist)
    add_cuff(mb, -0.004, 0.032, 0.037, 0.026, wrist, False)
    add_cuff(mb, 0.032, 0.078, 0.047, 0.037, wrist, True, True)

    # 손가락 네 개: 뿌리 마디 조금 뒤(손바닥 안)부터 끝까지
    for name, _mc, _k, _d, _lens, r in FINGERS:
        pts = [p[J[name + n]] for n in ("Proximal", "Intermediate", "Distal", "Tip")]
        bones = [J["Wrist"], J[name + "Proximal"], J[name + "Intermediate"], J[name + "Distal"]]
        add_tube(mb, pts, bones, r, r * 0.88, -0.012, np.array([0.0, 1.0, 0.0]))

    # 엄지: metacarpal 부터
    pts = [p[J[n]] for n in ("ThumbMetacarpal", "ThumbProximal", "ThumbDistal", "ThumbTip")]
    bones = [J["Wrist"], J["ThumbMetacarpal"], J["ThumbProximal"], J["ThumbDistal"]]
    hint = np.array([sx * THUMB_NAIL_HINT[0], THUMB_NAIL_HINT[1], THUMB_NAIL_HINT[2]])
    add_tube(mb, pts, bones, THUMB_RADIUS[0], THUMB_RADIUS[1], -0.006, norm(hint))

    mb.fix_winding()

    # 관절 노드: 로컬 이동 · 회전(부모 축 기준). 전역 = [R | p], inverseBind = 전역의 역.
    nodes = []
    ibm = []
    for j in range(26):
        par = PARENT[j]
        if par < 0:
            lt, lr = p[j], R[j]
        else:
            lt = R[par].T @ (p[j] - p[par])
            lr = R[par].T @ R[j]
        node = {"name": JOINTS[j], "translation": [float(v) for v in lt],
                "rotation": [float(v) for v in quat_from_matrix(lr)]}
        kids = [c for c in range(26) if PARENT[c] == j]
        if kids:
            node["children"] = kids
        nodes.append(node)
        g = np.eye(4)
        g[:3, :3] = R[j]
        g[:3, 3] = p[j]
        ibm.append(np.linalg.inv(g))
    return mb, nodes, ibm


def write_glb(path, name, mb, nodes, ibm):
    P = np.array(mb.pos, np.float32)
    N = np.array(mb.nrm, np.float32)
    UV = np.array(mb.uv, np.float32)
    JN = np.array(mb.jnt, np.uint16)
    WT = np.array(mb.wgt, np.float32)
    IDX = np.array(mb.tri, np.uint32 if len(P) > 65535 else np.uint16).reshape(-1)
    IBM = np.array([m.T.reshape(-1) for m in ibm], np.float32)   # glTF 행렬은 열 우선

    blobs, views, accessors = [], [], []
    offset = 0

    def add(arr, target, comp, typ, count, minmax=False):
        nonlocal offset
        data = arr.tobytes()
        pad = (-len(data)) % 4
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        blobs.append(data + b"\0" * pad)
        offset += len(data) + pad
        acc = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
        if minmax:
            acc["min"] = [float(v) for v in arr.min(axis=0)]
            acc["max"] = [float(v) for v in arr.max(axis=0)]
        accessors.append(acc)
        return len(accessors) - 1

    a_pos = add(P, 34962, 5126, "VEC3", len(P), True)
    a_nrm = add(N, 34962, 5126, "VEC3", len(P))
    a_uv = add(UV, 34962, 5126, "VEC2", len(P))
    a_jnt = add(JN, 34962, 5123, "VEC4", len(P))
    a_wgt = add(WT, 34962, 5126, "VEC4", len(P))
    a_idx = add(IDX, 34963, 5125 if IDX.dtype == np.uint32 else 5123, "SCALAR", len(IDX))
    a_ibm = add(IBM, None, 5126, "MAT4", len(IBM))

    mesh_node = len(nodes)
    doc = {
        "asset": {"version": "2.0", "generator": "Haruka src/tools/make_test_gloves.py"},
        "scene": 0,
        "scenes": [{"nodes": [mesh_node, JOINTS.index("Wrist")]}],
        "nodes": nodes + [{"name": name, "mesh": 0, "skin": 0}],
        "skins": [{"name": name + "_Skeleton", "joints": list(range(26)), "inverseBindMatrices": a_ibm,
                   "skeleton": JOINTS.index("Wrist")}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv, "JOINTS_0": a_jnt, "WEIGHTS_0": a_wgt},
            "indices": a_idx, "material": 0}]}],
        "materials": [{"name": "TestGloveFabric", "pbrMetallicRoughness": {
            "baseColorFactor": [0.78, 0.76, 0.70, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.75}}],
        "buffers": [{"byteLength": offset}],
        "bufferViews": views,
        "accessors": accessors,
    }
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    binary = b"".join(blobs)
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A))
        f.write(js)
        f.write(struct.pack("<II", len(binary), 0x004E4942))
        f.write(binary)
    return len(P), len(IDX) // 3


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                   "..", "..", "assets", "models", "hands")
    os.makedirs(out_dir, exist_ok=True)
    for mirror, suffix in ((False, "R"), (True, "L")):
        mb, nodes, ibm = build(mirror)
        name = "TestGlove_" + suffix
        path = os.path.join(out_dir, name + ".glb")
        nv, nt = write_glb(path, name, mb, nodes, ibm)
        print(f"{path}: {nv} vertices, {nt} triangles, 26 joints")


if __name__ == "__main__":
    main()
