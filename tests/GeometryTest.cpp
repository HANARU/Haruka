// 절차 도형 규약 테스트입니다. Vulkan 도, 디바이스도, 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유는 defaultCube 의 탄젠트 handedness 가 여섯 면 전부
// 반대인 채로 오래 남아 있었기 때문입니다 (DEF-39). 증상이 "볼록이 오목으로
// 보인다" 라서 원인을 텍스처나 셰이더에서 찾게 되고, 그러는 동안 화면은
// 그럴듯하게 나옵니다. 이런 결함은 실행이 아니라 검산으로만 잡힙니다.
//
// 실행: build/Release/HarukaGeometryTest.exe

#include "../src/geometry/PrimitiveBuilder.hpp"
#include "../src/geometry/MeshLod.hpp"
#include "../src/geometry/MeshOptimize.hpp"
#include "../src/geometry/QTangent.hpp"
#include "../src/core/TextFloat.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    // ── 씬 파일 형식 (PRIM: / PRIM_MAT:) ─────────────────────────────────────
    // 씬 직렬화 자체는 렌더러가 있어야 돌지만, 값의 텍스트 형식은 여기서 검산합니다.
    std::printf("\n== 씬 파일 형식 ==\n");
    {
        // PRIM: 값은 캐시 키 형식 그대로입니다. 키 형식을 바꾸면 저장된 씬을 못 읽습니다.
        PrimitiveDesc torus; torus.shape = EPrimitiveShape::Torus;
        torus.segU = 32; torus.segV = 16; torus.ratio = 0.4f;
        PrimitiveDesc back;
        check(PrimitiveDesc::fromKey(torus.toKey(), back) && back == torus,
              "PRIM 값(Torus, ratio 0.4)이 왕복합니다");

        PrimitiveMaterialDesc m;
        const PrimitiveMaterialDesc gray{ false, 0xB4B4B4FFu };
        check(gray.toText() == "color B4B4B4FF 0 0.6", "회색 머티리얼의 텍스트가 'color B4B4B4FF 0 0.6' 입니다");
        check(PrimitiveMaterialDesc::fromText(gray.toText(), m) && m == gray, "색 머티리얼이 왕복합니다");

        const PrimitiveMaterialDesc checker{ true };
        check(checker.toText() == "checker" && PrimitiveMaterialDesc::fromText("checker", m) && m.checker,
              "체커 머티리얼이 왕복합니다");

        const PrimitiveMaterialDesc odd{ false, 0x12AB34CDu, 0.123456789f, 0.987654321f };
        check(PrimitiveMaterialDesc::fromText(odd.toText(), m) && m == odd,
              "유효 숫자가 긴 금속도·거칠기도 값이 바뀌지 않고 왕복합니다");
        check(PrimitiveMaterialDesc::fromText("  color b4b4b4ff 0 0.6  ", m) && m == gray,
              "앞뒤 공백과 소문자 16진수를 받아들입니다");

        // 깨진 값은 조용히 통과하지 않습니다. 씬 로더는 실패를 로그로 알리고 기본값을 씁니다.
        check(!PrimitiveMaterialDesc::fromText("color B4B4B4 0 0.6", m),         "색이 8자리가 아니면 실패합니다");
        check(!PrimitiveMaterialDesc::fromText("color B4B4B4FF 0 0.6 extra", m), "꼬리에 쓰레기가 붙으면 실패합니다");
        check(!PrimitiveMaterialDesc::fromText("color B4B4B4FF 2 0.6", m),       "금속도가 [0,1] 밖이면 실패합니다");
        check(!PrimitiveMaterialDesc::fromText("color B4B4B4FF 0", m),           "값이 모자라면 실패합니다");
        check(!PrimitiveMaterialDesc::fromText("metal B4B4B4FF 0 0.6", m),       "모르는 종류는 실패합니다");

        // 씬 파일의 실수는 짧게 쓰되 값은 지킵니다(위치·크기·색 전부 이 형식입니다).
        check(floatToText(0.02f) == "0.02" && floatToText(20.f) == "20" && floatToText(-0.f) == "0",
              "흔한 값은 짧게 씁니다 (0.02, 20, -0 은 0)");
        const float v = 1.2345678f;
        check(std::strtof(floatToText(v).c_str(), nullptr) == v,
              "유효 숫자 6자리를 넘는 값도 되읽으면 같은 값입니다 (예전 기본 형식은 1.23457 로 잘림)");
    }


    std::printf("\n== 정점 캐시 순서(O3) ==\n");
    {
        PrimitiveDesc sphere; sphere.shape = EPrimitiveShape::Sphere;
        sphere.segU = 64; sphere.segV = 32;
        const MeshSourceData sm = PrimitiveBuilder::build(sphere);
        // 무작위로 섞은 순서(최악에 가까움)와 원래 순서에서 최적화합니다.
        std::vector<uint32_t> shuffled = sm.indices;
        uint32_t seed = 12345u;
        for (size_t t = shuffled.size() / 3; t > 1; --t) {
            seed = seed * 1664525u + 1013904223u;
            const size_t j = (seed >> 8) % t;
            for (int k = 0; k < 3; ++k) std::swap(shuffled[(t - 1) * 3 + k], shuffled[j * 3 + k]);
        }
        const uint32_t vc = (uint32_t)sm.vertices.size();
        const float before = hmesh::acmr(shuffled);
        std::vector<uint32_t> opt = shuffled;
        hmesh::optimizeVertexCache(opt, vc);
        const float after = hmesh::acmr(opt);
        std::printf("  (참고) ACMR(캐시 16): 섞은 순서 %.3f → 최적화 %.3f, 원래 순서 %.3f\n", before, after, hmesh::acmr(sm.indices));
        check(after < 0.8f && after < before * 0.6f, "섞은 구의 ACMR 이 크게 줄어듦(0.8 미만)");
        // 삼각형 집합과 감기 방향이 그대로(회전한 같은 삼각형은 같은 것으로 봄)
        auto canon = [](const std::vector<uint32_t>& idx) {
            std::vector<std::array<uint32_t, 3>> tris;
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                std::array<uint32_t, 3> t{ idx[i], idx[i + 1], idx[i + 2] };
                while (t[0] > t[1] || t[0] > t[2]) t = { t[1], t[2], t[0] };   // 가장 작은 번호가 앞에 오게 돌림(감기 유지)
                tris.push_back(t);
            }
            std::sort(tris.begin(), tris.end());
            return tris;
        };
        check(opt.size() == shuffled.size() && canon(opt) == canon(shuffled), "삼각형 집합 · 감기 방향 그대로");
        std::vector<uint32_t> bad{ 0, 1, 2, 0, 2, 9 };
        const std::vector<uint32_t> badCopy = bad;
        hmesh::optimizeVertexCache(bad, 4);
        check(bad == badCopy, "범위를 벗어난 인덱스가 있으면 그대로 둠");
    }

    {
        std::printf("\n== C2 QTangent: 법선 · 접선 · 손잡이를 snorm16 쿼터니언 하나로 ==\n");
        auto roundTrip = [](const float n[3], const float t[4], float& errN, float& errT, bool& signOk) {
            float q[4], dn[3], dt[3], sg;
            hqtan::encode(n, t, q);
            for (float& c : q) c = hqtan::fromSnorm16(hqtan::toSnorm16(c));
            hqtan::decode(q, dn, dt, sg);
            // 기대값: 정규화한 n, n 에 직교화한 t
            float nn[3] = { n[0], n[1], n[2] };
            hqtan::normalize3(nn);
            float d = t[0] * nn[0] + t[1] * nn[1] + t[2] * nn[2];
            float tt[3] = { t[0] - d * nn[0], t[1] - d * nn[1], t[2] - d * nn[2] };
            hqtan::normalize3(tt);
            errN = errT = 0.f;
            for (int i = 0; i < 3; ++i) {
                errN = std::max(errN, std::fabs(dn[i] - nn[i]));
                errT = std::max(errT, std::fabs(dt[i] - tt[i]));
            }
            signOk = (sg < 0.f) == (t[3] < 0.f);
        };
        uint32_t rng = 12345u;
        auto rnd = [&rng] { rng = rng * 1664525u + 1013904223u; return float(rng >> 8) / float(1u << 24) * 2.f - 1.f; };
        float worstN = 0.f, worstT = 0.f;
        bool  allSign = true;
        for (int k = 0; k < 2000; ++k) {
            const float n[3] = { rnd(), rnd(), rnd() + 0.01f };
            const float t[4] = { rnd(), rnd(), rnd(), (k & 1) ? 1.f : -1.f };
            float eN, eT; bool ok;
            roundTrip(n, t, eN, eT, ok);
            worstN = std::max(worstN, eN); worstT = std::max(worstT, eT); allSign = allSign && ok;
        }
        std::printf("  (참고) 무작위 2000개: 법선 오차 최대 %.2e, 접선 오차 최대 %.2e\n", worstN, worstT);
        check(worstN < 2e-4f && worstT < 2e-4f, "무작위 기저를 1e-4 수준으로 되살림");
        check(allSign, "손잡이(±1)를 모두 지킴");
        // w = 0 인 회전(180°): N = −Z, T = +X → x 축 둘레 180°. 바이어스가 없으면 손잡이를 잃는 경우.
        {
            const float n[3] = { 0.f, 0.f, -1.f };
            float eN, eT; bool okP, okM;
            const float tp[4] = { 1.f, 0.f, 0.f, 1.f }, tm[4] = { 1.f, 0.f, 0.f, -1.f };
            roundTrip(n, tp, eN, eT, okP);
            const bool accP = eN < 2e-4f && eT < 2e-4f;
            roundTrip(n, tm, eN, eT, okM);
            check(accP && eN < 2e-4f && eT < 2e-4f && okP && okM, "w = 0 회전(180°)에서도 기저 · 두 손잡이를 지킴");
        }
        {
            const float n[3] = { 0.f, 1.f, 0.f }, t[4] = { 0.f, 0.f, 0.f, 1.f };   // 접선 없음(노멀 맵 없는 메시)
            float q[4], dn[3], dt[3], sg;
            hqtan::encode(n, t, q);
            for (float& c : q) c = hqtan::fromSnorm16(hqtan::toSnorm16(c));
            hqtan::decode(q, dn, dt, sg);
            check(std::fabs(dn[1] - 1.f) < 2e-4f && std::fabs(dt[0] * dn[0] + dt[1] * dn[1] + dt[2] * dn[2]) < 1e-3f,
                  "접선이 0 이면 법선은 그대로, 접선은 아무 직교 방향");
        }
    }

    {
        std::printf("\n== O13 자동 LOD 생성(MeshLod) ==\n");
        PrimitiveDesc dense;
        dense.shape = EPrimitiveShape::Sphere;
        dense.segU  = 64;
        dense.segV  = 32;
        const MeshSourceData sm = PrimitiveBuilder::build(dense);
        // Sphere 는 UV 이음매·극점 링이 위상 경계이므로 lockBorder=true 로 감기 방향까지 확인
        const auto lods = hmesh::buildLods(sm.vertices, sm.indices, 0.5f, true);
        check(lods.size() >= 1 && lods.size() <= hmesh::kLodMaxLevels,
              "조밀한 구에서 1 ~ 3단계 LOD 생성 (실제 " + std::to_string(lods.size()) + "단계)");

        bool monoOk = !lods.empty();
        bool rangeOk = true;
        bool windOk = true;
        size_t prevIdx = sm.indices.size();
        float prevErr = 0.f;
        float prevScr = 1e9f;
        for (const auto& lv : lods) {
            if (lv.indices.empty() || lv.indices.size() >= prevIdx || lv.indices.size() % 3 != 0) monoOk = false;
            if (!(lv.error > prevErr) || !(lv.screenSize > 0.f && lv.screenSize < prevScr)) monoOk = false;
            prevIdx = lv.indices.size();
            prevErr = lv.error;
            prevScr = lv.screenSize;

            for (size_t i = 0; i + 2 < lv.indices.size(); i += 3) {
                const uint32_t i0 = lv.indices[i], i1 = lv.indices[i + 1], i2 = lv.indices[i + 2];
                if (i0 >= sm.vertices.size() || i1 >= sm.vertices.size() || i2 >= sm.vertices.size()) {
                    rangeOk = false;
                    break;
                }
                const glm::vec3 p0(sm.vertices[i0].pos[0], sm.vertices[i0].pos[1], sm.vertices[i0].pos[2]);
                const glm::vec3 p1(sm.vertices[i1].pos[0], sm.vertices[i1].pos[1], sm.vertices[i1].pos[2]);
                const glm::vec3 p2(sm.vertices[i2].pos[0], sm.vertices[i2].pos[1], sm.vertices[i2].pos[2]);
                const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
                const glm::vec3 c = (p0 + p1 + p2) / 3.f;
                if (glm::length(n) > 1e-8f && glm::dot(n, c) <= 0.f) windOk = false;
            }
        }
        check(monoOk, "단계마다 인덱스 감소 · 오차 증가 · 화면 크기 감소");
        check(rangeOk, "모든 LOD 인덱스가 정점 수 범위 안");
        check(windOk, "모든 LOD 삼각형의 감기 방향(바깥 법선)이 유지됨");

        PrimitiveDesc smallDesc;
        smallDesc.shape = EPrimitiveShape::Sphere;
        smallDesc.segU  = 12;
        smallDesc.segV  = 8;
        const MeshSourceData smallMesh = PrimitiveBuilder::build(smallDesc);
        const auto smallLods = hmesh::buildLods(smallMesh.vertices, smallMesh.indices, 0.5f);
        check(smallLods.empty(), "삼각형 512개 미만 메시는 LOD 0단계");

        // 열린 곡면 격자(20x20 = 800 tris): lockBorder=true 는 경계 정점을 보존하고, false 는 자유롭게 줄임
        constexpr int N = 20;
        std::vector<MeshSourceVertex> gridV((N + 1) * (N + 1));
        for (int iy = 0; iy <= N; ++iy)
            for (int ix = 0; ix <= N; ++ix) {
                const float x = (float)ix / N * 2.f - 1.f;
                const float y = (float)iy / N * 2.f - 1.f;
                const float z = 0.25f * (x * x + y * y);
                auto& v = gridV[iy * (N + 1) + ix];
                v = { { x, y, z }, { 0.f, 0.f, 1.f }, { (float)ix / N, (float)iy / N }, { 1.f, 0.f, 0.f, 1.f } };
            }
        std::vector<uint32_t> gridI;
        gridI.reserve(N * N * 6);
        for (int iy = 0; iy < N; ++iy)
            for (int ix = 0; ix < N; ++ix) {
                const uint32_t a = (uint32_t)(iy * (N + 1) + ix);
                const uint32_t b = a + 1, c = a + (N + 1), d = c + 1;
                gridI.insert(gridI.end(), { a, b, d, a, d, c });
            }
        const auto lodLocked   = hmesh::buildLods(gridV, gridI, 1.5f, true);
        const auto lodUnlocked = hmesh::buildLods(gridV, gridI, 1.5f, false);
        check(!lodLocked.empty() && !lodUnlocked.empty() &&
              lodUnlocked.back().indices.size() <= lodLocked.back().indices.size(),
              "열린 격자: lockBorder=false 가 경계 고정보다 같거나 더 많이 줄임");

        // UV 솔기가 있는 메시(Sphere 64x32, lockBorder=false)에서 보호한 UV-split 정점 쌍이 LOD 결과에 남는지 확인
        const auto seamLods = hmesh::buildLods(sm.vertices, sm.indices, 0.5f, false);
        bool seamPreserved = !seamLods.empty();
        for (const auto& lv : seamLods) {
            std::vector<uint8_t> used(sm.vertices.size(), 0);
            for (uint32_t idx : lv.indices) {
                if (idx < sm.vertices.size()) used[idx] = 1;
            }
            // 적도 부근 U=0 (ix=0) 과 U=1 (ix=64) 솔기 정점 쌍 중 실제로 양쪽이 모두 참조되는 쌍이 남아야 함
            size_t pairsKept = 0;
            const uint32_t stride = 65;
            for (uint32_t iy = 1; iy < 32; ++iy) {
                const uint32_t a = iy * stride;
                const uint32_t b = iy * stride + 64;
                if (used[a] && used[b] && std::fabs(sm.vertices[a].uv[0] - sm.vertices[b].uv[0]) > 1e-4f)
                    ++pairsKept;
            }
            if (pairsKept == 0) seamPreserved = false;
        }
        check(seamPreserved, "UV 솔기: Permissive에서도 보호된 UV 갈라짐 정점 쌍이 LOD에 남음");
    }

    std::printf("\n실패 %d 건\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
