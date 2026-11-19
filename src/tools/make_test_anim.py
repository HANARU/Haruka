"""애니메이션 시험 모델 생성기 (2026-11-20, Docs/Animation_System.md A2-5).

관절 4개(Root → Upper → Lower → Hand)로 움직이는 사각 기둥(높이 1.6 m, 굵기 0.1 m)과 클립 셋을 glb 로 씁니다.
최종 아트가 아니라 가져오기 · 샘플링 · 재생 · 블렌딩 · 루트 모션을 확인하는 자리 표시용입니다. 단위는 m, +Y 가 위입니다.

- Wave(1 s): Upper 회전 LINEAR(Z 축 0 → 45° → 0), Lower 회전 STEP(0 → 30°), 관절이 아닌 Marker 노드 이동(무시돼야 함)
- Bounce(1 s): Root 이동 CUBICSPLINE((0,0,0) → (0,0.5,0), 접선 (0,1,0)), Hand 배율 LINEAR(1 → 2)
- Walk(1 s): Root 이동 LINEAR((0,0,0) → (0,0,1)), Upper 회전 LINEAR(X 축 -20° → 20° → -20°)
- Flex(1 s): 모프 타깃 Bulge 의 무게 LINEAR(0 → 1). Bulge 는 팔꿈치 고리를 바깥으로 0.05 m 부풀림(A5)
- 팔꿈치 고리(y = 0.8)의 정점은 JOINTS_1 · WEIGHTS_1 까지 영향 8개(큰 4개를 남기는지 시험)

TestMorph.glb: 스킨 없는 모프 메시(사각형 하나, 노드 이동 (1, 0, 0)). 타깃 Lift = 위 두 정점 +0.5 m Y, 기본 무게 0.25,
클립 Lift(STEP 0 → 1 @0.5 s).

사용: python -I src/tools/make_test_anim.py [출력 폴더]   (기본: 이 스크립트 기준 ../../Projects/Samples/Content/models/anim)
"""
import json
import math
import os
import struct
import sys

HALF = 0.05
RING_Y = [0.0, 0.4, 0.8, 1.2, 1.6]
CORNERS = [(-HALF, -HALF), (HALF, -HALF), (HALF, HALF), (-HALF, HALF)]   # (x, z)
JOINT_NAMES = ["Root", "Upper", "Lower", "Hand"]
JOINT_Y = [0.0, 0.4, 0.8, 1.2]


def quat_axis(axis, deg):
    a = math.radians(deg) * 0.5
    s = math.sin(a)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(a)]


def ring_influences(r):
    """(joints0, weights0, joints1, weights1) for ring r."""
    if r == 0:
        return [0, 0, 0, 0], [1, 0, 0, 0], None, None
    if r == 1:
        return [1, 0, 0, 0], [1, 0, 0, 0], None, None
    if r == 2:
        # 8개: 큰 4개 = Lower 0.50 · Upper 0.40 · Upper 0.04 · Lower 0.03 (합 0.97 → 다시 정규화)
        return [0, 1, 2, 3], [0.02, 0.40, 0.03, 0.0], [2, 0, 1, 3], [0.50, 0.01, 0.04, 0.0]
    return [3, 0, 0, 0], [1, 0, 0, 0], None, None


def build():
    pos, nrm, j0, w0, j1, w1 = [], [], [], [], [], []
    for r, y in enumerate(RING_Y):
        ja, wa, jb, wb = ring_influences(r)
        for (x, z) in CORNERS:
            pos += [x, y, z]
            l = math.hypot(x, z)
            nrm += [x / l, 0.0, z / l]
            j0 += ja
            w0 += wa
            j1 += (jb if jb else [0, 0, 0, 0])
            w1 += (wb if wb else [0.0, 0.0, 0.0, 0.0])
    idx = []
    for r in range(len(RING_Y) - 1):
        for c in range(4):
            a = r * 4 + c
            b = r * 4 + (c + 1) % 4
            d = (r + 1) * 4 + c
            e = (r + 1) * 4 + (c + 1) % 4
            idx += [a, d, b, b, d, e]
    return pos, nrm, j0, w0, j1, w1, idx


class Bin:
    def __init__(self):
        self.data = bytearray()
        self.views = []
        self.accessors = []

    def add(self, fmt, values, comp_type, typ, count, minmax=None):
        while len(self.data) % 4:
            self.data.append(0)
        off = len(self.data)
        self.data += struct.pack('<%d%s' % (len(values), fmt), *values)
        self.views.append({"buffer": 0, "byteOffset": off, "byteLength": len(self.data) - off})
        acc = {"bufferView": len(self.views) - 1, "componentType": comp_type, "count": count, "type": typ}
        if minmax:
            acc["min"], acc["max"] = minmax
        self.accessors.append(acc)
        return len(self.accessors) - 1


FLOAT, USHORT, UINT = 5126, 5123, 5125


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', 'Projects', 'Samples', 'Content', 'models', 'anim')
    os.makedirs(out_dir, exist_ok=True)
    pos, nrm, j0, w0, j1, w1, idx = build()
    n = len(pos) // 3
    b = Bin()
    a_pos = b.add('f', pos, FLOAT, "VEC3", n, ([-HALF, 0.0, -HALF], [HALF, RING_Y[-1], HALF]))
    a_nrm = b.add('f', nrm, FLOAT, "VEC3", n)
    a_j0 = b.add('H', j0, USHORT, "VEC4", n)
    a_w0 = b.add('f', w0, FLOAT, "VEC4", n)
    a_j1 = b.add('H', j1, USHORT, "VEC4", n)
    a_w1 = b.add('f', w1, FLOAT, "VEC4", n)
    a_idx = b.add('I', idx, UINT, "SCALAR", len(idx))
    bulge, zero = [], []
    for r, y in enumerate(RING_Y):
        for (x, z) in CORNERS:
            k = 1.0 if r == 2 else 0.0
            bulge += [k * x, 0.0, k * z]   # 굵기 0.1 → 0.2(바깥으로 0.05 m)
            zero += [0.0, 0.0, 0.0]
    a_bulge = b.add('f', bulge, FLOAT, "VEC3", n, ([-HALF, 0.0, -HALF], [HALF, 0.0, HALF]))
    a_bulge_n = b.add('f', zero, FLOAT, "VEC3", n)
    ibm = []
    for y in JOINT_Y:
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -y, 0, 1]   # 열 우선: 이동 (0, -y, 0)
    a_ibm = b.add('f', ibm, FLOAT, "MAT4", 4)

    def track(times, values, typ):
        ti = b.add('f', times, FLOAT, "SCALAR", len(times), ([min(times)], [max(times)]))
        comps = {"VEC3": 3, "VEC4": 4}[typ]
        vo = b.add('f', values, FLOAT, typ, len(values) // comps)
        return ti, vo

    Z, X = (0, 0, 1), (1, 0, 0)
    anims = []
    # Wave
    s, ch = [], []
    ti, vo = track([0.0, 0.5, 1.0], quat_axis(Z, 0) + quat_axis(Z, 45) + quat_axis(Z, 0), "VEC4")
    s.append({"input": ti, "output": vo, "interpolation": "LINEAR"}); ch.append({"sampler": 0, "target": {"node": 2, "path": "rotation"}})
    ti, vo = track([0.0, 0.5], quat_axis(Z, 0) + quat_axis(Z, 30), "VEC4")
    s.append({"input": ti, "output": vo, "interpolation": "STEP"}); ch.append({"sampler": 1, "target": {"node": 3, "path": "rotation"}})
    ti, vo = track([0.0, 1.0], [0, 0, 0, 1, 0, 0], "VEC3")
    s.append({"input": ti, "output": vo, "interpolation": "LINEAR"}); ch.append({"sampler": 2, "target": {"node": 5, "path": "translation"}})
    anims.append({"name": "Wave", "samplers": s, "channels": ch})
    # Bounce: CUBICSPLINE 키마다 (in, value, out)
    s, ch = [], []
    ti, vo = track([0.0, 1.0], [0, 1, 0, 0, 0, 0, 0, 1, 0,
                                0, 1, 0, 0, 0.5, 0, 0, 1, 0], "VEC3")
    s.append({"input": ti, "output": vo, "interpolation": "CUBICSPLINE"}); ch.append({"sampler": 0, "target": {"node": 1, "path": "translation"}})
    ti, vo = track([0.0, 1.0], [1, 1, 1, 2, 2, 2], "VEC3")
    s.append({"input": ti, "output": vo, "interpolation": "LINEAR"}); ch.append({"sampler": 1, "target": {"node": 4, "path": "scale"}})
    anims.append({"name": "Bounce", "samplers": s, "channels": ch})
    # Walk
    s, ch = [], []
    ti, vo = track([0.0, 1.0], [0, 0, 0, 0, 0, 1], "VEC3")
    s.append({"input": ti, "output": vo, "interpolation": "LINEAR"}); ch.append({"sampler": 0, "target": {"node": 1, "path": "translation"}})
    ti, vo = track([0.0, 0.5, 1.0], quat_axis(X, -20) + quat_axis(X, 20) + quat_axis(X, -20), "VEC4")
    s.append({"input": ti, "output": vo, "interpolation": "LINEAR"}); ch.append({"sampler": 1, "target": {"node": 2, "path": "rotation"}})
    anims.append({"name": "Walk", "samplers": s, "channels": ch})
    # Flex: 모프 무게(SCALAR 접근자에 키마다 타깃 수만큼)
    ti = b.add('f', [0.0, 1.0], FLOAT, "SCALAR", 2, ([0.0], [1.0]))
    vo = b.add('f', [0.0, 1.0], FLOAT, "SCALAR", 2)
    anims.append({"name": "Flex", "samplers": [{"input": ti, "output": vo, "interpolation": "LINEAR"}],
                  "channels": [{"sampler": 0, "target": {"node": 0, "path": "weights"}}]})

    nodes = [
        {"name": "Arm", "mesh": 0, "skin": 0},
        {"name": "Root", "children": [2]},
        {"name": "Upper", "translation": [0, 0.4, 0], "children": [3]},
        {"name": "Lower", "translation": [0, 0.4, 0], "children": [4]},
        {"name": "Hand", "translation": [0, 0.4, 0]},
        {"name": "Marker", "translation": [0.5, 0, 0]},
    ]
    gltf = {
        "asset": {"version": "2.0", "generator": "Haruka make_test_anim.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1, 5]}],
        "nodes": nodes,
        "skins": [{"joints": [1, 2, 3, 4], "inverseBindMatrices": a_ibm, "skeleton": 1}],
        "meshes": [{"name": "TestArm", "primitives": [{
            "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "JOINTS_0": a_j0, "WEIGHTS_0": a_w0,
                           "JOINTS_1": a_j1, "WEIGHTS_1": a_w1},
            "indices": a_idx, "material": 0,
            "targets": [{"POSITION": a_bulge, "NORMAL": a_bulge_n}]}], "weights": [0.0]}],
        "materials": [{"name": "ArmOrange", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.45, 0.1, 1.0],
                                                                    "metallicFactor": 0.0, "roughnessFactor": 0.6}}],
        "animations": anims,
        "buffers": [{"byteLength": 0}],
        "bufferViews": b.views,
        "accessors": b.accessors,
    }
    write_glb(os.path.join(out_dir, 'TestArm.glb'), gltf, b)
    write_morph(out_dir)


def write_glb(path, gltf, b):
    while len(b.data) % 4:
        b.data.append(0)
    gltf["buffers"][0]["byteLength"] = len(b.data)
    js = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    while len(js) % 4:
        js += b' '
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(b.data))
    out += struct.pack('<II', len(js), 0x4E4F534A) + js
    out += struct.pack('<II', len(b.data), 0x004E4942) + bytes(b.data)
    with open(path, 'wb') as f:
        f.write(out)
    print('wrote', path, len(out), 'bytes')


def write_morph(out_dir):
    b = Bin()
    pos = [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0]
    nrm = [0, 0, 1] * 4
    a_pos = b.add('f', pos, FLOAT, "VEC3", 4, ([0, 0, 0], [1, 1, 0]))
    a_nrm = b.add('f', nrm, FLOAT, "VEC3", 4)
    a_idx = b.add('I', [0, 1, 2, 0, 2, 3], UINT, "SCALAR", 6)
    lift = [0, 0, 0, 0, 0, 0, 0, 0.5, 0, 0, 0.5, 0]
    a_lift = b.add('f', lift, FLOAT, "VEC3", 4, ([0, 0, 0], [0, 0.5, 0]))
    ti = b.add('f', [0.0, 0.5], FLOAT, "SCALAR", 2, ([0.0], [0.5]))
    vo = b.add('f', [0.0, 1.0], FLOAT, "SCALAR", 2)
    gltf = {
        "asset": {"version": "2.0", "generator": "Haruka make_test_anim.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Quad", "mesh": 0, "translation": [1, 0, 0]}],
        "meshes": [{"name": "TestMorph", "weights": [0.25], "primitives": [{
            "attributes": {"POSITION": a_pos, "NORMAL": a_nrm}, "indices": a_idx, "material": 0,
            "targets": [{"POSITION": a_lift}]}]}],
        "materials": [{"name": "MorphBlue", "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.4, 0.9, 1.0],
                                                                    "metallicFactor": 0.0, "roughnessFactor": 0.6},
                       "doubleSided": True}],
        "animations": [{"name": "Lift", "samplers": [{"input": ti, "output": vo, "interpolation": "STEP"}],
                        "channels": [{"sampler": 0, "target": {"node": 0, "path": "weights"}}]}],
        "buffers": [{"byteLength": 0}],
        "bufferViews": b.views,
        "accessors": b.accessors,
    }
    write_glb(os.path.join(out_dir, 'TestMorph.glb'), gltf, b)


main()
