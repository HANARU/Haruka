// 절차 도형 규약 테스트입니다. Vulkan 도, 디바이스도, 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유는 defaultCube 의 탄젠트 handedness 가 여섯 면 전부
// 반대인 채로 오래 남아 있었기 때문입니다 (DEF-39). 증상이 "볼록이 오목으로
// 보인다" 라서 원인을 텍스처나 셰이더에서 찾게 되고, 그러는 동안 화면은
// 그럴듯하게 나옵니다. 이런 결함은 실행이 아니라 검산으로만 잡힙니다.
//
// 실행: build/Release/HarukaGeometryTest.exe

#include "../src/geometry/PrimitiveBuilder.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

void checkShape(const char* label, const PrimitiveDesc& desc) {
    std::printf("\n== %s (%s) ==\n", label, desc.toKey().c_str());

    const MeshSourceData m = PrimitiveBuilder::build(desc);
    std::printf("  정점 %zu, 삼각형 %zu\n", m.vertices.size(), m.indices.size() / 3);

    std::string report;
    const bool ok = PrimitiveBuilder::validate(desc, m, &report);
    if (!ok) std::printf("%s", report.c_str());
    check(ok, "규약 검증 (와인딩 / 탄젠트 handedness / 바운드 / 개수)");
}

} // namespace

int main() {
    std::printf("Haruka 절차 도형 규약 테스트\n");

    // ── 키 왕복과 정규화 ─────────────────────────────────────────────────────
    std::printf("\n== 캐시 키 ==\n");
    {
        PrimitiveDesc a; a.shape = EPrimitiveShape::Sphere; a.segU = 32; a.segV = 16;
        PrimitiveDesc b;
        check(PrimitiveDesc::fromKey(a.toKey(), b), "toKey / fromKey 왕복이 성공합니다");
        check(a == b, "왕복한 desc 가 원본과 같습니다");

        // 미사용 필드가 키를 가르면 같은 메시가 두 번 올라갑니다.
        PrimitiveDesc c1; c1.shape = EPrimitiveShape::Box; c1.segU = 8;  c1.ratio = 0.3f;
        PrimitiveDesc c2; c2.shape = EPrimitiveShape::Box; c2.segU = 64; c2.ratio = 0.9f;
        check(c1.toKey() == c2.toKey(), "Box 는 미사용 파라미터가 달라도 같은 키입니다");

        // 래더 스냅 (규칙 R3)
        PrimitiveDesc s; s.shape = EPrimitiveShape::Sphere; s.segU = 30; s.segV = 15;
        s.canonicalize();
        check(s.segU == 32 && s.segV == 16, "세그먼트가 래더 {4,8,16,32,64} 로 스냅됩니다");

        check(snapSegmentLadder(1)   == 4,  "래더 하한이 4 입니다");
        check(snapSegmentLadder(999) == 64, "래더 상한이 64 입니다");
    }

    // ── 물리 대응 ────────────────────────────────────────────────────────────
    std::printf("\n== 물리 대응 ==\n");
    {
        EPhysicsShape ps; HalfExt half;
        PrimitiveDesc sphere; sphere.shape = EPrimitiveShape::Sphere;
        check(primitiveToPhysics(sphere, ps, half), "Sphere 는 해석적 대응이 있습니다");
        check(ps == EPhysicsShape::Sphere && std::fabs(half.x - 0.5f) < 1e-6f,
              "Sphere 의 반지름이 0.5 입니다 (halfExtent 기본값과 일치)");

        PrimitiveDesc caps; caps.shape = EPrimitiveShape::Capsule; caps.ratio = 0.5f;
        primitiveToPhysics(caps, ps, half);
        // Jolt 규약: halfExtent.y 는 원통부만의 반높이, halfExtent.x 가 반지름입니다.
        check(std::fabs(2.f * (half.y + half.x) - 1.f) < 1e-5f,
              "Capsule 의 총 높이가 1 m 입니다");

        PrimitiveDesc torus; torus.shape = EPrimitiveShape::Torus;
        check(!primitiveToPhysics(torus, ps, half), "Torus 는 해석적 대응이 없다고 알립니다");
    }

    // ── 도형 생성 ────────────────────────────────────────────────────────────
    {
        PrimitiveDesc box; box.shape = EPrimitiveShape::Box;
        checkShape("Box", box);

        const MeshSourceData bm = PrimitiveBuilder::build(box);
        std::printf("\n== Box 세부 ==\n");
        check(bm.vertices.size() == 24, "면마다 노멀을 분할해 24 정점입니다 (규칙 N2)");
        bool allNegW = true;
        for (const auto& v : bm.vertices) if (v.tangent[3] >= 0.f) allNegW = false;
        // 이 값은 하드코딩이 아니라 UV 정의식에서 유도된 결과입니다.
        // defaultCube 는 여기에 +1 이 박혀 있었고 여섯 면 전부 틀렸습니다.
        check(allNegW, "Box 여섯 면의 handedness 가 전부 -1 로 유도됩니다 (DEF-39)");
    }

    {
        PrimitiveDesc plane; plane.shape = EPrimitiveShape::Plane;
        plane.segU = 1; plane.segV = 1;
        checkShape("Plane 1x1", plane);

        PrimitiveDesc grid = plane; grid.segU = 4; grid.segV = 3;
        checkShape("Plane 4x3", grid);
    }

    {
        PrimitiveDesc sphere; sphere.shape = EPrimitiveShape::Sphere;
        sphere.segU = 32; sphere.segV = 16;
        checkShape("Sphere 32x16", sphere);

        const MeshSourceData sm = PrimitiveBuilder::build(sphere);
        std::printf("\n== Sphere 세부 ==\n");
        check(sm.vertices.size() == 561, "정점이 (32+1)*(16+1) = 561 개입니다");

        // U5. 이음매 정점이 복제되어 있어야 합니다. 공유하면 마지막 세그먼트에서
        // U 가 1 에서 0 으로 되감겨 텍스처 한 장이 통째로 역주행합니다.
        int seamPairs = 0;
        const int stride = 33;
        for (int j = 1; j < 16; ++j) {
            const auto& a = sm.vertices[(size_t)j * stride + 0];
            const auto& b = sm.vertices[(size_t)j * stride + 32];
            const float dp = std::fabs(a.pos[0] - b.pos[0]) + std::fabs(a.pos[1] - b.pos[1]) +
                             std::fabs(a.pos[2] - b.pos[2]);
            if (dp < 1e-5f && std::fabs(a.uv[0] - 0.f) < 1e-5f && std::fabs(b.uv[0] - 1.f) < 1e-5f)
                ++seamPairs;
        }
        check(seamPairs == 15, "U 이음매가 정점 복제로 처리되어 있습니다 (U5)");

        // U4. 극 링. 극 정점이 서로 다른 U 를 가져야 탄젠트가 정의됩니다.
        bool poleUvVaries = false;
        for (int i = 1; i <= 32; ++i)
            if (std::fabs(sm.vertices[i].uv[0] - sm.vertices[i - 1].uv[0]) > 1e-6f)
                poleUvVaries = true;
        check(poleUvVaries, "북극이 단일 정점이 아니라 U 가 다른 극 링입니다 (U4)");

        // 극에서 ∂P/∂u = 0 이므로 극 링 복제가 없으면 탄젠트가 정의되지 않습니다.
        bool poleTangentFinite = true;
        for (int i = 0; i <= 32; ++i) {
            const auto& v = sm.vertices[i];
            const float len = std::sqrt(v.tangent[0] * v.tangent[0] +
                                        v.tangent[1] * v.tangent[1] +
                                        v.tangent[2] * v.tangent[2]);
            if (!(len > 0.9f && len < 1.1f)) poleTangentFinite = false;
        }
        check(poleTangentFinite, "극 정점의 탄젠트가 단위 벡터로 정의되어 있습니다");
    }

    // ── Tier 1: 물리 대응 도형 ───────────────────────────────────────────────
    {
        PrimitiveDesc cyl; cyl.shape = EPrimitiveShape::Cylinder; cyl.segU = 32;
        checkShape("Cylinder 32 (capped)", cyl);

        PrimitiveDesc open = cyl; open.flags = 0;
        checkShape("Cylinder 32 (open)", open);
    }

    {
        PrimitiveDesc cone; cone.shape = EPrimitiveShape::Cone; cone.segU = 32;
        checkShape("Cone 32 (capped)", cone);

        const MeshSourceData cm = PrimitiveBuilder::build(cone);
        std::printf("\n== Cone 세부 ==\n");
        // apex 에서는 ∂P/∂u = 0 이라 노멀이 정의되지 않습니다. 세그먼트마다 apex 를
        // 복제해 각 삼각형이 자기 노멀을 갖게 한 것이 규칙 N2 입니다.
        int apexCount = 0;
        for (const auto& v : cm.vertices)
            if (v.pos[1] > 0.499f && std::fabs(v.pos[0]) < 1e-5f && std::fabs(v.pos[2]) < 1e-5f)
                ++apexCount;
        check(apexCount == 32, "apex 가 단일 정점이 아니라 세그먼트마다 복제되어 있습니다");
    }

    {
        PrimitiveDesc caps; caps.shape = EPrimitiveShape::Capsule;
        caps.segU = 32; caps.segV = 8; caps.ratio = 0.5f;
        checkShape("Capsule 32x8 (ratio 0.5)", caps);

        const MeshSourceData km = PrimitiveBuilder::build(caps);
        std::printf("\n== Capsule 세부 ==\n");

        // 총 높이가 정확히 1 m 여야 물리 Capsule 과 겹칩니다.
        float lo = 1e9f, hi = -1e9f;
        for (const auto& v : km.vertices) { lo = std::min(lo, v.pos[1]); hi = std::max(hi, v.pos[1]); }
        check(std::fabs((hi - lo) - 1.f) < 1e-4f, "총 높이가 1 m 입니다 (Jolt Capsule 규약과 일치)");

        // 반구와 원통의 경계는 C1 연속이므로 정점을 공유해야 합니다 (규칙 N2).
        // 공유하지 않으면 없는 링이 눈에 보입니다.
        const float r  = 0.25f;      // 0.5 * ratio
        const float hc = 0.25f;      // 0.5 - r
        int boundaryVerts = 0;
        for (const auto& v : km.vertices)
            if (std::fabs(v.pos[1] - hc) < 1e-4f &&
                std::fabs(std::sqrt(v.pos[0]*v.pos[0] + v.pos[2]*v.pos[2]) - r) < 1e-4f)
                ++boundaryVerts;
        // 33 개(이음매 복제 포함) 한 줄이어야 합니다. 두 줄이면 분할된 것입니다.
        check(boundaryVerts == 33, "반구와 원통 경계가 한 줄로 공유되어 있습니다 (V8)");

        // ratio 를 1 에 가깝게 하면 구가 되어야 합니다.
        PrimitiveDesc sph = caps; sph.ratio = 0.99f;
        checkShape("Capsule ratio 0.99 (거의 구)", sph);
    }

    // ── Tier 2: 디버그 지시자 ────────────────────────────────────────────────
    {
        PrimitiveDesc tor; tor.shape = EPrimitiveShape::Torus;
        tor.segU = 32; tor.segV = 16; tor.ratio = 0.5f;
        checkShape("Torus 32x16 (ratio 0.5)", tor);

        const MeshSourceData tm = PrimitiveBuilder::build(tor);
        std::printf("\n== Torus 세부 ==\n");
        check(tm.vertices.size() == 561, "정점이 (32+1)*(16+1) = 561 개입니다 (양방향 이음매 복제)");

        float maxR = 0.f;
        for (const auto& v : tm.vertices)
            maxR = std::max(maxR, std::sqrt(v.pos[0]*v.pos[0] + v.pos[2]*v.pos[2]));
        check(std::fabs(maxR - 0.5f) < 1e-4f, "바깥 반지름이 0.5 입니다 (단위 도형 규약 R2)");
    }

    {
        PrimitiveDesc arw; arw.shape = EPrimitiveShape::Arrow;
        arw.segU = 16; arw.ratio = 0.3f;
        checkShape("Arrow 16 (headRatio 0.3)", arw);

        const MeshSourceData am = PrimitiveBuilder::build(arw);
        std::printf("\n== Arrow 세부 ==\n");
        // 유일한 예외 카테고리입니다 (규칙 P2). 원점이 시작점이고 전방이 -Z 입니다.
        float zLo = 1e9f, zHi = -1e9f;
        for (const auto& v : am.vertices) { zLo = std::min(zLo, v.pos[2]); zHi = std::max(zHi, v.pos[2]); }
        check(std::fabs(zHi) < 1e-4f,        "원점이 시작점입니다 (z 최대가 0)");
        check(std::fabs(zLo + 1.f) < 1e-4f,  "길이가 1 m 이고 전방이 -Z 입니다");
    }

    {
        PrimitiveDesc ico; ico.shape = EPrimitiveShape::Icosphere; ico.segU = 2;
        checkShape("Icosphere level 2", ico);

        const MeshSourceData im = PrimitiveBuilder::build(ico);
        std::printf("\n== Icosphere 세부 ==\n");
        check(im.vertices.size() == 162, "중점 공유로 정점이 10*4^2+2 = 162 개입니다");

        // 균일 삼각형이 이 도형의 유일한 존재 이유입니다. 반지름이 전부 0.5 여야 합니다.
        float rLo = 1e9f, rHi = -1e9f;
        for (const auto& v : im.vertices) {
            const float r = std::sqrt(v.pos[0]*v.pos[0] + v.pos[1]*v.pos[1] + v.pos[2]*v.pos[2]);
            rLo = std::min(rLo, r); rHi = std::max(rHi, r);
        }
        check(rHi - rLo < 1e-5f && std::fabs(rHi - 0.5f) < 1e-5f,
              "모든 정점이 반지름 0.5 구 위에 있습니다");

        // UV 파라미터화가 없으므로 UV 는 전부 (0,0) 입니다. 노멀맵 금지의 근거입니다.
        bool allZeroUv = true;
        for (const auto& v : im.vertices)
            if (v.uv[0] != 0.f || v.uv[1] != 0.f) allZeroUv = false;
        check(allZeroUv, "UV 가 없습니다 (노멀맵 머티리얼 사용 금지)");
    }

    // ── LOD (규칙 R4) ────────────────────────────────────────────────────────
    std::printf("\n== LOD ==\n");
    {
        PrimitiveDesc sphere; sphere.shape = EPrimitiveShape::Sphere;
        sphere.segU = 32; sphere.segV = 16;
        const PrimitiveDesc l1 = primitiveLodDesc(sphere, 1);
        const PrimitiveDesc l2 = primitiveLodDesc(sphere, 2);
        check(l1.segU == 16 && l1.segV == 8, "LOD1 이 16x8 입니다");
        check(l2.segU == 8  && l2.segV == 4, "LOD2 가 8x4 입니다");

        PrimitiveDesc box; box.shape = EPrimitiveShape::Box;
        check(primitiveLodDesc(box, 2) == box, "Box 는 LOD 가 없습니다");

        uint32_t total = 0;
        for (int i = 0; i < 4; ++i)
            total += PrimitiveBuilder::estimateVertexCount(primitiveLodDesc(sphere, i));
        std::printf("  구 4 단 사다리 합계 %u 정점 (%.1f KB, 정점 예산의 %.3f %%)\n",
                    total, total * 48.f / 1024.f,
                    100.f * total * 48.f / (128.f * 1024.f * 1024.f));
    }

    // ── 전 도형 일괄 ─────────────────────────────────────────────────────────
    // 아홉 종이 전부 구현되었으므로, 하나라도 빈 메시를 내면 그것이 결함입니다.
    std::printf("\n== 전 도형 일괄 ==\n");
    {
        uint32_t totalVerts = 0;
        bool allNonEmpty = true, allValid = true;
        for (int i = 0; i < (int)EPrimitiveShape::Count; ++i) {
            PrimitiveDesc d;
            d.shape = (EPrimitiveShape)i;
            const MeshSourceData m = PrimitiveBuilder::build(d);
            if (m.vertices.empty() || m.indices.empty()) {
                std::printf("  빈 메시: %s\n", primitiveShapeName(d.shape));
                allNonEmpty = false;
                continue;
            }
            std::string rep;
            if (!PrimitiveBuilder::validate(d, m, &rep)) {
                std::printf("  규약 위반 %s:\n%s", primitiveShapeName(d.shape), rep.c_str());
                allValid = false;
            }
            totalVerts += (uint32_t)m.vertices.size();
        }
        check(allNonEmpty, "아홉 종이 전부 기본 파라미터로 메시를 만듭니다");
        check(allValid,    "아홉 종이 전부 규약을 지킵니다");

        // 카탈로그 전체를 한 번씩 스폰해도 정점 예산의 1 % 미만이어야 합니다 (V12).
        const float pct = 100.f * (float)totalVerts * 48.f / (128.f * 1024.f * 1024.f);
        std::printf("  카탈로그 전체 %u 정점 (%.1f KB, 정점 예산의 %.4f %%)\n",
                    totalVerts, totalVerts * 48.f / 1024.f, pct);
        check(pct < 1.f, "카탈로그 전체가 정점 예산의 1 % 미만입니다 (V12)");
    }

    // 범위를 벗어난 셰이프 값은 canonicalize 가 Box 로 눕힙니다.
    // 직렬화가 서수가 아니라 이름을 쓰는 이유이기도 합니다.
    {
        PrimitiveDesc bad;
        bad.shape = (EPrimitiveShape)200;
        bad.canonicalize();
        check(bad.shape == EPrimitiveShape::Box, "범위 밖 셰이프는 Box 로 정규화됩니다");

        PrimitiveDesc out;
        check(!PrimitiveDesc::fromKey("nosuchshape|32x16|0|0.5000", out),
              "모르는 셰이프 이름은 조용히 통과하지 않고 실패합니다");
    }


    std::printf("\n실패 %d 건\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
