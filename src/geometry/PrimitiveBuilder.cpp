#include "PrimitiveBuilder.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

// Arrow 의 굵기입니다. 길이 1 m 기준의 비율이며, 이 둘이 Arrow 의 AABB 를 정합니다.
constexpr float kArrowShaftRadius = 0.025f;
constexpr float kArrowHeadRadius  = 0.060f;

// ── 정점 조립 도우미 ──────────────────────────────────────────────────────────

struct Builder {
    MeshSourceData d;

    uint32_t push(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv,
                  const glm::vec3& t, float w) {
        MeshSourceVertex v{};
        v.pos[0] = p.x;  v.pos[1] = p.y;  v.pos[2] = p.z;
        v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
        v.uv[0] = uv.x;  v.uv[1] = uv.y;
        v.tangent[0] = t.x; v.tangent[1] = t.y; v.tangent[2] = t.z; v.tangent[3] = w;
        d.vertices.push_back(v);
        return (uint32_t)d.vertices.size() - 1;
    }

    void tri(uint32_t a, uint32_t b, uint32_t c) {
        d.indices.push_back(a); d.indices.push_back(b); d.indices.push_back(c);
    }
};

// 규칙 T1. handedness 를 상수로 하드코딩하지 않고 항상 해석적으로 구합니다.
// 소비 측 계약은 pbr.vert 의 `B = cross(N, T) * w` 입니다. 따라서 w 는
// cross(N,T) 를 진짜 bitangent 와 같은 방향으로 돌려놓는 부호입니다.
//
// defaultCube 가 여섯 면 전부 +1 로 하드코딩되어 있었고 전부 틀렸습니다 (DEF-39).
// 상수로 쓰면 같은 실수가 반복되므로 생성기에는 상수 w 가 한 곳도 없습니다.
float handedness(const glm::vec3& n, const glm::vec3& t, const glm::vec3& b) {
    return (glm::dot(glm::cross(n, t), b) < 0.f) ? -1.f : 1.f;
}

// ── Box ───────────────────────────────────────────────────────────────────────
// 24 정점입니다. 90 도 하드 엣지이므로 면마다 노멀을 분할합니다 (규칙 N2).
// 공유하면 코너 노멀이 대각선이 되어 평평한 면 전체가 그라데이션으로 물듭니다.
//
// 각 면은 (N, U, P) 로 정의합니다. U 는 u 증가 방향, P 는 v '감소' 방향입니다.
// U x P = N 이 되도록 잡으면 아래 네 코너 순서가 자동으로 바깥에서 CCW 가 됩니다.
// bitangent 는 B = -P 이고, 그 결과 여섯 면 모두 w = -1 이 나옵니다.
// defaultCube 의 레이아웃을 그대로 계승하되 w 만 옳게 구한 것입니다.
MeshSourceData buildBox() {
    struct Face { glm::vec3 n, u, p; };
    static const Face kFaces[6] = {
        { { 0, 0, 1}, { 1, 0, 0}, {0, 1, 0} },   // +Z
        { { 0, 0,-1}, {-1, 0, 0}, {0, 1, 0} },   // -Z
        { { 1, 0, 0}, { 0, 0,-1}, {0, 1, 0} },   // +X
        { {-1, 0, 0}, { 0, 0, 1}, {0, 1, 0} },   // -X
        { { 0, 1, 0}, { 1, 0, 0}, {0, 0,-1} },   // +Y
        { { 0,-1, 0}, { 1, 0, 0}, {0, 0, 1} },   // -Y
    };

    Builder b;
    b.d.name = "prim_box";
    for (const Face& f : kFaces) {
        const glm::vec3 c    = f.n * 0.5f;
        const glm::vec3 bit  = -f.p;                       // v 증가 방향
        const float     w    = handedness(f.n, f.u, bit);

        const uint32_t i0 = b.push(c - f.u * 0.5f - f.p * 0.5f, f.n, {0.f, 1.f}, f.u, w);
        const uint32_t i1 = b.push(c + f.u * 0.5f - f.p * 0.5f, f.n, {1.f, 1.f}, f.u, w);
        const uint32_t i2 = b.push(c + f.u * 0.5f + f.p * 0.5f, f.n, {1.f, 0.f}, f.u, w);
        const uint32_t i3 = b.push(c - f.u * 0.5f + f.p * 0.5f, f.n, {0.f, 0.f}, f.u, w);
        b.tri(i0, i1, i2);
        b.tri(i0, i2, i3);
    }
    return std::move(b.d);
}

// ── Plane ─────────────────────────────────────────────────────────────────────
// y = 0 평면, 법선 +Y, 1 m x 1 m 입니다. 단일 평면이므로 노멀을 공유합니다.
//
// UV 는 설계안 4-5절대로 V=0 이 +Z 쪽입니다. Box 의 +Y 면은 defaultCube 를
// 계승해 V=0 이 -Z 쪽이므로 두 도형의 윗면 텍스처가 180 도 돌아 보입니다.
// 이것은 규약대로의 동작이며 결함이 아닙니다. 두 규약이 문서에 따로 적혀 있습니다.
MeshSourceData buildPlane(const PrimitiveDesc& desc) {
    const int nu = std::max<int>(desc.segU, 1);
    const int nv = std::max<int>(desc.segV, 1);

    const glm::vec3 N(0.f, 1.f, 0.f);
    const glm::vec3 T(1.f, 0.f, 0.f);    // u 가 커지면 x 가 커집니다
    const glm::vec3 B(0.f, 0.f,-1.f);    // v 가 커지면 z 가 작아집니다
    const float     w = handedness(N, T, B);

    Builder b;
    b.d.name = "prim_plane";
    b.d.vertices.reserve((size_t)(nu + 1) * (nv + 1));

    for (int j = 0; j <= nv; ++j) {
        const float v = (float)j / (float)nv;
        for (int i = 0; i <= nu; ++i) {
            const float u = (float)i / (float)nu;
            b.push({ u - 0.5f, 0.f, 0.5f - v }, N, { u, v }, T, w);
        }
    }
    const int stride = nu + 1;
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const uint32_t a = (uint32_t)(j * stride + i);
            const uint32_t c = a + 1;
            const uint32_t e = (uint32_t)((j + 1) * stride + i);
            const uint32_t f = e + 1;
            // cross(c-a, f-a) 가 +Y 를 향합니다. 바깥에서 CCW 입니다.
            b.tri(a, c, f);
            b.tri(a, f, e);
        }
    return std::move(b.d);
}

// ── Sphere (UV 구) ────────────────────────────────────────────────────────────
// 반지름 0.5 입니다. RigidBodyComponent 의 halfExtent 기본값이 0.5 이고 주석이
// "radius = halfExtent.x" 이므로, 반지름 1.0 으로 두면 물리와 렌더가 두 배 어긋납니다.
//
// 파라미터화는 다음과 같습니다.
//   P(θ, φ) = 0.5 * (sinφ sinθ, cosφ, sinφ cosθ)
//   u = θ / 2π  경도. θ=0 이 +Z 이고 +Y 에서 내려다보아 CCW 로 돕니다.
//   v = φ / π   위도. v=0 이 북극 +Y, v=1 이 남극 -Y 입니다.
//
// 이음매(U5)와 극 링(U4)을 둘 다 정점 복제로 처리합니다. 극을 단일 정점으로 두면
// u 가 하나뿐이라 텍스처가 부채꼴로 뭉개지고, 무엇보다 극에서 ∂P/∂u = 0 이라
// 탄젠트가 정의되지 않습니다. 극 링 복제가 두 문제를 동시에 없앱니다.
MeshSourceData buildSphere(const PrimitiveDesc& desc) {
    const int nu = std::max<int>(desc.segU, 3);
    const int nv = std::max<int>(desc.segV, 2);
    constexpr float R = 0.5f;

    Builder b;
    b.d.name = "prim_sphere";
    b.d.vertices.reserve((size_t)(nu + 1) * (nv + 1));

    for (int j = 0; j <= nv; ++j) {
        const float v   = (float)j / (float)nv;
        const float phi = v * kPi;
        const float sp  = std::sin(phi), cp = std::cos(phi);

        for (int i = 0; i <= nu; ++i) {
            // 극 행에서는 u 를 인접 사각형의 중앙값으로 옮깁니다 (U4).
            // 북극에서 살아남는 삼각형은 열 i 의 극 정점을 쓰는 것이 (i-1) 번
            // 사각형이고, 남극에서는 i 번 사각형입니다. 그래서 부호가 다릅니다.
            float u = (float)i / (float)nu;
            if (j == 0)       u = ((float)i - 0.5f) / (float)nu;
            else if (j == nv) u = ((float)i + 0.5f) / (float)nu;

            const float theta = u * kTwoPi;
            const float st = std::sin(theta), ct = std::cos(theta);

            const glm::vec3 n(sp * st, cp, sp * ct);
            const glm::vec3 t(ct, 0.f, -st);                 // ∂P/∂θ 정규화
            const glm::vec3 bt(cp * st, -sp, cp * ct);       // ∂P/∂φ 정규화
            b.push(n * R, n, { u, v }, t, handedness(n, t, bt));
        }
    }

    const int stride = nu + 1;
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const uint32_t a = (uint32_t)(j * stride + i);          // (u,   v)
            const uint32_t c = a + 1;                               // (u+1, v)
            const uint32_t e = (uint32_t)((j + 1) * stride + i);    // (u,   v+1)
            const uint32_t f = e + 1;                               // (u+1, v+1)
            // (a, e, c) 와 (c, e, f) 가 바깥에서 CCW 입니다.
            // 극 행의 축퇴 삼각형은 버립니다. 남는 하나가 실제 면입니다.
            if (j != 0)      b.tri(a, e, c);
            if (j != nv - 1) b.tri(c, e, f);
        }
    return std::move(b.d);
}

// insideOut 처리입니다. 노멀을 뒤집고 와인딩을 뒤집습니다.
// N 이 뒤집히면 cross(N,T) 도 뒤집히므로 w 도 같이 뒤집어야 B 가 보존됩니다.
void flipInsideOut(MeshSourceData& d) {
    for (auto& v : d.vertices) {
        v.normal[0] = -v.normal[0];
        v.normal[1] = -v.normal[1];
        v.normal[2] = -v.normal[2];
        v.tangent[3] = -v.tangent[3];
    }
    for (size_t i = 0; i + 2 < d.indices.size(); i += 3)
        std::swap(d.indices[i + 1], d.indices[i + 2]);
}

// ── 회전체 공통 도우미 ────────────────────────────────────────────────────────
// 축은 +Y 이고 θ 는 +Z 에서 시작해 +X 쪽으로 돕니다 (+Y 에서 내려다보아 CCW).
// Sphere 와 같은 규약이며, 모든 회전체의 접선은 언제나 수평입니다.
//   T = ∂P/∂θ 를 정규화한 (cosθ, 0, -sinθ)

inline glm::vec3 revTangent(float ct, float st) { return { ct, 0.f, -st }; }

// 원판 캡 하나를 만듭니다. Box 의 ±Y 면 UV 레이아웃을 그대로 계승합니다.
//   윗면: u = x + 0.5, v = z + 0.5
//   아랫면: u = x + 0.5, v = 0.5 - z
// 두 경우 모두 handedness 가 -1 로 유도되며, 그래서 Box 윗면과 이어 붙여도
// 노멀맵이 뒤집히지 않습니다.
void addDisc(Builder& b, int nu, float radius, float y, bool up) {
    const glm::vec3 N(0.f, up ? 1.f : -1.f, 0.f);
    const glm::vec3 T(1.f, 0.f, 0.f);
    const glm::vec3 B(0.f, 0.f, up ? 1.f : -1.f);
    const float     w = handedness(N, T, B);

    auto uvOf = [&](const glm::vec3& p) {
        return glm::vec2(p.x + 0.5f, up ? (p.z + 0.5f) : (0.5f - p.z));
    };

    const glm::vec3 centerP(0.f, y, 0.f);
    const uint32_t  center = b.push(centerP, N, uvOf(centerP), T, w);

    // 캡의 UV 는 원둘레를 따라 연속이므로 이음매 복제가 필요 없습니다.
    const uint32_t first = (uint32_t)b.d.vertices.size();
    for (int i = 0; i < nu; ++i) {
        const float th = kTwoPi * (float)i / (float)nu;
        const glm::vec3 p(radius * std::sin(th), y, radius * std::cos(th));
        b.push(p, N, uvOf(p), T, w);
    }
    for (int i = 0; i < nu; ++i) {
        const uint32_t r0 = first + (uint32_t)i;
        const uint32_t r1 = first + (uint32_t)((i + 1) % nu);
        // 윗면은 (center, r0, r1) 이 +Y 를 향하고, 아랫면은 순서를 뒤집어야 -Y 입니다.
        if (up) b.tri(center, r0, r1);
        else    b.tri(center, r1, r0);
    }
}

// ── Cylinder ──────────────────────────────────────────────────────────────────
// 반지름 0.5, 높이 1, 축 +Y 입니다.
// 옆면은 링 안에서 노멀을 공유하고, 캡과 옆면 사이는 90 도 하드 엣지이므로
// 정점을 분할합니다 (규칙 N2).
//   옆면 UV: u = θ/2π, v = 1 - (y + 0.5) 이므로 v=0 이 윗면 쪽입니다 (규칙 U2).
MeshSourceData buildCylinder(const PrimitiveDesc& desc) {
    const int   nu = std::max<int>(desc.segU, 3);
    const float R  = 0.5f;

    Builder b;
    b.d.name = "prim_cylinder";

    // 옆면. 위 링(v=0)과 아래 링(v=1) 두 줄입니다.
    for (int row = 0; row < 2; ++row) {
        const float y = (row == 0) ? 0.5f : -0.5f;
        const float v = (float)row;
        for (int i = 0; i <= nu; ++i) {          // 이음매 복제 (U5)
            const float u  = (float)i / (float)nu;
            const float th = u * kTwoPi;
            const float st = std::sin(th), ct = std::cos(th);
            const glm::vec3 n(st, 0.f, ct);
            const glm::vec3 t = revTangent(ct, st);
            const glm::vec3 bt(0.f, -1.f, 0.f);   // v 가 커지면 y 가 작아집니다
            b.push({ R * st, y, R * ct }, n, { u, v }, t, handedness(n, t, bt));
        }
    }
    const uint32_t stride = (uint32_t)nu + 1;
    for (int i = 0; i < nu; ++i) {
        const uint32_t a = (uint32_t)i;            // 위 링
        const uint32_t c = a + 1;
        const uint32_t e = stride + (uint32_t)i;   // 아래 링
        const uint32_t f = e + 1;
        // cross(e-a, c-a) 가 바깥 법선과 같은 방향입니다.
        b.tri(a, e, c);
        b.tri(c, e, f);
    }

    if (desc.capped()) {
        addDisc(b, nu, R,  0.5f, true);
        addDisc(b, nu, R, -0.5f, false);
    }
    return std::move(b.d);
}

// ── Cone ──────────────────────────────────────────────────────────────────────
// 밑면 반지름 0.5 (y=-0.5), 꼭짓점 (0, +0.5, 0) 입니다.
//
// apex 에서는 노멀이 정의되지 않으므로 **세그먼트마다 apex 정점을 복제해**
// 각 삼각형이 자기 노멀을 갖게 합니다 (규칙 N2). 밑면 링은 해석적 노멀이
// 인접 삼각형에서 같으므로 공유합니다.
//
// 옆면 바깥 법선은 높이 h 와 반지름 R 에 대해 (h·sinθ, R, h·cosθ) 방향입니다.
MeshSourceData buildCone(const PrimitiveDesc& desc) {
    const int   nu = std::max<int>(desc.segU, 3);
    const float R  = 0.5f;
    const float h  = 1.f;

    Builder b;
    b.d.name = "prim_cone";

    auto sideNormal = [&](float st, float ct) {
        return glm::normalize(glm::vec3(h * st, R, h * ct));
    };
    // v 는 apex 에서 0, 밑면에서 1 입니다. 따라서 ∂P/∂v 는 아래 바깥 방향입니다.
    auto sideBitan = [&](float st, float ct) {
        return glm::normalize(glm::vec3(R * st, -h, R * ct));
    };

    // 밑면 링 (이음매 복제 포함). 옆면용 정점입니다.
    const uint32_t ringFirst = (uint32_t)b.d.vertices.size();
    for (int i = 0; i <= nu; ++i) {
        const float u  = (float)i / (float)nu;
        const float th = u * kTwoPi;
        const float st = std::sin(th), ct = std::cos(th);
        const glm::vec3 n = sideNormal(st, ct);
        const glm::vec3 t = revTangent(ct, st);
        b.push({ R * st, -0.5f, R * ct }, n, { u, 1.f }, t, handedness(n, t, sideBitan(st, ct)));
    }
    // 세그먼트마다 apex 를 하나씩 둡니다. u 는 그 세그먼트의 중앙값입니다.
    for (int i = 0; i < nu; ++i) {
        const float u  = ((float)i + 0.5f) / (float)nu;
        const float th = u * kTwoPi;
        const float st = std::sin(th), ct = std::cos(th);
        const glm::vec3 n = sideNormal(st, ct);
        const glm::vec3 t = revTangent(ct, st);
        const uint32_t apex =
            b.push({ 0.f, 0.5f, 0.f }, n, { u, 0.f }, t, handedness(n, t, sideBitan(st, ct)));
        // (apex, 밑면 i, 밑면 i+1) 이 바깥에서 CCW 입니다.
        b.tri(apex, ringFirst + (uint32_t)i, ringFirst + (uint32_t)i + 1);
    }

    if (desc.capped()) addDisc(b, nu, R, -0.5f, false);
    return std::move(b.d);
}

// ── Capsule ───────────────────────────────────────────────────────────────────
// 총 높이 1, 축 +Y 입니다. ratio 는 반지름을 반높이 0.5 로 나눈 값이므로
// radius = 0.5*ratio 이고 원통부 반높이 hc = 0.5 - radius 입니다.
// 이 규약은 Jolt 의 CapsuleShapeSettings(halfHeightOfCylinder, radius) 와 같습니다.
//
// **반구와 원통의 경계는 C1 연속이므로 정점을 공유합니다** (규칙 N2).
// 분할하면 없는 링이 눈에 보입니다.
//
// V 는 각도가 아니라 **호 길이**에 비례해 배분합니다. 각도 비례로 나누면
// 반구부 텍스처가 원통부 대비 눌립니다 (규칙 4-5).
MeshSourceData buildCapsule(const PrimitiveDesc& desc) {
    const int   nu = std::max<int>(desc.segU, 3);
    const int   nv = std::max<int>(desc.segV, 2);      // 반구 하나당 링 수
    const float r  = 0.5f * desc.ratio;
    const float hc = 0.5f - r;                          // 원통부 반높이
    const float arcLen = r * kPi + 2.f * hc;            // 자오선 전체 호 길이

    Builder b;
    b.d.name = "prim_capsule";

    const int rows = 2 * (nv + 1);   // 위 반구 nv+1 줄 + 아래 반구 nv+1 줄
    b.d.vertices.reserve((size_t)rows * (nu + 1));

    for (int row = 0; row < rows; ++row) {
        const bool  lower = (row > nv);
        const int   j     = lower ? (row - nv - 1) : row;
        // 위 반구는 φ 0→π/2, 아래 반구는 φ π/2→π 입니다.
        const float phi   = lower ? (kPi * 0.5f + (kPi * 0.5f) * (float)j / (float)nv)
                                  : ((kPi * 0.5f) * (float)j / (float)nv);
        const float sp = std::sin(phi), cp = std::cos(phi);
        const float yc = lower ? -hc : hc;              // 반구 중심의 y

        // 호 길이 누적. 위 반구 r*φ, 원통부 2*hc, 아래 반구 r*(φ-π/2).
        const float s = lower ? (r * (kPi * 0.5f) + 2.f * hc + r * (phi - kPi * 0.5f))
                              : (r * phi);
        const float v = s / arcLen;

        for (int i = 0; i <= nu; ++i) {
            // 극 링 (U4). Sphere 와 같은 이유이며 부호 규칙도 같습니다.
            float u = (float)i / (float)nu;
            if (row == 0)             u = ((float)i - 0.5f) / (float)nu;
            else if (row == rows - 1) u = ((float)i + 0.5f) / (float)nu;

            const float th = u * kTwoPi;
            const float st = std::sin(th), ct = std::cos(th);

            const glm::vec3 n(sp * st, cp, sp * ct);
            const glm::vec3 t  = revTangent(ct, st);
            const glm::vec3 bt(cp * st, -sp, cp * ct);
            b.push({ r * sp * st, yc + r * cp, r * sp * ct }, n, { u, v }, t,
                   handedness(n, t, bt));
        }
    }

    const uint32_t stride = (uint32_t)nu + 1;
    for (int row = 0; row + 1 < rows; ++row)
        for (int i = 0; i < nu; ++i) {
            const uint32_t a = (uint32_t)row * stride + (uint32_t)i;
            const uint32_t c = a + 1;
            const uint32_t e = a + stride;
            const uint32_t f = e + 1;
            // 극 행의 축퇴 삼각형만 버립니다. Sphere 와 같은 구조입니다.
            if (row != 0)          b.tri(a, e, c);
            if (row != rows - 2)   b.tri(c, e, f);
        }
    return std::move(b.d);
}

// ── Torus ─────────────────────────────────────────────────────────────────────
// 링 평면은 XZ, 축은 +Y 입니다. ratio 는 minor/major 이고 바깥 반지름이
// major + minor = 0.5 가 되도록 잡습니다.
//   P(θ, φ) = ((R + r cosφ) sinθ, r sinφ, (R + r cosφ) cosθ)
// θ 와 φ 두 방향 모두 이음매가 있으므로 양쪽 다 복제합니다 (U5).
MeshSourceData buildTorus(const PrimitiveDesc& desc) {
    const int   nu = std::max<int>(desc.segU, 3);
    const int   nv = std::max<int>(desc.segV, 3);
    const float R  = 0.5f / (1.f + desc.ratio);
    const float r  = R * desc.ratio;

    Builder b;
    b.d.name = "prim_torus";
    b.d.vertices.reserve((size_t)(nu + 1) * (nv + 1));

    for (int j = 0; j <= nv; ++j) {
        const float v  = (float)j / (float)nv;
        const float ph = v * kTwoPi;
        const float sp = std::sin(ph), cp = std::cos(ph);
        for (int i = 0; i <= nu; ++i) {
            const float u  = (float)i / (float)nu;
            const float th = u * kTwoPi;
            const float st = std::sin(th), ct = std::cos(th);

            const glm::vec3 n(cp * st, sp, cp * ct);
            const glm::vec3 t  = revTangent(ct, st);
            const glm::vec3 bt(-sp * st, cp, -sp * ct);
            b.push({ (R + r * cp) * st, r * sp, (R + r * cp) * ct }, n, { u, v }, t,
                   handedness(n, t, bt));
        }
    }

    const uint32_t stride = (uint32_t)nu + 1;
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const uint32_t a = (uint32_t)j * stride + (uint32_t)i;
            const uint32_t c = a + 1;
            const uint32_t e = a + stride;
            const uint32_t f = e + 1;
            // cross(c-a, e-a) ∝ cross(T, B) = N 이므로 (a, c, e) 가 바깥입니다.
            b.tri(a, c, e);
            b.tri(c, f, e);
        }
    return std::move(b.d);
}

// ── Arrow ─────────────────────────────────────────────────────────────────────
// **유일한 예외 카테고리입니다** (규칙 P2). 원점이 시작점이고 로컬 전방이 -Z 이며
// 길이가 1 m 입니다. 솔리드 볼륨이 아니라 위치와 방향을 나타내는 도구이기 때문입니다.
//
// 만드는 방식은 축 +Y 로 세워 조립한 뒤 (x,y,z) -> (x, z, -y) 로 한 번 돌리는 것입니다.
// 이 사상은 X 축 -90 도 회전이며 행렬식이 +1 인 진짜 회전이므로 와인딩과
// handedness 가 보존됩니다. 회전체 도우미를 그대로 재사용할 수 있습니다.
//
// ratio 는 머리 길이의 비율입니다.
MeshSourceData buildArrow(const PrimitiveDesc& desc) {
    const int   nu       = std::max<int>(desc.segU, 3);
    const float headLen  = std::clamp(desc.ratio, 0.05f, 0.9f);
    const float shaftLen = 1.f - headLen;
    const float shaftR   = kArrowShaftRadius;
    const float headR    = kArrowHeadRadius;

    Builder b;
    b.d.name = "prim_arrow";

    // 자루 옆면. y = 0 에서 y = shaftLen 까지입니다.
    for (int row = 0; row < 2; ++row) {
        const float y = (row == 0) ? shaftLen : 0.f;
        const float v = (float)row;
        for (int i = 0; i <= nu; ++i) {
            const float u  = (float)i / (float)nu;
            const float th = u * kTwoPi;
            const float st = std::sin(th), ct = std::cos(th);
            const glm::vec3 n(st, 0.f, ct);
            const glm::vec3 t = revTangent(ct, st);
            const glm::vec3 bt(0.f, -1.f, 0.f);
            b.push({ shaftR * st, y, shaftR * ct }, n, { u, v }, t, handedness(n, t, bt));
        }
    }
    const uint32_t stride = (uint32_t)nu + 1;
    for (int i = 0; i < nu; ++i) {
        const uint32_t a = (uint32_t)i, c = a + 1;
        const uint32_t e = stride + (uint32_t)i, f = e + 1;
        b.tri(a, e, c);
        b.tri(c, e, f);
    }

    // 자루 바닥 뚜껑 (y = 0, 아래를 봅니다).
    addDisc(b, nu, shaftR, 0.f, false);
    // 머리 밑면 원판 (y = shaftLen, 아래를 봅니다). 자루보다 넓으므로 고리가 아니라
    // 원판 하나로 덮습니다. 자루 옆면이 그 위를 지나므로 보이지 않습니다.
    addDisc(b, nu, headR, shaftLen, false);

    // 머리 원뿔. 밑면 y = shaftLen, 꼭짓점 y = 1 입니다.
    {
        const float h = headLen;
        auto sideNormal = [&](float st, float ct) {
            return glm::normalize(glm::vec3(h * st, headR, h * ct));
        };
        auto sideBitan = [&](float st, float ct) {
            return glm::normalize(glm::vec3(headR * st, -h, headR * ct));
        };
        const uint32_t ringFirst = (uint32_t)b.d.vertices.size();
        for (int i = 0; i <= nu; ++i) {
            const float u  = (float)i / (float)nu;
            const float th = u * kTwoPi;
            const float st = std::sin(th), ct = std::cos(th);
            const glm::vec3 n = sideNormal(st, ct);
            const glm::vec3 t = revTangent(ct, st);
            b.push({ headR * st, shaftLen, headR * ct }, n, { u, 1.f }, t,
                   handedness(n, t, sideBitan(st, ct)));
        }
        for (int i = 0; i < nu; ++i) {
            const float u  = ((float)i + 0.5f) / (float)nu;
            const float th = u * kTwoPi;
            const float st = std::sin(th), ct = std::cos(th);
            const glm::vec3 n = sideNormal(st, ct);
            const glm::vec3 t = revTangent(ct, st);
            const uint32_t apex =
                b.push({ 0.f, 1.f, 0.f }, n, { u, 0.f }, t, handedness(n, t, sideBitan(st, ct)));
            b.tri(apex, ringFirst + (uint32_t)i, ringFirst + (uint32_t)i + 1);
        }
    }

    // +Y 프레임을 -Z 프레임으로 돌립니다. X 축 -90 도이며 행렬식이 +1 입니다.
    for (auto& v : b.d.vertices) {
        const float py = v.pos[1], pz = v.pos[2];
        v.pos[1] = pz;  v.pos[2] = -py;
        const float ny = v.normal[1], nz = v.normal[2];
        v.normal[1] = nz;  v.normal[2] = -ny;
        const float ty = v.tangent[1], tz = v.tangent[2];
        v.tangent[1] = tz; v.tangent[2] = -ty;
    }
    return std::move(b.d);
}

// ── Icosphere ─────────────────────────────────────────────────────────────────
// 반지름 0.5 입니다. 정이십면체를 세분하고 구에 투영합니다.
//
// **UV 가 없습니다.** 정이십면체에는 이음매 없는 UV 파라미터화가 존재하지
// 않기 때문입니다. UV 를 (0,0) 으로 두고 탄젠트는 더미입니다.
// **이 도형에는 노멀맵 머티리얼을 쓰지 마십시오.** 결과가 정의되지 않습니다.
// 균일한 삼각형 분포가 필요할 때만 씁니다.
MeshSourceData buildIcosphere(const PrimitiveDesc& desc) {
    const int level = std::clamp<int>(desc.segU, 0, 3);

    const float t = (1.f + std::sqrt(5.f)) * 0.5f;
    std::vector<glm::vec3> pos = {
        {-1, t, 0}, { 1, t, 0}, {-1,-t, 0}, { 1,-t, 0},
        { 0,-1, t}, { 0, 1, t}, { 0,-1,-t}, { 0, 1,-t},
        { t, 0,-1}, { t, 0, 1}, {-t, 0,-1}, {-t, 0, 1},
    };
    for (auto& p : pos) p = glm::normalize(p);

    std::vector<uint32_t> idx = {
         0,11, 5,  0, 5, 1,  0, 1, 7,  0, 7,10,  0,10,11,
         1, 5, 9,  5,11, 4, 11,10, 2, 10, 7, 6,  7, 1, 8,
         3, 9, 4,  3, 4, 2,  3, 2, 6,  3, 6, 8,  3, 8, 9,
         4, 9, 5,  2, 4,11,  6, 2,10,  8, 6, 7,  9, 8, 1,
    };

    // 중점을 캐시해 정점을 공유합니다. 공유하지 않으면 정점 수가 3 배로 늘고
    // 곡면 노멀이 면마다 끊깁니다.
    for (int s = 0; s < level; ++s) {
        std::unordered_map<uint64_t, uint32_t> cache;
        auto midpoint = [&](uint32_t a, uint32_t c) {
            const uint64_t key = (uint64_t)std::min(a, c) << 32 | std::max(a, c);
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            pos.push_back(glm::normalize((pos[a] + pos[c]) * 0.5f));
            const uint32_t n = (uint32_t)pos.size() - 1;
            cache.emplace(key, n);
            return n;
        };
        std::vector<uint32_t> next;
        next.reserve(idx.size() * 4);
        for (size_t f = 0; f < idx.size(); f += 3) {
            const uint32_t a = idx[f], c = idx[f + 1], e = idx[f + 2];
            const uint32_t ac = midpoint(a, c), ce = midpoint(c, e), ea = midpoint(e, a);
            next.insert(next.end(), { a, ac, ea,  c, ce, ac,  e, ea, ce,  ac, ce, ea });
        }
        idx.swap(next);
    }

    Builder b;
    b.d.name = "prim_icosphere";
    b.d.vertices.reserve(pos.size());
    for (const auto& p : pos) {
        // UV 가 없으므로 탄젠트도 의미가 없습니다. 단위 벡터 규약만 지킵니다.
        const glm::vec3 t2 = std::fabs(p.y) > 0.99f ? glm::vec3(1, 0, 0)
                                                    : glm::normalize(glm::cross(glm::vec3(0, 1, 0), p));
        b.push(p * 0.5f, p, { 0.f, 0.f }, t2, 1.f);
    }
    b.d.indices = std::move(idx);
    return std::move(b.d);
}

} // namespace

// ── 공개 API ──────────────────────────────────────────────────────────────────

MeshSourceData PrimitiveBuilder::build(const PrimitiveDesc& descIn) {
    PrimitiveDesc desc = descIn;
    desc.canonicalize();

    MeshSourceData d;
    switch (desc.shape) {
        case EPrimitiveShape::Box:       d = buildBox();           break;
        case EPrimitiveShape::Plane:     d = buildPlane(desc);     break;
        case EPrimitiveShape::Sphere:    d = buildSphere(desc);    break;
        case EPrimitiveShape::Cylinder:  d = buildCylinder(desc);  break;
        case EPrimitiveShape::Cone:      d = buildCone(desc);      break;
        case EPrimitiveShape::Capsule:   d = buildCapsule(desc);   break;
        case EPrimitiveShape::Torus:     d = buildTorus(desc);     break;
        case EPrimitiveShape::Arrow:     d = buildArrow(desc);     break;
        case EPrimitiveShape::Icosphere: d = buildIcosphere(desc); break;

        default:
            // 아직 구현하지 않은 셰이프입니다. 다른 도형으로 조용히 대체하지
            // 않습니다. 빈 메시가 돌아가면 호출자가 즉시 알아차립니다.
            std::cerr << "[Primitive] '" << primitiveShapeName(desc.shape)
                      << "' 는 아직 구현되지 않았습니다. 빈 메시를 반환합니다.\n";
            return d;
    }

    if (desc.insideOut()) flipInsideOut(d);
    return d;
}

MeshBounds PrimitiveBuilder::analyticBounds(const PrimitiveDesc& descIn) {
    PrimitiveDesc desc = descIn;
    desc.canonicalize();

    MeshBounds mb;
    switch (desc.shape) {
        case EPrimitiveShape::Plane:
            mb.aabbMin = { -0.5f, 0.f, -0.5f };
            mb.aabbMax = {  0.5f, 0.f,  0.5f };
            break;

        case EPrimitiveShape::Sphere:
        case EPrimitiveShape::Icosphere:
            mb.aabbMin = glm::vec3(-0.5f);
            mb.aabbMax = glm::vec3( 0.5f);
            break;

        case EPrimitiveShape::Capsule: {
            const float r = 0.5f * desc.ratio;
            mb.aabbMin = { -r, -0.5f, -r };
            mb.aabbMax = {  r,  0.5f,  r };
            break;
        }

        case EPrimitiveShape::Torus: {
            const float major = 0.5f / (1.f + desc.ratio);
            const float minor = major * desc.ratio;
            mb.aabbMin = { -0.5f, -minor, -0.5f };
            mb.aabbMax = {  0.5f,  minor,  0.5f };
            break;
        }

        case EPrimitiveShape::Arrow:
            // 유일한 예외 카테고리입니다 (규칙 P2).
            // 원점이 시작점이고 로컬 전방이 -Z 이며 길이가 1 m 입니다.
            mb.aabbMin = { -kArrowHeadRadius, -kArrowHeadRadius, -1.f };
            mb.aabbMax = {  kArrowHeadRadius,  kArrowHeadRadius,  0.f };
            break;

        case EPrimitiveShape::Box:
        case EPrimitiveShape::Cylinder:
        case EPrimitiveShape::Cone:
        default:
            mb.aabbMin = glm::vec3(-0.5f);
            mb.aabbMax = glm::vec3( 0.5f);
            break;
    }

    mb.center = (mb.aabbMin + mb.aabbMax) * 0.5f;

    // 최소 외접구입니다. 구 계열만 정확히 반지름이고 나머지는 AABB 외접구입니다.
    if (desc.shape == EPrimitiveShape::Sphere ||
        desc.shape == EPrimitiveShape::Icosphere ||
        desc.shape == EPrimitiveShape::Capsule ||
        desc.shape == EPrimitiveShape::Torus)
        mb.radius = 0.5f;
    else
        mb.radius = glm::length(mb.aabbMax - mb.center);

    return mb;
}

uint32_t PrimitiveBuilder::estimateVertexCount(const PrimitiveDesc& descIn) {
    PrimitiveDesc d = descIn;
    d.canonicalize();
    const uint32_t nu = d.segU, nv = d.segV;
    switch (d.shape) {
        case EPrimitiveShape::Box:      return 24;                       // 면마다 분할
        case EPrimitiveShape::Plane:    return (nu + 1) * (nv + 1);
        case EPrimitiveShape::Sphere:   return (nu + 1) * (nv + 1);
        // 옆면 두 줄 + (뚜껑이 있으면) 원판 두 장. 원판 하나는 중심 1 + 링 nu 입니다.
        case EPrimitiveShape::Cylinder: return d.capped() ? 4 * (nu + 1) : 2 * (nu + 1);
        // 밑면 링(이음매 복제 포함) + 세그먼트마다 복제한 apex + (뚜껑이면) 원판
        case EPrimitiveShape::Cone:     return d.capped() ? 3 * nu + 2 : 2 * nu + 1;
        case EPrimitiveShape::Capsule:  return 2 * (nv + 1) * (nu + 1);
        case EPrimitiveShape::Torus:    return (nu + 1) * (nv + 1);
        // 자루 옆면 2*(nu+1) + 자루 바닥 원판 (nu+1) + 머리 밑면 원판 (nu+1)
        //   + 머리 원뿔 (nu+1) + nu
        case EPrimitiveShape::Arrow:    return 6 * nu + 5;
        // 중점을 공유하므로 세분 단계 n 에서 10*4^n + 2 입니다.
        case EPrimitiveShape::Icosphere: {
            uint32_t f = 1;
            for (uint32_t i = 0; i < nu; ++i) f *= 4;
            return 10 * f + 2;
        }
        default: return 0;
    }
}

uint32_t PrimitiveBuilder::estimateIndexCount(const PrimitiveDesc& descIn) {
    PrimitiveDesc d = descIn;
    d.canonicalize();
    const uint32_t nu = d.segU, nv = d.segV;
    switch (d.shape) {
        case EPrimitiveShape::Box:      return 36;
        case EPrimitiveShape::Plane:    return nu * nv * 6;
        case EPrimitiveShape::Sphere: {
            // 극 두 행은 사각형마다 삼각형이 하나뿐입니다.
            const uint32_t quads = nu * nv;
            const uint32_t pole  = (nv >= 2) ? nu * 2 : 0;
            return ((quads - pole) * 2 + pole) * 3;
        }
        case EPrimitiveShape::Cylinder: return d.capped() ? 12 * nu : 6 * nu;
        case EPrimitiveShape::Cone:     return d.capped() ?  6 * nu : 3 * nu;
        // 행이 2*(nv+1) 개이므로 사각형이 nu*(2nv+1) 이고, 극 사각형 2nu 개만
        // 삼각형 하나입니다. 정리하면 삼각형 4*nu*nv 개입니다.
        case EPrimitiveShape::Capsule:  return 12 * nu * nv;
        case EPrimitiveShape::Torus:    return nu * nv * 6;
        // 자루 옆면 6nu + 자루 바닥 3nu + 머리 밑면 3nu + 머리 원뿔 3nu
        case EPrimitiveShape::Arrow:    return 15 * nu;
        case EPrimitiveShape::Icosphere: {
            uint32_t f = 1;
            for (uint32_t i = 0; i < nu; ++i) f *= 4;
            return 60 * f;
        }
        default: return 0;
    }
}

// ── 검증 (규칙 T3) ────────────────────────────────────────────────────────────

bool PrimitiveBuilder::validate(const PrimitiveDesc& desc,
                                const MeshSourceData& mesh,
                                std::string* report) {
    std::string log;
    auto fail = [&](const std::string& msg) { log += "  - " + msg + "\n"; };
    char buf[256];

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        if (report) *report = "  - 빈 메시입니다.\n";
        return false;
    }
    if (mesh.indices.size() % 3 != 0)
        fail("인덱스 개수가 3의 배수가 아닙니다. 삼각형 목록이 아닙니다.");

    const uint32_t vn = (uint32_t)mesh.vertices.size();
    auto P = [&](uint32_t i) {
        return glm::vec3(mesh.vertices[i].pos[0], mesh.vertices[i].pos[1], mesh.vertices[i].pos[2]);
    };
    auto N = [&](uint32_t i) {
        return glm::vec3(mesh.vertices[i].normal[0], mesh.vertices[i].normal[1], mesh.vertices[i].normal[2]);
    };
    auto UV = [&](uint32_t i) {
        return glm::vec2(mesh.vertices[i].uv[0], mesh.vertices[i].uv[1]);
    };

    // 1. 인덱스 범위
    for (uint32_t idx : mesh.indices)
        if (idx >= vn) { fail("인덱스가 정점 배열을 벗어납니다."); break; }

    // 2. 노멀의 단위 길이
    for (uint32_t i = 0; i < vn; ++i) {
        const float len = glm::length(N(i));
        if (std::fabs(len - 1.f) > 1e-3f) {
            std::snprintf(buf, sizeof(buf),
                          "정점 %u 의 노멀 길이가 %.4f 입니다. 단위 벡터가 아닙니다.", i, len);
            fail(buf); break;
        }
    }

    // 3. 와인딩. 면 법선이 정점 노멀과 같은 쪽을 향해야 합니다 (규칙 W1).
    // 4. 탄젠트 handedness. UV 에서 유도한 bitangent 와 cross(N,T)*w 를 대조합니다.
    int badWinding = 0, badHand = 0;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1], ic = mesh.indices[t + 2];
        if (ia >= vn || ib >= vn || ic >= vn) continue;

        const glm::vec3 e1 = P(ib) - P(ia), e2 = P(ic) - P(ia);
        const glm::vec3 faceN = glm::cross(e1, e2);
        if (glm::length(faceN) < 1e-12f) continue;   // 축퇴 삼각형은 건너뜁니다

        const glm::vec3 avgN = N(ia) + N(ib) + N(ic);
        if (glm::dot(faceN, avgN) <= 0.f) ++badWinding;

        // UV 유도 bitangent 입니다. GltfLoader::generateTangents 와 같은 산식입니다.
        const glm::vec2 d1 = UV(ib) - UV(ia), d2 = UV(ic) - UV(ia);
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::fabs(det) < 1e-12f) continue;       // UV 축퇴는 건너뜁니다
        const float inv = 1.f / det;
        const glm::vec3 bitUV = (e2 * d1.x - e1 * d2.x) * inv;
        if (glm::length(bitUV) < 1e-12f) continue;

        for (uint32_t i : { ia, ib, ic }) {
            const glm::vec3 tv(mesh.vertices[i].tangent[0],
                               mesh.vertices[i].tangent[1],
                               mesh.vertices[i].tangent[2]);
            const float w = mesh.vertices[i].tangent[3];
            if (std::fabs(std::fabs(w) - 1.f) > 1e-4f) { ++badHand; continue; }
            if (glm::dot(glm::cross(N(i), tv) * w, bitUV) <= 0.f) ++badHand;
        }
    }
    if (badWinding) {
        std::snprintf(buf, sizeof(buf),
                      "삼각형 %d 개의 와인딩이 뒤집혀 있습니다. 바깥에서 CCW 여야 합니다.", badWinding);
        fail(buf);
    }
    if (badHand) {
        std::snprintf(buf, sizeof(buf),
                      "정점 참조 %d 건에서 탄젠트 handedness 가 UV 유도 bitangent 와 반대입니다. "
                      "노멀맵의 Y 가 뒤집혀 볼록이 오목으로 보입니다.", badHand);
        fail(buf);
    }

    // 5. 정점 순회 바운드 대 해석 바운드
    glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (uint32_t i = 0; i < vn; ++i) { lo = glm::min(lo, P(i)); hi = glm::max(hi, P(i)); }
    const MeshBounds ab = PrimitiveBuilder::analyticBounds(desc);
    // 곡면은 정점이 내접하므로 순회 바운드가 해석 바운드보다 작은 것이 정상입니다.
    // 반대로 '넘치는' 경우만 결함입니다.
    const float eps = 1e-3f;
    if (glm::any(glm::lessThan(lo, ab.aabbMin - eps)) ||
        glm::any(glm::greaterThan(hi, ab.aabbMax + eps))) {
        std::snprintf(buf, sizeof(buf),
                      "정점이 해석 바운드를 벗어납니다. 순회 (%.3f %.3f %.3f)~(%.3f %.3f %.3f), "
                      "해석 (%.3f %.3f %.3f)~(%.3f %.3f %.3f).",
                      lo.x, lo.y, lo.z, hi.x, hi.y, hi.z,
                      ab.aabbMin.x, ab.aabbMin.y, ab.aabbMin.z,
                      ab.aabbMax.x, ab.aabbMax.y, ab.aabbMax.z);
        fail(buf);
    }

    // 6. 예측 개수 대조
    const uint32_t ev = PrimitiveBuilder::estimateVertexCount(desc);
    const uint32_t ei = PrimitiveBuilder::estimateIndexCount(desc);
    if (ev && ev != vn) {
        std::snprintf(buf, sizeof(buf),
                      "정점 수가 예측과 다릅니다. 예측 %u, 실제 %u. 예산 검사가 무의미해집니다.",
                      ev, vn);
        fail(buf);
    }
    if (ei && ei != (uint32_t)mesh.indices.size()) {
        std::snprintf(buf, sizeof(buf),
                      "인덱스 수가 예측과 다릅니다. 예측 %u, 실제 %u.",
                      ei, (uint32_t)mesh.indices.size());
        fail(buf);
    }

    if (report) *report = log;
    return log.empty();
}
