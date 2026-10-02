#include "PrimitiveDesc.hpp"
#include "../core/TextFloat.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── 셰이프 이름 ───────────────────────────────────────────────────────────────

static const std::array<const char*, (size_t)EPrimitiveShape::Count> kShapeNames = {
    "box", "plane", "sphere", "cylinder", "cone", "capsule", "torus", "arrow", "icosphere"
};

const char* primitiveShapeName(EPrimitiveShape s) {
    const size_t i = (size_t)s;
    return i < kShapeNames.size() ? kShapeNames[i] : "box";
}

EPrimitiveShape primitiveShapeFromName(std::string_view name, bool* ok) {
    for (size_t i = 0; i < kShapeNames.size(); ++i)
        if (name == kShapeNames[i]) {
            if (ok) *ok = true;
            return (EPrimitiveShape)i;
        }
    if (ok) *ok = false;
    return EPrimitiveShape::Box;
}

// ── 세그먼트 래더 (규칙 R3) ───────────────────────────────────────────────────
// {4, 8, 16, 32, 64} 로 스냅합니다. 이유는 둘입니다.
//   1. 캐시 안전 장치입니다. 도달 가능한 키 공간이 유한해지므로 값을 왔다 갔다
//      편집해도 첫 방문 이후 전부 캐시 히트입니다. 메가버퍼가 회수되지 않으므로
//      이것은 UI 편의가 아니라 메모리 안전 문제입니다.
//   2. 2 의 거듭제곱이면 LOD_n 의 링 정점이 LOD_0 의 부분집합이 되어
//      실루엣이 제자리에서 줄어듭니다.

static constexpr std::array<uint16_t, 5> kLadder = { 4, 8, 16, 32, 64 };

uint16_t snapSegmentLadder(int requested) {
    if (requested <= (int)kLadder.front()) return kLadder.front();
    if (requested >= (int)kLadder.back())  return kLadder.back();
    // 가장 가까운 계단으로 붙입니다. 거리가 같으면 위쪽을 택합니다.
    uint16_t best  = kLadder.front();
    int      bestD = std::abs(requested - (int)best);
    for (uint16_t v : kLadder) {
        const int d = std::abs(requested - (int)v);
        if (d <= bestD) { bestD = d; best = v; }
    }
    return best;
}

// ── 정규화 ────────────────────────────────────────────────────────────────────
// 미사용 필드를 0 으로 눕히는 것이 핵심입니다. segV 가 의미 없는 Box 에서
// segV 만 다른 두 desc 가 서로 다른 키를 만들면 같은 메시가 두 번 올라갑니다.

void PrimitiveDesc::canonicalize() {
    _pad = 0;

    if ((uint8_t)shape >= (uint8_t)EPrimitiveShape::Count) shape = EPrimitiveShape::Box;

    if (!std::isfinite(ratio)) ratio = 0.5f;
    ratio = std::clamp(ratio, 0.01f, 0.99f);

    switch (shape) {
        case EPrimitiveShape::Box:
            // 형태 파라미터가 없습니다. 메시 1 개로 충분합니다.
            segU = 0; segV = 0; flags = 0; ratio = 0.f;
            break;

        case EPrimitiveShape::Plane:
            // segU / segV 는 격자 분할 수입니다. 래더를 쓰지 않고 1..64 로 둡니다.
            // 평면은 곡률이 없어 LOD 부분집합 성질이 필요 없고, 기본 1x1 이
            // 래더의 최솟값 4 보다 작기 때문입니다.
            segU  = (uint16_t)std::clamp<int>(segU ? segU : 1, 1, 64);
            segV  = (uint16_t)std::clamp<int>(segV ? segV : 1, 1, 64);
            flags = (uint8_t)(flags & kInsideOut);
            ratio = 0.f;
            break;

        case EPrimitiveShape::Sphere:
            segU  = snapSegmentLadder(segU);
            segV  = snapSegmentLadder(segV);
            flags = (uint8_t)(flags & kInsideOut);
            ratio = 0.f;
            break;

        case EPrimitiveShape::Cylinder:
        case EPrimitiveShape::Cone:
            segU  = snapSegmentLadder(segU);
            segV  = 0;                        // 축 방향 분할이 없습니다.
            ratio = 0.f;
            break;

        case EPrimitiveShape::Capsule:
            segU  = snapSegmentLadder(segU);
            segV  = snapSegmentLadder(segV);  // 반구 링 수
            flags = (uint8_t)(flags & kInsideOut);
            break;                            // ratio = 반지름 / 반높이

        case EPrimitiveShape::Torus:
            segU  = snapSegmentLadder(segU);
            segV  = snapSegmentLadder(segV);
            flags = (uint8_t)(flags & kInsideOut);
            break;                            // ratio = minor / major

        case EPrimitiveShape::Arrow:
            segU  = snapSegmentLadder(segU);
            segV  = 0;
            flags = 0;
            break;                            // ratio = headRatio

        case EPrimitiveShape::Icosphere:
            // segU 를 세분 단계로 재해석합니다. 0..3 입니다.
            segU  = (uint16_t)std::clamp<int>(segU > 3 ? 2 : segU, 0, 3);
            segV  = 0;
            flags = (uint8_t)(flags & kInsideOut);
            ratio = 0.f;
            break;

        default: break;
    }
}

// ── 캐시 키 ───────────────────────────────────────────────────────────────────

std::string PrimitiveDesc::toKey() const {
    PrimitiveDesc c = *this;
    c.canonicalize();
    char buf[96];
    // ratio 는 소수 4 자리로 고정합니다. 표현 차이로 키가 갈라지지 않게 합니다.
    std::snprintf(buf, sizeof(buf), "%s|%ux%u|%u|%.4f",
                  primitiveShapeName(c.shape),
                  (unsigned)c.segU, (unsigned)c.segV,
                  (unsigned)c.flags, (double)c.ratio);
    return std::string(buf);
}

bool PrimitiveDesc::fromKey(std::string_view key, PrimitiveDesc& out) {
    const size_t p1 = key.find('|');
    if (p1 == std::string_view::npos) return false;
    const size_t p2 = key.find('|', p1 + 1);
    if (p2 == std::string_view::npos) return false;
    const size_t p3 = key.find('|', p2 + 1);
    if (p3 == std::string_view::npos) return false;

    bool ok = false;
    PrimitiveDesc d;
    d.shape = primitiveShapeFromName(key.substr(0, p1), &ok);
    if (!ok) return false;

    // sscanf 대신 strtoul / strtod 를 씁니다. 파싱이 어디서 멈췄는지를 직접
    // 확인할 수 있어서, 꼬리에 쓰레기가 붙은 키를 조용히 통과시키지 않습니다.
    const std::string seg(key.substr(p1 + 1, p2 - p1 - 1));
    char* endp = nullptr;
    const unsigned long u = std::strtoul(seg.c_str(), &endp, 10);
    if (endp == seg.c_str() || *endp != 'x') return false;
    const char* vStart = endp + 1;
    const unsigned long v = std::strtoul(vStart, &endp, 10);
    if (endp == vStart || *endp != '\0') return false;

    const std::string flagStr(key.substr(p2 + 1, p3 - p2 - 1));
    const unsigned long f = std::strtoul(flagStr.c_str(), &endp, 10);
    if (endp == flagStr.c_str() || *endp != '\0') return false;

    const std::string ratioStr(key.substr(p3 + 1));
    const double r = std::strtod(ratioStr.c_str(), &endp);
    if (endp == ratioStr.c_str() || *endp != '\0') return false;

    d.segU  = (uint16_t)u;
    d.segV  = (uint16_t)v;
    d.flags = (uint8_t)f;
    d.ratio = (float)r;
    d.canonicalize();
    out = d;
    return true;
}

bool PrimitiveDesc::operator==(const PrimitiveDesc& o) const {
    PrimitiveDesc a = *this, b = o;
    a.canonicalize(); b.canonicalize();
    return a.shape == b.shape && a.flags == b.flags &&
           a.segU  == b.segU  && a.segV  == b.segV  &&
           a.ratio == b.ratio;
}

// ── 머티리얼 정체성 ───────────────────────────────────────────────────────────

std::string PrimitiveMaterialDesc::toText() const {
    if (checker) return "checker";
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%08X", (unsigned)rgba8);
    return std::string("color ") + hex + " " + floatToText(metallic) + " " + floatToText(roughness);
}

bool PrimitiveMaterialDesc::fromText(std::string_view text, PrimitiveMaterialDesc& out) {
    // 손으로 고친 파일을 위해 앞뒤 공백만 허용합니다.
    const std::string s(text);
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    const size_t e = s.find_last_not_of(" \t\r\n");
    const std::string t = s.substr(b, e - b + 1);

    if (t == "checker") {
        out = PrimitiveMaterialDesc{};
        out.checker = true;
        return true;
    }
    if (t.rfind("color ", 0) != 0) return false;

    // 색은 정확히 16진수 8자리입니다. strtoul 은 앞의 공백과 부호를 받아들이므로 직접 셉니다.
    const char* p = t.c_str() + 6;
    while (*p == ' ') ++p;
    for (int i = 0; i < 8; ++i)
        if (!std::isxdigit((unsigned char)p[i])) return false;
    if (p[8] != ' ') return false;
    const std::string hex(p, 8);
    const unsigned long rgba = std::strtoul(hex.c_str(), nullptr, 16);
    p += 8;

    char* endp = nullptr;
    const float metallic = std::strtof(p, &endp);
    if (endp == p) return false;
    p = endp;
    const float roughness = std::strtof(p, &endp);
    if (endp == p || *endp != '\0') return false;
    if (!(metallic  >= 0.f && metallic  <= 1.f)) return false;   // NaN 도 여기서 걸립니다
    if (!(roughness >= 0.f && roughness <= 1.f)) return false;

    out = PrimitiveMaterialDesc{};
    out.rgba8     = (uint32_t)rgba;
    out.metallic  = metallic;
    out.roughness = roughness;
    return true;
}

bool PrimitiveMaterialDesc::operator==(const PrimitiveMaterialDesc& o) const {
    if (checker != o.checker) return false;
    if (checker) return true;   // 체커는 색과 팩터를 쓰지 않습니다
    return rgba8 == o.rgba8 && metallic == o.metallic && roughness == o.roughness;
}

// ── LOD (규칙 R4) ─────────────────────────────────────────────────────────────

PrimitiveDesc primitiveLodDesc(const PrimitiveDesc& base, int level) {
    PrimitiveDesc d = base;
    d.canonicalize();
    if (level <= 0) return d;

    // Box 와 Plane 은 LOD 가 없습니다. 24 정점을 더 줄일 이유가 없습니다.
    if (d.shape == EPrimitiveShape::Box || d.shape == EPrimitiveShape::Plane) return d;

    if (d.shape == EPrimitiveShape::Icosphere) {
        d.segU = (uint16_t)std::max(0, (int)d.segU - level);
    } else {
        auto shrink = [&](uint16_t s) -> uint16_t {
            if (s == 0) return 0;
            return snapSegmentLadder(std::max((int)s >> level, 4));
        };
        d.segU = shrink(d.segU);
        d.segV = shrink(d.segV);
    }
    d.canonicalize();
    return d;
}

// ── 물리 대응 ─────────────────────────────────────────────────────────────────

bool primitiveToPhysics(const PrimitiveDesc& desc,
                        EPhysicsShape&       outShape,
                        HalfExt&             outHalf) {
    PrimitiveDesc d = desc;
    d.canonicalize();

    switch (d.shape) {
        case EPrimitiveShape::Box:
            outShape = EPhysicsShape::Box;
            outHalf  = HalfExt{ 0.5f, 0.5f, 0.5f };
            return true;

        case EPrimitiveShape::Plane:
            // 두께 0 인 셰이프는 Jolt 에 없습니다. 얇은 박스로 근사합니다.
            outShape = EPhysicsShape::Box;
            outHalf  = HalfExt{ 0.5f, 0.001f, 0.5f };
            return true;

        case EPrimitiveShape::Sphere:
        case EPrimitiveShape::Icosphere:
            outShape = EPhysicsShape::Sphere;
            outHalf  = HalfExt{ 0.5f, 0.5f, 0.5f };   // radius = halfExtent.x
            return true;

        case EPrimitiveShape::Capsule: {
            // 총 높이 1 = 2*(cylHalfHeight + radius) 입니다.
            // ratio 는 반지름을 반높이 0.5 로 나눈 값입니다.
            const float radius = 0.5f * d.ratio;
            outShape = EPhysicsShape::Capsule;
            outHalf  = HalfExt{ radius, 0.5f - radius, radius };
            return true;
        }

        // 해석적 대응이 없습니다. 호출자가 Box 근사와 스킵 중에 결정합니다.
        case EPrimitiveShape::Cylinder:
        case EPrimitiveShape::Cone:
        case EPrimitiveShape::Torus:
        case EPrimitiveShape::Arrow:
        default:
            outShape = EPhysicsShape::Box;
            outHalf  = HalfExt{ 0.5f, 0.5f, 0.5f };
            return false;
    }
}
